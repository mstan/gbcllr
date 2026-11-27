#include "desktop_ui.hpp"
#include "desktop_net.hpp"
#include "recomp_netplay_host.h"
#include "identity.h"
#include "gen2_native_mods.hpp"
#include "recomp_net/session.h"
#include "recomp_net/host_ice.h"
#include <SDL.h>
#include <algorithm>
#include <cstring>
#include <cstdio>
#include <sstream>
#include <iostream>
#include <stdexcept>

namespace {
bool coop_selection(const DesktopConfig& c) {
    return c.coop_enabled&&c.package&&(c.package->feature.plugin=="gbcllr.xtreme2.adaptive-view"||c.package->feature.plugin=="gbcllr.xtreme2.coop");
}
bool gen2_pair(const DesktopConfig& c) {
    return !c.link_rom.empty()&&gbcllr::Gen2NativeMods::linked_pair(gbcllr::sha256(gbcllr::read_file(c.rom)),
        gbcllr::sha256(gbcllr::read_file(c.link_rom)));
}
bool gen2_selection(const DesktopConfig& c) {
    if(!c.package||!gen2_pair(c))return false;
    const auto& f=c.package->feature;
    return f.plugin=="gbcllr.gen2.adaptive-view"&&f.abi==1&&!f.colorization&&f.asset_input.empty()&&!f.gift&&!c.gift_enabled;
}
struct IceHandover {
    RNetIceAgent* agent=nullptr;
    bool host=false;
    ~IceHandover(){if(agent)rnet_host_ice_destroy_agent(agent);}
    int start(RNetSession* session) {
        if(!agent)return -1;
        RNetIceAdoptSeat seat{1,agent};
        int result=host?rnet_session_start_ice_hub_adopt(session,&seat,1):
            rnet_session_adopt_ice_agent(session,agent);
        if(!result)agent=nullptr; // The session now owns it, including teardown.
        return result;
    }
};
struct PeerIce {
    RNetIceConfig config{};
    std::string stun,turn,user,password;
    explicit PeerIce(unsigned slot) {
        rnet_ice_config_init_defaults(&config);config.controlling=slot==0;
        auto credentials=rnet_lobby_turn_credentials();
        if(credentials&&credentials->valid) {
            stun=credentials->stun_host;turn=credentials->turn_host;
            user=credentials->username;password=credentials->password;
            config.stun_host=stun.c_str();config.stun_port=uint16_t(credentials->stun_port);
            config.turn_host=turn.c_str();config.turn_port=uint16_t(credentials->turn_port);
            config.turn_user=user.c_str();config.turn_pass=password.c_str();
        }
    }
};
}
struct DesktopNetLobby {
    static DesktopNetLobby* active; // The shared lobby client is process-global.
    std::string name,fingerprint,registry;
    RecompLauncherCNetplayCallbacks callbacks{};
    std::shared_ptr<IceHandover> ice;
    uint32_t ice_session=0;
    bool coop=false;
    unsigned first=0,width=384;
    bool shoulders=false,shared=true,smooth=false;
    RecompNetplayModHooks mods{};
    static constexpr const char* coop_package="gbcllr.xtreme2.coop";
    std::string effective() const {
        return std::string(coop_package)+"@1/coop first="+std::to_string(first)+" shoulders="+std::to_string(shoulders)+
            " shared="+std::to_string(shared)+" width="+std::to_string(width)+" smooth="+std::to_string(smooth)+"\n";
    }
    int match(const char* want,bool adopt,char* reason,uint32_t cap) {
        unsigned f=2,l=2,s=2,w=0,g=2;int end=0;
        bool valid=want&&std::sscanf(want,"gbcllr.xtreme2.coop@1/coop first=%u shoulders=%u shared=%u width=%u smooth=%u%n",&f,&l,&s,&w,&g,&end)==5&&
            f<2&&l<2&&s<2&&g<2&&w>=160&&w<=4096;
        if(valid)for(const char* tail=want+end;*tail;++tail)if(*tail!='\n'&&*tail!='\r')valid=false;
        if(!valid){if(cap)std::snprintf(reason,cap,"This room requires an unsupported co-op configuration.");return -1;}
        if(adopt){first=f;shoulders=l;shared=s;width=w;smooth=g;}
        if(first==f&&shoulders==bool(l)&&shared==bool(s)&&width==w&&smooth==bool(g))return 0;
        if(cap)std::snprintf(reason,cap,"Use the host's co-op character and camera rules for this session.");return -1;
    }
    void setup_mods() {
        mods.ctx=this;
        mods.plan_rows=[](void*,RNetLobbyModPkg* out,int max){if(max<1)return 0;out[0]={};std::snprintf(out[0].id,sizeof out[0].id,"%s",coop_package);std::snprintf(out[0].ver,sizeof out[0].ver,"1");std::snprintf(out[0].name,sizeof out[0].name,"X + Zero co-op");std::snprintf(out[0].feats,sizeof out[0].feats,"coop");return 1;};
        mods.installed_rows=mods.plan_rows;
        mods.effective_set=[](void* p,char* out,uint32_t cap){auto text=static_cast<DesktopNetLobby*>(p)->effective();if(cap)std::snprintf(out,cap,"%s",text.c_str());return int(text.size());};
        mods.check_set=[](void* p,const char* want,char* reason,uint32_t cap){return static_cast<DesktopNetLobby*>(p)->match(want,false,reason,cap);};
        mods.adopt_set=[](void* p,const char* want,char* reason,uint32_t cap){return static_cast<DesktopNetLobby*>(p)->match(want,true,reason,cap);};
        mods.have_package=[](void*,const char* id,const char* ver,char* name,uint32_t cap){bool have=id&&!std::strcmp(id,coop_package)&&(!ver||!std::strcmp(ver,"1"));if(have&&cap)std::snprintf(name,cap,"X + Zero co-op");return int(have);};
    }
    void capture_transport(const RecompLauncherCNetplayLaunch& launch) {
        if(!launch.transport_ice_hub)return;
        if(ice&&ice_session==launch.session_id)return;
        if(launch.is_spectator||launch.host_spectates||launch.player_count!=2||
           launch.local_slot<0||launch.local_slot>1)
            throw std::runtime_error("Online linked play requires two player consoles.");
        auto handover=std::make_shared<IceHandover>();handover->host=launch.local_slot==0;
        if(handover->host) {
            RNetLobbyIceSeat seats[RNET_LOBBY_MAX_PLAYERS]{};
            int count=rnet_lobby_ice_take_hub(seats,RNET_LOBBY_MAX_PLAYERS);
            if(count!=1) {
                for(int i=0;i<count;++i)if(seats[i].agent)rnet_host_ice_destroy_agent(seats[i].agent);
                throw std::runtime_error("The Online lobby did not provide exactly one connected peer.");
            }
            handover->agent=seats[0].agent;
            if(launch.slot_port_valid&&seats[0].lobby_slot!=launch.slot_port[1])
                throw std::runtime_error("The Online connection does not match the selected console seat.");
        } else handover->agent=rnet_lobby_ice_take_guest_agent();
        if(!handover->agent)throw std::runtime_error("The Online connection is not ready.");
        ice=std::move(handover);ice_session=launch.session_id;
    }
    DesktopNetLobby(DesktopConfig& c) {
        coop=coop_selection(c);first=c.coop_first;shoulders=c.weapon_cycle;shared=c.coop_shared&&c.coop_view==0;smooth=c.smooth_gameplay;
        width=c.adaptive?(c.view_width<0?384u:unsigned(c.view_width)):160u;
        if(!coop&&c.link_rom.empty())throw std::runtime_error("Choose the other cartridge in Linked play first.");
        auto primary=gbcllr::sha256(gbcllr::read_file(c.rom));
        auto second=coop?std::string{}:gbcllr::sha256(gbcllr::read_file(c.link_rom));
        if(coop&&primary!="e37a14a8e61bd64f2226dacc26a7ee75dd90abfcf88c5016eb97b76ecda0ddca")throw std::runtime_error("Co-op requires the supported Xtreme 2 cartridge.");
        name=coop?"Mega Man Xtreme 2 co-op":desktop_title(primary)+" / "+desktop_title(second);
        if(!coop&&second<primary) {std::swap(primary,second);name=desktop_title(primary)+" / "+desktop_title(second);}
        std::string content=primary+":"+(coop?"xtreme2-coop-1":second);
        fingerprint=gbcllr::sha256({reinterpret_cast<const uint8_t*>(content.data()),content.size()});
        registry=(c.profile/(coop?"coop-lan.txt":"linked-lan.txt")).string();
        RecompNetplayHostHooks hooks{};hooks.game_name=name.c_str();hooks.game_version=coop?"gbcllr-xtreme2-coop-1":"gbcllr-linked-1";
        if(coop){setup_mods();hooks.mods=&mods;}
        hooks.content_fingerprint=fingerprint.c_str();hooks.platform="gb";hooks.default_lobby_name=name.c_str();
        hooks.lan_registry_path=registry.c_str();hooks.max_players=hooks.netplay_max_players=2;
        hooks.slot_policy=RECOMP_NETPLAY_SLOTS_HOST_FIRST;hooks.input_player=0;
        hooks.ctx=&c;
        hooks.exe_dir_path=[](void* context,const char* leaf,char* out,size_t cap) {
            auto& config=*static_cast<DesktopConfig*>(context);auto path=(config.profile/leaf).string();
            if(path.size()>=cap)return 0;std::memcpy(out,path.c_str(),path.size()+1);return 1;
        };
        if(recomp_netplay_host_init(&hooks))throw std::runtime_error("Could not open the linked-play lobby.");
        callbacks=*recomp_netplay_host_callbacks();
        callbacks.connect=[](void* context) {
            auto base=recomp_netplay_host_callbacks();auto result=base->connect(context);
            if(!result)rnet_lobby_request_turn_credentials();
            return result;
        };
        if(!rnet_host_ice_available())callbacks.connect=nullptr;
        // Take the completed waiting-room transport before the launcher clears
        // launch_pending. Repeated seat/UI inspection returns the same lease.
        callbacks.fill_launch=[](void* context,RecompLauncherCNetplayLaunch* out) {
            auto base=recomp_netplay_host_callbacks();
            if(!base||!base->fill_launch||!base->fill_launch(context,out))return 0;
            try {if(!active)throw std::runtime_error("The linked lobby has closed.");active->capture_transport(*out);return 1;}
            catch(const std::exception& error) {recomp_netplay_host_set_runtime_error(error.what());return 0;}
        };
        callbacks.memcard_offer_set=nullptr;callbacks.guest_memcard_get=nullptr;callbacks.guest_memcard_set=nullptr;
        callbacks.allow_spectators_get=[](void*){return 0;};
        callbacks.allow_spectators_set=[](void*,int value){return value==0?0:-1;};
        callbacks.lobby_allow_spectators=[](void*){return 0;};
        callbacks.lobby_max_spectators=[](void*){return 0;};
        if(callbacks.input_delay_set)callbacks.input_delay_set(callbacks.ctx,3);
        if(callbacks.input_prediction_set)callbacks.input_prediction_set(callbacks.ctx,6);
        if(callbacks.rollback_set)callbacks.rollback_set(callbacks.ctx,1);
        if(callbacks.force_input_relay_set)callbacks.force_input_relay_set(callbacks.ctx,0);
        callbacks.input_delay_set=[](void* context,int value) {
            auto base=recomp_netplay_host_callbacks();
            return value>=3&&value<=8&&base->input_delay_set?base->input_delay_set(context,value):-1;
        };
        callbacks.input_prediction_get=[](void*){return 6;};
        callbacks.input_prediction_set=[](void*,int value){return value==6?0:-1;};
        callbacks.rollback_get=[](void*){return 1;};
        callbacks.rollback_set=[](void*,int value){return value==1?0:-1;};
        callbacks.force_input_relay_get=[](void*){return 0;};
        callbacks.force_input_relay_set=[](void*,int value){return value==0?0:-1;};
        callbacks.force_turn_get=nullptr;callbacks.force_turn_set=nullptr;
        active=this;
    }
    ~DesktopNetLobby(){if(active==this)active=nullptr;ice.reset();recomp_netplay_host_shutdown();}
};
DesktopNetLobby* DesktopNetLobby::active=nullptr;

