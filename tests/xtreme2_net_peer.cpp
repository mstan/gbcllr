#include <gbcllr/netplay.hpp>
#include "../src/xtreme2_coop.hpp"
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
using namespace gbcllr;
static void check(bool pass,const char* message){if(!pass)throw std::runtime_error(message);}
template<class F> static bool rejects(F f){try{f();return false;}catch(const std::runtime_error&){return true;}}
static LogicalInput pad(unsigned port,uint32_t t,bool bike,bool menu,bool save=false,bool campaign_save=false){
 LogicalInput p;if(t<3)return p;
 if(campaign_save){if(port&&t>=6&&t<14)p.buttons=16;return p;}
 if(save){if(port&&t>=6&&t<8)p.buttons=16;if(port&&t>=24&&t<26)p.buttons=32;return p;}
 if(bike){if(t<40)p.buttons=1;if(port&&t>=14&&t<22)p.buttons|=16;if(t>=22&&t<30)p.buttons|=32;return p;}
 if(menu){if(!port&&t>=14&&t<16)p.buttons=128;if(t>=160+port*8&&t<164+port*8)p.buttons=port?8:1;return p;}
 if(t<60)p.buttons=port?2:1;
 if(port&&t>=14&&t<22)p.buttons|=16;
 if(t>=32&&t<42)p.buttons|=32;
 if((t>=20&&t<28)||(t>=80&&t<88))p.virtual_buttons=virtual_r;
 if(t>=60&&t<68)p.virtual_buttons=virtual_l;
 return p;
}
int main(int argc,char** argv){try{
 check(argc==11,"ROM BOOT START slot bind peer session frames checkpoint|campaign swap required");
 auto rom=read_file(argv[1]);Options o;o.model=Model::cgb;o.boot_rom=read_file(argv[2]);
 std::string mode=argv[9];bool bike=mode=="checkpoint-bike",save=mode=="checkpoint-save",campaign_save=mode=="checkpoint-campaign-save",menu=mode=="checkpoint-menu"||save,shared=mode=="checkpoint-shared";
 o.mods={read_mod_package(std::filesystem::path(__FILE__).parent_path().parent_path()/"mods/xtreme2-coop.gbmod",sha256(rom)).feature};o.mods[0].weapon_cycle=!(bike||campaign_save);
 o.mods[0].smooth_gameplay=std::getenv("GBCLLR_QA_SMOOTH")!=nullptr;
 unsigned seat=unsigned(std::stoul(argv[4])),hold=unsigned(std::stoul(argv[8]));bool campaign=mode=="campaign",swap=std::stoi(argv[10])!=0;
 check(seat<2&&hold>=32&&hold<=1200,"Invalid owned co-op test seat/frame count");
 NetOptions n;n.local_slot=seat;n.bind=argv[5];n.peer=argv[6];n.session_id=uint32_t(std::stoul(argv[7]));n.coop_shared=shared;n.schedule=CableSchedule::cycle_continuations;
 if(swap)n.machine_slots={1,0};auto ports=n.machine_slots;
 auto initial=read_file(argv[3]);
 if(campaign){if(!seat)n.initial_host_battery=initial;}else n.initial_machine=initial;
 n.sample_logical=[=](uint32_t t){return pad(ports[seat],t,bike,menu,save,campaign_save);};
 NetSession* owned=nullptr;unsigned resets=0;size_t pcm_samples=0;std::vector<bool> shown(hold);
 n.present=[&](uint32_t t,const Machine& m,std::span<const int16_t> pcm){check(owned&&&m==&owned->machine()&&t<hold&&!shown[t],"One-world presentation ownership/replay watermark differs");shown[t]=true;pcm_samples+=pcm.size();};
 n.present_view=[&](uint32_t t,const ModView& view){check(t<hold&&view.pixels.size()==size_t(view.width)*144,"Invalid local co-op view");if(shared)check(view.width>=384,"A narrow network window cropped the agreed shared gameplay footprint");};
 n.presentation_reset=[&]{++resets;};
 if(seat){auto bad=n;bad.initial_machine.clear();bad.initial_host_battery=Machine(rom,o).battery();bool transport=false;
  bad.start_transport=[&](auto*){transport=true;return -1;};
  check(rejects([&]{NetSession refused(rom,o,bad);})&&!transport,"A guest campaign reached network startup");}
 NetSession net(rom,o,std::move(n));owned=&net;net.presentation_viewport(seat?512:256,144);if(menu||shared)net.presentation_coop_view(0);net.hold_at(hold);
 check(rejects([&]{net.confirmed_battery();})&&rejects([&]{net.confirmed_progress();}),"Startup exported an unconfirmed campaign");
 auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(75);NetProgress live;
 while(!net.settled()){
  if(std::chrono::steady_clock::now()>deadline)throw std::runtime_error("One-world startup/hold timeout: "+net.diagnostics());
  if(!net.poll())net.wait(1);
  if(net.ready()&&net.stats().tick>0&&!net.settled()){
   check(rejects([&]{net.confirmed_battery();}),"Speculative campaign export accepted");
   auto s=net.stats();if(!seat&&!live.frames&&s.confirmed&&s.tick>s.confirmed+1&&!net.replaying())live=net.confirmed_progress();
   if(seat)check(rejects([&]{net.confirmed_progress();}),"Guest exported a personal campaign during play");
  }
 }
 net.quiesce();while(!net.drained()){check(std::chrono::steady_clock::now()<deadline,"One-world drain timeout");net.poll();net.wait(1);}
 Machine offline(rom,o);std::string error;
 if(campaign)check(offline.load_battery(initial,error),error.c_str());else check(offline.restore(initial,error),error.c_str());
 offline.mod_viewport(384,144);offline.mod_coop_shared_world(shared);
 auto before_battery=save||campaign_save?offline.battery():std::vector<uint8_t>{};auto before_setting=save?offline.debug_read(0xc001,1)[0]:0;
 std::vector<uint8_t> expected_live;
 for(uint32_t t=0;t<hold;++t){std::array<LogicalInput,2> pads{};for(unsigned wire=0;wire<2;++wire)pads[ports[wire]]=pad(ports[wire],t,bike,menu,save,campaign_save);
  offline.run_frame(pads[0],pads[1]);offline.take_audio();
  if(t+1==live.frames)expected_live=offline.battery();
 }
 auto stats=net.stats();check(stats.tick==hold&&stats.module_profile==2,"One-world profile/hold differs");
 if(auto force=std::getenv("GBCLLR_RB_FORCE_MISPREDICT");force&&std::atoi(force)>0)
  check(stats.episodes&&stats.replayed&&resets&&(seat||live.frames),"Forced one-world replay or confirmed live host export was not exercised");
 check(net.confirmed_snapshot()==offline.snapshot(),"One-world rollback differs from authoritative offline inputs");
 if(offline.stats().instructions_native){
  auto reference_options=o;reference_options.native=false;Machine reference(rom,reference_options);
  if(campaign)check(reference.load_battery(initial,error),error.c_str());else check(reference.restore(initial,error),error.c_str());
  reference.mod_viewport(384,144);reference.mod_coop_shared_world(shared);
  for(uint32_t t=0;t<hold;++t){std::array<LogicalInput,2> pads{};for(unsigned wire=0;wire<2;++wire)pads[ports[wire]]=pad(ports[wire],t,bike,menu,save,campaign_save);
   reference.run_frame(pads[0],pads[1]);reference.take_audio();}
  check(reference.snapshot()==offline.snapshot(),"Generated-cartridge co-op differs from the original instruction decoder");
 }
 if(save)check(offline.debug_read(0xc001,1)[0]==(before_setting^1)&&offline.battery()!=before_battery,"Original partner Options transaction did not change and save the host settings");
 if(campaign_save)check((offline.debug_read(0xc0d0,1)[0]&1)&&offline.debug_read(0xc0f0,4)==std::vector<uint8_t>({0,0,0,2})&&offline.battery()!=before_battery,"Original P2 campaign SAVE did not persist the genuinely purchased part and paid Souls");
 if(!seat){check(net.confirmed_battery()==offline.battery(),"Host campaign export differs");auto progress=net.confirmed_progress();check(progress.frames==hold&&progress.battery==offline.battery(),"Host confirmed progress differs");if(live.frames)check(live.battery==expected_live,"Host live export used speculative SRAM");}
 else check(rejects([&]{net.confirmed_battery();})&&rejects([&]{net.confirmed_progress();}),"Guest exported the settled campaign");
 if(campaign_save&&!seat)write_file_atomic("confirmed-campaign.sav",net.confirmed_battery());
 if(menu){auto saved=offline.snapshot();offline.mod_coop_view(1,ports[seat]);auto ref=offline.coop_presentation_view(384,144);std::vector<uint32_t> own(ref.pixels.begin(),ref.pixels.end());
  for(unsigned mode=0;mode<3;++mode){offline.mod_coop_view(mode,ports[seat]);auto view=offline.coop_presentation_view(384,144,int(ports[seat]));
   check(view.width==384&&std::equal(own.begin(),own.end(),view.pixels.begin()),"Network world view exposed the partner's personal menu");check(offline.snapshot()==saved,"Personal menu rendering mutated common state");}}
 if(!campaign&&!campaign_save){Xtreme2Coop state(0);state.restore(net.machine().coop_state());auto actors=state.actors();check(state.passes()>0,"The common world did not run");
  if(menu)check(state.menus().active&&state.menus().ready==3,"Both native personal menus did not initialize across rollback");
  else {check(std::abs(actors[0].x-actors[1].x)>8,"Logical players did not move independently");
   if(bike)check(state.bikes().riding[0]&&state.bikes().riding[1]&&actors[0].hp&&actors[1].hp,"Both original riders did not survive network bike controls/replay");
   else check(state.weapons()[0]==1&&!state.weapons()[1],"Extended inputs did not reach only X's native selector");}}
 // Live presentation may have used predicted inputs. Its watermark above
 // forbids duplicate/replayed output; deterministic PCM is checked from the
 // settled authoritative checkpoint, including the actual APU continuation.
 Machine replayed(rom,o);check(replayed.restore(net.confirmed_snapshot(),error),error.c_str());
 for(unsigned f=0;f<16;++f){offline.run_frame(LogicalInput{},LogicalInput{});replayed.run_frame(LogicalInput{},LogicalInput{});
  check(offline.take_audio()==replayed.take_audio(),"Settled one-world PCM replay differs");auto a=offline.mod_view(),b=replayed.mod_view();check(a.width==b.width&&std::equal(a.pixels.begin(),a.pixels.end(),b.pixels.begin()),"Settled one-world raster replay differs");}
 check(offline.snapshot()==replayed.snapshot(),"Settled one-world state replay differs");
 check(pcm_samples&&std::count(shown.begin(),shown.end(),true)>0,"No live one-world audio/picture presentation");
 std::cout<<"PASS one shared Machine, exact authoritative state and settled PCM/raster replay, logical inputs and host-only confirmed campaign; episodes="<<stats.episodes<<" replayed="<<stats.replayed<<" resets="<<resets<<" live="<<live.frames<<" native="<<net.machine().stats().instructions_native<<" simulation="<<sha256(net.machine().simulation_snapshot())<<'\n';
 return 0;
}catch(const std::exception& e){std::cerr<<"Xtreme2 network check: "<<e.what()<<'\n';return 1;}}
