#include <gbcllr/machine.hpp>
#include <algorithm>
#include <array>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <stdexcept>
using namespace gbcllr;
static void check(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
// Reuse each title's original actor and live Buster fixture. The far actor is
// introduced only after one actual shot has travelled beyond its near position.
static void straddling_projectile(Machine& m,bool first,const std::vector<uint8_t>& actor,const std::vector<uint8_t>& collision,const std::filesystem::path& out){
 auto byte=[&](unsigned a){return m.debug_read(uint16_t(a),1)[0];};auto put=[&](unsigned a,uint8_t v){m.debug_write(uint16_t(a),{&v,1});};
 auto frame=[&](uint8_t keys=0){if(first){put(0xd839,64);put(0xdfa3,152);}m.run_frame(keys);m.take_audio();check(first?byte(0xd802)==1&&!byte(0xccf5)&&byte(0xdfa3)>0:!byte(0xcf00)&&!byte(0xcf60)&&byte(0xcfd0)>0,"Straddling shot fixture left live gameplay");};
 auto point=m.snapshot();unsigned travel_frames=0;std::ostringstream report;report<<"[\n";unsigned row=0;
 for(bool far:{true,false})for(bool fire:{true,false}){
  std::string why;check(m.restore(point,why),why.c_str());unsigned target=248;
  if(first){put(0xc398,0);put(0xd838,0);}
  else{std::array<uint8_t,256> empty{};m.debug_write(0xcd00,empty);put(0xcf15,48);put(0xcf16,112);put(0xcf42,0);put(0xcf43,0);put(0xfff9,1);put(0xcfd0,152);}
  auto place=[&](){auto copy=actor;
   if(first){unsigned cam=(byte(0xd80f)|(unsigned(byte(0xd810))<<8))-80;copy[10]=uint8_t(target);copy[11]=0;copy[13]=uint8_t(target-cam);copy[15]=byte(0xd824);copy[0x18]=1;copy[0x19]=copy[0x20]=0;m.debug_write(0xc600,copy);put(0xc380,0);put(0xc398,1);}
   else{copy[1]=3;copy[3]=uint8_t((target-7)/16);copy[5]=96;copy[7]=100;copy[8]=0;copy[9]=1;copy[12]=200;m.debug_write(0xcdf0,copy);auto rect=collision;rect[4]=1;m.debug_write(0xcef0,rect);}
  };
  if(!far)place();unsigned elapsed=0;int world=-1,previous=0;bool tracked=false;
  if(far&&fire){
   for(;elapsed<240;elapsed++){
    frame(elapsed<4?32:0);unsigned base=first?0xc239:0xcc60;uint8_t id=byte(base);if(!id){check(!tracked,"Real Buster was culled before the straddling alias window");continue;}
    int raw=first?byte(base+4):int(byte(0xcf0c))*16+(byte(0xff91)&15)+int(byte(base+5))-8;
    if(!tracked){world=raw;tracked=true;}else world+=int8_t(uint8_t(raw)-uint8_t(previous));previous=raw;
    if(world>=500){check(world<=510,"Real Buster skipped the straddling alias window");++elapsed;break;}
   }
   check(tracked&&world>=500&&elapsed<240,"Real Buster did not reach the far straddling window");travel_frames=elapsed;
  }else{unsigned limit=far?travel_frames:100;for(;elapsed<limit;elapsed++)frame(fire&&elapsed<4?32:0);}
  if(far){place();auto start=m.snapshot();auto run=[&](){for(unsigned n=0;n<12;n++)m.run_frame();auto audio=m.take_audio();auto view=m.mod_view();return std::pair{audio,std::vector<uint32_t>(view.pixels.begin(),view.pixels.end())};};auto expected=run();auto done=m.snapshot();check(m.restore(start,why),why.c_str());check(run()==expected&&m.snapshot()==done,"Straddling Buster state/PCM/view replay differs");for(unsigned n=0;n<4;n++)frame();}
  unsigned alive=first?byte(0xc618)!=0:byte(0xcdf0)!=0;check(alive==(far||!fire),"Actual near/far first-page straddling Buster result differs");
  if(alive)check(first?(byte(0xc60a)|(unsigned(byte(0xc60b))<<8))==target:byte(0xcdf3)==15&&byte(0xcdf5)==96,"Straddling target left its original world position");
  if(row++)report<<",\n";report<<" {\"target_world\":248,\"delayed_far_target\":"<<far<<",\"fire\":"<<fire<<",\"travel_frames\":"<<elapsed<<",\"shot_world_at_insert\":"<<world<<",\"alive_final\":"<<alive<<"}";
 }
 report<<"\n]\n";auto text=report.str();write_file_atomic(out/"straddling-projectiles.json",{reinterpret_cast<const uint8_t*>(text.data()),text.size()});std::cout<<text;
}
static void projectiles(std::span<const uint8_t> rom,NativeFeature feature,const std::filesystem::path& out,bool extended=false,bool straddle=false){
 feature.view_width=-1;Options options;options.mods={feature};Machine m(rom,options);m.mod_viewport(512,144);
 auto byte=[&](unsigned a){return m.debug_read(uint16_t(a),1)[0];};auto put=[&](unsigned a,uint8_t v){m.debug_write(uint16_t(a),std::span<const uint8_t>(&v,1));};
 auto frames=[&](unsigned n,uint8_t keys=0){while(n--){m.run_frame(keys);m.take_audio();}};
 frames(300);frames(1,128);frames(120);frames(1,128);frames(550);frames(1,1);frames(30);
 check(byte(0xd802)==1&&byte(0xd803)==1&&byte(0xccf5)==0,"Projectile fixture is not live and genuinely facing Right");
 check(byte(0xc398)>0,"Projectile fixture needs an originally spawned actor");auto actor=m.debug_read(0xc600,64);check(actor[0]&&(actor[0x11]&1)&&actor[0x18]>0,"Missing original damageable actor");
 unsigned player=byte(0xd80d)|(unsigned(byte(0xd80e))<<8),cam=(byte(0xd80f)|(unsigned(byte(0xd810))<<8))-80,y=byte(0xd824);if(straddle){straddling_projectile(m,true,actor,{},out);return;}
 auto point=m.snapshot();std::ostringstream report;report<<"[\n";unsigned count=0;
 for(unsigned distance:{0u,256u})for(bool fire:{false,true}){
  std::string why;check(m.restore(point,why),why.c_str());auto copy=actor;unsigned target=player+48+distance;copy[10]=uint8_t(target);copy[11]=uint8_t(target>>8);copy[13]=uint8_t(target-cam);copy[15]=uint8_t(y);copy[0x18]=1;copy[0x19]=copy[0x20]=0;m.debug_write(0xc600,copy);put(0xc380,0);put(0xc398,1);put(0xd838,0);put(0xd839,64);put(0xdfa3,152);
  // HP1 and placement are one-shot test prerequisites. Native B input alone
  // must produce the real damage; no defeat, terminal or ownership result is set.
  for(unsigned n=0;n<24;n++){if(!(n&3)){put(0xd839,64);put(0xdfa3,152);}frames(1,fire?32:0);check(byte(0xd802)==1&&byte(0xdfa3)>0&&!byte(0xccf5),"Projectile fixture left live ordinary gameplay");}
  unsigned hp=byte(0xc618);check(hp==(fire&&!distance?0u:1u),"Actual near/far shot counterfactual differs");
  check((byte(0xc60a)|(unsigned(byte(0xc60b))<<8))==target,"Target moved from its actual world position");
  if(fire&&distance){auto replay=m.snapshot();auto run=[&](){for(unsigned n=0;n<12;n++)m.run_frame(32);auto audio=m.take_audio();auto v=m.mod_view();return std::pair{audio,std::vector<uint32_t>(v.pixels.begin(),v.pixels.end())};};auto expected=run();auto done=m.snapshot();check(m.restore(replay,why),why.c_str());check(run()==expected&&m.snapshot()==done,"In-flight projectile state/PCM/view replay differs");check(m.restore(replay,why),why.c_str());}
  if(extended&&distance){
   for(unsigned n=24;n<180;n++){
    if(!(n&3)){put(0xd839,64);put(0xdfa3,152);}frames(1,fire?32:0);check(byte(0xd802)==1&&byte(0xdfa3)>0&&!byte(0xccf5),"Extended projectile fixture left live gameplay");
    if(fire&&n==79){
     check(byte(0xc239)>0,"Buster did not survive past its original viewport cull");auto replay=m.snapshot();auto run=[&](){for(unsigned i=0;i<12;i++)m.run_frame(32);auto audio=m.take_audio();auto v=m.mod_view();return std::pair{audio,std::vector<uint32_t>(v.pixels.begin(),v.pixels.end())};};auto expected=run();auto done=m.snapshot();check(m.restore(replay,why),why.c_str());check(run()==expected&&m.snapshot()==done,"Extended Buster state/PCM/view replay differs");check(m.restore(replay,why),why.c_str());
     auto v=m.mod_view();std::string header="P6\n"+std::to_string(v.width)+" 144\n255\n";std::vector<uint8_t> ppm(header.begin(),header.end());for(auto pixel:v.pixels){ppm.push_back(uint8_t(pixel>>16));ppm.push_back(uint8_t(pixel>>8));ppm.push_back(uint8_t(pixel));}write_file_atomic(out/"mm1-extended-buster.ppm",ppm);
    }
   }
   check(byte(0xc618)==(fire?0u:1u),"Actual full-world extended Buster hit/no-fire control differs");
   check((byte(0xc60a)|(unsigned(byte(0xc60b))<<8))==target,"Original target moved during the extended projectile check");
  }
  if(count++)report<<",\n";report<<" {\"world_separation\":"<<distance<<",\"fire\":"<<fire<<",\"display_frames\":24,\"target_world\":"<<target<<",\"health_before\":1,\"health_after\":"<<hp<<",\"total_display_frames\":"<<(extended&&distance?180:24)<<",\"health_final\":"<<unsigned(byte(0xc618))<<"}";
 }
 report<<"\n]\n";auto text=report.str();write_file_atomic(out/"projectiles.json",{reinterpret_cast<const uint8_t*>(text.data()),text.size()});std::cout<<text;
}
static void projectiles2(std::span<const uint8_t> rom,NativeFeature feature,const std::filesystem::path& out,bool extended,bool straddle=false){
 feature.view_width=-1;Options options;options.mods={feature};Machine m(rom,options);m.mod_viewport(512,144);
 auto byte=[&](unsigned a){return m.debug_read(uint16_t(a),1)[0];};auto put=[&](unsigned a,uint8_t v){m.debug_write(uint16_t(a),std::span<const uint8_t>(&v,1));};
 auto frames=[&](unsigned n,uint8_t keys=0){while(n--){m.run_frame(keys);m.take_audio();}};
 frames(300);frames(1,128);frames(120);put(0xfff9,1);frames(1,16);frames(550);
 put(0xcf0a,1);put(0xcf0b,1);const std::array<uint8_t,6> returns{0xde,0x2a,0xc9,0x27,0x22,0x27};m.debug_write(0xdff0,returns);m.debug_registers({0,0,0,0,0xdff0,0x2ac7,1});m.debug_wake();frames(360);
 auto all=m.debug_read(0xcd00,256);unsigned slot=0;while(slot<16&&all[slot*16]!=0x94)++slot;check(slot<16,"Projectile fixture needs an originally spawned Mechakkero");
 std::vector<uint8_t> actor(all.begin()+slot*16,all.begin()+slot*16+16);auto collision=m.debug_read(uint16_t(0xce00+slot*16),16);check(collision[4]>0,"Originally spawned target has no native HP");check(byte(0xcfdf)==0,"Projectile fixture did not retain the ordinary Buster");
 if(straddle){put(0xcf0a,0);put(0xcf0b,1);m.debug_write(0xdff0,returns);m.debug_registers({0,0,0,0,0xdff0,0x2ac7,1});m.debug_wake();frames(360);straddling_projectile(m,false,actor,collision,out);return;}
 auto point=m.snapshot();std::ostringstream report;report<<"[\n";unsigned count=0;
 for(unsigned distance:{0u,256u})for(bool fire:{false,true}){
  std::string why;check(m.restore(point,why),why.c_str());std::array<uint8_t,256> empty{};m.debug_write(0xcd00,empty);
  auto copy=actor;unsigned target=216+distance;copy[1]=3;copy[3]=uint8_t((target-7)/16);copy[5]=64;copy[7]=100;copy[8]=0;copy[9]=1;copy[12]=200;m.debug_write(0xcdf0,copy);auto rect=collision;rect[4]=1;m.debug_write(0xcef0,rect);
  put(0xcf15,48);put(0xcf16,112);put(0xcf42,0);put(0xcf43,0);put(0xfff9,1);put(0xcfd0,152);
  // Wait0/timer200 is a supplied stationary-target prerequisite. Native
  // Buster movement, hit processing and target defeat remain the result.
  for(unsigned n=0;n<24;n++){frames(1,fire?32:0);check(byte(0xcf0c)==10&&byte(0xcfd0)>0,"MMII projectile fixture left native gameplay");}
  unsigned alive=byte(0xcdf0)!=0;check(alive==(fire&&!distance?0u:1u),"MMII actual near/far shot counterfactual differs");
  if(alive)check(byte(0xcdf3)==(target-7)/16&&byte(0xcdf5)==64,"MMII target left its supplied native waiting pose");
  if(extended&&distance){
   for(unsigned n=24;n<180;n++){
    frames(1,fire?32:0);check(byte(0xcf0c)==10&&byte(0xcfd0)>0,"Extended MMII fixture left native gameplay");
    if(fire&&n==79){
     bool active=false;for(unsigned s=0;s<4;s++)active|=byte(0xcc60+16*s)!=0;check(active,"MMII Buster did not survive its original viewport cull");
     auto replay=m.snapshot();auto run=[&](){for(unsigned i=0;i<12;i++)m.run_frame(32);auto audio=m.take_audio();auto v=m.mod_view();return std::pair{audio,std::vector<uint32_t>(v.pixels.begin(),v.pixels.end())};};auto expected=run();auto done=m.snapshot();check(m.restore(replay,why),why.c_str());check(run()==expected&&m.snapshot()==done,"Extended MMII Buster state/PCM/view replay differs");check(m.restore(replay,why),why.c_str());
     auto v=m.mod_view();std::string header="P6\n"+std::to_string(v.width)+" 144\n255\n";std::vector<uint8_t> ppm(header.begin(),header.end());for(auto pixel:v.pixels){ppm.push_back(uint8_t(pixel>>16));ppm.push_back(uint8_t(pixel>>8));ppm.push_back(uint8_t(pixel));}write_file_atomic(out/"mm2-extended-buster.ppm",ppm);
    }
   }
   check((byte(0xcdf0)!=0)==!fire,"MMII full-world extended Buster hit/no-fire control differs");
   if(!fire)check(byte(0xcdf3)==(target-7)/16&&byte(0xcdf5)==64,"MMII no-fire target moved during the range check");
  }
  if(count++)report<<",\n";report<<" {\"world_separation\":"<<distance<<",\"fire\":"<<fire<<",\"display_frames\":24,\"target_world\":"<<target<<",\"alive_after24\":"<<alive<<",\"total_display_frames\":"<<(extended&&distance?180:24)<<",\"alive_final\":"<<unsigned(byte(0xcdf0)!=0)<<"}";
 }
 report<<"\n]\n";auto text=report.str();write_file_atomic(out/"projectiles.json",{reinterpret_cast<const uint8_t*>(text.data()),text.size()});std::cout<<text;
}
static void contact(std::span<const uint8_t> rom,NativeFeature feature,bool first,const std::filesystem::path& out){
 feature.view_width=-1;Options options;options.mods={feature};Machine m(rom,options);m.mod_viewport(512,144);
 auto byte=[&](unsigned a){return m.debug_read(uint16_t(a),1)[0];};
 auto put=[&](unsigned a,uint8_t v){m.debug_write(uint16_t(a),std::span<const uint8_t>(&v,1));};
 auto frames=[&](unsigned count,uint8_t keys=0){while(count--){m.run_frame(keys);m.take_audio();}};
 frames(300);frames(1,128);frames(120);if(!first)put(0xfff9,1);frames(1,first?128:16);frames(550);
 if(!first){put(0xcf0a,1);put(0xcf0b,1);const std::array<uint8_t,6> returns{0xde,0x2a,0xc9,0x27,0x22,0x27};m.debug_write(0xdff0,returns);m.debug_registers({0,0,0,0,0xdff0,0x2ac7,1});m.debug_wake();frames(360);}
 std::vector<uint8_t> actor,collision;
 if(first){check(byte(0xc398)>0,"Contact fixture needs an originally spawned actor");actor=m.debug_read(0xc600,64);check(actor[0]&&(actor[0x11]&1)&&actor[0x1a],"Original contact actor is not harmful");}
 else{auto all=m.debug_read(0xcd00,256);unsigned slot=0;while(slot<16&&all[slot*16]!=0x94)++slot;check(slot<16,"Contact fixture needs an originally spawned Mechakkero");actor.assign(all.begin()+slot*16,all.begin()+slot*16+16);collision=m.debug_read(uint16_t(0xce00+slot*16),16);}
 auto point=m.snapshot();std::ostringstream report;report<<"[\n";
 for(unsigned distance:{256u,0u}){
  std::string why;check(m.restore(point,why),why.c_str());auto copy=actor;
  if(first){
   unsigned world=128+distance;copy[10]=uint8_t(world);copy[11]=uint8_t(world>>8);copy[13]=128;copy[15]=83;m.debug_write(0xc600,copy);
   put(0xc380,0);put(0xc398,1);put(0xd80d,128);put(0xd80e,0);put(0xd821,128);put(0xd81c,128);put(0xd824,83);put(0xd81e,83);put(0xd838,0);put(0xd839,0);put(0xccf5,0);put(0xdfa3,152);
  }else{
   std::array<uint8_t,240> empty{};m.debug_write(0xcd00,empty);copy[3]=uint8_t((200+distance-7)/16);copy[5]=48;copy[7]=112;copy[8]=0;copy[9]=1;m.debug_write(0xcdf0,copy);m.debug_write(0xcef0,collision);
   put(0xcf15,48);put(0xcf16,112);put(0xcf42,0);put(0xcf43,0);put(0xfff9,0);put(0xcfd0,152);
  }
  frames(4);unsigned hp=byte(first?0xdfa3:0xcfd0);
  if(distance)check(hp==152,"Far native actor damaged player through modulo256 contact alias");
  else check(hp<152,"Native nearby contact no longer damages player");
  if(first)check((byte(0xc60a)|(unsigned(byte(0xc60b))<<8))==128+distance,"Contact actor unexpectedly moved from its true world position");
  else check(byte(0xcdf3)==(200+distance-7)/16&&byte(0xcdf5)==48,"Contact actor unexpectedly moved from its original native position");
  report<<" {\"title\":\"mm"<<(first?1:2)<<"\",\"world_separation\":"<<distance<<",\"display_frames\":4,\"health_before\":152,\"health_after\":"<<hp<<"}"<<(distance?",\n":"\n");
 }
 report<<"]\n";auto content=report.str();write_file_atomic(out/"contact.json",{reinterpret_cast<const uint8_t*>(content.data()),content.size()});std::cout<<content;
}
static void timing(std::span<const uint8_t> rom,NativeFeature feature,bool first,const std::filesystem::path& out,const NativeFeature* colors=nullptr){
 if(colors){feature.view_width=160;feature.weapon_cycle=false;}
 std::vector<uint8_t> fixture_actor,fixture_collision;
 if(colors&&!first){
  // Reuse the already qualified original Top-room actor template. The wider
  // setup reaches its native spawn service; both measured runs stay160 wide.
  auto source=feature;source.view_width=256;source.smooth_gameplay=false;Options setup;setup.mods={source};Machine spawned(rom,setup);
  auto frames=[&](unsigned n,uint8_t keys=0){while(n--){spawned.run_frame(keys);spawned.take_audio();}};
  auto put=[&](unsigned a,uint8_t v){spawned.debug_write(uint16_t(a),{&v,1});};
  frames(300);frames(1,128);frames(120);put(0xfff9,1);frames(1,16);frames(550);put(0xcf0a,1);put(0xcf0b,1);const std::array<uint8_t,6> returns{0xde,0x2a,0xc9,0x27,0x22,0x27};spawned.debug_write(0xdff0,returns);spawned.debug_registers({0,0,0,0,0xdff0,0x2ac7,1});spawned.debug_wake();frames(240);
  auto all=spawned.debug_read(0xcd00,512);unsigned slot=0;while(slot<16&&all[slot*16]!=0x94)++slot;check(slot<16,"Standalone stress prerequisite needs the original Top-room Mechakkero");fixture_actor.assign(all.begin()+slot*16,all.begin()+slot*16+16);fixture_collision.assign(all.begin()+256+slot*16,all.begin()+256+slot*16+16);
 }
 std::array<uint64_t,2> totals{},cycles{},audio{};std::ostringstream report;report<<"[\n";
 for(unsigned smooth=0;smooth<2;smooth++){
  feature.smooth_gameplay=smooth!=0;Options options;options.mods=colors&&smooth?std::vector<NativeFeature>{*colors}:std::vector<NativeFeature>{feature};Machine m(rom,options);
  auto byte=[&](unsigned a){return m.debug_read(uint16_t(a),1)[0];};
  auto put=[&](unsigned a,uint8_t v){m.debug_write(uint16_t(a),std::span<const uint8_t>(&v,1));};
  auto frames=[&](unsigned n,uint8_t keys=0){for(unsigned i=0;i<n;i++){m.run_frame(keys);m.take_audio();}};
  frames(300);frames(1,128);frames(120);if(!first)put(0xfff9,1);frames(1,first?128:16);frames(550);
  if(first){put(0xd839,64);put(0xdfa3,152);check(byte(0xccf5)==0,"Stress fixture supplied native boss-defeat latch");}
  else{put(0xcf0a,1);put(0xcf0b,1);const std::array<uint8_t,6> returns{0xde,0x2a,0xc9,0x27,0x22,0x27};m.debug_write(0xdff0,returns);m.debug_registers({0,0,0,0,0xdff0,0x2ac7,1});m.debug_wake();frames(240);}
  std::vector<uint8_t> actor,collision;
  if(first){check(byte(0xc398)>0,"Heavy fixture needs an original live actor");actor=m.debug_read(0xc600,64);check(actor[0]&&actor[0x1b],"Missing original actor service");}
  else if(colors){actor=fixture_actor;collision=fixture_collision;}
  else{auto all=m.debug_read(0xcd00,512);unsigned slot=0;while(slot<16&&all[slot*16]!=0x94)++slot;check(slot<16,"Heavy fixture needs the original native Mechakkero");actor.assign(all.begin()+slot*16,all.begin()+slot*16+16);collision.assign(all.begin()+256+slot*16,all.begin()+256+slot*16+16);}
  auto initial=m.mod_view().logic_updates;auto initial_cycles=m.stats().cycles;
  unsigned player_world=first?byte(0xd80d)|(unsigned(byte(0xd80e))<<8):0;
  unsigned camera=first?(byte(0xd80f)|(unsigned(byte(0xd810))<<8))-80:0;
  report<<" {\"title\":\"mm"<<(first?1:2)<<"\",\"smooth\":"<<smooth<<",\"standalone_dx\":"<<(colors&&smooth?"true":"false")<<",\"display_frames\":600,\"completed_updates_per60\":[";
  uint64_t previous=initial;
  for(unsigned sample=0;sample<10;sample++){
   // Cheat-loaded stress prerequisites only: sixteen copies of one actor that
   // was born through its original loader, running original AI/render/collision.
   // Refreshing once a second keeps density matched despite natural culling.
   for(unsigned slot=0;slot<16;slot++){
    auto copy=actor;
    if(first){unsigned world=player_world-56+slot*8;copy[0x0a]=uint8_t(world);copy[0x0b]=uint8_t(world>>8);copy[0x0d]=uint8_t(world-camera);copy[0x0f]=112;copy[4]=0;m.debug_write(uint16_t(0xc600+slot*64),copy);put(0xc380+slot,uint8_t(slot));}
    else{copy[3]=0;copy[5]=uint8_t(24+slot*9);copy[7]=110;copy[9]=1;copy[12]=uint8_t(30+slot*3);m.debug_write(uint16_t(0xcd00+slot*16),copy);m.debug_write(uint16_t(0xce00+slot*16),collision);}
   }
   if(first){put(0xc398,16);put(0xd839,64);put(0xdfa3,152);}
   for(unsigned frame=0;frame<60;frame++){
    if(first){put(0xd839,64);put(0xdfa3,152);check(byte(0xd802)==1&&!byte(0xd869)&&!byte(0xdfb1)&&!byte(0xccf5),"Heavy fixture left live eligible gameplay");}
    if(!first)check(!byte(0xcf00)&&!byte(0xcf60)&&byte(0xcfd0)>0&&byte(0xcf16)<144,"MMII heavy fixture left live eligible gameplay");
    m.run_frame();audio[smooth]+=m.take_audio().size();
    if(!first)check(!byte(0xcf00)&&!byte(0xcf60)&&byte(0xcfd0)>0&&byte(0xcf16)<144,"MMII heavy fixture died or entered a terminal script");
    if(first)check(byte(0xd802)==1&&byte(0xdfa3)>0&&byte(0xd824)<128&&!byte(0xccf5),"Heavy fixture died or entered a terminal script");
   }
   auto completed=m.mod_view().logic_updates;if(sample)report<<',';report<<completed-previous;previous=completed;
   if(first)std::cerr<<"I stress smooth="<<smooth<<" sample="<<sample<<" mode="<<unsigned(byte(0xd802))<<" transition="<<unsigned(byte(0xd869))<<" script="<<unsigned(byte(0xdfb1))<<" terminal="<<unsigned(byte(0xccf5))<<" mercy="<<unsigned(byte(0xd839))<<" health="<<unsigned(byte(0xdfa3))<<" y="<<unsigned(byte(0xd824))<<" world="<<(byte(0xd80d)|(unsigned(byte(0xd80e))<<8))<<'\n';
  }
  totals[smooth]=previous-initial;cycles[smooth]=m.stats().cycles-initial_cycles;
  if(colors&&smooth){
   check(m.mod_view().width==160&&!m.mod_view().expanded,"Standalone DX unexpectedly enabled widescreen");
   auto point=m.snapshot();m.take_audio();auto replay=[&](){for(unsigned n=0;n<12;n++)m.run_frame();auto pcm=m.take_audio();auto view=m.mod_view();return std::pair{pcm,std::vector<uint32_t>(view.pixels.begin(),view.pixels.end())};};auto expected=replay();auto done=m.snapshot();std::string why;check(m.restore(point,why),why.c_str());check(replay()==expected&&m.snapshot()==done,"Standalone DX clock state/PCM/view continuation differs");
  }
  report<<"],\"completed_updates\":"<<totals[smooth]<<",\"physical_ticks\":"<<cycles[smooth]<<",\"pcm_samples\":"<<audio[smooth]<<",\"live_eligible_each_frame\":true,\"player_health_final\":"<<unsigned(byte(first?0xdfa3:0xcfd0))<<"}"<<(smooth?"\n":",\n");
 }
 report<<"]\n";auto content=report.str();write_file_atomic(out/(colors?"dx-slowdown.json":"slowdown.json"),std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(content.data()),content.size()));std::cout<<content;
 check(totals[1]>=599&&totals[1]<=601,"Smooth heavy scene did not complete one update per display frame");
 check(totals[0]<totals[1],"Heavy scene did not demonstrate stock slowdown");
 check(audio[0]==audio[1]&&uint64_t(std::abs(int64_t(cycles[0])-int64_t(cycles[1])))<=32,"Smooth changed physical device/audio clock duration");
}
int main(int argc,char** argv){try{
 check(argc==4||argc==5,"ROM mm1|mm2 OUTPUT [timing|dx-timing|dx|dx-preview|dx-gameover|dx-password|dx-intros|contact|projectiles|projectiles-range|projectiles-straddle] required");std::string title=argv[2];check(title=="mm1"||title=="mm2","Invalid title");
 auto rom=read_file(argv[1]);const auto identity=sha256(rom);bool first=title=="mm1";std::filesystem::path out=argv[3];std::filesystem::create_directories(out);
 NativeFeature feature;feature.plugin="gbcllr."+title+".adaptive-view";feature.view_width=256;feature.no_flicker=true;feature.smooth_gameplay=true;feature.weapon_cycle=true;
 if(argc==5&&(std::string(argv[4])=="projectiles"||std::string(argv[4])=="projectiles-range"||std::string(argv[4])=="projectiles-straddle")){if(first)projectiles(rom,feature,out,std::string(argv[4])=="projectiles-range",std::string(argv[4])=="projectiles-straddle");else projectiles2(rom,feature,out,std::string(argv[4])=="projectiles-range",std::string(argv[4])=="projectiles-straddle");return 0;}
 if(argc==5&&std::string(argv[4])=="contact"){contact(rom,feature,first,out);return 0;}
 if(argc==5&&std::string(argv[4])=="timing"){timing(rom,feature,first,out);return 0;}
 if(argc==5&&std::string(argv[4])=="dx-timing"){auto manifest=std::filesystem::path(argv[1]).parent_path().parent_path().parent_path()/"mods"/(title+"-dx.gbmod");auto colors=read_mod_package(manifest,identity).feature;timing(rom,feature,first,out,&colors);return 0;}
 Options options;options.mods={feature};bool gameover=argc==5&&std::string(argv[4])=="dx-gameover",preview=argc==5&&std::string(argv[4])=="dx-preview",intros=argc==5&&std::string(argv[4])=="dx-intros",password=argc==5&&std::string(argv[4])=="dx-password",dx=gameover||preview||intros||password||(argc==5&&std::string(argv[4])=="dx");
 if(dx){
  auto manifest=std::filesystem::path(argv[1]).parent_path().parent_path().parent_path()/"mods"/(title+"-dx.gbmod");auto colors=read_mod_package(manifest,identity).feature;
  auto bad=colors;bad.asset_input=std::string(64,'0');auto invalid=options;invalid.mods.push_back(bad);bool rejected=false;try{Machine wrong(rom,invalid);}catch(const std::runtime_error&){rejected=true;}check(rejected,"DX accepted a mismatched presentation asset pin");
  options.mods.push_back(colors);
 }
 Machine m(rom,options);
 auto byte=[&](unsigned a){return m.debug_read(uint16_t(a),1)[0];};
 auto put=[&](unsigned a,uint8_t v){m.debug_write(uint16_t(a),std::span<const uint8_t>(&v,1));};
 auto frames=[&](unsigned n,uint8_t keys=0,uint8_t extra=0){for(unsigned i=0;i<n;i++){m.run_frame(LogicalInput{keys,extra});m.take_audio();}};
 frames(300);
 if(gameover){
  check(!first,"Game-over gate targets MMII");frames(1,128);frames(120);unsigned budget=1000000;while(m.stats().pc!=0x2c5b&&budget--)m.debug_step_instruction();check(m.stats().pc==0x2c5b,"Game-over fixture missed original selector loop");
  auto registers=m.debug_registers();registers.sp-=2;const std::array<uint8_t,2> returned{0xb6,0x26};m.debug_write(registers.sp,returned);registers.pc=0x292c;registers.ime=1;put(0x2100,1);put(0xff9d,1);m.debug_registers(registers);m.debug_wake();frames(120);
  m.set_buttons(16);budget=1000000;while(m.stats().pc!=0x2933&&budget--)m.debug_step_instruction();check(m.stats().pc==0x2933,"Original A did not leave game-over password preview");m.set_buttons(0);budget=1000000;while(m.stats().pc!=0x2944&&budget--)m.debug_step_instruction();check(m.stats().pc==0x2944,"Original game-over font/map did not load");frames(8);
  auto capture=[&](const char* name){auto v=m.mod_view();std::string header="P6\n"+std::to_string(v.width)+" 144\n255\n";std::vector<uint8_t> ppm(header.begin(),header.end());for(auto pixel:v.pixels){ppm.push_back(uint8_t(pixel>>16));ppm.push_back(uint8_t(pixel>>8));ppm.push_back(uint8_t(pixel));}write_file_atomic(out/name,ppm);};capture("mm2-dx-gameover.ppm");
  auto point=m.snapshot();m.take_audio();auto run=[&](){for(unsigned n=0;n<12;n++)m.run_frame();auto pcm=m.take_audio();auto v=m.mod_view();return std::pair{pcm,std::vector<uint32_t>(v.pixels.begin(),v.pixels.end())};};auto expected=run();auto done=m.snapshot();std::string why;check(m.restore(point,why),why.c_str());check(run()==expected&&m.snapshot()==done,"Game-over current-state/PCM/view replay differs");m.set_buttons(16);budget=1000000;while(m.stats().pc!=0x26b6&&budget--)m.debug_step_instruction();check(m.stats().pc==0x26b6&&byte(0xcf6e)==1,"Native game-over A did not return to original caller");std::cout<<"MMII original password preview/game-over A, native caller return and replay passed; death prerequisite bypassed\n";return 0;
 }
 if(preview){
  check(!first,"The password preview gate currently targets MMII");frames(1,128);frames(120);
  unsigned budget=1000000;while(m.stats().pc!=0x2c5b&&budget--)m.debug_step_instruction();check(m.stats().pc==0x2c5b,"Preview fixture missed the native selector main loop");
  // Explicit preview prerequisites only: two first-set weapons and two tanks.
  // The original encoder produces every dot; no completion flag is supplied.
  put(0xcfdd,0);put(0xcfde,0x0a);put(0xcfe9,2);auto registers=m.debug_registers();registers.sp-=2;const std::array<uint8_t,2> returned{0xd8,0x26};m.debug_write(registers.sp,returned);registers.pc=0x4f69;registers.ime=1;put(0x2100,1);m.debug_registers(registers);m.debug_wake();frames(120);
  auto encoded=m.debug_read(0xcfc0,16);check(std::count(encoded.begin(),encoded.end(),255)>0,"Original preview encoder did not produce any dots");check(byte(0xcfde)==0x0a&&byte(0xcfe9)==2,"Password preview changed supplied ownership");auto view=m.mod_view();check(view.width==160&&!view.expanded,"Preview did not remain a fixed native view");check(std::count_if(view.pixels.begin(),view.pixels.end(),[](uint32_t v){return uint8_t(v>>16)!=uint8_t(v>>8)||uint8_t(v>>8)!=uint8_t(v);})>100,"Preview did not render authored colors");
  std::string header="P6\n160 144\n255\n";std::vector<uint8_t> image(header.begin(),header.end());for(auto v:view.pixels){image.push_back(uint8_t(v>>16));image.push_back(uint8_t(v>>8));image.push_back(uint8_t(v));}write_file_atomic(out/"mm2-dx-password-preview.ppm",image);
  auto point=m.snapshot();m.take_audio();auto replay=[&](){frames(1,0,virtual_r);for(unsigned n=0;n<12;n++)m.run_frame();auto pcm=m.take_audio();auto v=m.mod_view();return std::pair{pcm,std::vector<uint32_t>(v.pixels.begin(),v.pixels.end())};};auto expected=replay();auto done=m.snapshot();std::string why;check(m.restore(point,why),why.c_str());check(replay()==expected&&m.snapshot()==done,"Preview current-state/PCM/view replay differs");check(m.debug_read(0xcfc0,16)==encoded&&byte(0xcfde)==0x0a&&byte(0xcfe9)==2,"Shoulder input changed preview/password ownership");frames(1,16);frames(120);budget=1000000;while(m.stats().pc!=0x2c5b&&budget--)m.debug_step_instruction();check(m.stats().pc==0x2c5b,"Native A did not return from preview to the real selector loop");
  // Decode the displayed password in a separate cold machine, using only the
  // real title password choice and cursor/A input. The decoder writes ownership.
  Machine decoder(rom,options);auto step=[&](unsigned count,uint8_t keys=0){while(count--){decoder.run_frame(keys);decoder.take_audio();}};step(300);step(1,8);step(8);step(1,128);step(120);check(decoder.debug_read(0xcfde,1)[0]==0&&decoder.debug_read(0xcfe9,1)[0]==0,"Cold decoder already owns the expected progress");
  for(unsigned y=0;y<4;y++){for(unsigned x=0;x<4;x++){if(encoded[y*4+x]){step(1,16);step(8);}step(1,1);step(8);}step(1,8);step(8);}
  check(decoder.debug_read(0xcfc0,16)==encoded,"Real cursor/A entry differs from original encoded dots");step(1,128);step(240);check(decoder.debug_read(0xcfde,1)[0]==0x0a&&decoder.debug_read(0xcfe9,1)[0]==2,"Original decoder did not restore preview weapon/tank progress");check(identity==sha256(read_file(argv[1]))&&decoder.rom_identity()==identity,"Original cartridge changed in password round trip");std::ostringstream report;report<<"{\"supplied_weapons\":10,\"supplied_tanks\":2,\"decoded_weapons\":"<<unsigned(decoder.debug_read(0xcfde,1)[0])<<",\"decoded_tanks\":"<<unsigned(decoder.debug_read(0xcfe9,1)[0])<<",\"native_encoded_dots\":[";for(unsigned i=0;i<16;i++){if(i)report<<',';report<<unsigned(encoded[i]);}report<<"]}\n";auto content=report.str();write_file_atomic(out/"password-preview.json",{reinterpret_cast<const uint8_t*>(content.data()),content.size()});std::cout<<"MMII authored preview, native encoder/real-input decoder round trip and replay passed\n";return 0;
 }
 if(password&&first){
  check(first,"The password gate currently targets MM1");frames(1,8);frames(8);check(byte(0xcc00)==1,"Original Down did not select the password option");frames(1,128);frames(120);auto menu=m.mod_view();check(menu.width==160&&!menu.expanded,"Original password did not retain its fixed view");check(std::count_if(menu.pixels.begin(),menu.pixels.end(),[](uint32_t p){return uint8_t(p>>16)!=uint8_t(p>>8)||uint8_t(p>>8)!=uint8_t(p);})>100,"Authored password colors did not render");
  auto capture=[&](const char* name){auto v=m.mod_view();std::string header="P6\n160 144\n255\n";std::vector<uint8_t> ppm(header.begin(),header.end());for(auto pixel:v.pixels){ppm.push_back(uint8_t(pixel>>16));ppm.push_back(uint8_t(pixel>>8));ppm.push_back(uint8_t(pixel));}write_file_atomic(out/name,ppm);};capture("mm1-dx-password.ppm");
  auto ownership=m.debug_read(0xdfa1,2);unsigned x=byte(0xc601),y=byte(0xc602);frames(1,0,virtual_r);frames(4);check(byte(0xc601)==x&&byte(0xc602)==y&&m.debug_read(0xdfa1,2)==ownership,"Virtual shoulder escaped password isolation");frames(1,1);frames(8);check(byte(0xc601)==x+1,"Original Right did not move the password cursor");check(byte(0xc60f)==5,"Native password did not begin with five empty dot slots");frames(1,16);frames(8);check(byte(0xc60f)==4,"Original A did not add a real password dot");capture("mm1-dx-password-dot.ppm");
  auto point=m.snapshot();m.take_audio();auto run=[&](){for(unsigned n=0;n<12;n++)m.run_frame();auto audio=m.take_audio();auto v=m.mod_view();return std::pair{audio,std::vector<uint32_t>(v.pixels.begin(),v.pixels.end())};};auto expected=run();auto done=m.snapshot();std::string why;check(m.restore(point,why),why.c_str());check(run()==expected&&m.snapshot()==done,"Password current-state/PCM/view replay differs");frames(1,32);frames(8);check(byte(0xc60f)==5&&m.debug_read(0xdfa1,2)==ownership,"Original B did not remove the dot or changed ownership");check(identity==sha256(read_file(argv[1]))&&m.rom_identity()==identity,"Original cartridge changed during password gate");std::cout<<"MM1 authored password input, native cursor/dots, shoulder isolation and state/PCM/view replay passed\n";return 0;
 }
 if(password&&!first){
  unsigned title_cursor=byte(0xdf00);frames(1,8);frames(8);check(byte(0xdf00)!=title_cursor,"Original Down did not select the MMII password option");frames(1,128);frames(120);auto menu=m.mod_view();check(menu.width==160&&!menu.expanded,"MMII original password did not retain its fixed view");check(std::count_if(menu.pixels.begin(),menu.pixels.end(),[](uint32_t p){return uint8_t(p>>16)!=uint8_t(p>>8)||uint8_t(p>>8)!=uint8_t(p);})>100,"MMII authored password colors did not render");
  auto capture=[&](const char* name){auto v=m.mod_view();std::string header="P6\n160 144\n255\n";std::vector<uint8_t> ppm(header.begin(),header.end());for(auto pixel:v.pixels){ppm.push_back(uint8_t(pixel>>16));ppm.push_back(uint8_t(pixel>>8));ppm.push_back(uint8_t(pixel));}write_file_atomic(out/name,ppm);};capture("mm2-dx-password.ppm");auto ownership=m.debug_read(0xcfdd,2);unsigned x=byte(0xcff4),y=byte(0xcff5);frames(1,0,virtual_r);frames(4);check(byte(0xcff4)==x&&byte(0xcff5)==y&&m.debug_read(0xcfdd,2)==ownership,"Virtual shoulder escaped MMII password isolation");frames(1,1);frames(8);check(byte(0xcff4)==((x+1)&3),"Original Right did not move the MMII password cursor");unsigned dot=0xcfc0+y*4+byte(0xcff4);check(byte(dot)==0,"MMII password dot did not begin empty");frames(1,16);frames(8);check(byte(dot)==255,"Original A did not add a native MMII password dot");capture("mm2-dx-password-dot.ppm");
  auto point=m.snapshot();m.take_audio();auto run=[&](){for(unsigned n=0;n<12;n++)m.run_frame();auto audio=m.take_audio();auto v=m.mod_view();return std::pair{audio,std::vector<uint32_t>(v.pixels.begin(),v.pixels.end())};};auto expected=run();auto done=m.snapshot();std::string why;check(m.restore(point,why),why.c_str());check(run()==expected&&m.snapshot()==done,"MMII password current-state/PCM/view replay differs");frames(1,16);frames(8);check(byte(dot)==0&&m.debug_read(0xcfdd,2)==ownership,"Original A did not clear the MMII dot or changed ownership");check(identity==sha256(read_file(argv[1]))&&m.rom_identity()==identity,"Original cartridge changed during MMII password gate");std::cout<<"MMII authored password input, native cursor/dot toggle, shoulder isolation and state/PCM/view replay passed\n";return 0;
 }
 if(intros){
  check(!first,"The intro gate currently targets MMII");frames(1,128);frames(120);
  unsigned budget=1000000;while(m.stats().pc!=0x2c5b&&budget--)m.debug_step_instruction();check(m.stats().pc==0x2c5b,"Native stage selector did not reach its real main-loop boundary");m.take_audio();auto point=m.snapshot();std::ostringstream report;report<<"[\n";
  auto capture=[&](const std::string& name){auto v=m.mod_view();std::string header="P6\n"+std::to_string(v.width)+" 144\n255\n";std::vector<uint8_t> ppm(header.begin(),header.end());for(auto pixel:v.pixels){ppm.push_back(uint8_t(pixel>>16));ppm.push_back(uint8_t(pixel>>8));ppm.push_back(uint8_t(pixel));}write_file_atomic(out/name,ppm);};
  for(unsigned stage=0;stage<8;stage++){
   std::string why;check(m.restore(point,why),why.c_str());check(m.debug_registers().pc==0x2c5b,"Intro entry abandoned the native main loop");put(0xcf0a,uint8_t(stage));auto registers=m.debug_registers();registers.pc=0x2d15;m.debug_registers(registers);m.debug_wake();unsigned actors80=0,actors180=0;
   // Stage identity and entry placement are explicit prerequisites. Native
   // graphics requests, boss animation, name timing and gameplay load execute.
   for(unsigned n=0;n<550;n++){
    frames(1);if(n==79||n==179){check(m.mod_view().width==160&&!m.mod_view().expanded,"Original boss intro did not retain its fixed view");auto actors=m.debug_read(0xcd00,256);unsigned count=0;for(unsigned s=0;s<16;s++)count+=actors[s*16]!=0;check(count>0,"Original boss intro did not create an actor");if(n==79)actors80=count;else actors180=count;capture("stage"+std::to_string(stage)+"-intro"+std::to_string(n+1)+".ppm");}
    if(n==179){auto replay=m.snapshot();auto run=[&](){for(unsigned i=0;i<12;i++)m.run_frame();auto audio=m.take_audio();auto v=m.mod_view();return std::pair{audio,std::vector<uint32_t>(v.pixels.begin(),v.pixels.end())};};auto expected=run();auto done=m.snapshot();check(m.restore(replay,why),why.c_str());check(run()==expected&&m.snapshot()==done,"Native intro current-state/PCM/view replay differs");check(m.restore(replay,why),why.c_str());}
   }
   auto field=m.mod_view();check(byte(0xcf0a)==stage&&field.expanded&&field.sprites>0,"Original boss intro did not hand off to live wide gameplay");capture("stage"+std::to_string(stage)+"-field.ppm");if(stage)report<<",\n";report<<" {\"stage\":"<<stage<<",\"actors80\":"<<actors80<<",\"actors180\":"<<actors180<<",\"frames\":550,\"field_width\":"<<field.width<<"}";std::cout<<"MMII native intro stage "<<stage<<" passed\n";
  }
  report<<"\n]\n";auto text=report.str();write_file_atomic(out/"intros.json",{reinterpret_cast<const uint8_t*>(text.data()),text.size()});check(identity==sha256(read_file(argv[1]))&&m.rom_identity()==identity,"Original cartridge changed during intro gate");return 0;
 }
 if(dx&&first){
  auto title_view=m.mod_view();check(title_view.width==160&&!title_view.expanded,"DX title used a gameplay viewport");
  check(std::count_if(title_view.pixels.begin(),title_view.pixels.end(),[](uint32_t p){return uint8_t(p>>16)!=uint8_t(p>>8)||uint8_t(p>>8)!=uint8_t(p);})>5000,"Authored DX title palette/art did not render");
  auto picture=[&](){auto v=m.mod_view();return std::vector<uint32_t>(v.pixels.begin(),v.pixels.end());};
  auto point=m.snapshot();auto run=[&](){frames(12);return std::pair{m.snapshot(),picture()};};auto expected=run();std::string why;check(m.restore(point,why),why.c_str());check(run()==expected,"Authored DX title frame/state replay differs");check(m.restore(point,why),why.c_str());
  std::string header="P6\n160 144\n255\n";std::vector<uint8_t> ppm(header.begin(),header.end());for(auto pixel:picture()){ppm.push_back(uint8_t(pixel>>16));ppm.push_back(uint8_t(pixel>>8));ppm.push_back(uint8_t(pixel));}write_file_atomic(out/"mm1-dx-title.ppm",ppm);
  std::cout<<"mm1: authored title palette, source art/map and bank1 logo rendered with exact current-state replay\n";
 }
 if(dx&&!first){
  auto title_view=m.mod_view();check(title_view.width==160&&!title_view.expanded,"MMII DX title used a gameplay viewport");check(std::count_if(title_view.pixels.begin(),title_view.pixels.end(),[](uint32_t p){return uint8_t(p>>16)!=uint8_t(p>>8)||uint8_t(p>>8)!=uint8_t(p);})>2000,"MMII authored title palette/art did not render");
  auto capture_title=[&](const char* name){auto v=m.mod_view();std::string header="P6\n160 144\n255\n";std::vector<uint8_t> ppm(header.begin(),header.end());for(auto pixel:v.pixels){ppm.push_back(uint8_t(pixel>>16));ppm.push_back(uint8_t(pixel>>8));ppm.push_back(uint8_t(pixel));}write_file_atomic(out/name,ppm);};capture_title("mm2-dx-title-remix.ppm");
  auto title_point=m.snapshot();unsigned cursor=byte(0xdf00);m.take_audio();
  auto music=[&](bool select){std::vector<int16_t> pcm;for(unsigned n=0;n<120;n++){m.run_frame(select&&n<8?64:0);auto samples=m.take_audio();pcm.insert(pcm.end(),samples.begin(),samples.end());}return pcm;};
  auto remixed=music(false);std::string why;check(m.restore(title_point,why),why.c_str());auto original=music(true);
  check(byte(0xdf00)==cursor,"DX Select soundtrack choice changed the original title cursor");
  capture_title("mm2-dx-title-classic.ppm");
  check(original!=remixed&&std::any_of(original.begin(),original.end(),[](int16_t v){return v!=0;}),"DX soundtrack choice did not change actual PCM");
  auto selected=m.snapshot();auto sound=music(false);auto selected_end=m.snapshot();check(m.restore(selected,why),why.c_str());check(music(false)==sound&&m.snapshot()==selected_end,"Selected soundtrack state/PCM replay differs");
  check(m.restore(title_point,why),why.c_str());std::cout<<"mm2: native remix/original title choice preserved cursor, changed PCM and replayed exactly\n";
 }
 frames(1,128);frames(120);
 if(dx&&!first){
  auto picture=[&](){auto v=m.mod_view();return std::vector<uint32_t>(v.pixels.begin(),v.pixels.end());};auto select=m.mod_view();check(select.width==160&&!select.expanded,"MMII original stage select used a gameplay viewport");check(std::count_if(select.pixels.begin(),select.pixels.end(),[](uint32_t p){return uint8_t(p>>16)!=uint8_t(p>>8)||uint8_t(p>>8)!=uint8_t(p);})>2000,"MMII authored stage-select art/palettes did not render");
  auto capture=[&](const char* name){std::string header="P6\n160 144\n255\n";std::vector<uint8_t> ppm(header.begin(),header.end());for(auto pixel:picture()){ppm.push_back(uint8_t(pixel>>16));ppm.push_back(uint8_t(pixel>>8));ppm.push_back(uint8_t(pixel));}write_file_atomic(out/name,ppm);};capture("mm2-dx-stage-select.ppm");
  auto point=m.snapshot();unsigned original=byte(0xcf66);frames(1,0,virtual_r);frames(4);check(byte(0xcf66)==original,"MMII virtual shoulder changed native stage selection");frames(1,1);frames(8);check(byte(0xcf66)!=original,"MMII original Right did not move the stage selector");capture("mm2-dx-stage-selected.ppm");
  auto selected=m.snapshot();m.take_audio();auto run=[&](){for(unsigned n=0;n<12;n++)m.run_frame();return std::pair{m.take_audio(),picture()};};auto expected=run();auto done=m.snapshot();std::string why;check(m.restore(selected,why),why.c_str());check(run()==expected&&m.snapshot()==done,"MMII authored stage-select state/PCM/view replay differs");check(m.restore(point,why),why.c_str());std::cout<<"mm2: authored stage-select BG/window maps, native cursor controls and exact state/PCM/view replay passed\n";
 }
 if(dx&&first){
  auto picture=[&](){auto v=m.mod_view();return std::vector<uint32_t>(v.pixels.begin(),v.pixels.end());};auto select=m.mod_view();check(select.width==160&&!select.expanded,"Original stage select used a gameplay viewport");check(std::count_if(select.pixels.begin(),select.pixels.end(),[](uint32_t p){return uint8_t(p>>16)!=uint8_t(p>>8)||uint8_t(p>>8)!=uint8_t(p);})>2000,"Authored stage-select art/palettes did not render");
  auto capture=[&](const char* name){std::string header="P6\n160 144\n255\n";std::vector<uint8_t> ppm(header.begin(),header.end());for(auto pixel:picture()){ppm.push_back(uint8_t(pixel>>16));ppm.push_back(uint8_t(pixel>>8));ppm.push_back(uint8_t(pixel));}write_file_atomic(out/name,ppm);};capture("mm1-dx-stage-select.ppm");
  auto point=m.snapshot();unsigned original=byte(0xdf8c);frames(1,0,virtual_r);frames(4);check(byte(0xdf8c)==original,"Virtual shoulder changed native stage selection");frames(1,1);frames(8);check(byte(0xdf8c)!=original,"Original Right did not move the stage selector");capture("mm1-dx-stage-selected.ppm");
  auto selected=m.snapshot();m.take_audio();auto run=[&](){for(unsigned n=0;n<12;n++)m.run_frame();return std::pair{m.take_audio(),picture()};};auto expected=run();auto done=m.snapshot();std::string why;check(m.restore(selected,why),why.c_str());check(run()==expected&&m.snapshot()==done,"Authored stage-select state/PCM/view replay differs");check(m.restore(point,why),why.c_str());std::cout<<"mm1: authored native stage-select art/palettes/faces, original cursor and state/PCM/view replay passed\n";
 }
 if(!first)put(0xfff9,1);frames(1,first?128:16);frames(550);
 auto view=m.mod_view();check(view.width==256&&view.expanded&&view.sprites>0,"Original gameplay did not publish a wide complete frame");
 if(dx){check(std::any_of(view.pixels.begin(),view.pixels.end(),[](uint32_t p){return uint8_t(p>>16)!=uint8_t(p>>8)||uint8_t(p>>8)!=uint8_t(p);}),"Pinned DX presentation did not produce color");
  std::string header="P6\n"+std::to_string(view.width)+" 144\n255\n";std::vector<uint8_t> ppm(header.begin(),header.end());for(auto pixel:view.pixels){ppm.push_back(uint8_t(pixel>>16));ppm.push_back(uint8_t(pixel>>8));ppm.push_back(uint8_t(pixel));}write_file_atomic(out/(title+"-dx-field.ppm"),ppm);
 }
 auto weapon=[&](){return byte(first?0xdfa0:0xcfdf);};unsigned entries=first?11:13;
 if(dx&&first){
  auto field=m.snapshot();put(0xdfa1,255);put(0xdfa2,7);frames(1,128);frames(90);auto menu=m.mod_view();check(menu.width==256&&menu.expanded,"MM1 original pause lost the adaptive DX canvas");
  check(std::count_if(menu.pixels.begin(),menu.pixels.end(),[](uint32_t p){return uint8_t(p>>16)!=uint8_t(p>>8)||uint8_t(p>>8)!=uint8_t(p);})>100,"MM1 authored menu icons/palettes did not render");
  auto capture_menu=[&](const char* name){auto v=m.mod_view();std::string header="P6\n"+std::to_string(v.width)+" 144\n255\n";std::vector<uint8_t> ppm(header.begin(),header.end());for(auto pixel:v.pixels){ppm.push_back(uint8_t(pixel>>16));ppm.push_back(uint8_t(pixel>>8));ppm.push_back(uint8_t(pixel));}write_file_atomic(out/name,ppm);};capture_menu("mm1-dx-menu.ppm");
  unsigned original=byte(0xc300);frames(1,8);frames(8);unsigned selected=byte(0xc300);check(selected!=original,"MM1 original Down did not change its native menu selection");auto reserves=m.debug_read(0xdfa3,11);frames(1,0,virtual_r);frames(4);check(byte(0xc300)==selected&&m.debug_read(0xdfa3,11)==reserves,"MM1 virtual shoulder escaped menu isolation");capture_menu("mm1-dx-menu-selected.ppm");
  auto point=m.snapshot();m.take_audio();auto run=[&](){for(unsigned n=0;n<12;n++)m.run_frame();auto audio=m.take_audio();auto v=m.mod_view();return std::pair{audio,std::vector<uint32_t>(v.pixels.begin(),v.pixels.end())};};auto expected=run();auto done=m.snapshot();std::string why;check(m.restore(point,why),why.c_str());check(run()==expected&&m.snapshot()==done,"MM1 authored pause current-state/PCM/view replay differs");
  frames(1,128);frames(90);check(m.mod_view().expanded&&weapon()==selected,"MM1 native pause close did not retain the selected weapon");check(m.restore(field,why),why.c_str());std::cout<<"mm1: authored pause font/icons/palettes, native Down selection, shoulder isolation and state/PCM/view replay passed\n";
 }
 if(dx&&!first){
  auto field=m.snapshot();put(0xcfdd,15);put(0xcfde,255);frames(1,128);frames(90);auto menu=m.mod_view();check(menu.width==256&&menu.expanded,"Original pause lost the adaptive DX canvas");
  check(std::count_if(menu.pixels.begin(),menu.pixels.end(),[](uint32_t p){return uint8_t(p>>16)!=uint8_t(p>>8)||uint8_t(p>>8)!=uint8_t(p);})>100,"Authored DX menu icons/palettes did not render");
  auto capture_menu=[&](const char* name){auto v=m.mod_view();std::string header="P6\n"+std::to_string(v.width)+" 144\n255\n";std::vector<uint8_t> ppm(header.begin(),header.end());for(auto pixel:v.pixels){ppm.push_back(uint8_t(pixel>>16));ppm.push_back(uint8_t(pixel>>8));ppm.push_back(uint8_t(pixel));}write_file_atomic(out/name,ppm);};
  capture_menu("mm2-dx-menu.ppm");frames(1,1);frames(8);check(weapon()==1,"Original menu Right did not select Rush Coil");auto reserves=m.debug_read(0xcfd0,13);frames(1,0,virtual_r);frames(4);check(weapon()==1&&m.debug_read(0xcfd0,13)==reserves,"Virtual shoulder escaped menu isolation");capture_menu("mm2-dx-menu-selected.ppm");
  auto point=m.snapshot();m.take_audio();auto run=[&](){for(unsigned n=0;n<12;n++)m.run_frame();auto audio=m.take_audio();auto v=m.mod_view();return std::pair{audio,std::vector<uint32_t>(v.pixels.begin(),v.pixels.end())};};auto expected=run();auto done=m.snapshot();std::string why;check(m.restore(point,why),why.c_str());check(run()==expected&&m.snapshot()==done,"Authored pause current-state/PCM/view replay differs");
  frames(1,128);frames(90);check(m.mod_view().expanded&&weapon()==1,"Native pause close did not retain the original selection");check(m.restore(field,why),why.c_str());std::cout<<"mm2: authored pause icons/font/palettes, original selection, shoulder isolation and state/PCM/view replay passed\n";
 }
 if(first){put(0xdfa1,255);put(0xdfa2,7);}else {put(0xcfde,255);put(0xcfdd,15);}
 // Availability is a supplied test prerequisite, not an acquisition result.
 // Set different reserves, including empty owned weapons, to catch refill or
 // energy-based skipping. Buster health remains unchanged.
 for(unsigned n=1;n<entries;n++)put((first?0xdfa3:0xcfd0)+n,uint8_t(n==3?0:(n*7)%152));
 auto energy=m.debug_read(first?0xdfa4:0xcfd1,entries-1);unsigned current=weapon();
 auto press=[&](uint8_t extra){frames(24,0,extra);frames(4);};
 for(unsigned n=0;n<entries;n++){
  current=(current+1)%entries;press(virtual_r);
  if(weapon()!=current){auto r=m.debug_registers();std::cerr<<"expected="<<current<<" actual="<<unsigned(weapon())<<" pc="<<m.stats().pc<<" bank="<<m.stats().rom_bank<<" sp="<<r.sp<<" health="<<unsigned(byte(first?0xdfa3:0xcfd0))<<" mode="<<unsigned(byte(first?0xd802:0xcf1d))<<" transition="<<unsigned(byte(first?0xd869:0xcf00))<<" flags="<<unsigned(byte(first?0xdfb1:0xcf60))<<'\n';throw std::runtime_error("Native next-weapon selection differs");}
  check(m.debug_read(first?0xdfa4:0xcfd1,entries-1)==energy,"Selection changed weapon reserves");
  if(dx&&first&&(current==3||current==6)){auto v=m.mod_view();std::string header="P6\n"+std::to_string(v.width)+" 144\n255\n";std::vector<uint8_t> ppm(header.begin(),header.end());for(auto pixel:v.pixels){ppm.push_back(uint8_t(pixel>>16));ppm.push_back(uint8_t(pixel>>8));ppm.push_back(uint8_t(pixel));}write_file_atomic(out/("mm1-dx-hud-weapon"+std::to_string(current)+".ppm"),ppm);}
  if(dx&&!first){auto v=m.mod_view();unsigned colored=0;for(unsigned y=128;y<144;y++)for(unsigned x=(v.width-160)/2;x<(v.width+160)/2;x++){auto pixel=v.pixels[y*v.width+x];colored+=uint8_t(pixel>>16)!=uint8_t(pixel>>8)||uint8_t(pixel>>8)!=uint8_t(pixel);}check(colored>8,"Authored selected-weapon HUD did not produce color");unsigned ammo_pixels=0;for(unsigned y=128;y<144;y++)for(unsigned x=(v.width-160)/2;x<(v.width-160)/2+40;x++)ammo_pixels+=(v.pixels[y*v.width+x]&0xffffff)!=0;if(current==3)check(ammo_pixels==0,"Empty native ammo bar is not blank");if(current==12)check(ammo_pixels>20,"Nonempty native ammo disappeared from the authored HUD");std::string header="P6\n"+std::to_string(v.width)+" 144\n255\n";std::vector<uint8_t> ppm(header.begin(),header.end());for(auto pixel:v.pixels){ppm.push_back(uint8_t(pixel>>16));ppm.push_back(uint8_t(pixel>>8));ppm.push_back(uint8_t(pixel));}write_file_atomic(out/("mm2-dx-hud-weapon"+std::to_string(current)+".ppm"),ppm);}
 }
 frames(24,0,virtual_r);current=(current+1)%entries;check(weapon()==current,"Fresh edge not selected");frames(48,0,virtual_r);check(weapon()==current,"Held shoulder repeated");frames(4);
 press(3);check(weapon()==current,"Opposed shoulders changed equipment");press(virtual_l);current=(current+entries-1)%entries;check(weapon()==current,"Previous weapon differs");
 // Only buster and slot2 remain owned; an empty reserve must stay selectable.
 if(first){put(0xdfa1,5);put(0xdfa2,0);}else {put(0xcfde,0);put(0xcfdd,2);}
 put(first?0xdfa0:0xcfdf,0);for(unsigned n=0;n<4;n++){press(virtual_r);current=n%2?0:2;check(weapon()==current,"Owned skip/wrap differs");}
 frames(1,0,virtual_r);auto point=m.snapshot();
 auto run=[&](){for(unsigned n=0;n<60;n++)m.run_frame(LogicalInput{});auto pcm=m.take_audio();auto v=m.mod_view();return std::pair{pcm,std::vector<uint32_t>(v.pixels.begin(),v.pixels.end())};};
 auto expected=run();auto state=m.snapshot();std::string error;check(m.restore(point,error),error.c_str());check(run()==expected&&m.snapshot()==state,"In-flight state/PCM/view replay differs");
 write_file_atomic(out/(title+"-controls.state"),m.snapshot());m.write_frame(out/(title+"-controls-native.ppm"));
 {
  auto before=m.snapshot();uint16_t trap=first?0x2a54:0x1801;const std::array<uint8_t,2> returns{uint8_t(trap),uint8_t(trap>>8)};m.debug_write(0xdff0,returns);
  auto call=[&](Registers regs){m.debug_registers(regs);m.debug_wake();m.run_until(m.stats().cycles+2048);auto done=m.debug_registers();check(done.pc==trap&&done.sp==0xdff2,"Original sensor did not return normally");return done;};
  if(first){
   unsigned center=byte(0xd80f)|(unsigned(byte(0xd810))<<8),cam=center-80,world=cam+216;
   unsigned row=byte(0xd885)|(unsigned(byte(0xd886))<<8),bank=byte(0xdf9b),offset=bank*0x4000+row+(world>>4)-0x4000;
   check(offset<rom.size(),"Original terrain row outside cartridge");uint8_t block=rom[offset];
   put(0xcc40,0);put(0xcc41,0xc6);put(0xc60a,uint8_t(world));put(0xc60b,uint8_t(world>>8));put(0xc60d,216);put(0xc61b,3);put(0xd860,0);
   unsigned cache=0xde70|(((cam+216)>>4)&15);put(cache,uint8_t(block^255));put(0x2000,3);
   auto done=call({0,0,0x70d8,0,0xdff0,0x102a,0});check(uint8_t(done.af>>8)==block,"Original far sensor read stale ring terrain");
  }else{
   put(0xcf0c,10);put(0xff92,0);put(0xcf2f,0xf0);put(0xffa0,0x94);put(0xffa3,0);put(0xffa5,216);put(0xcf0d,220);put(0xcf0e,112);
   put(0xcc01,1);put(0xc617,0);put(0x2000,2);
   auto done=call({0,0,0,0,0xdff0,0x332f,0});check(uint8_t(done.af>>8)==0&&(done.af&16),"Far D0 sensor falsely collided with empty ground");
   put(0xffa5,248);put(0xcf0d,250);put(0xc619,0x22);put(0xc609,0);
   done=call({0,0,0,0,0xdff0,0x332f,0});check(uint8_t(done.af>>8)==0x22&&!(done.af&16),"Far wrapped sensor missed its true solid column");
  }
  check(m.restore(before,error),error.c_str());std::cout<<title<<": original far-range terrain sensor returned true column data/solidity\n";
 }
 if(first){
  put(0xd839,64);put(0xdfa3,152);put(0xd80d,224);put(0xd80e,0);bool born=false;
  for(unsigned n=0;n<90&&!born;n++){frames(1);unsigned camera=(byte(0xd80f)|(unsigned(byte(0xd810))<<8))-80;auto actors=m.debug_read(0xc600,24*64);auto wide=m.mod_view();
   for(unsigned slot=0;slot<24;slot++){unsigned at=slot*64,world=actors[at+10]|(unsigned(actors[at+11])<<8);if(actors[at]&&world>camera+160&&world<unsigned(wide.left)+wide.width)born=true;}
  }
  check(born,"Original MM1 actor did not activate in extended view");check(m.mod_view().spawn_reads>0,"Extended original stream diagnostics missing");
  std::cout<<"mm1: original actor activated beyond native160-pixel display band\n";
  // Full original checkpoint2 loader reaches Cut Man's shutter room. Its
  // standing hero producer runs before0A17; the door producer runs after it.
  // Both lists must survive the completed-update publication together.
  put(0xdf8c,1);put(0xdf8d,2);put(0xdfc8,0);const std::array<uint8_t,2> returns{0x5e,0x09};m.debug_write(0xfffc,returns);m.debug_registers({0,0,0,0,0xfffc,0x2ff6,1});m.debug_wake();frames(360);put(0xd839,0);put(0xdfa3,152);frames(120);
  auto oam=m.debug_read(0xc000,160);unsigned authored=0;for(unsigned at=0;at<160;at+=4)authored+=oam[at]!=160;
  check(authored>=25&&m.mod_view().sprites>=authored,"Cut shutter/hero complete producer regression");
  std::cout<<"mm1: Cut checkpoint2 retained original hero and shutter pieces together\n";
 }
 if(!first){
  // Top Man's first native room has a normal Mechakkero at layout column22.
  // Its world origin359 is beyond the original camera160+160 display and
  // beyond the original initial spawn scan, but inside a256-pixel view.
  // Execute the original loader and check the original actor, not a painted
  // substitute or a supplied spawn/victory result.
  put(0xcf0a,1);put(0xcf0b,1);const std::array<uint8_t,4> returns{0xde,0x2a,0xc9,0x27};m.debug_write(0xdff0,returns);
  m.debug_registers({0,0,0,0,0xdff0,0x2ac7,1});m.debug_wake();bool born=false;
  for(unsigned n=0;n<360&&!born;n++){
   frames(1);if(byte(0xcf0c)!=10)continue;
   auto actors=m.debug_read(0xcd00,256);
   for(unsigned slot=0;slot<16;slot++)if(actors[slot*16]==0x94&&actors[slot*16+3]==22&&actors[slot*16+5]>168&&actors[slot*16+5]<224)born=true;
  }
  check(born,"Original extended-range Mechakkero did not spawn outside the160-pixel band");frames(2);
  auto wide=m.mod_view();check(wide.width==256&&wide.spawn_reads&&wide.activation_adjustments,"Extended native spawn diagnostics missing");
  std::string header="P6\n"+std::to_string(wide.width)+" 144\n255\n";std::vector<uint8_t> ppm(header.begin(),header.end());
  for(auto pixel:wide.pixels){ppm.push_back(uint8_t(pixel>>16));ppm.push_back(uint8_t(pixel>>8));ppm.push_back(uint8_t(pixel));}
  write_file_atomic(out/"mm2-extended-actor.ppm",ppm);write_file_atomic(out/"mm2-extended-actor.state",m.snapshot());
  std::cout<<"mm2: original Mechakkero column22 activated beyond native160-pixel band; native spawn service and world culler exercised\n";
 }
 check(identity==sha256(read_file(argv[1]))&&m.rom_identity()==identity,"Original cartridge changed");
 std::cout<<title<<": original gameplay, all owned/empty weapons, wrap, held/opposed edges, energy and exact state/PCM/view replay passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
