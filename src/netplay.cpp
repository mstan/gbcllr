#include <gbcllr/netplay.hpp>
#include <recomp_net/recomp_net.h>
#include <recomp_net/rb_driver.h>
#include <recomp_net/startup_profiles.h>
#include <retcomm_rbengine/snap_ring.h>
#include <retcomm_rbengine/mono_ms.h>
#include "identity.h"
#include "net_state.hpp"
#include "gen2_native_mods.hpp"
#include <algorithm>
#include <bit>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <limits>
#include <stdexcept>
#if defined(__i386__) || defined(__x86_64__)
#include <cpuid.h>
#endif

namespace gbcllr {
namespace {
uint32_t fingerprint(const std::string& s) {return rnet_checksum(s.data(),s.size());}
uint64_t word(std::span<const uint8_t> b,size_t at) {
    if(at>b.size()||b.size()-at<8)throw std::runtime_error("truncated local paired checkpoint");
    uint64_t n=0;for(unsigned i=0;i<8;++i)n|=uint64_t(b[at+i])<<(i*8);return n;
}
uint32_t cpu_features() {
#if defined(__i386__) || defined(__x86_64__)
    unsigned a,b,c,d;
    if(__get_cpuid(1,&a,&b,&c,&d))return (d&bit_SSE2)?1u:0u;
#endif
    return 0;
}
bool qualified_x64_abi(CableSchedule schedule) {
#if GBCLLR_QUALIFIED_X64_NET && !GBCLLR_NATIVE_BLOCKS && GBCLLR_CABLE_BUS_BATCHES
    if(schedule!=CableSchedule::cycle_continuations||std::endian::native!=std::endian::little||
       std::string(GBCLLR_NET_BUILD_TYPE)!="Release"||!(cpu_features()&1))return false;
    const std::string abi=GBCLLR_ABI_ID,cxx=GBCLLR_NET_CXX_ID;
    return (abi=="Windows-AMD64-GNU-15.2.0-8"&&cxx=="GNU-15.2.0")||
           (abi=="Linux-x86_64-GNU-13.3.0-8"&&cxx=="GNU-13.3.0");
#else
    return false;
#endif
}
}
namespace detail {
std::array<uint32_t,4> simulation_digest(std::span<const uint8_t> state,bool cycle) {
    if(state.size()<96)throw std::runtime_error("truncated local paired checkpoint");
    const auto left=word(state,16),right=word(state,24);
    if(left>state.size()-96||right>state.size()-96-left)
        throw std::runtime_error("invalid local pair length");
    uint32_t master=0x811c9dc5u;
    // Independent dependency chains let the compiler overlap master and part
    // accumulation. Every byte is represented exactly as in rnet_checksum.
    auto segment=[&](size_t at,size_t length) {
        uint32_t part=0x811c9dc5u;
        for(auto byte:state.subspan(at,length)) {
            master=(master^byte)*0x01000193u;
            part=(part^byte)*0x01000193u;
        }
        return part;
    };
    const auto header=segment(0,32);
    std::array<uint32_t,4> result{0,segment(32,size_t(left)),segment(32+size_t(left),size_t(right)),header};
    const auto tail=32+size_t(left+right);
    if(cycle) {
        const uint32_t parts[]{header,segment(tail,state.size()-tail-64)};
        result[3]=rnet_checksum(parts,sizeof parts);
        segment(state.size()-64,64);
    } else segment(tail,state.size()-tail);
    result[0]=master;return result;
}
// A network digest projects only the qualified, fixed-size local ABI tags
// and the checksums that include them. Guest payload and continuation inputs
// remain byte-for-byte represented; native rollback files are never changed.
std::vector<uint8_t> simulation_state(std::span<const uint8_t> input,bool validate) {
    if(input.empty())return {};
    const std::array<uint8_t,8> magic{'G','B','P','A','I','R',4,0};
    if(input.size()<96||input.size()>16*1024*1024||!std::equal(magic.begin(),magic.end(),input.begin()))
        throw std::runtime_error("qualified network profile requires current cycle checkpoints");
    if(validate&&sha256(input.first(input.size()-64))!=std::string(reinterpret_cast<const char*>(input.data()+input.size()-64),64))
        throw std::runtime_error("initial paired checkpoint checksum mismatch");
    std::vector<uint8_t> state(input.begin(),input.end());
    const std::string abi=GBCLLR_ABI_ID;
    const auto abi_hash=sha256({reinterpret_cast<const uint8_t*>(abi.data()),abi.size()});
    auto machine=[&](size_t at,uint64_t length) {
        if(at>state.size()-64||length<260||length>state.size()-64-at)
            throw std::runtime_error("invalid qualified checkpoint machine length");
        auto data=input.subspan(at,size_t(length));
        auto format=uint32_t(data[0])|uint32_t(data[1])<<8|uint32_t(data[2])<<16|uint32_t(data[3])<<24;
        if(format!=Machine::snapshot_schema&&format!=Machine::partial_snapshot_schema)throw std::runtime_error("qualified checkpoint machine schema mismatch");
        if(validate&&(std::string(reinterpret_cast<const char*>(data.data()+68),64)!=abi_hash||
           sha256(data.first(data.size()-64))!=std::string(reinterpret_cast<const char*>(data.data()+data.size()-64),64)))
            throw std::runtime_error("initial checkpoint must match the local compiler ABI and checksum");
        std::fill_n(state.begin()+at+68,64,0);
        std::fill_n(state.begin()+at+size_t(length)-64,64,0);
    };
    auto left=word(input,16),right=word(input,24);
    if(left>state.size()-96||right>state.size()-96-left)
        throw std::runtime_error("invalid qualified checkpoint pair lengths");
    machine(32,left);machine(32+size_t(left),right);
    size_t cursor=32+size_t(left+right)+16;
    for(unsigned i=0;i<2;++i) {
        auto size=word(input,cursor+56),events=word(input,cursor+64);
        if(events>65536)throw std::runtime_error("invalid qualified checkpoint event count");
        machine(cursor+80,size);cursor+=80+size_t(size);
        if(cursor>state.size()-64||events>(state.size()-64-cursor)/40)
            throw std::runtime_error("invalid qualified checkpoint event lengths");
        cursor+=size_t(events)*40;
    }
    if(cursor!=state.size()-64)throw std::runtime_error("qualified checkpoint trailing data");
    std::fill_n(state.end()-64,64,0);return state;
}
}
using detail::simulation_state;
struct NetSession::Impl {
    static Impl* owner;
    std::array<std::vector<uint8_t>,2> roms;
    std::array<Options,2> machines;
    NetOptions options;
    std::unique_ptr<CablePair> cable;
    std::unique_ptr<Machine> world;
    std::unique_ptr<RbeSnapRing,decltype(&rbe_snap_ring_destroy)> ring{nullptr,rbe_snap_ring_destroy};
    std::unique_ptr<RNetSession,decltype(&rnet_session_destroy)> session{nullptr,rnet_session_destroy};
    std::unique_ptr<RNetRbDriver,decltype(&rnet_rb_driver_destroy)> driver{nullptr,rnet_rb_driver_destroy};
    RNetSession* binding=nullptr;
    int local=0,slots=2,delay=3,prediction=6;
    uint32_t created=rbe_mono_ms(),tick=0,profile=0,transcript=0,hold=UINT32_MAX;
    bool started=false,resim=false,hold_armed=false,canonical=false,gen2_view=false,coop=false;
    bool campaign_sent=false,campaign_ready=false;
    unsigned view_width=256,view_height=144; // Host only; never serialized.
    unsigned coop_view=1;
    std::string error,identity,build_identity;
    std::array<LogicalInput,2> buttons{};
    uint64_t presented_until=0; // Host-only; rewinds never make heard frames new.
    std::vector<uint8_t> cached;
    RNetRbDigestParts digest{};
    // One opt-in, host-only phase sample; inclusive poll time is reported
    // separately from the non-overlapping execution/cache/copy/load phases.
    bool phase_sample=std::getenv("GBCLLR_NET_PHASE_SAMPLE")!=nullptr;
    std::array<double,5> phase_seconds{};
    struct PhaseSample {
        Impl& owner;unsigned phase;std::chrono::steady_clock::time_point start{};
        PhaseSample(Impl& x,unsigned p):owner(x),phase(p) {if(x.phase_sample)start=std::chrono::steady_clock::now();}
        ~PhaseSample(){if(owner.phase_sample)owner.phase_seconds[phase]+=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();}
    };

