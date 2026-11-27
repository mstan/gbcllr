// Manual owned-cartridge check. Link against the desktop host objects from
// the same build; no visible window, user profile, or original save is used.
#include "../src/desktop_net.hpp"
#include "../src/desktop_ui.hpp"
#include "../src/xtreme2_coop.hpp"
#include "recomp_netplay_host.h"
#include "recomp_net/host_ice.h"
#include <SDL.h>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
int desktop_network(const DesktopNetStart&,bool,DesktopConfig&);
int desktop_network(const DesktopNetStart&,bool,DesktopConfig&,const DesktopFrameQa&);
namespace gbcllr {void execute_commands(Machine&,const std::filesystem::path&,bool*);}
static void check(bool v,const char* why){if(!v)throw std::runtime_error(why);}
static int coop_devices(DesktopConfig& c,DesktopNetStart& start,const std::filesystem::path& save,std::span<const uint8_t> original) {
 using namespace gbcllr;
 check(!SDL_Init(SDL_INIT_GAMECONTROLLER),"SDL controller setup failed");SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS,"1");
 SDL_Joystick* devices[2]{};c.source=c.partner_source=2;c.shortcuts=false;
 for(unsigned i=0;i<2;++i){int index=SDL_JoystickAttachVirtual(SDL_JOYSTICK_TYPE_GAMECONTROLLER,SDL_CONTROLLER_AXIS_MAX,SDL_CONTROLLER_BUTTON_MAX,0);
  check(index>=0&&SDL_IsGameController(index),"Virtual controller unavailable");devices[i]=SDL_JoystickOpen(index);check(devices[i],"Virtual controller open failed");
  char guid[64]{};SDL_JoystickGetGUIDString(SDL_JoystickGetDeviceGUID(index),guid,sizeof guid);
  if(i){c.partner_guid=guid;c.partner_instance=uint32_t(SDL_JoystickInstanceID(devices[i]))+1;}
  else{c.gamepad_guid=guid;c.gamepad_instance=uint32_t(SDL_JoystickInstanceID(devices[i]))+1;}}
 // A second local device holds inputs throughout. It must never drive either
 // actor: network play owns only this peer's primary configured controller.
 SDL_JoystickSetVirtualButton(devices[1],SDL_CONTROLLER_BUTTON_DPAD_LEFT,1);SDL_JoystickSetVirtualButton(devices[1],SDL_CONTROLLER_BUTTON_RIGHTSHOULDER,1);SDL_JoystickUpdate();
 struct Probe {
  SDL_Joystick* device;unsigned wire;std::array<Xtreme2Coop::Actor,2> initial{},middle{};unsigned checks=0;
  static bool present(void* context,uint32_t frame,const Machine& game,std::span<const int16_t>){auto& p=*static_cast<Probe*>(context);
   auto button=[&](SDL_GameControllerButton b,int value){check(!SDL_JoystickSetVirtualButton(p.device,b,value),"Virtual network input failed");SDL_JoystickUpdate();};
   if(frame==40||frame==80||frame==140||frame==160||frame==220){auto state=std::make_unique<Xtreme2Coop>(0);state->restore(game.coop_state());auto actors=state->actors();
    if(frame==40){p.initial=actors;if(!p.wire)button(SDL_CONTROLLER_BUTTON_DPAD_LEFT,1);}
    if(frame==80){check(actors[1].x<p.initial[1].x&&actors[0].x==p.initial[0].x,"Host controller crossed network seats or unassigned device leaked input");p.middle=actors;++p.checks;}
    if(frame==140){check(actors[0].x>p.middle[0].x&&actors[1].x==p.middle[1].x,"Guest controller crossed network seats");++p.checks;}
    if(frame==160){check(state->weapons()==std::array<uint8_t,2>{0,0},"Unassigned local shoulders changed equipment");button(SDL_CONTROLLER_BUTTON_RIGHTSHOULDER,1);++p.checks;}
    if(frame==220){check(state->weapons()==std::array<uint8_t,2>{1,0},"Network shoulder input crossed seats or changed Zero's technique");++p.checks;}}
   if(frame==60&&!p.wire)button(SDL_CONTROLLER_BUTTON_DPAD_LEFT,0);
   if(frame==100&&p.wire)button(SDL_CONTROLLER_BUTTON_DPAD_RIGHT,1);
   if(frame==120&&p.wire)button(SDL_CONTROLLER_BUTTON_DPAD_RIGHT,0);
   if(frame==180)button(SDL_CONTROLLER_BUTTON_RIGHTSHOULDER,0);
   return frame<240;
  }
 } probe{devices[0],start.options.local_slot};
 unsigned shown=0;uint64_t pcm=0;start.options.present=[&](uint32_t,const Machine&,std::span<const int16_t> samples){++shown;pcm+=samples.size();};
 DesktopFrameQa qa{&probe,nullptr,Probe::present};check(desktop_network(start,true,c,qa)==0&&probe.checks==4&&shown>=240&&pcm,"Actual SDL network checks did not complete");
 if(start.options.local_slot)check(read_file(save)==std::vector<uint8_t>(original.begin(),original.end()),"Network device check wrote the guest campaign");
 std::cout<<"PASS actual SDL network device isolation, swapped seats, Virtual R, unused partner device and host-only file authority\n";return 0;
}
static int run(int argc,char** argv,DesktopConfig* retained=nullptr){try{
 using namespace gbcllr;
 check(argc==11,"ROM0 ROM1 BOOT BATTERY0 BATTERY1 NEW_PROFILE BIND PEER SLOT EXTERNAL required");
 unsigned slot=unsigned(std::stoul(argv[9]));bool external=std::stoi(argv[10])!=0;
 check(slot<2,"invalid local slot");DesktopConfig fresh;auto& c=retained?*retained:fresh;bool retry=retained!=nullptr;
 bool drop_host=SDL_getenv("GBCLLR_QA_DROP_HOST")!=nullptr,drop_peer=SDL_getenv("GBCLLR_QA_DROP_PEER")!=nullptr;
 if(!retry){c.profile=argv[6];
 check(!std::filesystem::exists(c.profile),"QA needs a new private profile");std::filesystem::create_directories(c.profile);
 c.scale=1;c.volume=0;c.rom=argv[1+slot];c.link_rom=argv[2-slot];c.boot=argv[3];c.link_save=argv[5-slot];
 if(auto package=SDL_getenv("GBCLLR_QA_MOD")) {
  c.package=read_mod_package(package,sha256(read_file(c.rom)));
  c.adaptive=slot==0;c.view_width=slot?336:-1;c.persist();
  DesktopConfig saved;saved.profile=c.profile;saved.load();
  check(saved.adaptive==c.adaptive&&saved.view_width==c.view_width,"local linked choices did not persist");
 }
 if(auto pair=SDL_getenv("GBCLLR_QA_PAIR"))c.link_state=pair;
 if(SDL_getenv("GBCLLR_QA_COOP")){
  check(c.package.has_value(),"Co-op QA needs the bundled package");c.coop_enabled=true;c.weapon_cycle=!slot;c.coop_first=slot;c.coop_shared=slot!=0;
  c.smooth_gameplay=SDL_getenv("GBCLLR_QA_SMOOTH")&&!slot;
  // Deliberately different guest preferences must be adopted only for the room.
  c.link_rom.clear();c.link_save.clear();c.persist();
 }
 }
 const auto original=read_file(argv[4+slot]);auto save=c.profile/"native.sav";if(!retry)write_file_atomic(save,original);
 Options primary;primary.boot_rom=read_file(c.boot);RecompLauncherCGameInfo game{};desktop_net_lobby(c,game);
 auto cb=game.netplay;bool online=SDL_getenv("GBCLLR_QA_ONLINE")!=nullptr;
 check(cb&&game.netplay_supported&&(!online||cb->connect),"linked bridge callbacks missing");
 if(!rnet_host_ice_available())check(!cb->connect,"non-ICE build advertises an unavailable Online path");
 check(cb->input_delay_get(cb->ctx)==3&&cb->input_delay_set(cb->ctx,2)<0&&cb->input_prediction_get(cb->ctx)==6,
       "lobby offers an unsupported simulation setting");
 cb->set_player_name(cb->ctx,slot?"gbcllr-qa-two":"gbcllr-qa-one");RecompLauncherCSettings settings{};
 char endpoint[64]{};std::snprintf(endpoint,sizeof endpoint,"%s",argv[7]);
 const char* room=online?"gbcllr bounded private Online":"gbcllr bounded LAN";
 if(online) {
  check(cb->connect(cb->ctx)==0,"private Online connect failed");
  auto until=std::chrono::steady_clock::now()+std::chrono::seconds(15);
  while(!rnet_lobby_ready()) {check(std::chrono::steady_clock::now()<until,"private server welcome timed out");cb->pump(cb->ctx);SDL_Delay(2);}
 }
 if(!slot){check(cb->create(cb->ctx,room,endpoint,"",&settings,online?0:1,2)==0,"lobby create failed");
  if(SDL_getenv("GBCLLR_QA_SWAP"))check(cb->move_member(cb->ctx,0,1)==0,"host console seat move failed");}
 else if(!online){std::string id="lan:"+std::string(argv[8]);check(cb->join(cb->ctx,id.c_str(),"",endpoint)==0,"Direct IP join failed");}
 auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(45);bool requested=false,joined=!online||!slot,ready_sent=false;
 unsigned polls=0,proof_polls=0;
 while(!cb->launch_pending(cb->ctx)) {
  if(std::chrono::steady_clock::now()>=deadline) {
   char status_text[1024]{};if(cb->relay_status)cb->relay_status(cb->ctx,status_text,sizeof status_text);
   RNetHostIceStatus status{};rnet_lobby_host_ice_status(&status);
   std::cerr<<"lobby members="<<cb->member_count(cb->ctx)<<" ready="<<cb->all_ready(cb->ctx)
    <<" requested="<<requested<<" ice_role="<<status.role<<" peers="<<status.peer_count<<" completed="<<status.completed
    <<" reports="<<status.reports_sent<<" status="<<status_text<<" error="<<cb->last_error(cb->ctx)<<'\n';
   for(int i=0;i<rnet_lobby_member_count();++i){RNetLobbyMember member{};if(rnet_lobby_member_get(i,&member))
    std::cerr<<"member slot="<<member.slot<<" ready="<<member.ready<<" path="<<member.path<<" fresh="<<member.path_fresh<<'\n';}
   throw std::runtime_error("shared lobby launch timed out");
  }cb->pump(cb->ctx);
  if(!ready_sent&&cb->member_count(cb->ctx)>0){check(cb->set_ready(cb->ctx,1)==0,"shared ready failed");ready_sent=true;}
  if(!joined) {
   if(++polls%100==1)cb->request_list(cb->ctx);
   for(int i=0;i<cb->list_count(cb->ctx)&&!joined;++i) {RecompLauncherCNetplayLobby row{};
    if(cb->list_get(cb->ctx,i,&row)&&std::string(row.name)==room) {
     check(cb->join(cb->ctx,row.lobby_id,"",endpoint)==0,"Online join failed");joined=true;
    }
   }
  }
  if(!slot&&!requested&&cb->member_count(cb->ctx)==2&&cb->all_ready(cb->ctx)) {
   if(online&&!SDL_getenv("GBCLLR_QA_EARLY_START")) {
    RNetHostIceStatus status{};if(!rnet_lobby_host_ice_status(&status)||status.completed!=1){SDL_Delay(2);continue;}
    // path_report is acknowledged without a room broadcast. A normal ready
    // publication refreshes that room snapshot; wait for the server's actual
    // proof so this check exercises completed-agent adoption specifically.
    if(++proof_polls%100==1)cb->set_ready(cb->ctx,1);
    bool proven=true;for(int i=0;i<rnet_lobby_member_count();++i){RNetLobbyMember member{};
     if(rnet_lobby_member_get(i,&member)&&!member.is_spectator&&std::string(member.player_id)!=rnet_lobby_player_id())
      proven&=member.path_fresh&&std::string(member.path)=="direct";
    }if(!proven){SDL_Delay(2);continue;}
   }
   check(cb->request_start(cb->ctx,&settings)==0,"shared lobby start failed");requested=true;
  }SDL_Delay(1);
 }
 RecompLauncherCNetplayLaunch launch{};check(cb->fill_launch(cb->ctx,&launch),"shared launch missing");
 check(cb->fill_launch(cb->ctx,&launch),"repeated shared launch inspection lost transport ownership");
 if(cb->clear_launch_pending)cb->clear_launch_pending(cb->ctx);
 std::cout<<"launch online="<<online<<" host_udp="<<launch.transport_host<<" host_ice="<<launch.transport_ice_hub<<'\n';
 desktop_net_launch(c,launch,primary,save);c.link_error.clear(); // Successful launcher start does the same.
 check(c.net_start&&c.net_start->options.local_slot==slot,"wrong local launch role");
 auto& start=*c.net_start;unsigned local=start.options.machine_slots[slot];
 check(local==(SDL_getenv("GBCLLR_QA_SWAP")?(slot^1):slot),"shared lobby console seat move was lost");
 check(start.options.delay==3&&start.options.prediction==6,"desktop start settings differ");
 bool coop=SDL_getenv("GBCLLR_QA_COOP")!=nullptr;
 if(coop){
  check(start.options.topology==NetTopology::xtreme2_coop&&start.images[1].empty()&&start.machines[1].mods.empty(),"Co-op created a second console");
  check(start.machines[0].mods.size()==1&&start.machines[0].mods[0].plugin=="gbcllr.xtreme2.coop"&&start.machines[0].mods[0].player1_character==0&&start.machines[0].mods[0].weapon_cycle&&!start.options.coop_shared,"Host co-op plan was not adopted");
  check(c.coop_first==slot&&c.weapon_cycle==!slot&&c.coop_shared==bool(slot),"Room adoption overwrote offline preferences");
  check(start.machines[0].mods[0].smooth_gameplay==bool(SDL_getenv("GBCLLR_QA_SMOOTH"))&&c.smooth_gameplay==bool(SDL_getenv("GBCLLR_QA_SMOOTH")&&!slot),"Host smooth-gameplay plan or guest offline preference differs");
  check(start.write_progress==!slot&&(!slot||start.local_save.empty())&&(!slot||start.options.initial_host_battery.empty()),"Guest personal save reached session startup");
  start.options.initial_host_battery.clear();start.options.initial_machine=read_file(SDL_getenv("GBCLLR_QA_COOP"));
 }else if(c.package)for(const auto& machine:start.machines) {
  check(machine.mods.size()==1&&machine.mods[0].plugin=="gbcllr.gen2.adaptive-view"&&
        machine.mods[0].view_width==-1&&!machine.mods[0].colorization,"desktop did not settle the fixed Gen2 profile");
 }
 if(coop&&SDL_getenv("GBCLLR_QA_DEVICES"))return coop_devices(c,start,save,original);
 if(auto movie=SDL_getenv("GBCLLR_QA_INPUTS")) {
  check(!c.link_state.empty()&&!SDL_getenv("GBCLLR_QA_SWAP"),"stock movie needs its prepared two-console order");
  std::ifstream input(movie);check(bool(input),"cannot read stock movie");std::vector<std::array<uint8_t,2>> pads;std::string line;
  while(std::getline(input,line)){std::istringstream in(line);std::string op,extra;if(!(in>>op)||op.starts_with('#'))continue;
   unsigned count=0,a=0,b=0;check(op=="frames"&&bool(in>>count>>a>>b)&&!(in>>extra)&&a<256&&b<256&&count<=1000000-pads.size(),"invalid stock movie");
   pads.insert(pads.end(),count,{uint8_t(a),uint8_t(b)});
  }check(!pads.empty(),"empty stock movie");auto delay=start.options.delay;
  start.options.sample=[&](uint32_t wire){return wire>=delay&&wire-delay<pads.size()?pads[wire-delay][slot]:uint8_t(0);};
  unsigned shown=0,views=0,expanded=0;uint32_t last=0,last_view=0;uint64_t samples=0;
  start.options.present=[&](uint32_t frame,const Machine& machine,std::span<const int16_t> pcm){
   check(machine.rom_identity()==sha256(start.images[local]),"remote console reached stock presentation");
   check(!shown||frame>last,"stock session repeated presented audio");last=frame;++shown;samples+=pcm.size();
   check(views==shown&&frame==last_view,"local view/audio watermark differs");
  };
  start.options.present_view=[&](uint32_t frame,const ModView& view) {
   check(views==shown&&(!views||frame>last_view),"local view repeated or omitted a live frame");last_view=frame;++views;expanded+=view.expanded;
   check(view.width==(c.package&&c.adaptive?256u:160u),"local view preference was lost");
  };
  NetSession session(start.images,start.machines,start.options);auto frames=uint32_t(pads.size()+delay);session.hold_at(frames);
  session.presentation_viewport(c.package&&c.adaptive?1280:160,c.package&&c.adaptive?720:144);
  auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(300);
  while(!session.settled()){check(std::chrono::steady_clock::now()<deadline,"stock Online session timed out");if(!session.poll())session.wait(1);}
  auto state=session.confirmed_snapshot(),battery=session.confirmed_battery();auto stats=session.stats();
  write_file_atomic(c.profile/"online.state",state);write_file_atomic(start.local_save,battery);
  session.pair().machine(local).write_frame(c.profile/"local-frame.ppm");
  session.quiesce();while(!session.drained()){check(std::chrono::steady_clock::now()<deadline,"stock session did not drain");session.poll();session.wait(1);}
  CablePair offline(start.images[0],start.machines[0],start.images[1],start.machines[1],start.options.schedule);std::string error;
  check(offline.restore(start.options.initial_pair,error),error.c_str());
  for(unsigned frame=0;frame<frames;++frame){auto pad=frame>=delay?pads[frame-delay]:std::array<uint8_t,2>{};offline.run_frame(pad[0],pad[1]);offline.machine(0).take_audio();offline.machine(1).take_audio();}
  check(state==offline.snapshot(),"stock Online full state differs from offline delayed input");
  check(battery==offline.machine(local).battery(),"stock Online confirmed native differs from offline");
  check(stats.episodes>0&&stats.replayed>0&&shown>0&&samples>0,"stock Online disruption/presentation was not exercised");
  check(views==shown&&(!c.package||stats.module_profile==1),"desktop local view/common module profile was not exercised");
  if(SDL_getenv("GBCLLR_QA_FIELD"))check((expanded>0)==(c.package&&c.adaptive),"actual linked field did not honor local enable");
  check(read_file(argv[4+slot])==original,"stock session changed original native input");
  std::cout<<"PASS stock Online slot="<<slot<<" frames="<<frames<<" corrections="<<stats.episodes<<" replayed="<<stats.replayed
   <<" profile="<<stats.profile<<" module_profile="<<stats.module_profile<<" views="<<views<<" expanded="<<expanded
   <<" transcript="<<stats.transcript<<" state="<<sha256(state)<<" native="<<sha256(battery)<<'\n';return 0;
 }
 if(!coop){if(start.options.initial_pair.empty())check(start.options.initial_batteries[local]==original,"local native start changed");
 check(start.local_save==(c.link_state.empty()?save:c.profile/"linked-native.sav"),"desktop native destination differs");}
 unsigned shown=0;uint32_t last=0;uint64_t samples=0;bool changed=false;
 start.options.present=[&](uint32_t frame,const Machine& machine,std::span<const int16_t> pcm) {
  check(machine.rom_identity()==sha256(start.images[coop?0:local]),"remote console reached desktop presentation");
  check(!shown||frame>last,"desktop repeated a presented frame");last=frame;++shown;samples+=pcm.size();
  if(frame==30||frame==60) {SDL_Event e{};e.type=SDL_KEYDOWN;e.key.keysym.scancode=SDL_SCANCODE_ESCAPE;SDL_PushEvent(&e);}
  if(frame==120&&!retry) {
   machine.write_frame(c.profile/"local-frame.ppm");auto window=SDL_GetWindowFromID(1);check(window,"desktop window missing");
   int width=0,height=0;check(SDL_GetRendererOutputSize(SDL_GetRenderer(window),&width,&height)==0&&width>0&&height>0,"desktop output size failed");
   std::vector<uint32_t> pixels(size_t(width)*height);check(SDL_RenderReadPixels(SDL_GetRenderer(window),nullptr,SDL_PIXELFORMAT_ARGB8888,pixels.data(),width*4)==0,"desktop framebuffer capture failed");
   std::ofstream out(c.profile/"desktop-frame.ppm",std::ios::binary);out<<"P6\n"<<width<<' '<<height<<"\n255\n";
   for(auto pixel:pixels){char rgb[]={char(pixel>>16),char(pixel>>8),char(pixel)};out.write(rgb,3);}
   if(external){const uint8_t sentinel[]={11,22,33,44};write_file_atomic(save,sentinel);changed=true;}
  }
  if(drop_peer&&frame==180){std::cout<<"Owned peer exits without drain at live frame "<<frame<<std::endl;std::_Exit(23);}
  if(frame==240&&!slot&&(!drop_host||retry)){SDL_Event e{};e.type=SDL_QUIT;SDL_PushEvent(&e);}
 };
 std::ostringstream diagnostics;auto previous=std::cerr.rdbuf(diagnostics.rdbuf());int result=0;
 try {result=desktop_network(start,true,c);}catch(...){std::cerr.rdbuf(previous);std::cerr<<diagnostics.str();throw;}
 std::cerr.rdbuf(previous);auto text=diagnostics.str();std::cout<<text;
 check(result==((external||(drop_host&&!retry))?2:0)&&shown>120&&samples>0,"desktop presentation or save-error return failed");
 if(drop_host&&!retry)check(c.link_error.find("disconnected")!=std::string::npos,"abrupt peer loss was not returned to the launcher");
 if(external) {const std::vector<uint8_t> sentinel{11,22,33,44};check(changed&&read_file(save)==sentinel,"external native save was overwritten");}
 else {
  if(coop&&slot){check(read_file(save)==original,"Guest personal save was modified");check(text.find("Confirmed native progress saved after ")==std::string::npos,"Guest wrote a campaign file");}
  else {
  const std::string marker="Confirmed native progress saved after ";auto at=text.find(marker);check(at!=std::string::npos,"desktop did not export confirmed progress");
  auto frames=uint32_t(std::stoul(text.substr(at+marker.size())));
  if(coop){Machine offline(start.images[0],start.machines[0]);std::string why;check(offline.restore(start.options.initial_machine,why),why.c_str());offline.mod_viewport(start.options.coop_width,144);offline.mod_coop_shared_world(start.options.coop_shared);
   for(uint32_t frame=0;frame<frames;++frame){offline.run_frame();offline.take_audio();}check(read_file(save)==offline.battery(),"Host file differs from confirmed one-world campaign");
  }else {
  CablePair offline(start.images[0],start.machines[0],start.images[1],start.machines[1],start.options.schedule);
  if(!start.options.initial_pair.empty()){std::string why;check(offline.restore(start.options.initial_pair,why),why.c_str());}
  else for(unsigned i=0;i<2;++i){std::string why;check(offline.machine(i).load_battery(start.options.initial_batteries[i],why),"offline native start refused");}
  for(uint32_t frame=0;frame<frames;++frame){offline.run_frame();offline.machine(0).take_audio();offline.machine(1).take_audio();}
  check(read_file(start.local_save)==offline.machine(local).battery(),"desktop save differs from confirmed offline local console");
  if(drop_host&&!retry) {
   auto native=read_file(start.local_save);write_file_atomic(c.profile/"drop-confirmed.sav",native);
   auto party=offline.machine(local).debug_read(0xda22,60),ot=offline.machine(local).debug_read(0xdb4a,11);
   auto command=c.profile/"drop-continue.commands";std::ofstream script(command);
   script<<"frames 3600 0\nframes 8 128\nframes 300 0\nframes 8 16\nframes 120 0\nframes 8 16\nframes 240 0\nuntil-pc 0x66b8 6000000 37\nframes 60 0\nexpect 0xda00 20 1 3 5\nexpect 0xda22";
   for(auto v:party)script<<' '<<unsigned(v);script<<"\nexpect 0xdb4a";for(auto v:ot)script<<' '<<unsigned(v);
   script<<"\ncapture \""<<(c.profile/"drop-continue.ppm").generic_string()<<"\"\nsave-state \""<<(c.profile/"drop-continue.state").generic_string()<<"\"\n";script.close();
   Machine continued(start.images[local],start.machines[local]);std::string why;check(continued.load_battery(native,why),why.c_str());bool rewound=false;execute_commands(continued,command,&rewound);check(!rewound,"unexpected native recovery rewind");
   std::cout<<"PASS abrupt-loss confirmed cut="<<frames<<" native="<<sha256(native)<<" full party/OT Continue\n";
  }
  }
  }
 }
 check(read_file(argv[4+slot])==original,"QA changed original native data");
 std::cout<<"PASS desktop local="<<slot<<" console="<<local<<" frames="<<shown<<" pcm_samples="<<samples<<" external_guard="<<external<<" retry="<<retry<<'\n';
 if(drop_host&&!retry) {
  c.link_lobby_requested=true;std::filesystem::path rom=c.rom,boot=c.boot,selected_save=save;Options next;
  if(auto package=SDL_getenv("GBCLLR_QA_MOD"))c.package_override=package;
  check(desktop_prepare(c,rom,boot,selected_save,c.profile,false,true,false,next),"launcher retry preparation failed");
  check(!c.net_start&&!c.link_retry&&c.link_lobby_requested,"launcher retry retained consumed session state");
  const uint8_t ready=1;write_file_atomic(c.profile/"retry-ready",{&ready,1});
  return run(argc,argv,&c);
 }
 return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
int main(int argc,char** argv){return run(argc,argv);}