void desktop_net_lobby(DesktopConfig& c,RecompLauncherCGameInfo& game) {
    if(c.package&&c.package->feature.plugin=="gbcllr.gen1.extended"&&c.extended_content)throw std::runtime_error("Extended content is not yet available for linked play.");
    if(c.gift_enabled||(c.package&&c.package->feature.gift))throw std::runtime_error("Native gifts require offline play. Unqueue the gift first.");
    if(c.sgb_requested)throw std::runtime_error("Online play requires a native Color session.");
    if(!coop_selection(c)&&c.package&&c.adaptive&&!gen2_selection(c))throw std::runtime_error("This enhancement has not been qualified for this cartridge pair. Select stock play first.");
    c.net_lobby.reset();
    c.net_lobby=std::make_shared<DesktopNetLobby>(c);
    if(!c.link_error.empty())recomp_netplay_host_set_runtime_error(c.link_error.c_str());
    game.netplay_supported=1;game.netplay=&c.net_lobby->callbacks;
}
void desktop_net_launch(DesktopConfig& c,const RecompLauncherCNetplayLaunch& launch,
    const gbcllr::Options& primary,const std::filesystem::path& save) {
    if((c.package&&c.package->feature.plugin=="gbcllr.gen1.extended"&&c.extended_content)||std::any_of(primary.mods.begin(),primary.mods.end(),[](const auto& f){return f.plugin=="gbcllr.gen1.extended";}))
        throw std::runtime_error("Extended content is not yet available for linked play.");
    if(c.gift_enabled||(c.package&&c.package->feature.gift)||std::any_of(primary.mods.begin(),primary.mods.end(),[](const auto& feature){return feature.gift!=0;}))
        throw std::runtime_error("Native gifts require offline play. Unqueue the gift first.");
    const bool coop=coop_selection(c);
    if(!coop&&c.package&&c.adaptive&&!gen2_selection(c))throw std::runtime_error("Select stock play before starting this cartridge pair.");
    if(launch.local_slot<0||launch.local_slot>1||launch.is_spectator||launch.host_spectates||
       launch.player_count!=2||!launch.rollback||launch.force_input_relay)
        throw std::runtime_error("Linked play requires two player consoles with rollback enabled.");
    if(launch.input_delay<3||launch.input_delay>8||(launch.input_prediction&&launch.input_prediction!=6))
        throw std::runtime_error("The lobby selected unsupported linked-play delay or prediction settings.");
    auto start=std::make_shared<DesktopNetStart>();start->options.local_slot=unsigned(launch.local_slot);
    for(int i=0;i<2;++i) {
        int position=recomp_launcher_netplay_dense_position(&launch,i);
        if(position<0||position>1)throw std::runtime_error("Both console seats must be occupied.");
        start->options.machine_slots[unsigned(i)]=unsigned(position);
    }
    unsigned local=start->options.machine_slots[start->options.local_slot],remote=local^1;
    if(coop) {
        if(!c.net_lobby||!c.net_lobby->coop||c.sgb_requested)throw std::runtime_error("Open the Xtreme 2 co-op lobby before starting.");
        auto& room=*c.net_lobby;
        start->images[0]=gbcllr::read_file(c.rom);start->machines[0]=primary;
        auto& machine=start->machines[0];machine.model=gbcllr::Model::cgb;
        if(!c.boot.empty())machine.boot_rom=gbcllr::read_file(c.boot);
        if(gbcllr::sha256(machine.boot_rom)!="b4f2e416a35eef52cba161b159c7c8523a92594facb924b3ede0d722867c50c7")throw std::runtime_error("Select the verified Color boot ROM for co-op.");
        gbcllr::NativeFeature feature;feature.plugin="gbcllr.xtreme2.coop";feature.player1_character=room.first;feature.weapon_cycle=room.shoulders;feature.smooth_gameplay=room.smooth;
        machine.mods={feature};start->options.topology=gbcllr::NetTopology::xtreme2_coop;
        start->options.coop_width=room.width;start->options.coop_shared=room.shared;
        start->write_progress=launch.local_slot==0;
        // Do not even inspect the guest's personal campaign path.
        if(start->write_progress){start->local_save=save;if(std::filesystem::exists(save))start->options.initial_host_battery=gbcllr::read_file(save);}
    } else {
    start->images[local]=gbcllr::read_file(c.rom);start->images[remote]=gbcllr::read_file(c.link_rom);
    for(const auto& image:start->images)if(primary.native&&GBCLLR_PROGRAM_COUNT) {
        auto sha=gbcllr::sha256(image);bool compiled=false;
        for(unsigned i=0;i<GBCLLR_PROGRAM_COUNT;++i)compiled|=sha==gbcllr_program_sha256[i];
        if(!compiled)throw std::runtime_error("This build does not include both selected cartridges.");
    }
    start->machines[local]=primary;start->machines[local].mods.clear();
    if(!c.boot.empty())start->machines[local].boot_rom=gbcllr::read_file(c.boot);
    start->machines[remote]=start->machines[local];
    if(!c.link_boot.empty())start->machines[remote].boot_rom=gbcllr::read_file(c.link_boot);
    for(unsigned i=0;i<2;++i) {
        auto& options=start->machines[i];auto sha=gbcllr::sha256(options.boot_rom);
        if(sha!="cf053eccb4ccafff9e67339d4e78e98dce7d1ed59be819d2a1ba2232c6fce1c7"&&
           sha!="b4f2e416a35eef52cba161b159c7c8523a92594facb924b3ede0d722867c50c7")
            throw std::runtime_error("Select the verified boot ROM for each console.");
        options.model=options.boot_rom.size()>256?gbcllr::Model::cgb:gbcllr::Model::dmg;
        if(start->images[i].size()>0x143&&start->images[i][0x143]==0xc0&&options.model!=gbcllr::Model::cgb)
            throw std::runtime_error("The selected cartridge needs a Game Boy Color boot ROM.");
    }
    if(gen2_pair(c)&&start->machines[0].model==gbcllr::Model::cgb&&start->machines[1].model==gbcllr::Model::cgb) {
        // Fixed, portable, value-preserving simulation observers on both
        // consoles. Local enable/aspect preferences control only host output,
        // so disabling a view never changes the negotiated guest profile.
        gbcllr::NativeFeature feature;feature.plugin="gbcllr.gen2.adaptive-view";
        for(auto& machine:start->machines)machine.mods={feature};
    } else if(c.package&&c.adaptive)throw std::runtime_error("This linked view requires two qualified CGB Gen2 cartridges.");
    if(!c.link_state.empty()) {
        start->options.initial_pair=gbcllr::read_file(c.link_state);
        // Disposable prepared sessions never replace the player's original.
        start->local_save=c.profile/"linked-native.sav";
    } else {
        if(std::filesystem::exists(save))start->options.initial_batteries[local]=gbcllr::read_file(save);
        if(!c.link_save.empty())start->options.initial_batteries[remote]=gbcllr::read_file(c.link_save);
        start->local_save=save;
    }
    }
    if(start->write_progress&&std::filesystem::exists(start->local_save))start->original_save_sha=gbcllr::sha256(gbcllr::read_file(start->local_save));
    start->options.bind=launch.bind_hostport;start->options.peer=launch.peer_hostport;start->options.session_id=launch.session_id;
    if(launch.transport_ice_hub) {
        if(!c.net_lobby)throw std::runtime_error("The Online lobby has closed.");
        c.net_lobby->capture_transport(launch);
        auto handover=std::move(c.net_lobby->ice);c.net_lobby->ice_session=0;
        start->options.start_transport=[handover](RNetSession* session){return handover->start(session);};
    } else if(!recomp_netplay_host_in_lan()&&!launch.transport_host) {
        if(!rnet_host_ice_available())throw std::runtime_error("Online ICE transport is unavailable in this build.");
        auto peer=std::make_shared<PeerIce>(unsigned(launch.local_slot));
        start->options.start_transport=[peer](RNetSession* session){return rnet_session_start_ice(session,&peer->config);};
        start->options.signal_transport=[](const RNetSignal& message) {
            int type=message.type;
            if(type==RNET_SIGNAL_LOCAL_SDP)type=RNET_SIGNAL_REMOTE_SDP;
            if(type==RNET_SIGNAL_LOCAL_CANDIDATE)type=RNET_SIGNAL_REMOTE_CANDIDATE;
            if(rnet_lobby_send_signal(type,message.flag,message.text))
                throw std::runtime_error("Could not send the Online connection signal.");
        };
        start->options.pump_transport=[](RNetSession* session) {
            rnet_lobby_pump();
            bool running=rnet_session_is_running(session)!=0;
            if(!running&&!rnet_lobby_connected())throw std::runtime_error("The Online lobby disconnected before the peer connected.");
            int type=0,flag=0;char text[2048]{};
            while(rnet_lobby_poll_signal(&type,&flag,text,sizeof text))if(!running) {
                RNetSignal message{};message.type=RNetSignalType(type);message.flag=uint8_t(flag);message.peer_slot=0xff;
                std::memcpy(message.text,text,sizeof text);rnet_session_push_signal(session,&message);
            }
        };
    }
    start->options.delay=unsigned(launch.input_delay);start->options.prediction=6;
    start->options.schedule=gbcllr::CableSchedule::cycle_continuations;
    c.net_start=std::move(start);
}

void desktop_net_save(const DesktopNetStart& start,const gbcllr::NetSession& session) {
    if(!start.write_progress)return;
    auto progress=session.confirmed_progress();
    auto current=std::filesystem::exists(start.local_save)?gbcllr::sha256(gbcllr::read_file(start.local_save)):std::string{};
    if(current!=start.original_save_sha)throw std::runtime_error("The native save changed outside this session; the existing file was preserved.");
    if(!progress.battery.empty())gbcllr::write_file_atomic(start.local_save,progress.battery);
    std::cerr<<"Confirmed native progress saved after "<<progress.frames<<" frames.\n";
}