    Impl(std::array<std::vector<uint8_t>,2> images,std::array<Options,2> mo,NetOptions no):
        roms(std::move(images)),machines(std::move(mo)),options(std::move(no)) {
        if(owner)throw std::runtime_error("only one recomp-net driver may run per process");
        if(options.topology!=NetTopology::cable&&options.topology!=NetTopology::xtreme2_coop)throw std::runtime_error("unknown network topology");
        coop=options.topology==NetTopology::xtreme2_coop;
        for(const auto& machine:machines)if(machine.sgb_service)
            throw std::runtime_error("Native SGB service is not qualified for connected play");
        if(options.local_slot>1||options.delay<3||options.delay>8||options.prediction<2||options.prediction>16||
           options.snapshot_depth<32||options.snapshot_depth>256||!options.session_id)
            throw std::runtime_error("invalid network slot/delay/prediction/snapshot budget");
        if(options.machine_slots[0]>1||options.machine_slots[1]>1||options.machine_slots[0]==options.machine_slots[1])
            throw std::runtime_error("network console assignment must contain both consoles exactly once");
        bool batteries=!options.initial_batteries[0].empty()||!options.initial_batteries[1].empty();
        bool modules=!machines[0].mods.empty()||!machines[1].mods.empty();
        if(coop) {
            if(sha256(roms[0])!="e37a14a8e61bd64f2226dacc26a7ee75dd90abfcf88c5016eb97b76ecda0ddca"||
               !roms[1].empty()||!machines[1].mods.empty()||machines[0].model!=Model::cgb||machines[0].mods.size()!=1||machines[0].mods[0].plugin!="gbcllr.xtreme2.coop"||
               machines[0].mods[0].view_width!=-1||machines[0].mods[0].gift||machines[0].mods[0].expanded_encounters||
               options.coop_width<160||options.coop_width>4096||batteries||!options.initial_pair.empty()||
               (!options.initial_machine.empty()&&!options.initial_host_battery.empty())||
               (options.local_slot&&!options.initial_host_battery.empty()))
                throw std::runtime_error("invalid one-world Xtreme2 co-op profile or host campaign");
        } else if(!options.initial_machine.empty()||!options.initial_host_battery.empty()||options.sample_logical)
            throw std::runtime_error("logical co-op startup/input requires the one-world topology");
        else if(modules) {
            if(!Gen2NativeMods::linked_pair(sha256(roms[0]),sha256(roms[1])))
                throw std::runtime_error("native linked views require the qualified Gold/Silver cartridge pair");
            for(const auto& machine:machines) {
                if(!Gen2NativeMods::linked_profile(machine.mods,machine.model==Model::cgb))
                    throw std::runtime_error("connected native modules require a qualified common simulation profile");
            }
            gen2_view=true;
        }
        if(batteries&&!options.initial_pair.empty())
            throw std::runtime_error("choose native saves or a paired checkpoint for network startup");
        // Validate local inputs before opening the transport. Nothing advances
        // until the common profile and starting state have been accepted.
        if(coop) {
            world=std::make_unique<Machine>(roms[0],machines[0]);std::string why;
            if(!options.initial_machine.empty()) {
                if(!world->restore(options.initial_machine,why))throw std::runtime_error(why);
                campaign_ready=true;
            } else if(!options.initial_host_battery.empty()&&!world->load_battery(options.initial_host_battery,why))throw std::runtime_error(why);
            world->mod_viewport(options.coop_width,144);world->mod_coop_shared_world(options.coop_shared);
            world->mod_coop_view(1,options.machine_slots[options.local_slot]);
        } else cable=std::make_unique<CablePair>(roms[0],machines[0],roms[1],machines[1],options.schedule);
        for(unsigned i=0;i<2;++i)if(!options.initial_batteries[i].empty()) {
            std::string why;
            if(!cable->machine(i).load_battery(options.initial_batteries[i],why))throw std::runtime_error(why);
        }
        if(!options.initial_pair.empty()) {
            std::string why;if(!cable->restore(options.initial_pair,why))throw std::runtime_error(why);
        }
        local=int(options.local_slot);delay=int(options.delay);prediction=int(options.prediction);
        canonical=qualified_x64_abi(options.schedule);
        build_identity=canonical?std::string(GBCLLR_NET_SIM_ID):std::string(GBCLLR_NET_BUILD_ID)+":"+GBCLLR_ABI_ID;
        identity=build_identity;
        identity+=coop?"\nmodule-profile:xtreme2-coop-1":gen2_view?"\nmodule-profile:gen2-presentation-1":"\nmodule-profile:stock";
        for(unsigned i=0;i<(coop?1u:2u);++i)identity+="\n"+sha256(roms[i])+":"+sha256(machines[i].boot_rom)+":"+
            std::to_string(unsigned(machines[i].model))+":"+std::to_string(machines[i].seed)+":"+std::to_string(machines[i].rtc_epoch);
        auto initial=coop?(canonical&&!options.initial_machine.empty()?world->simulation_snapshot():options.initial_machine):
            (canonical?simulation_state(options.initial_pair,true):options.initial_pair);
        identity+="\n"+sha256(initial)+":"+std::to_string(delay)+":"+std::to_string(prediction)+":"+std::to_string(unsigned(options.schedule));
        if(coop)identity+="\ncoop:"+std::to_string(options.coop_width)+":"+std::to_string(options.coop_shared)+":"+
            std::to_string(machines[0].mods[0].player1_character)+":"+std::to_string(machines[0].mods[0].weapon_cycle)+":"+std::to_string(machines[0].mods[0].smooth_gameplay);
        for(const auto& battery:options.initial_batteries)identity+="\nbattery:"+sha256(battery);
        identity+="\nconsoles:"+std::to_string(options.machine_slots[0])+":"+std::to_string(options.machine_slots[1]);
        RNetConfig cfg{};rnet_config_init_defaults(&cfg);cfg.local_slot=uint8_t(local);
        cfg.input_delay=uint8_t(delay);cfg.session_id=options.session_id;
        RNetHostVTable host{};host.ctx=this;host.sample_local=sample;host.publish=unused_publish;
        if(options.signal_transport)host.on_signal=signal;
        session.reset(rnet_session_create(&cfg,&host));
        ring.reset(rbe_snap_ring_create(options.snapshot_depth));driver.reset(rnet_rb_driver_create());
        if(!session||!ring||!driver)throw std::runtime_error("network session allocation failed");
        binding=session.get();
        // Profile zero is the functioning portable implementation of the chosen
        // scheduler. Different scheduling semantics have distinct qualification
        // namespaces; no startup fallback may silently change that selection.
        // Retain the compiler/ABI gate until mixed-ABI execution is qualified.
        RNetProfileCaps caps{};caps.cpu_features=cpu_features();caps.backends=1u|(GBCLLR_PROGRAM_COUNT?2u:0u);
        // The Gen2 read hook is value-preserving on every portable backend.
        // Both consoles keep the complete deterministic module snapshot;
        // only the host render output dimensions vary independently.
        caps.profiles=1u; // Functioning portable implementation in this namespace.
#if defined(__x86_64__)
        caps.architecture=1;
#elif defined(__aarch64__)
        caps.architecture=2;
#endif
        auto qualification=build_identity+":"+std::to_string(unsigned(options.schedule));
        if(coop)qualification+=":xtreme2-coop-1";
        if(gen2_view)qualification+=":gen2-presentation-1";
        if(rnet_session_profiles_begin(binding,fingerprint(qualification),&caps)!=0)
            throw std::runtime_error("network profile setup failed");
        auto transport=options.start_transport?options.start_transport(binding):
            rnet_session_start_lan(binding,options.bind.c_str(),options.peer.c_str());
        if(transport!=0)throw std::runtime_error("network transport setup failed");
        owner=this;
    }
    ~Impl() {
        if(phase_sample)std::cerr<<"gbcllr phase seconds: execute="<<phase_seconds[0]<<" cache="<<phase_seconds[1]
            <<" snapshot_copy="<<phase_seconds[2]<<" restore="<<phase_seconds[3]<<" poll_inclusive="<<phase_seconds[4]<<'\n';
        if(started)rnet_rb_driver_shutdown(driver.get());
        if(session)rnet_session_send_bye(session.get());
        if(owner==this)owner=nullptr;
    }
    static Impl& self(void* p) {return *static_cast<Impl*>(p);}
    template<class F> static int guard(void* p,F f) noexcept {
        try {f(self(p));return 1;}catch(const std::exception& e){self(p).error=e.what();return 0;}
        catch(...){self(p).error="network host callback failed";return 0;}
    }
    static void sample(uint32_t t,RNetInputSample* out,void* p) noexcept {
        *out={};out->valid=1;
        // recomp-net asks for the current SIM tick, then stores that sample
        // at wire=sim+D. The engine provider is explicitly indexed by wire.
        guard(p,[&](Impl& x){out->size=x.coop?2:1;auto wire=t+uint32_t(x.delay);
            LogicalInput input=x.options.sample_logical?x.options.sample_logical(wire):LogicalInput{x.options.sample?x.options.sample(wire):uint8_t(0),0};
            out->bytes[0]=input.buttons;if(x.coop)out->bytes[1]=input.virtual_buttons&3;
        });
    }
    static void unused_publish(uint32_t,const RNetInputSample*,int,void*) {}
    static void signal(const RNetSignal* message,void* p) noexcept {
        if(message)guard(p,[&](Impl& x){if(x.options.signal_transport)x.options.signal_transport(*message);});
    }
    void cache() {
        if(!cached.empty())return;
        PhaseSample sample(*this,1);
        cached=coop?world->snapshot():cable->snapshot();
        auto projected=canonical?(coop?world->simulation_snapshot():simulation_state(cached,false)):std::vector<uint8_t>{};
        std::span<const uint8_t> compared=canonical?std::span<const uint8_t>(projected):std::span<const uint8_t>(cached);
        if(coop){digest.master=rnet_checksum(compared.data(),compared.size());
            for(unsigned i=0;i<3;++i){auto begin=compared.size()*i/3,end=compared.size()*(i+1)/3;digest.part[i]=rnet_checksum(compared.data()+begin,end-begin);}return;}
        const auto hashes=detail::simulation_digest(compared,options.schedule==CableSchedule::cycle_continuations);
        digest.master=hashes[0];
        std::copy_n(hashes.begin()+1,3,digest.part);
    }
    static int save(void* p,uint32_t t) noexcept {return guard(p,[&](Impl& x){
        if(t!=x.tick)throw std::runtime_error("snapshot tick differs from connected simulation");
        x.cache();PhaseSample sample(x,2);auto data=static_cast<uint8_t*>(std::malloc(x.cached.size()));
        if(!data)throw std::bad_alloc();std::memcpy(data,x.cached.data(),x.cached.size());
        if(!rbe_snap_ring_store(x.ring.get(),t,data,x.cached.size())) {std::free(data);throw std::runtime_error("network snapshot store failed");}
    });}
    static int load(void* p,uint32_t t) noexcept {return guard(p,[&](Impl& x){
        PhaseSample sample(x,3);
        size_t n=0;auto data=rbe_snap_ring_peek(x.ring.get(),t,&n);std::string why;
        if(!data||!(x.coop?x.world->restore({data,n},why):x.cable->restore({data,n},why)))throw std::runtime_error("network snapshot restore failed: "+why);
        x.tick=t;x.cached.clear();if(x.coop)x.world->take_audio();else for(unsigned i=0;i<2;++i)x.cable->machine(i).take_audio();
        if(x.options.presentation_reset)x.options.presentation_reset();
    });}
    static int has(void* p,uint32_t t) {return rbe_snap_ring_has(self(p).ring.get(),t);}
    static int oldest(void* p,uint32_t* t) {auto& x=self(p);if(!rbe_snap_ring_count(x.ring.get()))return 0;*t=rbe_snap_ring_oldest_tick(x.ring.get());return 1;}
    static void drop(void* p,uint32_t t) {rbe_snap_ring_drop_after(self(p).ring.get(),t);}
    static void publish(void* p,uint32_t t,const RNetRbFrame* rows,int count,int) noexcept {guard(p,[&](Impl& x){
        if(count!=2||t!=x.tick)throw std::runtime_error("network publish differs from connected tick");
        for(unsigned i=0;i<2;++i)x.buttons[x.options.machine_slots[i]]={uint8_t(rows[i].buttons),uint8_t((rows[i].buttons>>8)&3)};
    });}
    static uint32_t master(void* p) noexcept {uint32_t value=0;guard(p,[&](Impl& x){x.cache();value=x.digest.master;});return value;}
    static void parts(void* p,RNetRbDigestParts* out) noexcept {guard(p,[&](Impl& x){x.cache();*out=x.digest;});}
    static void begin(void* p) {self(p).resim=true;guard(p,[](Impl& x){if(x.options.presentation_reset)x.options.presentation_reset();});}
    static void end(void* p) {self(p).resim=false;}
    static void decode(void* p,int,const RNetInputSample* in,RNetRbFrame* out) {bool coop=self(p).coop;out->buttons=in->size==(coop?2:1)?uint32_t(in->bytes[0])|(coop?uint32_t(in->bytes[1]&3)<<8:0u):0;}
    static void sanitize(void* p,int,RNetRbFrame* row) {row->buttons&=self(p).coop?1023:255;row->stick_x=row->stick_y=0;row->analog=row->rx=row->ry=0;}
    static void neutral(void*,int,RNetRbFrame* row) {*row={};row->is_valid=1;}
    static void refused(void* p) {self(p).error="network match refused";}
    static uint32_t now(void*) {return rbe_mono_ms();}
    static void log(void* p,const char* line) noexcept {guard(p,[&](Impl& x){if(x.options.log)x.options.log(line);else std::cerr<<line;});}
    static int check_modset(const char* want,char* reason,uint32_t n) {
        if(owner&&want&&owner->identity==want)return 0;
        if(reason&&n) {const char* s="connected build/content differs";std::strncpy(reason,s,n-1);reason[n-1]=0;}return 1;
    }
    void start() {
        RNetRbHost h{};h.ctx=this;h.snap_save=save;h.snap_load=load;h.snap_has=has;h.snap_oldest=oldest;
        h.snap_drop_after=drop;h.publish=publish;h.resim_begin=begin;h.resim_end=end;
        h.digest_master=master;h.digest_parts=parts;h.decode_sample=decode;h.sanitize_row=sanitize;
        h.neutral_row=neutral;h.request_return_to_lobby=refused;h.log=log;h.now_ms=now;
        RNetRbDriverConfig c{};c.session=&binding;c.local_slot=&local;c.slot_count=&slots;c.input_delay=&delay;c.input_prediction=&prediction;
        c.replay_mode=RNET_RB_REPLAY_INCREMENTAL;c.part_names[0]=coop?"world-a":"left";c.part_names[1]=coop?"world-b":"right";
        c.part_names[2]=coop?"world-c":options.schedule==CableSchedule::cycle_continuations?"continuation":"wire";
        c.snap_depth=options.snapshot_depth;c.log_prefix="gbcllr";c.env_alias="GBCLLR_RB";c.inject_flip_bits=1;
        rnet_rb_driver_set_identity(driver.get(),fingerprint(build_identity),fingerprint(identity+":"+std::to_string(profile)+":"+std::to_string(transcript)));
        rnet_rb_driver_set_modset(driver.get(),identity.c_str(),check_modset,nullptr);
        if(!rnet_rb_driver_start(driver.get(),&c,&h))throw std::runtime_error("rollback driver start failed");
        started=true;
    }
    bool campaign_startup() {
        if(!coop||campaign_ready)return true;
        if(!local&&!campaign_sent) {
            auto battery=world->battery();
            if(battery.empty()||rnet_session_state_begin(binding,RNET_STATE_OP_SRAM,0,battery.data(),battery.size()))
                throw std::runtime_error("host campaign startup transfer failed");
            campaign_sent=true;
        }
        int from=-1;uint8_t op=0,slot=0;const void* data=nullptr;size_t size=0;
        if(!rnet_session_state_take_ready_from(binding,&from,&op,&slot,&data,&size))return false;
        if(from!=0||op!=RNET_STATE_OP_SRAM||slot||!data||!size||size>128*1024)
            throw std::runtime_error("unexpected host campaign startup payload");
        std::span bytes(static_cast<const uint8_t*>(data),size);std::string why;
        if(local&&!world->load_battery(bytes,why))throw std::runtime_error("host campaign startup refused: "+why);
        identity+="\nhost-campaign:"+sha256(bytes);campaign_ready=true;
        rnet_session_state_finish(binding,0);return true;
    }
    bool poll() {
        PhaseSample sample(*this,4);
        if(options.pump_transport)options.pump_transport(binding);
        rnet_session_pump(binding);
        if(!error.empty())throw std::runtime_error(error);
        auto status=rnet_session_profiles_status(binding,&profile,&transcript);
        if(status<0)throw std::runtime_error("network simulation profile refused");
        if(!started) {
            if(uint32_t(rbe_mono_ms()-created)>(coop?30000u:10000u))throw std::runtime_error("network startup timed out");
            if(status!=1||!rnet_session_is_running(binding))return false;
            if(!campaign_startup())return false;
            start();
        }
        // recomp-net's hold also forbids prediction while approaching it.
        // Arm at the final boundary, so a future QA/export stop does not
        // silently turn the entire live match into lockstep.
        if(!hold_armed&&hold!=UINT32_MAX&&uint64_t(tick)+1>=hold) {
            rnet_rb_driver_set_hold(driver.get(),hold);hold_armed=true;
        }
        auto admitted=rnet_rb_driver_poll_admit(driver.get());
        if(auto why=rnet_rb_driver_refusal(driver.get()))throw std::runtime_error(why);
        if(!error.empty())throw std::runtime_error(error);
        auto q=rnet_rb_driver_quiesce_state(driver.get());
        if(q==RNET_RB_QUIESCE_TIMED_OUT)throw std::runtime_error("network drain timed out");
        if(q==RNET_RB_QUIESCE_NONE&&rnet_session_peer_disconnected(binding,5000))throw std::runtime_error("network peer disconnected");
        if(admitted==RNET_RB_ADMIT_STALL)return false;
        uint32_t frame=tick;bool present=!resim&&uint64_t(frame)>=presented_until&&(bool(options.present)||bool(options.present_view));
        {PhaseSample execute(*this,0);if(coop)world->run_frame(buttons[0],buttons[1]);else cable->run_frame(buttons[0].buttons,buttons[1].buttons);}
        std::vector<int16_t> local_audio;
        if(coop){auto samples=world->take_audio();if(present)local_audio=std::move(samples);}
        else for(unsigned i=0;i<2;++i) {
            auto samples=cable->machine(i).take_audio();
            if(present&&i==options.machine_slots[options.local_slot])local_audio=std::move(samples);
        }
        ++tick;cached.clear();rnet_rb_driver_finish_frame(driver.get());
        if(!error.empty())throw std::runtime_error(error);
        // A tip-hold can replay only to an earlier target, then admit the
        // remaining catch-up frames as live. Those frames were already heard.
        // Keep the presentation watermark across both forms of correction.
        if(present) {
            presented_until=uint64_t(frame)+1;
            auto& machine=coop?*world:cable->machine(options.machine_slots[options.local_slot]);
            if(options.present_view) {
                unsigned w=view_width,h=view_height;
                if(coop&&options.coop_shared&&!coop_view){w=unsigned(std::clamp<uint64_t>(h?(uint64_t(w)*144+h/2)/h:256,options.coop_width,4096));h=144;}
                auto view=coop?machine.coop_presentation_view(w,h,int(options.machine_slots[options.local_slot])):gen2_view?machine.gen2_presentation_view(view_width,view_height):ModView{160,false,machine.pixels()};
                options.present_view(frame,view);
            }
            if(options.present)options.present(frame,machine,local_audio);
        }
        return true;
    }
};
NetSession::Impl* NetSession::Impl::owner=nullptr;
NetSession::NetSession(std::array<std::vector<uint8_t>,2> r,std::array<Options,2> m,NetOptions o):impl_(std::make_unique<Impl>(std::move(r),std::move(m),std::move(o))) {}
namespace {NetOptions logical_options(NetOptions o){o.topology=NetTopology::xtreme2_coop;return o;}}
NetSession::NetSession(std::vector<uint8_t> r,Options m,NetOptions o):impl_(std::make_unique<Impl>(std::array<std::vector<uint8_t>,2>{std::move(r),{}},std::array<Options,2>{std::move(m),{}},logical_options(std::move(o)))) {}
NetSession::~NetSession()=default;
bool NetSession::poll(){return impl_->poll();}
void NetSession::wait(unsigned ms){rnet_session_wait_recv(impl_->binding,int(std::min(ms,50u)));}
bool NetSession::ready() const{return impl_->started;}
bool NetSession::replaying() const{return impl_->resim;}
unsigned NetSession::local_console() const noexcept{return impl_->options.machine_slots[impl_->options.local_slot];}
void NetSession::presentation_viewport(unsigned width,unsigned height){impl_->view_width=width;impl_->view_height=height;}
void NetSession::presentation_coop_view(unsigned mode){if(!impl_->coop)throw std::runtime_error("this session has no co-op view");impl_->world->mod_coop_view(mode,local_console());impl_->coop_view=mode;}
void NetSession::frame_observer(FrameObserver observer){if(impl_->coop)impl_->world->frame_observer(observer);}
void NetSession::hold_at(uint32_t t){
    auto& x=*impl_;
    if(!t||t<x.tick||x.hold_armed)throw std::runtime_error("hold requires a positive future tick before its barrier is armed");
    x.hold=t;
}
bool NetSession::settled() const {
    auto& x=*impl_;
    return x.started&&x.tick>0&&x.error.empty()&&!x.resim&&!rnet_rb_driver_refusal(x.driver.get())&&
        rnet_rb_driver_hold_settled(x.driver.get())&&
        uint64_t(rnet_rb_driver_confirmed_through(x.driver.get()))+1>=x.tick;
}
void NetSession::quiesce(){if(!ready())throw std::runtime_error("network has not started");rnet_rb_driver_request_quiesce(impl_->driver.get());}
bool NetSession::drained() const{return ready()&&rnet_rb_driver_quiesce_state(impl_->driver.get())==RNET_RB_QUIESCE_DRAINED;}
NetStats NetSession::stats() const {
    auto& x=*impl_;NetStats s{};s.profile=x.profile;s.transcript=x.transcript;s.module_profile=x.coop?2u:x.gen2_view?1u:0u;
    if(x.started) {auto d=x.driver.get();s.tick=rnet_rb_driver_sim_tick(d);s.confirmed=rnet_rb_driver_confirmed_through(d);
        s.episodes=rnet_rb_driver_episode_count(d);s.predicted=rnet_rb_driver_invent_count(d);s.replayed=rnet_rb_driver_resim_ticks(d);}return s;
}
std::string NetSession::diagnostics() const {
    // Before profile/content agreement the driver has no simulation callbacks.
    // Its debug visitor calls those callbacks, so pending startup must be
    // reported without entering the uninitialized rollback driver.
    char text[1024]{};
    if(impl_->started)rnet_rb_driver_debug_state(impl_->driver.get(),text,sizeof text);
    else std::strcpy(text,"startup pending");
    auto s=stats();return std::string(text)+" tick="+std::to_string(s.tick)+" confirmed="+std::to_string(s.confirmed)+
        " held="+std::to_string(settled())+" drained="+std::to_string(drained());
}
const CablePair& NetSession::pair() const {if(!ready()||impl_->coop)throw std::runtime_error("this session has no active cable pair");return *impl_->cable;}
const Machine& NetSession::machine()const {if(!ready()||!impl_->coop)throw std::runtime_error("this session has no active one-world co-op machine");return *impl_->world;}
std::vector<uint8_t> NetSession::confirmed_battery() const {if(!settled()||(impl_->coop&&impl_->local))throw std::runtime_error("native saves require a settled hold and campaign authority");return impl_->coop?machine().battery():pair().machine(local_console()).battery();}
std::vector<uint8_t> NetSession::confirmed_snapshot() const {if(!settled())throw std::runtime_error("checkpoint requires a settled network hold");return impl_->coop?machine().snapshot():pair().snapshot();}
NetProgress NetSession::confirmed_progress() const {
    auto& x=*impl_;
    if((x.coop&&x.local)||!x.started||x.resim||!x.error.empty()||rnet_rb_driver_refusal(x.driver.get())||
       rnet_rb_driver_desync_count(x.driver.get()))throw std::runtime_error("network progress is not confirmed");
    auto through=rnet_rb_driver_confirmed_through(x.driver.get());
    // Zero also denotes the startup watermark before the boot digest agrees;
    // refuse that ambiguous first frame instead of exposing unconfirmed SRAM.
    if(!through||uint64_t(through)+1>x.tick)throw std::runtime_error("no confirmed network progress is available");
    uint32_t frames=through+1;std::vector<uint8_t> state;
    if(frames==x.tick)state=x.coop?x.world->snapshot():x.cable->snapshot();
    else {
        size_t size=0;auto saved=rbe_snap_ring_peek(x.ring.get(),frames,&size);
        if(!saved)throw std::runtime_error("confirmed network checkpoint is unavailable");
        state.assign(saved,saved+size);
    }
    if(x.coop){Machine confirmed(x.roms[0],x.machines[0]);std::string why;
        if(!confirmed.restore(state,why))throw std::runtime_error("confirmed checkpoint refused: "+why);
        return {frames,confirmed.battery()};}
    CablePair confirmed(x.roms[0],x.machines[0],x.roms[1],x.machines[1],x.options.schedule);
    std::string why;if(!confirmed.restore(state,why))throw std::runtime_error("confirmed checkpoint refused: "+why);
    return {frames,confirmed.machine(local_console()).battery()};
}
}
