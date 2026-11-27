#include <gbcllr/machine.hpp>
#include "../src/native_mods.hpp"
#include "../src/xtreme2_coop.hpp"
#include <algorithm>
#include <cstring>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <map>
#include <queue>
#include <tuple>
int main(int argc,char** argv) {
 try {
  if(argc!=4&&argc!=5)throw std::runtime_error("ROM BOOT CHECKPOINT [--coop-x|--coop-zero|--visual-x|--visual-zero] required");
  std::string mode=argc==5?argv[4]:"";
  if(argc==5&&mode!="--coop-x"&&mode!="--coop-zero"&&mode!="--visual-x"&&mode!="--visual-zero"&&mode!="--combat-x"&&mode!="--heart-x"&&mode!="--forward-x"&&mode!="--forward-zero"&&mode!="--boss-x"&&mode!="--boss-zero"&&mode!="--scene-x"&&mode!="--scene-zero"&&mode!="--scene-p1-x"&&mode!="--tether-x"&&mode!="--shots-x"&&mode!="--shots-zero"&&mode!="--shots-post-x"&&mode!="--shots-post-zero"&&mode!="--reward-x"&&mode!="--reward-zero"&&mode!="--reward-p2-x"&&mode!="--reward-p2-zero"&&mode!="--reward-reference-x"&&!mode.starts_with("--campaign-")&&!mode.starts_with("--lifecycle-")&&!mode.starts_with("--menu-"))throw std::runtime_error("Unknown co-op QA mode");
  auto rom=gbcllr::read_file(argv[1]);gbcllr::Options o;o.model=gbcllr::Model::cgb;if(std::getenv("GBCLLR_QA_REFERENCE"))o.native=false;
  o.boot_rom=gbcllr::read_file(argv[2]);o.mods.push_back(gbcllr::read_mod_package(std::filesystem::path(__FILE__).parent_path().parent_path()/"mods/xtreme2-adaptive.gbmod",gbcllr::sha256(rom)).feature);
  auto seed=gbcllr::read_file(argv[3]);
  if(std::getenv("GBCLLR_QA_WEAPON_CYCLE"))o.mods[0].weapon_cycle=true;
  if(std::getenv("GBCLLR_QA_SMOOTH")){o.mods[0].smooth_gameplay=true;if(!std::getenv("GBCLLR_QA_CURRENT_SMOOTH")){seed.insert(seed.end()-64,4,0);auto hash=gbcllr::sha256(std::span<const uint8_t>(seed).first(seed.size()-64));std::copy(hash.begin(),hash.end(),seed.end()-64);}}
  bool reference=mode=="--reward-reference-x";
  if(argc==5&&!reference) {
   // Offline fixture adoption only: retain the exact owned original hardware
   // state and add a fresh current-build native module. No battery output.
   if(!mode.starts_with("--shots-post-")&&!mode.starts_with("--campaign-")&&!mode.starts_with("--lifecycle-post-")) {
   gbcllr::NativeMods plain(gbcllr::sha256(rom),o.mods);
   const auto old_size=plain.snapshot().size();
   if(seed.size()<64+5+old_size+4||gbcllr::sha256(std::span<const uint8_t>(seed).first(seed.size()-64))!=
       std::string(reinterpret_cast<const char*>(seed.data()+seed.size()-64),64))throw std::runtime_error("Invalid owned fixture checksum/size");
   o.mods[0].plugin="gbcllr.xtreme2.coop";o.mods[0].player1_character=mode.ends_with("-x")?0u:1u;
   gbcllr::NativeMods next(gbcllr::sha256(rom),o.mods);
   auto extra=next.coop_snapshot();
   size_t offset=seed.size()-64-5-(o.mods[0].smooth_gameplay?4:0)-old_size-4;
   uint32_t size=0;std::memcpy(&size,seed.data()+offset,4);
   if(size!=old_size)throw std::runtime_error("Owned fixture module layout differs");
   uint32_t prior_version=0;std::memcpy(&prior_version,seed.data()+offset+4,4);
   if(prior_version!=2)throw std::runtime_error("Owned fixture requires the stock native view adapter");
   size+=uint32_t(extra.size());std::memcpy(seed.data()+offset,&size,4);
   uint32_t version=3;std::memcpy(seed.data()+offset+4,&version,4);
   seed.insert(seed.begin()+offset+4+old_size,extra.begin(),extra.end());
   auto hash=gbcllr::sha256(std::span<const uint8_t>(seed).first(seed.size()-64));
   std::copy(hash.begin(),hash.end(),seed.end()-64);
   }
   o.mods[0].plugin="gbcllr.xtreme2.coop";
   o.mods[0].player1_character=mode.ends_with("-x")?0u:1u;
  }
  gbcllr::Machine m(rom,o);std::string error;
  if(!m.restore(seed,error))throw std::runtime_error(error);
  if(argc==4){m.write_frame("native-reference.ppm");}
  if(argc==5) {
   if(mode.starts_with("--campaign-death-art-")){
    unsigned particle_poses=0;bool replayed=false;
    for(unsigned f=0;f<300;++f){m.run_frame(0,0);m.take_audio();auto v=m.mod_view();gbcllr::Xtreme2Coop s(0);s.restore(m.coop_state());
     bool particles=false;for(unsigned n=0;n<v.sprites;++n)if(s.sprite_owner(n)==3&&s.sprite_tile(n)){++particle_poses;particles=true;}
     if(particles&&!replayed){auto checkpoint=m.snapshot();std::vector<int16_t> a,b;std::vector<uint32_t> va,vb;
      auto run=[&](auto& pcm,auto& pixels){for(unsigned n=0;n<16;++n){m.run_frame();auto sound=m.take_audio();pcm.insert(pcm.end(),sound.begin(),sound.end());auto view=m.mod_view();pixels.insert(pixels.end(),view.pixels.begin(),view.pixels.end());}};
      run(a,va);auto expected=m.snapshot();if(!m.restore(checkpoint,error))throw std::runtime_error(error);run(b,vb);if(a!=b||va!=vb||m.snapshot()!=expected)throw std::runtime_error("Death effect state/PCM/raster replay diverged");if(!m.restore(checkpoint,error))throw std::runtime_error(error);replayed=true;}
     if(f%16==0){std::ofstream out("death-art-"+std::to_string(f)+".ppm",std::ios::binary);out<<"P6\n"<<v.width<<" 144\n255\n";for(auto p:v.pixels){char rgb[]{char(p>>16),char(p>>8),char(p)};out.write(rgb,3);}}
    }
    if(!particle_poses||!replayed)throw std::runtime_error("Original death effect never published independent particle graphics");
    gbcllr::write_file_atomic("death-art-end.state",m.snapshot());std::cout<<particle_poses<<" original death particle poses and exact in-flight state/PCM/raster replay passed\n";return 0;
   }
   if(mode.starts_with("--campaign-review-")){
    std::ofstream csv("review-frames.csv");csv<<"frame,updates,expanded,sprites,body1,body2,hp1,hp2,status1,status2,mode,submode,bank,pc\n";
    unsigned smooth_frames=0,updates=0,missing=0;size_t samples=0;
    for(unsigned frame=0;frame<480;++frame){
     // Resume the exact original menu when present, then use ordinary inputs.
     gbcllr::Xtreme2Coop before(0);before.restore(m.coop_state());
     uint8_t p=before.menus_active()?uint8_t(frame<8?128:0):uint8_t(frame%120<80?33:0);
     m.run_frame(p,p);auto pcm=m.take_audio();if(frame>=40)samples+=pcm.size();auto v=m.mod_view();gbcllr::Xtreme2Coop s(0);s.restore(m.coop_state());auto a=s.actors(true);auto life=s.lifecycle();
     unsigned pieces[2]{};for(unsigned i=0;i<v.sprites;++i){unsigned owner=s.sprite_owner(i);if(owner==1||owner==2)++pieces[owner-1];}
     if(frame==40)gbcllr::write_file_atomic("review-live.state",m.snapshot());
     if(frame>=40){++smooth_frames;updates+=s.passes()-before.passes();for(unsigned seat=0;seat<2;++seat)if(!life.status[seat]&&a[seat].hp&&!s.transport(seat)&&!pieces[seat])++missing;}
     auto regs=m.debug_registers();csv<<frame<<','<<s.passes()<<','<<v.expanded<<','<<v.sprites<<','<<pieces[0]<<','<<pieces[1]<<','<<a[0].hp<<','<<a[1].hp<<','<<unsigned(life.status[0])<<','<<unsigned(life.status[1])<<','<<unsigned(m.debug_read(0xc0b0,1)[0])<<','<<unsigned(m.debug_read(0xc0c0,1)[0])<<','<<m.stats().rom_bank<<','<<regs.pc<<'\n';
     if(frame==40||frame==120||!v.expanded||(!pieces[0]&&a[0].hp&&!life.status[0])||(!pieces[1]&&a[1].hp&&!life.status[1])){if(frame<60||frame%30==0){std::ofstream out("review-"+std::to_string(frame)+".ppm",std::ios::binary);out<<"P6\n"<<v.width<<" 144\n255\n";for(auto c:v.pixels){char rgb[]{char(c>>16),char(c>>8),char(c)};out.write(rgb,3);}}}
    }
    gbcllr::write_file_atomic("review-end.state",m.snapshot());
    if(o.mods[0].smooth_gameplay&&(updates!=smooth_frames||missing))throw std::runtime_error("Smooth gameplay missed a world update or living body pose");
    auto saved=m.snapshot();std::vector<int16_t> a,b;std::vector<uint32_t> va,vb;
    auto replay=[&](auto& sound,auto& pictures){for(unsigned i=0;i<24;++i){m.run_frame();auto pcm=m.take_audio();sound.insert(sound.end(),pcm.begin(),pcm.end());auto view=m.mod_view();pictures.insert(pictures.end(),view.pixels.begin(),view.pixels.end());}};
    replay(a,va);auto expected=m.snapshot();if(!m.restore(saved,error))throw std::runtime_error(error);replay(b,vb);
    if(a!=b||va!=vb||m.snapshot()!=expected)throw std::runtime_error("Review effect state/PCM/raster replay diverged");
    std::cout<<"Measured "<<updates<<" updates / "<<smooth_frames<<" display frames, "<<missing<<" missing living body poses, "<<samples<<" interleaved audio samples; state/PCM/raster replay exact\n";return 0;
   }
   if(mode.starts_with("--campaign-shop-")){
    unsigned driver=mode.find("-p2-")!=std::string::npos?1u:0u;
    auto step=[&](unsigned count,uint8_t buttons=0){for(unsigned i=0;i<count;++i){m.run_frame(driver?0:buttons,driver?buttons:0);m.take_audio();}};
    auto press=[&](uint8_t button,unsigned wait=90){step(8,button);step(wait);};
    auto picture=[&](const char* file){auto view=m.mod_view();std::ofstream out(file,std::ios::binary);out<<"P6\n"<<view.width<<" 144\n255\n";for(auto p:view.pixels){char rgb[]{char(p>>16),char(p>>8),char(p)};out.write(rgb,3);}};
    auto balance=[&](){auto bytes=m.debug_read(0xc0f0,4);unsigned total=0,multiplier=1;for(auto digit:bytes){if(digit>9)throw std::runtime_error("Invalid original Souls digits");total+=digit*multiplier;multiplier*=10;}return total;};
    if(m.debug_read(0xc0d0,1)[0]&1)throw std::runtime_error("An unpurchased original Shop item is required");
    // Supply a menu position, sale availability and currency only. The
    // original input/Shop/confirmation handlers perform the actual purchase.
    const uint8_t shop[]{2},souls[]{0,0,0,3},available[]{255};m.debug_write(0xcbd8,shop);m.debug_write(0xc0f0,souls);m.debug_write(0xc0f7,available);step(4);
    m.trace_enabled(true);press(16,120);auto trace=m.take_trace();m.trace_enabled(false);
    if(std::find(trace.begin(),trace.end(),0x1d*0x4000+0x73b0-0x4000)==trace.end()){
     picture("shop-open-failed.ppm");gbcllr::write_file_atomic("shop-open-failed.state",m.snapshot());throw std::runtime_error("The selected logical player did not enter the original shared Shop");}
    picture("shop-open.ppm");press(16);press(16);picture("shop-confirm.ppm");press(16);picture("shop-purchased.ppm");
    if(!(m.debug_read(0xc0d0,1)[0]&1)||balance()!=2000){gbcllr::write_file_atomic("shop-purchase-failed.state",m.snapshot());throw std::runtime_error("Original Shop did not grant the part for one native price: part="+std::to_string(m.debug_read(0xc0d0,1)[0])+" Souls="+std::to_string(balance()));}
    auto inventory=m.debug_read(0xc0d0,3);press(16);press(16);if(balance()!=2000||m.debug_read(0xc0d0,3)!=inventory)throw std::runtime_error("An owned Shop part was charged or granted twice");
    gbcllr::write_file_atomic("shop-purchased.state",m.snapshot());
    press(32,120);const uint8_t save_cursor[]{0};m.debug_write(0xcbd8,save_cursor);step(4);press(16,120);press(16,180);press(16,180);press(2);
    gbcllr::write_file_atomic("shop-save-ready.state",m.snapshot());press(16,180);press(16,180);picture("shop-saved.ppm");
    auto battery=m.battery();gbcllr::write_file_atomic("shop-campaign.sav",battery);
    gbcllr::Machine loaded(rom,o);if(!loaded.load_battery(battery,error))throw std::runtime_error(error);
    auto cold=[&](unsigned count,uint8_t buttons=0){for(unsigned i=0;i<count;++i){loaded.run_frame(driver?0:buttons,driver?buttons:0);loaded.take_audio();}};
    cold(60);cold(2,128);cold(118);cold(2,128);cold(118);cold(2,16);cold(118);cold(2,16);cold(118);cold(2,128);cold(58);
    cold(4,8);cold(8);cold(4,16);cold(60);cold(4,128);cold(60);cold(4,8);cold(8);cold(4,16);cold(90);
    cold(4,8);cold(12);cold(4,8);cold(12);cold(4,16);cold(120);cold(4,16);cold(120);cold(4,16);cold(180);cold(4,16);cold(240);cold(360);
    if(loaded.debug_read(0xc0d0,3)!=inventory||loaded.debug_read(0xc0f0,4)!=std::vector<uint8_t>({0,0,0,2})){
     loaded.write_frame("shop-load-failed.ppm");gbcllr::write_file_atomic("shop-load-failed.state",loaded.snapshot());throw std::runtime_error("Native SAVE/fresh LOAD did not preserve the purchased part and paid Souls");}
    gbcllr::write_file_atomic("shop-loaded.state",loaded.snapshot());loaded.write_frame("shop-loaded.ppm");
    std::cout<<"Original shared Shop driver="<<driver<<" grants one part, spends1000 Souls, rejects repeat purchase and retains inventory/balance through native SAVE/fresh LOAD\n";return 0;
   }
   if(mode.starts_with("--campaign-")) {
    // Current-build reward receipt, already earned through native combat.
    // Drive the actual save UI; never manufacture a saved record or flag.
    gbcllr::Xtreme2Coop state(o.mods[0].player1_character);state.restore(m.coop_state());
    auto reward=state.reward();if(!reward.owner||reward.grants!=1)throw std::runtime_error("An earned native reward receipt is required");
    const bool zero=bool(o.mods[0].player1_character^(reward.owner-1));
    auto expected=m.debug_read(0xc000,256);
    auto step=[&](unsigned n,uint8_t buttons){for(unsigned f=0;f<n;++f){m.run_frame(buttons,0);m.take_audio();}};
    auto confirm=[&](unsigned delay){step(8,16);step(delay,0);};
    confirm(60);confirm(60);step(8,2);step(12,0);confirm(180);confirm(180);
    if(zero){step(360,0);confirm(180);confirm(180);confirm(180);}
    step(8,2);step(12,0);confirm(180);
    m.write_frame("native-save-complete.ppm");
    auto battery=m.battery();gbcllr::write_file_atomic("campaign.sav",battery);
    gbcllr::Machine loaded(rom,o);if(!loaded.load_battery(battery,error))throw std::runtime_error(error);
    auto cold=[&](unsigned n,uint8_t buttons){for(unsigned f=0;f<n;++f){loaded.run_frame(buttons,0);loaded.take_audio();}};
    cold(60,0);cold(2,128);cold(118,0);cold(2,128);cold(118,0);
    cold(2,16);cold(118,0);cold(2,16);cold(118,0);cold(2,128);cold(58,0);
    cold(4,8);cold(8,0);cold(4,16);cold(60,0);cold(4,128);cold(60,0);
    cold(4,8);cold(8,0);cold(4,16);cold(90,0);cold(4,8);cold(12,0);
    cold(4,8);cold(12,0);cold(4,16);cold(120,0);cold(4,16);cold(120,0);
    cold(4,16);cold(180,0);cold(4,16);cold(240,0);cold(360,0);
    auto actual=loaded.debug_read(0xc000,256);
    // Native stage-clear bookkeeping completes after the reward receipt.
    // The newly cleared Neon bit must be added, not compared to the receipt.
    for(unsigned at:{0xa0u,0xb0u,0xd6u,0xd7u,0xd8u,0xd9u,0xdbu,0xdcu,0xddu,0xdeu})
     if(actual[at]!=expected[at])throw std::runtime_error("Native campaign record was not retained at "+std::to_string(0xc000+at));
    for(unsigned at=0x6d;at<=0x76;++at)if(actual[at]!=expected[at])throw std::runtime_error("Native X weapon record was not retained");
    if(actual[0xc0]!=(expected[0xc0]|8)||bool(actual[0x6f]&0x40)==zero||bool(actual[0xa0]&1)!=zero)throw std::runtime_error("Fresh native LOAD did not retain the earned character reward");
    gbcllr::write_file_atomic("campaign-loaded.state",loaded.snapshot());loaded.write_frame("native-loaded.ppm");
    std::cout<<"Actual native SAVE/fresh LOAD retains both character records and only the killing character's ability\n";return 0;
   }
   if(mode.starts_with("--menu-arsenal-")||mode.starts_with("--menu-techniques-")||mode.starts_with("--menu-special-")) {
    const uint8_t zero_skills[]{uint8_t(mode.starts_with("--menu-special-technique-")?1:255)},weapon[]{0x5c},zero_parts[]{uint8_t(m.debug_read(0xc0dd,1)[0]|2)};
    m.debug_write(0xc0a0,zero_skills);m.debug_write(0xc0dd,zero_parts);
    for(unsigned at=0xc06e;at<=0xc076;++at)m.debug_write(uint16_t(at),weapon);
   }
   if(mode.starts_with("--menu-pickups-")){const uint8_t weapon[]{0x44},energy[]{4},skill[]{1};m.debug_write(0xc06f,weapon);m.debug_write(0xc06d,energy);m.debug_write(0xc0a0,skill);}
   if(mode.starts_with("--menu-bikes-")){const uint8_t spare_lives[]{3};m.debug_write(0xc065,spare_lives);}
   for(unsigned f=0;f<(mode.starts_with("--menu-door-")||mode.starts_with("--lifecycle-post-view-")?0u:80u);++f){m.run_frame(0,0);m.take_audio();}
   gbcllr::write_file_atomic("controller-entry.state",m.snapshot());
   if(mode.starts_with("--menu-")) {
    m.mod_viewport(384,144);m.mod_coop_shared_world(false);
    auto state=[&](){gbcllr::Xtreme2Coop s(o.mods[0].player1_character);s.restore(m.coop_state());return s;};
    auto step=[&](unsigned n,uint8_t p1=0,uint8_t p2=0){for(unsigned f=0;f<n;++f){m.run_frame(p1,p2);m.take_audio();}};
    auto picture=[&](const std::string& name){auto v=m.mod_view();std::ofstream out(name+".ppm",std::ios::binary);out<<"P6\n"<<v.width<<" 144\n255\n";for(auto p:v.pixels){char rgb[]{char(p>>16),char(p>>8),char(p)};out.write(rgb,3);}};
    auto original=m.snapshot();
    auto press=[&](unsigned seat,uint8_t button,unsigned wait=12){step(2,seat?0:button,seat?button:0);step(wait);};
    auto replay_effect=[&](){
     auto saved=m.snapshot();std::vector<int16_t> a,b;std::vector<uint32_t> va,vb;
     auto sample=[&](auto& audio,auto& pixels){for(unsigned f=0;f<16;++f){m.run_frame(0,0);auto pcm=m.take_audio();audio.insert(audio.end(),pcm.begin(),pcm.end());auto v=m.mod_view();pixels.insert(pixels.end(),v.pixels.begin(),v.pixels.end());}};
     sample(a,va);auto expected=m.snapshot();if(!m.restore(saved,error))throw std::runtime_error(error);sample(b,vb);
     if(a!=b||va!=vb||m.snapshot()!=expected)throw std::runtime_error("Native weapon/technique state, PCM or raster replay diverged");
     if(!m.restore(saved,error))throw std::runtime_error(error);
    };
    auto cursor=[&](unsigned seat,unsigned target){
     auto menu=state().menus();unsigned table=menu.page[seat]==2?0x493b:0x46c1;
     std::map<unsigned,std::vector<uint8_t>> paths;std::queue<unsigned> todo;paths[menu.cursor[seat]]={};todo.push(menu.cursor[seat]);
     while(!todo.empty()&&!paths.contains(target)){auto at=todo.front();todo.pop();for(unsigned d=0;d<4;++d){auto next=rom[0x1d*0x4000+table-0x4000+4*at+d];if(next!=255&&!paths.contains(next)){paths[next]=paths[at];paths[next].push_back(uint8_t(1u<<d));todo.push(next);}}}
     if(!paths.contains(target))throw std::runtime_error("Native menu cursor target is unreachable");
     for(auto direction:paths[target])press(seat,direction);
     if(state().menus().cursor[seat]!=target)throw std::runtime_error("Native menu direction path did not reach the requested item");
    };
    if(mode.starts_with("--menu-special-")) {
     const bool technique=mode.find("-technique-")!=std::string::npos;
     const bool zero_area=mode.find("-zeroarea-")!=std::string::npos;
     unsigned attacker=o.mods[0].player1_character^unsigned(technique||zero_area);
     if(technique){
      for(unsigned f=0;f<180&&state().actors()[attacker].action!=0;++f)step(1);
      auto ground=m.snapshot();std::array<int,2> rise{};std::array<bool,2> entered{};
      for(unsigned owned=0;owned<2;++owned){
       if(!m.restore(ground,error))throw std::runtime_error(error);const uint8_t skill[]{uint8_t(owned)};m.debug_write(0xc0a0,skill);
       auto actors=state().actors();unsigned energy=m.debug_read(0xc06d,1)[0]&31;int top=actors[attacker].y;m.trace_enabled(true);
       for(unsigned f=0;f<48;++f){step(1,attacker?0:uint8_t(f<8?36:0),attacker?uint8_t(f<8?36:0):0);top=std::min(top,state().actors()[attacker].y);if(f==2||f==8)picture("technique-owned-"+std::to_string(owned)+"-"+std::to_string(f));}
       auto trace=m.take_trace();m.trace_enabled(false);entered[owned]=std::find(trace.begin(),trace.end(),0x5*0x4000+0x7b06-0x4000)!=trace.end();rise[owned]=actors[attacker].y-top;
       if((m.debug_read(0xc06d,1)[0]&31)!=energy||state().actors()[attacker^1].hp!=actors[attacker^1].hp)throw std::runtime_error("Free technique spent energy or changed its idle partner's health");
       replay_effect();
      }
      std::cout<<"Original Up+B earned gate="<<entered[0]<<','<<entered[1]<<" rise="<<rise[0]<<','<<rise[1]<<'\n';
      if(entered[0]||!entered[1]||rise[1]<=rise[0])throw std::runtime_error("Earned native Up+B technique did not change the original movement");
      gbcllr::write_file_atomic("technique-result.state",m.snapshot());return 0;
     }
     auto place=[&](unsigned seat,int offset){
      auto deadline=m.stats().cycles+140448*4;bool found=false;
      while(m.stats().cycles<deadline){auto at=m.stats();if(at.rom_bank==5&&at.pc==0x4004&&m.debug_read(0xc0c9,1)[0]==(o.mods[0].player1_character^seat)){found=true;break;}m.run_until(at.cycles+1);m.take_audio();}
      if(!found)throw std::runtime_error("Special attack approach missed its controller");
      auto boss=m.debug_read(0xb804,6);int bx=boss[4]|unsigned(boss[5])<<8;int x=offset?std::clamp(bx+24,3832,3936):bx;
      const uint8_t position[]{0,uint8_t(offset?132:boss[1]),uint8_t(offset?1:boss[2]),0,uint8_t(x),uint8_t(x>>8)},protection[]{255};m.debug_write(0xc104,position);m.debug_write(0xc130,protection);
     };
     place(attacker^1,0);place(attacker,-24);step(2);
     // Select the original special through X's own native menu. Zero's
     // earned Up+B command uses its original free technique gate instead.
     auto choose=[&](){press(attacker,128,118);cursor(attacker,1);press(attacker,16);press(attacker,128,180);if(state().menus().active)throw std::runtime_error("Special weapon selection did not resume");};
     unsigned energy=m.debug_read(zero_area?0xc06d:0xc06e,1)[0]&31;auto before=state().combat();unsigned initial_hp=m.debug_read(0xb821,1)[0]&127;choose();
     unsigned hp=m.debug_read(0xb821,1)[0]&127;
     if(zero_area&&(hp>=initial_hp||state().combat().hits[attacker]<=before.hits[attacker]))throw std::runtime_error("The native menu-selected Zero technique did not cause its own attributed damage");
     bool killed=false,area_replay=false,spent=(m.debug_read(zero_area?0xc06d:0xc06e,1)[0]&31)<energy;
     for(unsigned f=0;f<3600&&hp;++f){
      if(f%96==0){place(attacker^1,0);place(attacker,-24);const uint8_t supply[]{0x5c};m.debug_write(zero_area?0xc06d:0xc06e,supply);}
      if(zero_area&&f&&f%240==0)choose();
      auto boss=m.debug_read(0xb808,2);unsigned face=(boss[0]|unsigned(boss[1])<<8)<unsigned(state().actors()[attacker].x)?2u:1u;
      uint8_t buttons=f%96<2?uint8_t(face):f%96>=24&&f%96<32?uint8_t(face|32):0;step(1,attacker?0:buttons,attacker?buttons:0);
      spent|=(m.debug_read(zero_area?0xc06d:0xc06e,1)[0]&31)<energy;
      unsigned next=m.debug_read(0xb821,1)[0]&127;if(next!=hp){std::cout<<"Special native boss HP "<<hp<<" -> "<<next<<" frame="<<f<<'\n';hp=next;}
      if(f==12||f==24)picture("special-"+std::to_string(f));
      if(f==24)replay_effect();
      if(!area_replay&&m.debug_read(0xc0a4,1)[0]){replay_effect();area_replay=true;}
      if(!hp)killed=true;
     }
     auto hit=state().combat();gbcllr::write_file_atomic("special-result.state",m.snapshot());picture("special-result");std::cout<<"Special hits "<<before.hits[0]<<','<<before.hits[1]<<" -> "<<hit.hits[0]<<','<<hit.hits[1]<<" defeat="<<hit.defeated_owner<<" expected="<<attacker+1<<" area replay="<<area_replay<<" spent="<<spent<<'\n';
     if(hp>=initial_hp||hit.hits[attacker]<=before.hits[attacker]||hit.hits[attacker^1]!=before.hits[attacker^1])throw std::runtime_error("Special damage did not retain its actual attacker with a nearer partner");
     if(!killed||hit.defeated_owner!=attacker+1||!spent||(!zero_area&&!area_replay))throw std::runtime_error("Original special weapon did not spend energy and own its native defeat");
     replay_effect();std::cout<<"Original "<<(zero_area?"Zero active technique":"Giga Crush")<<" actual damage owner="<<attacker<<" kill="<<killed<<" spends energy="<<spent<<", state/PCM/view replay exact\n";return 0;
    }
    auto host_view=[&](){
     for(unsigned view_mode=0;view_mode<3;++view_mode){m.mod_coop_view(view_mode,1);auto saved=m.snapshot();
      for(unsigned width:{160u,256u,384u,512u}){auto view=m.coop_presentation_view(width,144);unsigned expected=view_mode==2?2*std::max(160u,width/2):width;
       if(view.width!=expected||view.pixels.size()!=size_t(expected)*144||!view.expanded||m.snapshot()!=saved)throw std::runtime_error("Local co-op presentation dimensions changed the common simulation");
       std::vector<uint32_t> pixels(view.pixels.begin(),view.pixels.end());auto again=m.coop_presentation_view(width,144);
       if(!std::equal(pixels.begin(),pixels.end(),again.pixels.begin())||m.snapshot()!=saved)throw std::runtime_error("Repeated host-only co-op rendering changed its raster or simulation");
      }
     }
     m.mod_coop_view(0);replay_effect();std::cout<<"Local unified/peer/split resizing preserves the common gameplay snapshot and replay\n";
    };
    if(mode.starts_with("--menu-door-")){
     if(m.debug_read(0xc05f,1)[0]!=4||m.debug_read(0xc066,1)[0]!=4)throw std::runtime_error("The native Neon boss corridor is required");
     unsigned leader=mode.find("-p1-")!=std::string::npos?0:1;auto before=state();bool boss=false,scene=false,replayed=false;
     for(unsigned f=0;f<1500;++f){
      if(f%120==0){for(unsigned seat=0;seat<2;++seat){auto stop=m.stats().cycles+140448*4;bool found=false;while(m.stats().cycles<stop){auto at=m.stats();if(at.rom_bank==5&&at.pc==0x4004&&m.debug_read(0xc0c9,1)[0]==(o.mods[0].player1_character^seat)){found=true;break;}m.run_until(at.cycles+1);m.take_audio();}if(!found)throw std::runtime_error("Door approach missed a player controller");const uint8_t protection[]{255};m.debug_write(0xc130,protection);}}
      bool active=m.debug_read(0xc9b0,1)[0];scene|=!active||state().departures()>before.departures();
      uint8_t drive=active?uint8_t(1|(f%30<12?16:0)):uint8_t(f%30<8?16:0);step(1,leader?0:drive,leader?drive:0);
      if(!replayed&&state().departures()>before.departures()&&state().transport(leader^1)){replay_effect();replayed=true;}
      if(m.debug_read(0xc9a1,1)[0]==0xb8&&m.debug_read(0xb801,1)[0]==2&&(m.debug_read(0xb821,1)[0]&127)==32&&active){boss=true;break;}
     }
     step(120);auto after=state();picture("door-boss-ready");gbcllr::write_file_atomic("door-boss-ready.state",m.snapshot());
     std::cout<<"Native door boss="<<boss<<" scene="<<scene<<" anchor="<<after.anchor()<<" xy="<<after.actors()[0].x<<','<<after.actors()[0].y<<'/'<<after.actors()[1].x<<','<<after.actors()[1].y<<" actions="<<after.actors()[0].action<<','<<after.actors()[1].action<<" teleports="<<after.departures()-before.departures()<<','<<after.arrivals()-before.arrivals()<<" transport="<<after.transport(0)<<','<<after.transport(1)<<'\n';
     if(!boss||!scene||!replayed||after.anchor()!=leader||after.departures()<=before.departures()||after.arrivals()<=before.arrivals()||after.transport(leader^1)||!after.actors()[0].hp||!after.actors()[1].hp)throw std::runtime_error("Native boss-door/intro/partner return did not complete");
     replay_effect();return 0;
    }
    if(mode.starts_with("--menu-host-view-")){host_view();return 0;}
    if(mode.starts_with("--menu-bikes-")) {
     if(m.debug_read(0xc05f,1)[0]!=6)throw std::runtime_error("An original parked Ride Chaser section is required");
     const bool lower=m.debug_read(0xc066,1)[0]!=0;
     for(unsigned f=0;f<360&&(!state().bikes().mounts[0]||!state().bikes().mounts[1]);++f)step(1,1,1);
     auto mounted=state().bikes();picture("bikes-mounted");gbcllr::write_file_atomic("bikes-mounted.state",m.snapshot());
     auto a=state().actors();
     std::cout<<"Native bike mounts="<<mounted.mounts[0]<<','<<mounted.mounts[1]<<" passes="<<mounted.passes[0]<<','<<mounted.passes[1]<<" actions="<<a[0].action<<','<<a[1].action<<" xy="<<a[0].x<<','<<a[0].y<<'/'<<a[1].x<<','<<a[1].y<<'\n';
     if(!mounted.active||mounted.mounts[0]!=1||mounted.mounts[1]!=1)throw std::runtime_error("Two original bikes did not mount their assigned players exactly once");
     for(unsigned f=0;f<600&&(!state().bikes().riding[0]||!state().bikes().riding[1]);++f)step(1);
     if(!state().bikes().riding[0]||!state().bikes().riding[1])throw std::runtime_error("Both original mounting cinematics did not reach playable riding");
     auto scroll=m.debug_read(0xca42,2);unsigned before=scroll[0]|unsigned(scroll[1])<<8,count=state().bikes().scrolls;bool distinct=false;
     for(unsigned f=0;f<48;++f){step(1,1,f<8?18:lower?1:2);auto next=state().actors();distinct|=std::abs(next[0].x-next[1].x)>=8&&std::abs(next[0].y-next[1].y)>=8;if(f==8||f==24)picture("bikes-movement-"+std::to_string(f));}
     scroll=m.debug_read(0xca42,2);unsigned after=scroll[0]|unsigned(scroll[1])<<8,events=state().bikes().scrolls-count;
     std::cout<<"Bike scroll="<<before<<'>'<<after<<" native advances="<<events<<" distinct="<<distinct<<'\n';
     if(!events||uint16_t(after-before)!=2*events)throw std::runtime_error("The second bike repeated or lost canonical autoscroll");
     if(!distinct)throw std::runtime_error("Bike movement/jumping was not independently controlled");
     replay_effect();
     auto place=[&](unsigned seat,unsigned px,unsigned py=65536){bool found=false;auto deadline=m.stats().cycles+140448*4;while(m.stats().cycles<deadline){auto at=m.stats();if(at.rom_bank==5&&at.pc==0x4004&&m.debug_read(0xc0c9,1)[0]==(o.mods[0].player1_character^seat)){found=true;break;}m.run_until(at.cycles+1);m.take_audio();}if(!found)throw std::runtime_error("Bike approach missed its controller");const uint8_t position[]{uint8_t(px),uint8_t(px>>8)};m.debug_write(0xc108,position);if(py<65536){const uint8_t height[]{0,uint8_t(py),uint8_t(py>>8)},shield[]{255};m.debug_write(0xc104,height);m.debug_write(0xc130,shield);}};
     // Separate the already mounted riders on the same original floor for
     // visual comparison. This supplies position, never a mounting result.
     if(lower){auto camera=m.debug_read(0xca08,2);unsigned cx=camera[0]|unsigned(camera[1])<<8;place(0,cx+96);place(1,cx+32);}else{place(0,480);place(1,320);}
     step(8);auto final=state();const auto* first=final.bike_attachment(0);const auto* second=final.bike_attachment(1);
     if(!first||!second||!first->count||!second->count||first->colors==second->colors){auto a=final.actors();auto b=final.bikes();std::cerr<<"Bike art pointers="<<bool(first)<<','<<bool(second)<<" colors equal="<<(first&&second&&first->colors==second->colors)<<" HP="<<a[0].hp<<','<<a[1].hp<<" action="<<a[0].action<<','<<a[1].action<<" riding="<<unsigned(b.riding[0])<<','<<unsigned(b.riding[1])<<" transport="<<final.transport(0)<<','<<final.transport(1)<<'\n';picture("bikes-asset-failed");gbcllr::write_file_atomic("bikes-asset-failed.state",m.snapshot());throw std::runtime_error("Both bike attachments lack independent original tiles/palettes");}
     picture("bikes-moving");m.mod_coop_view(2);picture("bikes-split");m.mod_coop_view(0);gbcllr::write_file_atomic("bikes-moving.state",m.snapshot());
     if(mode.starts_with("--menu-bikes-prepare-")){std::cout<<"Current original two-bike fixture prepared\n";return 0;}
     host_view();
     auto playable=m.snapshot();
     // The upper section qualifies common destruction/lives/rejoin behavior.
     // The later authored section adds its distinct entrance, map and exit;
     // its obstacles do not share the upper fixture's safe survivor positions.
     if(!lower){
     for(unsigned victim=0;victim<2;++victim) {
      if(!m.restore(playable,error))throw std::runtime_error(error);
      auto camera=m.debug_read(0xca08,2);unsigned cx=camera[0]|unsigned(camera[1])<<8,left=std::max(112,int(cx)-112);
      auto target=m.debug_read(0xca42,2);unsigned tx=target[0]|unsigned(target[1])<<8;
      std::cerr<<"Bike hazard approach victim="<<victim<<" camera="<<cx<<" target="<<tx<<" left="<<left<<'\n';
      place(victim^1,std::max(cx,tx)+80);place(victim,left+4);
      for(unsigned f=0;f<360&&state().lifecycle().status[victim]!=1;++f)step(1,victim?2:0,victim?0:2);
      auto fallen=state();auto lives=m.debug_read(0xc065,1)[0];
      if(fallen.lifecycle().status[victim]!=1||fallen.actors()[victim].hp||fallen.actors()[victim^1].hp!=16||lives!=3){picture("bike-hazard-failed");gbcllr::write_file_atomic("bike-hazard-failed.state",m.snapshot());std::cerr<<"Bike hazard status="<<unsigned(fallen.lifecycle().status[0])<<','<<unsigned(fallen.lifecycle().status[1])<<" HP="<<fallen.actors()[0].hp<<','<<fallen.actors()[1].hp<<" lives="<<unsigned(lives)<<" retries="<<fallen.lifecycle().retries<<'\n';throw std::runtime_error("A native autoscroll destruction changed the survivor or P1 life pool");}
      unsigned advanced=fallen.bikes().scrolls;auto prior=fallen.actors()[victim^1];step(8,victim?18:0,victim?0:18);step(8,victim?2:0,victim?0:2);
      auto survivor=state();auto next=survivor.actors()[victim^1];if(!next.hp||survivor.bikes().scrolls<=advanced||(next.x==prior.x&&next.y==prior.y)||survivor.lifecycle().status[victim]!=1){std::cerr<<"Surviving bike HP="<<next.hp<<" xy="<<prior.x<<','<<prior.y<<'>'<<next.x<<','<<next.y<<" scroll="<<advanced<<'>'<<survivor.bikes().scrolls<<" fallen="<<unsigned(survivor.lifecycle().status[victim])<<" anchor="<<survivor.anchor()<<'\n';throw std::runtime_error("The surviving bike did not keep its controls and autoscroll");}
      replay_effect();picture("bike-survivor-"+std::to_string(victim));gbcllr::write_file_atomic("bike-survivor-"+std::to_string(victim)+".state",m.snapshot());
      std::cout<<"Native bike destruction seat="<<victim<<" survivor HP="<<survivor.actors()[victim^1].hp<<" P1 lives="<<unsigned(lives)<<'\n';
      if(victim==0){
       unsigned retries=survivor.lifecycle().retries;auto camera=m.debug_read(0xca08,2);unsigned cx=camera[0]|unsigned(camera[1])<<8;
       place(1,unsigned(std::max(112,int(cx)-112)+4));
       bool retried=false;for(unsigned f=0;f<1200;++f){step(1);auto s=state();if(s.lifecycle().retries>retries&&s.actors()[0].hp==16&&s.actors()[1].hp==16&&!s.lifecycle().status[0]&&!s.lifecycle().status[1]){retried=true;break;}}
       if(!retried||state().lifecycle().retries!=retries+1||m.debug_read(0xc065,1)[0]!=2)throw std::runtime_error("A native two-bike team wipe did not charge exactly one P1 life and restore both riders");
       replay_effect();picture("bikes-team-retry");gbcllr::write_file_atomic("bikes-team-retry.state",m.snapshot());
       std::cout<<"Native two-bike team wipe restarts both for one P1 life\n";
      }
     }
     for(unsigned seat=0;seat<2;++seat) {
      if(!m.restore(playable,error))throw std::runtime_error(error);
      place(0,340);place(1,340);step(2);
      for(unsigned f=0;f<180&&state().lifecycle().status[seat]!=2;++f)step(1,seat?1:65,seat?65:1);
      step(8,1,1);
      if(state().lifecycle().status[seat]!=2){auto s=state();std::cerr<<"Bike withdrawal seat="<<seat<<" status="<<unsigned(s.lifecycle().status[0])<<','<<unsigned(s.lifecycle().status[1])<<" HP="<<s.actors()[0].hp<<','<<s.actors()[1].hp<<" xy="<<s.actors()[0].x<<','<<s.actors()[0].y<<'/'<<s.actors()[1].x<<','<<s.actors()[1].y<<" physical ground="<<unsigned(m.debug_read(0xc160,1)[0])<<'\n';picture("bike-withdraw-failed");gbcllr::write_file_atomic("bike-withdraw-failed.state",m.snapshot());throw std::runtime_error("A mounted living rider could not withdraw");}
      press(seat,64,0);
      for(unsigned f=0;f<240&&(state().lifecycle().status[seat]||state().transport(seat));++f)step(1,2,2);
      step(8,2,2);
      auto joined=state();
      if(joined.lifecycle().status[seat]||joined.transport(seat)||!joined.bikes().riding[seat]||joined.actors()[seat].hp!=16||m.debug_read(0xc065,1)[0]!=3){picture("bike-return-failed");gbcllr::write_file_atomic("bike-return-failed.state",m.snapshot());throw std::runtime_error("A living rider failed to return to its own bike after the original teleport");}
      replay_effect();picture("bike-return-"+std::to_string(seat));
      std::cout<<"Mounted withdrawal/rejoin seat="<<seat<<" retains native riding, HP and P1 lives\n";
     }
     if(!m.restore(playable,error))throw std::runtime_error(error);
     m.mod_viewport(160,144);{auto camera=m.debug_read(0xca08,2);unsigned cx=camera[0]|unsigned(camera[1])<<8;place(0,cx+24);place(1,cx+244);}m.mod_coop_shared_world(true);
     {unsigned departures=state().departures(),arrivals=state().arrivals();int x=state().actors()[0].x;
      step(24);auto s=state();
      if(s.departures()!=departures||s.arrivals()!=arrivals||s.transport(0)||s.transport(1)||(s.actors()[0].hp&&s.actors()[0].x<=x))throw std::runtime_error("Mounted catch-up did not use native push/destruction without teleporting");
      replay_effect();picture("bikes-catchup");std::cout<<"Shared-view mounted edge uses push/destruction without automatic teleport\n";
     }
     }
     if(!m.restore(playable,error))throw std::runtime_error(error);
     m.mod_viewport(384,144);m.mod_coop_shared_world(false);
     // Flight/protection supplies travel to the exit, while the native camera
     // streams every intervening column. Jumping its camera directly would
     // leave the original PPU cache inconsistent and invalidate visual checks.
     // Do not set C1F4, C1E5, scroll or checkpoint results.
     unsigned exit_approach=lower?0xbe0:0x6d0;
     for(unsigned f=0;f<1800;f+=4){auto camera=m.debug_read(0xca08,2);unsigned cx=camera[0]|unsigned(camera[1])<<8;if(cx>=exit_approach)break;place(0,cx+80,64);place(1,cx+96,64);step(4,1,1);}
     place(0,exit_approach+64,96);place(1,exit_approach+32,96);
     for(unsigned f=0;f<900;++f){step(1,1,1);auto s=state();if((lower||s.bikes().ends)&&s.bikes().exits[0]&&s.bikes().exits[1]&&!s.bikes().mounted[0]&&!s.bikes().mounted[1]&&s.actors()[0].action==0&&s.actors()[1].action==0)break;}
     step(16);auto exited=state();std::cout<<"Native bike exit events="<<exited.bikes().ends<<" original exit actor updates="<<exited.bikes().exits[0]<<','<<exited.bikes().exits[1]<<" actions="<<exited.actors()[0].action<<','<<exited.actors()[1].action<<" HP="<<exited.actors()[0].hp<<','<<exited.actors()[1].hp<<" wide="<<m.mod_view().expanded<<'\n';
     picture("bikes-exit");gbcllr::write_file_atomic("bikes-exit.state",m.snapshot());
     // The later authored exit can dismount through its CC actor before
     // the camera reaches the original C1F4 terminal-scroll write.
     if((lower?exited.bikes().ends>1:exited.bikes().ends!=1)||exited.bikes().mounted[0]||exited.bikes().mounted[1]||!exited.bikes().exits[0]||!exited.bikes().exits[1]||exited.actors()[0].action||exited.actors()[1].action||!exited.actors()[0].hp||!exited.actors()[1].hp||!m.mod_view().expanded)throw std::runtime_error("The native bike section did not dismount both players alive through one shared exit");
     {auto on_foot=m.snapshot();
      // Supply a subsequent room domain, not a door/checkpoint result. The
      // original second controller's return is the follow-service boundary;
      // inject its input there so a room script cannot replace the fixture.
      unsigned other=state().anchor()^1;bool found=false;auto deadline=m.stats().cycles+140448*4;
      while(m.stats().cycles<deadline){auto at=m.stats();if(at.rom_bank==5&&at.pc==0x4078&&m.debug_read(0xc0c9,1)[0]==(o.mods[0].player1_character^other)){found=true;break;}m.run_until(at.cycles+1);m.take_audio();}
      if(!found)throw std::runtime_error("On-foot room follow missed its original return boundary");
      const uint8_t left[]{0,9},right[]{0,10};m.debug_write(0xca0d,left);m.debug_write(0xca40,right);
      m.run_until(m.stats().cycles+1);m.take_audio();auto s=state();s.complete_frame({});auto camera=s.camera(other);
      if(camera[0]<2304||camera[0]>2560)throw std::runtime_error("On-foot camera retained the completed vehicle's room bounds");
      if(!m.restore(on_foot,error))throw std::runtime_error(error);
      std::cout<<"Native on-foot follow adopts the next room domain after both dismount\n";
     }
     replay_effect();
     std::cout<<"Two original mounting events, independent native bike movement/jump, one autoscroll and exact replay pass\n";return 0;
    }
    if(mode.starts_with("--menu-capsule-")) {
     const unsigned x=o.mods[0].player1_character,zero=x^1;
     auto select=[&](unsigned seat){auto deadline=m.stats().cycles+140448*4;while(m.stats().cycles<deadline){auto at=m.stats();if(at.rom_bank==5&&at.pc==0x4004&&m.debug_read(0xc0c9,1)[0]==(o.mods[0].player1_character^seat))return true;m.run_until(at.cycles+1);m.take_audio();}return false;};
     auto place=[&](unsigned seat,unsigned px,unsigned py){if(!select(seat))return false;const uint8_t position[]{0,uint8_t(py),uint8_t(py>>8),0,uint8_t(px),uint8_t(px>>8)},shield[]{255};m.debug_write(0xc104,position);m.debug_write(0xc130,shield);return true;};
     auto capsule=[&](){for(unsigned page=0xaa;page<0xb0;++page){auto b=m.debug_read(uint16_t(page<<8),0x30);if(b[0]&&b[0x2d]==6)return page;}return 0u;};
     if(m.debug_read(0xc05f,1)[0]!=1||(m.debug_read(0xc0d8,1)[0]&2))throw std::runtime_error("An unclaimed original Mammoth capsule-area fixture is required");
     if(!place(zero,448,448)||!place(x,512,416))throw std::runtime_error("Capsule dialogue approach missed a controller");
     unsigned opened=0;
     for(unsigned f=0;f<96&&!opened;++f){step(8,x?0:16,x?16:0);step(24);opened=capsule();}
     if(!opened){picture("capsule-dialogue-failed");gbcllr::write_file_atomic("capsule-dialogue-failed.state",m.snapshot());std::cerr<<"Capsule flags=";for(auto b:m.debug_read(0xc0c0,32))std::cerr<<' '<<unsigned(b);std::cerr<<'\n';throw std::runtime_error("Original Dr Light dialogue did not expose its upgrade capsule");}
     step(120);if(m.debug_read(0xc0d8,1)[0]&2)throw std::runtime_error("Capsule approach collected the upgrade before its contact check");
     // Zero is closer, but this original stage's capsule is X-only.
     // Supply original contact height at its native helper, not a grant.
     if(!place(zero,416,420)||!place(x,424,420))throw std::runtime_error("Capsule contact approach missed a controller");
     gbcllr::write_file_atomic("capsule-contact-ready.state",m.snapshot());unsigned contacts=0;
     auto deadline=m.stats().cycles+140448*900;
     while(m.stats().cycles<deadline){auto at=m.stats();if(at.rom_bank==0x36&&at.pc==0x5952){if(m.debug_read(0xc0c9,1)[0])throw std::runtime_error("Ineligible nearer Zero blocked X's original capsule");const uint8_t contact[]{0,164,1,0,160,1};m.debug_write(0xc104,contact);++contacts;}if(at.rom_bank==0x36&&at.pc==0x5983)break;m.run_until(at.cycles+1);m.take_audio();}
     if(!contacts||!(m.debug_read(0xc0d8,1)[0]&2)||(m.debug_read(0xc0dd,1)[0]&2)) {picture("capsule-contact-failed");auto a=state().actors();std::cerr<<"Capsule contacts="<<contacts<<" records="<<unsigned(m.debug_read(0xc0d8,1)[0])<<','<<unsigned(m.debug_read(0xc0dd,1)[0])<<" actors="<<a[0].x<<','<<a[0].y<<','<<a[0].hp<<'/'<<a[1].x<<','<<a[1].y<<','<<a[1].hp<<" flags="<<unsigned(m.debug_read(0xc014,1)[0])<<','<<unsigned(m.debug_read(0xc021,1)[0])<<','<<unsigned(m.debug_read(0xc9b0,1)[0])<<" box=";for(auto b:m.debug_read(0xcaae,10))std::cerr<<' '<<unsigned(b);std::cerr<<'\n';throw std::runtime_error("Native armor capsule did not grant only its eligible character");}
     step(1);replay_effect();picture("capsule-granted");gbcllr::write_file_atomic("capsule-granted.state",m.snapshot());step(600);
     for(unsigned page=0;page<32&&m.debug_read(0xc014,1)[0];++page){step(4,zero?0:16,zero?16:0);step(60);}
     step(180);
     if(!(m.debug_read(0xc0d8,1)[0]&2)||(m.debug_read(0xc0dd,1)[0]&2))throw std::runtime_error("Capsule continuation changed armor ownership");
     if(m.debug_read(0xc014,1)[0]||state().transport(zero)||state().lifecycle().status[zero]||!state().actors()[zero].hp){picture("capsule-return-failed");gbcllr::write_file_atomic("capsule-return-failed.state",m.snapshot());throw std::runtime_error("Native capsule cinematic did not return its living partner");}
     replay_effect();picture("capsule-returned");gbcllr::write_file_atomic("capsule-returned.state",m.snapshot());
     std::cout<<"Native capsule dialogue/contact grants X alone with nearer Zero, contacts="<<contacts<<'\n';return 0;
    }
    if(mode.starts_with("--menu-pickups-")) {
     auto select=[&](unsigned seat){auto deadline=m.stats().cycles+140448*4;while(m.stats().cycles<deadline){auto at=m.stats();if(at.rom_bank==5&&at.pc==0x4004&&m.debug_read(0xc0c9,1)[0]==(o.mods[0].player1_character^seat))return;m.run_until(at.cycles+1);m.take_audio();}throw std::runtime_error("Pickup approach missed its controller");};
     auto boundary=[&](){auto deadline=m.stats().cycles+140448*4;while(m.stats().cycles<deadline){auto at=m.stats();if(at.rom_bank==1&&at.pc==0x656f)return;m.run_until(at.cycles+1);m.take_audio();}throw std::runtime_error("Pickup approach missed the shared world boundary");};
     auto available=[&](unsigned seat,unsigned kind,unsigned variant){
      boundary();auto a=state().actors()[seat];unsigned slot=0;for(unsigned page=0xd8;page<0xe0;++page)if(!m.debug_read(uint16_t(page<<8),1)[0]){slot=page<<8;break;}if(!slot)throw std::runtime_error("Pickup availability requires an empty original item slot");
      std::array<uint8_t,256> item{};item[0]=1;unsigned y=unsigned(a.y-8);item[5]=uint8_t(y);item[6]=uint8_t(y>>8);item[8]=uint8_t(a.x);item[9]=uint8_t(a.x>>8);item[10]=uint8_t(variant);item[0x2d]=uint8_t(kind);m.debug_write(uint16_t(slot),item);
      // Availability/contact only. Original init, collision, collection,
      // health/energy/currency/ownership writes remain native outcomes.
      for(unsigned f=0;f<360;++f){step(1);if(!m.debug_read(uint16_t(slot),1)[0]){step(8);return;}}
      picture("pickup-failed");gbcllr::write_file_atomic("pickup-failed.state",m.snapshot());
      std::cerr<<"Pickup kind="<<kind<<" seat="<<seat<<" slot="<<std::hex<<slot<<" actor bytes=";for(auto b:m.debug_read(uint16_t(slot),0x48))std::cerr<<' '<<unsigned(b);std::cerr<<" globals=";for(auto b:m.debug_read(0xc000,0x40))std::cerr<<' '<<unsigned(b);std::cerr<<" selected="<<unsigned(m.debug_read(0xc131,1)[0])<<std::dec<<'\n';
      throw std::runtime_error("The supplied original pickup did not complete native collection");
     };
     for(unsigned collector=0;collector<2;++collector)for(unsigned kind:{2u,0x0cu,4u,0x0eu,5u}) {
      if(!m.restore(original,error))throw std::runtime_error(error);
      if(kind==0x0c&&!(o.mods[0].player1_character^collector)){press(collector,128,118);cursor(collector,2);press(collector,16);press(collector,128,90);}
      for(unsigned seat=0;seat<2;++seat){select(seat);auto a=state().actors()[seat];unsigned x=seat==collector?176:48;const uint8_t position[]{0,uint8_t(a.y),uint8_t(a.y>>8),0,uint8_t(x),0},health[]{uint8_t(kind==2?4:16)},shield[]{255};m.debug_write(0xc104,position);m.debug_write(0xc121,health);m.debug_write(0xc130,shield);}
      boundary();unsigned lives=m.debug_read(0xc065,1)[0];auto wallet=m.debug_read(0xc0f0,4);unsigned souls=wallet[0]+10*wallet[1]+100*wallet[2]+1000*wallet[3];
      available(collector,kind,kind==5?16:1);auto a=state().actors();wallet=m.debug_read(0xc0f0,4);unsigned after_souls=wallet[0]+10*wallet[1]+100*wallet[2]+1000*wallet[3];
      std::cout<<"Native pickup="<<kind<<" collector="<<collector<<" HP="<<a[0].hp<<','<<a[1].hp<<" energy="<<unsigned(m.debug_read(0xc06d,1)[0]&31)<<','<<unsigned(m.debug_read(0xc06f,1)[0]&31)<<" lives="<<unsigned(m.debug_read(0xc065,1)[0])<<" souls="<<after_souls<<" common="<<unsigned(m.debug_read(0xc0d8,1)[0])<<','<<unsigned(m.debug_read(0xc0dd,1)[0])<<'\n';
      if(kind==2&&(a[collector].hp!=6||a[collector^1].hp!=4))throw std::runtime_error("Health pickup did not remain collector-owned");
      if(kind==0x0c){unsigned character=o.mods[0].player1_character^collector;if((m.debug_read(uint16_t(character?0xc06d:0xc06f),1)[0]&31)!=6||(m.debug_read(uint16_t(character?0xc06f:0xc06d),1)[0]&31)!=4)throw std::runtime_error("Energy pickup changed the other character's energy");}
      if(kind==4&&m.debug_read(0xc065,1)[0]!=lives+1)throw std::runtime_error("Extra life did not increment P1's shared pool once");
      if(kind==0x0e&&after_souls!=souls+100)throw std::runtime_error("Native Soul pickup did not enter the shared wallet once");
      if(kind==5){if(!(m.debug_read(0xc0d8,1)[0]&16)||!(m.debug_read(0xc0dd,1)[0]&16))throw std::runtime_error("Native tank acquisition did not update both original common records");available(collector,2,1);std::cout<<"Acquired tank charge="<<unsigned(m.debug_read(0xc068,1)[0])<<" HP="<<state().actors()[0].hp<<','<<state().actors()[1].hp<<" common="<<unsigned(m.debug_read(0xc083,1)[0])<<'\n';if((m.debug_read(0xc068,1)[0]&127)!=1)throw std::runtime_error("Original full-health overflow did not charge the acquired tank");select(collector^1);const uint8_t hurt[]{8};m.debug_write(0xc121,hurt);step(2);press(collector^1,128,118);cursor(collector^1,10);press(collector^1,16,120);if(state().actors()[collector^1].hp<=8||(m.debug_read(0xc068,1)[0]&127))throw std::runtime_error("The partner could not use the genuinely acquired common tank");press(collector^1,128,120);}
      replay_effect();picture("pickup-"+std::to_string(kind)+"-"+std::to_string(collector));gbcllr::write_file_atomic("pickup-"+std::to_string(kind)+"-"+std::to_string(collector)+".state",m.snapshot());
     }
     std::cout<<"Original health/energy, P1 lives, shared Souls and acquired common tank transactions passed\n";return 0;
    }
    if(mode.starts_with("--menu-techniques-")) {
     unsigned zero=o.mods[0].player1_character^1;
     step(64,zero?0:1,zero?1:0);original=m.snapshot();
     for(unsigned technique=0;technique<10;++technique) {
      if(!m.restore(original,error))throw std::runtime_error(error);
      press(zero,128,118);cursor(zero,technique);press(zero,16);
      unsigned energy=m.debug_read(0xc06d,1)[0]&31,spawned=0;step(2,zero?0:128,zero?128:0);
      for(unsigned f=0;f<180&&state().menus().active;++f)step(1);
      if(state().menus().active)throw std::runtime_error("Native Zero technique did not resume");
      std::cout<<"Native Zero technique="<<technique<<" action="<<state().actors()[zero].action;
      std::map<unsigned,unsigned> packages;
      for(unsigned f=0;f<96;++f){uint8_t buttons=f<16?16:f<32?36:f<48?40:f<64?33:f<80?32:0;step(1,zero?0:buttons,zero?buttons:0);++packages[m.debug_read(0xcbc4,1)[0]];for(unsigned page=0xb0;page<0xb8;++page)spawned+=bool(m.debug_read(uint16_t(page<<8),1)[0]);if(f==0||f==8||f==24||f==64)picture("technique-"+std::to_string(technique)+"-"+std::to_string(f));if(f==8)replay_effect();}
      std::cout<<" shot samples="<<spawned<<" energy="<<energy<<'>'<<unsigned(m.debug_read(0xc06d,1)[0]&31)<<" packages=";for(auto [id,n]:packages)std::cout<<id<<':'<<n<<' ';std::cout<<'\n';
      gbcllr::write_file_atomic("technique-"+std::to_string(technique)+".state",m.snapshot());
     }
     return 0;
    }
    if(mode.starts_with("--menu-arsenal-")) {
     unsigned x=o.mods[0].player1_character;
     step(64,x?0:1,x?1:0);original=m.snapshot();
     for(unsigned weapon=0;weapon<10;++weapon) {
      if(!m.restore(original,error))throw std::runtime_error(error);
      press(x,128,118);cursor(x,weapon);press(x,16);press(x,128,180);
      if(state().menus().active)throw std::runtime_error("Arsenal selection did not finish the native menu");
      unsigned energy=m.debug_read(uint16_t(0xc06d+weapon),1)[0]&31,spawned=0;
      for(unsigned f=0;f<96;++f){step(1,x?0:uint8_t(f%24<12?32:0),x?uint8_t(f%24<12?32:0):0);for(unsigned page=0xb0;page<0xb8;++page)spawned+=bool(m.debug_read(uint16_t(page<<8),1)[0]);if(f==8||f==32||f==64)picture("weapon-"+std::to_string(weapon)+"-"+std::to_string(f));if(f==8)replay_effect();}
      std::cout<<"Native X weapon="<<weapon<<" active shot samples="<<spawned<<" energy="<<energy<<'>'<<unsigned(m.debug_read(uint16_t(0xc06d+weapon),1)[0]&31)<<'\n';
      gbcllr::write_file_atomic("weapon-"+std::to_string(weapon)+".state",m.snapshot());
     }
     return 0;
    }
    if(mode.starts_with("--menu-survivor-")) {
     for(unsigned fallen=0;fallen<2;++fallen) {
      if(!m.restore(original,error))throw std::runtime_error(error);
      bool found=false;auto deadline=m.stats().cycles+140448*4;
      while(m.stats().cycles<deadline){auto at=m.stats();if(at.rom_bank==5&&at.pc==0x4004&&m.debug_read(0xc0c9,1)[0]==(o.mods[0].player1_character^fallen)){found=true;break;}m.run_until(at.cycles+1);m.take_audio();}
      if(!found)throw std::runtime_error("Menu survivor approach missed its controller");
      auto bottom=m.debug_read(0xca12,2);unsigned y=(bottom[0]|unsigned(bottom[1])<<8)+184;const uint8_t position[]{uint8_t(y),uint8_t(y>>8)};m.debug_write(0xc105,position);
      bool dead=false;for(unsigned f=0;f<480;++f){step(1);if(state().lifecycle().status[fallen]==1){dead=true;break;}}
      if(!dead)throw std::runtime_error("Menu survivor approach did not complete the native death");
      auto lives=m.debug_read(0xc065,1)[0];press(fallen^1,128,118);auto menu=state().menus();
      if(!menu.active||menu.ready!=3||menu.primary!=(fallen^1))throw std::runtime_error("The survivor did not open both original menus");
      auto prior=menu.cursor;press(fallen,1);press(fallen,16);press(fallen,128);
      if(!state().menus().active||state().menus().cursor[fallen]!=prior[fallen]||state().actors()[fallen].hp||state().lifecycle().status[fallen]!=1)throw std::runtime_error("A fallen menu accepted input or revived its actor");
      picture("survivor-menu-"+std::to_string(fallen));press(fallen^1,128,180);
      if(state().menus().active||state().actors()[fallen].hp||state().lifecycle().status[fallen]!=1||m.debug_read(0xc065,1)[0]!=lives)throw std::runtime_error("Menu resume changed fallen participation or P1 lives");
     }
     std::cout<<"Either survivor opens/resumes authentic menus; fallen menus stay read-only without revival or life charge\n";return 0;
    }
    for(unsigned opener=0;opener<2;++opener) {
     if(!m.restore(original,error))throw std::runtime_error(error);
     step(2,opener?0:128,opener?128:0);step(118);
     auto s=state();auto menu=s.menus();
     std::cout<<"Menu opener="<<opener<<" active="<<menu.active<<" frames="<<menu.frames<<" page="<<unsigned(menu.page[0])<<','<<unsigned(menu.page[1])<<" cursors="<<unsigned(menu.cursor[0])<<','<<unsigned(menu.cursor[1])<<'\n';
     picture("menus-"+std::to_string(opener));gbcllr::write_file_atomic("menus-"+std::to_string(opener)+".state",m.snapshot());
     if(!menu.active||menu.frames<60||menu.page[0]||menu.page[1])throw std::runtime_error("Either-seat Start did not open both native personal menus");
     press(0,16);press(1,16);if(!state().menus().active)throw std::runtime_error("An equipment confirmation dismissed the partner's menu");
     auto passes=s.passes();auto actors=s.actors();auto cursors=menu.cursor;
     step(2,1,0);step(10);auto next=state().menus();
     if(next.cursor[0]==cursors[0]||next.cursor[1]!=cursors[1])throw std::runtime_error("P1 cursor crossed into the other personal menu");
     cursors=next.cursor;step(2,0,8);step(10);next=state().menus();
     if(next.cursor[1]==cursors[1]||next.cursor[0]!=cursors[0])throw std::runtime_error("P2 cursor crossed into the other personal menu");
     for(unsigned seat=0;seat<2;++seat){m.mod_coop_view(1,seat);picture("personal-"+std::to_string(opener)+"-"+std::to_string(seat));}m.mod_coop_view(0,0);
     auto saved=m.snapshot();std::vector<int16_t> a,b;std::vector<uint32_t> view_a,view_b;
     for(unsigned f=0;f<16;++f){m.run_frame(0,0);auto pcm=m.take_audio();a.insert(a.end(),pcm.begin(),pcm.end());auto v=m.mod_view();view_a.insert(view_a.end(),v.pixels.begin(),v.pixels.end());}
     auto expected=m.snapshot();if(!m.restore(saved,error))throw std::runtime_error(error);
     for(unsigned f=0;f<16;++f){m.run_frame(0,0);auto pcm=m.take_audio();b.insert(b.end(),pcm.begin(),pcm.end());auto v=m.mod_view();view_b.insert(view_b.end(),v.pixels.begin(),v.pixels.end());}
     if(a!=b||view_a!=view_b||m.snapshot()!=expected)throw std::runtime_error("Native personal menu state/PCM/view replay diverged");
     s=state();auto after=s.actors();
     if(s.passes()!=passes||after[0].x!=actors[0].x||after[0].y!=actors[0].y||after[1].x!=actors[1].x||after[1].y!=actors[1].y)throw std::runtime_error("Personal menus advanced the shared world");
     step(2,opener?128:0,opener?0:128);step(178);picture("resumed-"+std::to_string(opener));
     s=state();if(s.menus().active||s.passes()<=passes)throw std::runtime_error("Common Start did not resume the shared world");
     step(16,1,2);step(24,32,32);picture("after-menu-"+std::to_string(opener));
    }
    if(!m.restore(original,error))throw std::runtime_error(error);
    // Supply approach resources, not the result: the original menu must
    // consume one tank and perform every health increment itself.
    for(unsigned seat=0;seat<2;++seat){bool found=false;auto deadline=m.stats().cycles+140448*4;while(m.stats().cycles<deadline){auto at=m.stats();if(at.rom_bank==5&&at.pc==0x4004&&m.debug_read(0xc0c9,1)[0]==(o.mods[0].player1_character^seat)){found=true;break;}m.run_until(at.cycles+1);m.take_audio();}if(!found)throw std::runtime_error("Menu health approach missed its controller");const uint8_t health[]{8},owned_tank[]{uint8_t(m.debug_read(0xc083,1)[0]|16)};m.debug_write(0xc121,health);m.debug_write(0xc083,owned_tank);}
    const uint8_t tank[]{0x8c};m.debug_write(0xc068,tank);
    for(unsigned record:{0xc0d8u,0xc0ddu}){const uint8_t owned[]{uint8_t(m.debug_read(uint16_t(record),1)[0]|16)};m.debug_write(uint16_t(record),owned);}step(2);
    press(0,128,118);cursor(0,10);cursor(1,10);picture("tank-ready");gbcllr::write_file_atomic("tank-ready.state",m.snapshot());
    step(2,16,16);step(6);auto healing=state().actors();
    std::cout<<"Healing HP="<<healing[0].hp<<','<<healing[1].hp<<" tank="<<unsigned(m.debug_read(0xc068,1)[0])<<" idle="<<state().menus().idle<<'\n';
    unsigned healer=healing[0].hp>8?0:1;
    if(healing[healer].hp<=8||healing[healer^1].hp!=8)throw std::runtime_error("Simultaneous tank input did not serialize native healing");
    press(1,128,2);if(!state().menus().active)throw std::runtime_error("Start interrupted a pending native heal");
    auto healing_state=m.snapshot();step(80);auto healed=state().actors();
    std::cout<<"Serialized tank HP="<<healed[0].hp<<','<<healed[1].hp<<" tank="<<unsigned(m.debug_read(0xc068,1)[0])<<'\n';
    if(healed[healer].hp!=16||healed[healer^1].hp!=8||m.debug_read(0xc068,1)[0]!=0x80)throw std::runtime_error("Native tank was duplicated or healed the wrong player");
    if(!m.restore(healing_state,error))throw std::runtime_error(error);step(80);if(state().actors()[healer].hp!=16||state().actors()[healer^1].hp!=8)throw std::runtime_error("Pending native heal did not restore");
    picture("tank-complete");press(healer^1,16,80);if(state().actors()[healer^1].hp!=8)throw std::runtime_error("An empty shared tank healed the partner");
    // B and the native Exit item are common resume controls too.
    press(1,32,180);if(state().menus().active)throw std::runtime_error("Native B did not resume both menus");
    if(!m.restore(original,error))throw std::runtime_error(error);
    // Acquired-part fixture prerequisite. Native equip/unequip must change
    // only the chosen character's active workspace and original record.
    const uint8_t available[]{1};m.debug_write(0xc0d0,available);gbcllr::write_file_atomic("parts-approach.state",m.snapshot());press(1,128,118);
    for(unsigned seat=0;seat<2;++seat){cursor(seat,14);press(seat,16,90);if(state().menus().page[seat]!=2)throw std::runtime_error("Original parts page did not open");}
    for(unsigned seat=0;seat<2;++seat){auto record=uint16_t((o.mods[0].player1_character^seat)?0xc0db:0xc0d6);press(seat,16);if(!(m.debug_read(record,1)[0]&1))throw std::runtime_error("The native part did not equip for its owner");}
    picture("parts-equipped");
    for(unsigned seat=0;seat<2;++seat){auto record=uint16_t((o.mods[0].player1_character^seat)?0xc0db:0xc0d6);press(seat,32);if(m.debug_read(record,1)[0]&1)throw std::runtime_error("The native part did not unequip for its owner");}
    press(1,128,180);if(state().menus().active)throw std::runtime_error("Parts menus did not resume");
    press(0,128,118);cursor(1,15);auto setting=m.debug_read(0xc001,1)[0];auto battery=m.battery();press(1,16,90);picture("options");press(1,16);press(1,32,90);
    if(m.debug_read(0xc001,1)[0]!=(setting^1)||m.battery()==battery)throw std::runtime_error("P2 native Options did not update and save the host's common preferences");
    std::cout<<"Native character parts equip/unequip and P2 host-owned Options save passed\n";
    press(0,128,180);if(state().menus().active)throw std::runtime_error("Options return did not release the common pause");
    gbcllr::write_file_atomic("menus-current.state",m.snapshot());
    std::cout<<"Either-seat menus, isolated cursors, frozen world, common resume and exact state/PCM/view replay passed\n";return 0;
   }
   if(mode.starts_with("--lifecycle-")) {
    m.mod_viewport(384,144);m.mod_coop_shared_world(false);
    auto original=m.snapshot();
    auto state=[&](){gbcllr::Xtreme2Coop s(o.mods[0].player1_character);s.restore(m.coop_state());return s;};
    auto step=[&](unsigned n,uint8_t p1=0,uint8_t p2=0){for(unsigned f=0;f<n;++f){m.run_frame(p1,p2);m.take_audio();}};
    auto picture=[&](const std::string& name){auto v=m.mod_view();std::ofstream out(name+".ppm",std::ios::binary);out<<"P6\n"<<v.width<<" 144\n255\n";for(auto p:v.pixels){char rgb[]{char(p>>16),char(p>>8),char(p)};out.write(rgb,3);}};
    auto replay=[&](){auto before=m.snapshot();std::vector<int16_t> a,b;for(unsigned f=0;f<16;++f){m.run_frame();auto pcm=m.take_audio();a.insert(a.end(),pcm.begin(),pcm.end());}auto expected=m.snapshot();if(!m.restore(before,error))throw std::runtime_error(error);for(unsigned f=0;f<16;++f){m.run_frame();auto pcm=m.take_audio();b.insert(b.end(),pcm.begin(),pcm.end());}if(a!=b||m.snapshot()!=expected)throw std::runtime_error("Lifecycle state/PCM replay diverged");if(!m.restore(before,error))throw std::runtime_error(error);};
    auto select=[&](unsigned seat){
     auto deadline=m.stats().cycles+140448*4;bool found=false;
     while(m.stats().cycles<deadline){auto s=m.stats();if(s.rom_bank==5&&s.pc==0x4004&&m.debug_read(0xc0c9,1)[0]==(o.mods[0].player1_character^seat)){found=true;break;}m.run_until(s.cycles+1);m.take_audio();}
     if(!found){picture("pit-approach");auto at=m.stats();std::cerr<<"Pit wait bank="<<at.rom_bank<<" pc="<<at.pc<<" physical="<<unsigned(m.debug_read(0xc0c9,1)[0])<<" body="<<unsigned(m.debug_read(0xc101,1)[0])<<" pairs="<<state().passes()<<'\n';throw std::runtime_error("Pit approach did not reach the selected controller");}
    };
    auto fall=[&](unsigned seat){
     select(seat);
     if(mode.starts_with("--lifecycle-post-view-")&&state().bikes().mounted[seat]){
      auto camera=m.debug_read(0xca08,2);int cx=camera[0]|unsigned(camera[1])<<8;unsigned x=unsigned(std::max(112,cx-112)+4);
      const uint8_t position[]{uint8_t(x),uint8_t(x>>8)};m.debug_write(0xc108,position);return;
     }
     auto bottom=m.debug_read(0xca12,2);unsigned y=(bottom[0]|unsigned(bottom[1])<<8)+184;
     const uint8_t position[]{uint8_t(y),uint8_t(y>>8)};m.debug_write(0xc105,position);
     // Position beyond the original pit plane; native physics determines
     // the death. HP, action, retry, life and outcome flags are untouched.
    };
    if(mode.starts_with("--lifecycle-post-bike-push-")){
     auto initial=state();if(!initial.bikes().riding[0]||!initial.bikes().riding[1])throw std::runtime_error("Two mounted riders required");
     // Reproduce the original attachment's temporary free on a copied native
     // state. A mounted rider retains that last complete producer until redraw.
     {auto cached=state();const auto* art=cached.bike_attachment(1);if(!art)throw std::runtime_error("Original rider attachment required");auto before=*art;
      std::array<uint8_t,0x8000> ram{};gbcllr::ModContext c{};c.wram=ram.data();unsigned page;
      if(art->key<512){c.ram_bank=art->key/32;page=0xa0+art->key%32;}
      else if(art->key<528)page=0xc0+art->key-512;
      else {c.wram_bank=(art->key-528)/16;page=0xd0+(art->key-528)%16;}
      cached.write(c,uint16_t(page<<8),0);for(unsigned n=0;n<4;++n)cached.complete_frame(c);art=cached.bike_attachment(1);
      if(!art||art->active||art->texture!=before.texture||std::memcmp(art->pieces.data(),before.pieces.data(),sizeof before.pieces))throw std::runtime_error("Native attachment refresh dropped or corrupted a mounted bike");}
     auto mounted=m.snapshot();unsigned retained=0,visible=0;
     auto place=[&](unsigned seat,int x,int y=-1){select(seat);const uint8_t p[]{uint8_t(x),uint8_t(x>>8)},shield[]{255};m.debug_write(0xc108,p);m.debug_write(0xc130,shield);if(y>=0){const uint8_t py[]{uint8_t(y),uint8_t(y>>8)};m.debug_write(0xc105,py);}};
     // A short flat-floor approach qualifies native pushing separately from
     // the authored obstacle below. Positions are prerequisites, not results.
     m.mod_viewport(160,144);m.mod_coop_shared_world(true);
     auto camera=m.debug_read(0xca08,2);int cx=camera[0]|unsigned(camera[1])<<8;
     int y=initial.actors()[0].y;place(0,cx+80);place(1,cx+4,y);int start=state().actors()[1].x;
     gbcllr::write_file_atomic("bike-push-clear.state",m.snapshot());
     for(unsigned f=0;f<16;++f){step(1);auto s=state();if(!s.actors()[1].hp)throw std::runtime_error("Unobstructed trailing bike was destroyed");}
     auto pushed=state();picture("bike-push-clear");
     if(pushed.actors()[1].x<=start||pushed.departures()!=initial.departures()||pushed.arrivals()!=initial.arrivals())throw std::runtime_error("Trailing bike did not advance through native movement without teleporting");
     replay();
     if(!m.restore(mounted,error))throw std::runtime_error(error);
     m.mod_viewport(384,144);m.mod_coop_shared_world(true);camera=m.debug_read(0xca08,2);cx=camera[0]|unsigned(camera[1])<<8;
     place(0,cx+80);place(1,std::max(112,cx-112)+4);
     gbcllr::write_file_atomic("bike-push-blocked.state",m.snapshot());m.trace_enabled(true);
     for(unsigned f=0;f<40&&state().actors()[1].hp;++f)step(1);
     auto trace=m.take_trace();m.trace_enabled(false);auto crushed=state();picture("bike-push-blocked");
     if(crushed.actors()[1].hp||!crushed.actors()[0].hp||crushed.departures()!=initial.departures()||m.debug_read(0xc065,1)[0]!=3||std::find(trace.begin(),trace.end(),0x2c*0x4000+0x7cc2-0x4000)==trace.end())throw std::runtime_error("Blocked bike did not use original destruction while retaining survivor and team lives");
     replay();
     if(!m.restore(mounted,error))throw std::runtime_error(error);
     m.mod_viewport(384,144);m.mod_coop_shared_world(true);
     auto scroll_before=m.debug_read(0xca42,2);unsigned start_scroll=scroll_before[0]|unsigned(scroll_before[1])<<8,start_events=state().bikes().scrolls;
     for(unsigned f=0;f<96;++f){step(1,1|(f%24<8?16:0),1|(f%24<8?16:0));auto s=state();for(unsigned seat=0;seat<2;++seat)if(s.actors()[seat].hp&&s.bikes().mounted[seat]&&!s.transport(seat)){auto* art=s.bike_attachment(seat);if(!art)throw std::runtime_error("Living mounted rider lost its complete bike attachment");++visible;retained+=!art->active;}if(s.shared_camera()!=s.camera(s.anchor()))throw std::runtime_error("Mounted shared camera averaged rider positions");}
     auto scroll_after=m.debug_read(0xca42,2);unsigned finish_scroll=scroll_after[0]|unsigned(scroll_after[1])<<8;
     if(uint16_t(finish_scroll-start_scroll)!=2*(state().bikes().scrolls-start_events))throw std::runtime_error("Two riders repeated the shared autoscroll");
     picture("bike-stable");replay();
     std::cout<<"Native unobstructed push, terrain crush, no mounted teleport, survivor/team lives, "<<visible<<" complete bike poses ("<<retained<<" retained), fixed scroll camera and replay passed\n";return 0;
    }
    if(mode.starts_with("--lifecycle-post-death-art-")){
     // Supply an on-screen death prerequisite; the original controller,
     // particle spawner and graphics producers generate the inspected result.
     select(0);const uint8_t hp[]{0},death[]{0x8c};m.debug_write(0xc121,hp);m.debug_write(0xc1a5,death);
     gbcllr::write_file_atomic("death-art-setup.state",m.snapshot());
     std::cout<<"Original death-art prerequisite prepared; run --campaign-death-art-x for qualification\n";return 0;
    }
    if(mode.starts_with("--lifecycle-post-view-")){
     unsigned inspected=0;
     auto wide=[&](){for(unsigned view=0;view<3;++view){m.mod_coop_view(view);auto v=m.mod_view();if(!v.expanded||v.width!=384){picture("death-view-failed");gbcllr::write_file_atomic("death-view-failed.state",m.snapshot());throw std::runtime_error("Death/retry lost the expanded co-op canvas");}++inspected;}m.mod_coop_view(0);};
     unsigned victims=mode.find("-bike-")!=std::string::npos?1u:2u;
     for(unsigned victim=0;victim<victims;++victim){
      if(!m.restore(original,error))throw std::runtime_error(error);unsigned lives=m.debug_read(0xc065,1)[0];fall(victim);
      bool fallen=false;for(unsigned f=0;f<480;++f){step(1,victim?1:2,victim?2:1);wide();if(state().lifecycle().status[victim]==1){fallen=true;break;}}
      if(!fallen){picture("death-approach-failed");gbcllr::write_file_atomic("death-approach-failed.state",m.snapshot());auto a=state().actors();auto b=state().bikes();std::cerr<<"Death approach victim="<<victim<<" HP="<<a[0].hp<<','<<a[1].hp<<" status="<<unsigned(state().lifecycle().status[0])<<','<<unsigned(state().lifecycle().status[1])<<" action="<<a[0].action<<','<<a[1].action<<" x="<<a[0].x<<','<<a[1].x<<" y="<<a[0].y<<','<<a[1].y<<" mounted="<<unsigned(b.mounted[0])<<','<<unsigned(b.mounted[1])<<" transport="<<state().transport(0)<<','<<state().transport(1)<<'\n';throw std::runtime_error("Native pit death did not complete");}
      step(8,victim?1:0,victim?0:1);wide();fall(victim^1);
      bool retried=false;for(unsigned f=0;f<720;++f){step(1,victim?2:0,victim?0:2);wide();if(state().lifecycle().retries&&state().actors()[0].hp&&state().actors()[1].hp){retried=true;break;}}
      if(!retried||m.debug_read(0xc065,1)[0]+1!=lives)throw std::runtime_error("Team retry did not spend one P1 life");
     }
     std::cout<<"Original death and team retry retain all three expanded views: "<<victims<<" approaches, "<<inspected<<" inspected frames/views\n";return 0;
    }
    if(mode.starts_with("--lifecycle-boss-")||mode.starts_with("--lifecycle-miniboss-")) {
     bool mini=mode.starts_with("--lifecycle-miniboss-");
     auto active=[&](){if(!mini)return bool(m.debug_read(0xc9a1,1)[0]&&(m.debug_read(0xb821,1)[0]&127));for(unsigned page=0xb8;page<0xc0;++page){auto b=m.debug_read(uint16_t(page<<8),0x30);if(b[0]&&b[0x2d]==0x28&&(b[0x21]&127))return true;}return false;};
     if(mini){select(1);const uint8_t position[]{0,149,0,0,128,2},shield[]{255};m.debug_write(0xc104,position);m.debug_write(0xc130,shield);step(100);for(unsigned f=0;f<180&&!active();++f)step(1,0,1);}
     if(!active())throw std::runtime_error("Native boss/miniboss encounter must be active");
     unsigned lives=m.debug_read(0xc065,1)[0];fall(0);
     auto protect=[&](){select(1);const uint8_t shield[]{255};m.debug_write(0xc130,shield);};
     bool fallen=false;for(unsigned f=0;f<480;++f){if(f%60==0)protect();step(1);if(state().lifecycle().status[0]==1){fallen=true;break;}}
     if(!fallen)throw std::runtime_error("Boss fixture did not complete the native death");
     step(1,64,0);for(unsigned f=0;f<180;++f){if(f%60==0)protect();step(1);}
     auto life=state().lifecycle();auto a=state().actors();
     if(life.status[0]!=1||life.pending||a[0].hp||!a[1].hp||m.debug_read(0xc065,1)[0]!=lives||!active())throw std::runtime_error("A fallen player rejoined during the native encounter");
     picture("boss-deferred");replay();gbcllr::write_file_atomic("boss-deferred.state",m.snapshot());
     std::cout<<"The fallen player stays out with no life charge while the native encounter and survivor continue\n";return 0;
    }
    if(m.debug_read(0xc9a1,1)[0])throw std::runtime_error("Ordinary-stage lifecycle fixture must precede a boss encounter");
    for(unsigned seat=0;seat<2;++seat) {
     if(!m.restore(original,error))throw std::runtime_error(error);
     unsigned lives=m.debug_read(0xc065,1)[0];fall(seat);
     bool fallen=false;for(unsigned f=0;f<480;++f){step(1);if(f==40){picture("death-"+std::to_string(seat));replay();}if(state().lifecycle().status[seat]==1){fallen=true;break;}}
     auto s=state();auto a=s.actors();auto life=s.lifecycle();
     std::cout<<"Pit seat="<<seat<<" statuses="<<unsigned(life.status[0])<<','<<unsigned(life.status[1])<<" HP="<<a[0].hp<<','<<a[1].hp<<" action="<<a[0].action<<','<<a[1].action<<" stage="<<unsigned(m.debug_read(0xc0ac,1)[0])<<" lives="<<unsigned(m.debug_read(0xc065,1)[0])<<'\n';
     if(!fallen||a[seat].hp||!a[seat^1].hp||m.debug_read(0xc065,1)[0]!=lives||m.debug_read(0xc0ac,1)[0])throw std::runtime_error("Solo death stopped the survivor or charged a retry");
     auto pairs=s.passes();step(16,seat?1:0,seat?0:1);if(state().passes()<=pairs)throw std::runtime_error("The survivor did not advance the world");
     gbcllr::write_file_atomic("survivor-"+std::to_string(seat)+".state",m.snapshot());picture("survivor-"+std::to_string(seat));replay();
     step(1,seat?0:64,seat?64:0);step(120);
     s=state();a=s.actors();life=s.lifecycle();
     std::cout<<"Fallen Select seat="<<seat<<" status="<<unsigned(life.status[seat])<<" HP="<<a[seat].hp<<" lives="<<unsigned(m.debug_read(0xc065,1)[0])<<'\n';
     if(life.status[seat]!=1||life.pending||a[seat].hp||m.debug_read(0xc065,1)[0]!=lives)throw std::runtime_error("Select revived a fallen player or spent P1's life");
     picture("waiting-"+std::to_string(seat));replay();
     if(!m.restore(original,error))throw std::runtime_error(error);
     auto before=state().actors();step(96,seat?0:64,seat?64:0);replay();step(30);
     if(state().lifecycle().status[seat]!=2)throw std::runtime_error("Select hold did not withdraw the selected player");
     picture("withdraw-"+std::to_string(seat));step(1,seat?0:64,seat?64:0);step(120);
     a=state().actors();if(state().lifecycle().status[seat]||a[seat].hp!=before[seat].hp||m.debug_read(0xc065,1)[0]!=lives)throw std::runtime_error("Voluntary rejoin lost health or spent a life");
     step(24,seat?0:2,seat?2:0);picture("rejoin-"+std::to_string(seat));
    }
    if(!m.restore(original,error))throw std::runtime_error(error);
    unsigned lives=m.debug_read(0xc065,1)[0];fall(0);fall(1);step(2);
    gbcllr::write_file_atomic("team-wipe-ready.state",m.snapshot());replay();step(598);
    auto s=state();auto a=s.actors();auto life=s.lifecycle();
    std::cout<<"Team wipe retries="<<life.retries<<" HP="<<a[0].hp<<','<<a[1].hp<<" lives="<<unsigned(m.debug_read(0xc065,1)[0])<<'\n';
    if(life.retries!=1||a[0].hp!=a[0].max_hp||a[1].hp!=a[1].max_hp||m.debug_read(0xc065,1)[0]!=lives-1)throw std::runtime_error("Team wipe did not perform one native full-health checkpoint retry");
    step(24,2,1);picture("team-retry");replay();gbcllr::write_file_atomic("lifecycle-current.state",m.snapshot());
    for(unsigned mode=0;mode<3;++mode){m.mod_coop_view(mode,1);picture("lives-view-"+std::to_string(mode));}
    std::cout<<"Native survivors, no mid-stage revival, voluntary rejoin and one-charge P1 team retry passed\n";return 0;
   }
   if(mode.starts_with("--shots-")) {
    m.mod_viewport(384,144);m.mod_coop_shared_world(false);
    const bool post=mode.starts_with("--shots-post-");
    // Walk the other actor away in the opening fixture, or advance X to
    // the firing position after the actual intro. Then face the open left.
    for(unsigned f=0;f<64;++f){bool p1_moves=post?!o.mods[0].player1_character:bool(o.mods[0].player1_character);m.run_frame(p1_moves?1:0,p1_moves?0:1);m.take_audio();}
    for(unsigned f=0;f<2;++f){m.run_frame(o.mods[0].player1_character?0:2,o.mods[0].player1_character?2:0);m.take_audio();}
    for(unsigned f=0;f<30;++f){m.run_frame(0,0);m.take_audio();}
    gbcllr::write_file_atomic("shots-ready.state",m.snapshot());
    for(unsigned f=0;f<480;++f) {
     uint8_t fire=f<96?(f%16<4?32:0):((f<224||(f>=256&&f<448))?32:0);
     m.run_frame(o.mods[0].player1_character?0:fire,o.mods[0].player1_character?fire:0);m.take_audio();
     if(f==8) {
      if(!m.debug_read(0xb000,1)[0])throw std::runtime_error("Native Buster shot precondition was not reached");
      gbcllr::write_file_atomic("shots-visible.state",m.snapshot());
      auto saved=m.snapshot();std::vector<int16_t> expected_pcm,actual_pcm;
      for(unsigned n=0;n<16;++n){m.run_frame(0,0);auto pcm=m.take_audio();expected_pcm.insert(expected_pcm.end(),pcm.begin(),pcm.end());}
      auto expected=m.snapshot();if(!m.restore(saved,error))throw std::runtime_error(error);
      for(unsigned n=0;n<16;++n){m.run_frame(0,0);auto pcm=m.take_audio();actual_pcm.insert(actual_pcm.end(),pcm.begin(),pcm.end());}
      if(m.snapshot()!=expected||actual_pcm!=expected_pcm)throw std::runtime_error("Projectile state/PCM replay diverged");
      if(!m.restore(saved,error))throw std::runtime_error(error);
     }
     if((f<80&&f%8==0)||f==224||f==232||f==448||f==456) {
      auto view=m.mod_view();std::ofstream picture("shots-"+std::to_string(f)+".ppm",std::ios::binary);picture<<"P6\n"<<view.width<<" 144\n255\n";
      for(auto p:view.pixels){const char rgb[]={char(p>>16),char(p>>8),char(p)};picture.write(rgb,3);}
     }
    }
    gbcllr::write_file_atomic("shots-current.state",m.snapshot());
    m.write_frame("shots-native.ppm");std::cout<<"Native Buster shots and exact state/PCM replay passed\n";return 0;
   }
   if(mode.starts_with("--scene-")||mode=="--tether-x") {
    unsigned leader=mode=="--scene-p1-x"?0:1;
    m.mod_viewport(384,144);m.mod_coop_shared_world(false);
    auto deadline=m.stats().cycles+140448*3;bool positioned=false;
    while(m.stats().cycles<deadline) {
     auto stat=m.stats();
     if(stat.rom_bank==5&&stat.pc==0x4004&&m.debug_read(0xc0c9,1)[0]==(o.mods[0].player1_character^leader)) {
      unsigned approach=mode=="--tether-x"?400:640;
      const uint8_t position[]{0,149,0,0,uint8_t(approach),uint8_t(approach>>8)},protection[]{255};m.debug_write(0xc104,position);m.debug_write(0xc130,protection);positioned=true;break;
     }
     m.run_until(stat.cycles+1);m.take_audio();
    }
    if(!positioned)throw std::runtime_error("Did not reach partner travel precondition");
    // Let the original camera/stream reach the supplied approach, before
    // walking through the source-defined672..720 dialogue trigger.
    for(unsigned f=0;f<100;++f){m.run_frame(0,0);m.take_audio();}
    gbcllr::write_file_atomic("scene-ready.state",m.snapshot());
    if(mode=="--tether-x") {
     auto start=m.snapshot();gbcllr::Xtreme2Coop before(0);before.restore(m.coop_state());
     for(unsigned f=0;f<64;++f){m.run_frame(f<32?2:0,f<32?0:1);m.take_audio();}
     gbcllr::Xtreme2Coop separated(0);separated.restore(m.coop_state());
     if(separated.departures()!=before.departures()||separated.transport(0)||separated.transport(1)||
        std::abs(separated.actors()[0].x-separated.actors()[1].x)<352)throw std::runtime_error("Independent forward/backward separation triggered distance transport");
     if(!m.restore(start,error))throw std::runtime_error(error);
     m.mod_coop_shared_world(true);
     for(unsigned f=0;f<60;++f){m.run_frame(2,0);m.take_audio();}
     // Original-clock gameplay can need more than80 display frames to finish
     // its native arrival. Require the actual completion within a bound.
     for(unsigned f=0;f<400;++f){m.run_frame(0,0);m.take_audio();gbcllr::Xtreme2Coop current(0);current.restore(m.coop_state());if(!current.transport(0))break;}
     gbcllr::Xtreme2Coop state(0);state.restore(m.coop_state());auto a=state.actors();
     std::cout<<"Catch-up result: departures="<<state.departures()<<" arrivals="<<state.arrivals()<<" phase="<<state.transport(0)<<','<<state.transport(1)<<" HP="<<a[0].hp<<','<<a[1].hp<<" x="<<a[0].x<<','<<a[1].x<<'\n';
     gbcllr::write_file_atomic("tether-result.state",m.snapshot());
     if(!state.departures()||!state.arrivals()||state.transport(0)||std::abs(a[0].x-a[1].x)>32||a[0].hp!=16)throw std::runtime_error("Shared-view catch-up/health retention failed");
     gbcllr::write_file_atomic("tether-returned.state",m.snapshot());
     std::cout<<"Shared-view catch-up returned partner with HP="<<a[0].hp<<", distance="<<std::abs(a[0].x-a[1].x)<<'\n';return 0;
    }
    std::ofstream log("scene.csv");log<<"frame,anchor,p1x,p1y,p2x,p2y,checkpoint,scene,teleport1,teleport2,departures,arrivals\n";
    bool ready_saved=false,saw_story=false;
    for(unsigned f=0;f<1800;++f) {
     bool story=!m.debug_read(0xc9b0,1)[0];saw_story|=story;
     unsigned advance=story&&f%30<24?16:0;
     // Approach and fire from the ground; jumping on a fixed schedule made
     // Buster shots phase-lock with the moving guard's invulnerability.
     uint8_t drive=uint8_t((!saw_story&&f<600?1:0)|advance|((!saw_story&&f>=100&&f%16<4)?32:0));
     m.run_frame(leader?uint8_t(advance):drive,leader?drive:uint8_t(advance));m.take_audio();
     gbcllr::Xtreme2Coop state(o.mods[0].player1_character);state.restore(m.coop_state());auto a=state.actors();
     if(f==60||f==400) {
      auto saved=m.snapshot();std::vector<int16_t> expected_pcm,actual_pcm;
      for(unsigned n=0;n<16;++n){m.run_frame(0,0);auto pcm=m.take_audio();expected_pcm.insert(expected_pcm.end(),pcm.begin(),pcm.end());}
      auto expected=m.snapshot();if(!m.restore(saved,error))throw std::runtime_error(error);
      for(unsigned n=0;n<16;++n){m.run_frame(0,0);auto pcm=m.take_audio();actual_pcm.insert(actual_pcm.end(),pcm.begin(),pcm.end());}
      if(m.snapshot()!=expected||actual_pcm!=expected_pcm)throw std::runtime_error("Transport/dialogue state/PCM replay diverged");
      if(!m.restore(saved,error))throw std::runtime_error(error);
     }
     if(!ready_saved&&f>100)for(unsigned page=0xb8;page<0xc0;++page){auto b=m.debug_read(uint16_t(page<<8),0x30);if(b[0]&&b[0x2d]==0x28&&b[0xa]==1&&(b[0x21]&127)==2){gbcllr::write_file_atomic("intro-trigger-ready.state",m.snapshot());ready_saved=true;break;}}
     log<<f<<','<<state.anchor()<<','<<a[0].x<<','<<a[0].y<<','<<a[1].x<<','<<a[1].y<<','<<unsigned(m.debug_read(0xc066,1)[0])<<','<<unsigned(m.debug_read(0xc9b0,1)[0])<<','<<state.transport(0)<<','<<state.transport(1)<<','<<state.departures()<<','<<state.arrivals()<<'\n';
     if((f<480&&f%20==0)||(f>=480&&f%120==0)||f==479||f==1199||f==1799) {
      for(unsigned view_mode=0;view_mode<3;++view_mode){m.mod_coop_view(view_mode,0);auto view=m.mod_view();std::ofstream picture("scene-"+std::to_string(f)+"-"+std::to_string(view_mode)+".ppm",std::ios::binary);picture<<"P6\n"<<view.width<<" 144\n255\n";for(auto p:view.pixels){const char rgb[]={char(p>>16),char(p>>8),char(p)};picture.write(rgb,3);}}
     }
    }
    gbcllr::write_file_atomic("scene-end.state",m.snapshot());
    m.write_frame("scene-native.ppm");
    auto flow=m.debug_read(0xc9b0,12);std::cout<<"Final scene flow:";for(auto b:flow)std::cout<<' '<<unsigned(b);std::cout<<'\n';
    gbcllr::Xtreme2Coop state(o.mods[0].player1_character);state.restore(m.coop_state());auto a=state.actors();
    std::cout<<"Original intro result; anchor="<<state.anchor()<<" departures="<<state.departures()<<" arrivals="<<state.arrivals()<<" transport="<<state.transport(0)<<','<<state.transport(1)<<" HP="<<a[0].hp<<','<<a[1].hp<<"; transport/dialogue replay exact\n";
    unsigned expected_checkpoint=(o.mods[0].player1_character^leader)?1:2; // Original X and Zero scripts resume in different rooms.
    if(m.debug_read(0xc066,1)[0]!=expected_checkpoint||!flow[0]||state.departures()<2||!state.arrivals()||state.transport(leader^1))throw std::runtime_error("Original intro/checkpoint/partner return did not complete");
    return 0;
   }
   if(mode.starts_with("--boss-")||mode.starts_with("--reward-")) {
    bool reward=mode.starts_with("--reward-");unsigned attacker=mode.starts_with("--reward-p2-")?1u:0u;
    bool zero=bool(o.mods[0].player1_character^attacker);
    auto before_reward=m.debug_read(0xc000,256);
    if(reward&&before_reward[0xb0]!=1)throw std::runtime_error("Native intro progression must precede the reward fixture");
    auto place=[&](unsigned seat,std::span<const uint8_t> position) {
     if(reward&&!reference){
      auto deadline=m.stats().cycles+140448*3;bool found=false;
      while(m.stats().cycles<deadline){auto s=m.stats();if(s.rom_bank==5&&s.pc==0x4004&&m.debug_read(0xc0c9,1)[0]==(o.mods[0].player1_character^seat)){found=true;break;}m.run_until(s.cycles+1);m.take_audio();}
      if(!found)throw std::runtime_error("Reward approach did not reach selected actor");
     }
     if(!position.empty())m.debug_write(0xc104,position);
     const uint8_t protection[]{255};m.debug_write(0xc130,protection);
    };
    auto start_hp=m.debug_read(0xb821,1)[0]&127;
    bool killed=false;unsigned prior_hp=start_hp;
    for(unsigned f=0;f<(reward?3600u:168u);++f) {
     // Reuse the stock Neon contact/height/protection fixture. Do not write
     // boss HP, projectile state, hit results or ownership/reward flags.
     if(reward&&!reference&&f%56==0){auto boss=m.debug_read(0xb804,6);unsigned x=boss[4]|unsigned(boss[5])<<8;const uint8_t idle[]{0,boss[1],boss[2],0,uint8_t(x),uint8_t(x>>8)};place(attacker^1,idle);}
     if(f%56==0){const uint8_t position[]{0,132,1,0,192,14};place(attacker,position);}
     if(zero&&f%56==8) {
      // Zero's original basic attack is melee. Supply contact beside the
      // native boss, rather than applying the ranged Buster fixture to it.
      auto boss=m.debug_read(0xb804,6);unsigned x=(boss[4]|unsigned(boss[5])<<8)-16;
      const uint8_t position[]{0,boss[1],boss[2],0,uint8_t(x),uint8_t(x>>8)};place(attacker,position);
     }
     uint8_t drive=f%56<8?1:f%56<16?33:0;m.run_frame(attacker?0:drive,attacker?drive:0);m.take_audio();
     auto hp=m.debug_read(0xb821,1)[0]&127;
     if(hp!=prior_hp){std::cout<<"Original boss HP "<<unsigned(prior_hp)<<" -> "<<unsigned(hp)<<" frame="<<f<<'\n';prior_hp=hp;if(!reward)break;}
     if(!hp){killed=true;break;}
    }
    if(reward){
     if(!killed)throw std::runtime_error("Native boss defeat did not complete");
     if(!reference){gbcllr::Xtreme2Coop s(o.mods[0].player1_character);s.restore(m.coop_state());auto p=s.actors();std::cout<<"Killing-blow actor health "<<p[0].hp<<','<<p[1].hp<<" action "<<p[0].action<<','<<p[1].action<<'\n';}
     gbcllr::write_file_atomic("boss-killed.state",m.snapshot());
     m.trace_enabled(true);
     bool granted=false;
     for(unsigned f=0;f<2400;++f){m.run_frame(f>=600&&f%30<24?16:0,0);m.take_audio();if((m.debug_read(0xc06f,1)[0]&0x40)||(m.debug_read(0xc0a0,1)[0]&1)){granted=true;break;}}
     auto trace=m.take_trace();std::ofstream code("reward.trace");for(auto at:trace)code<<std::hex<<at<<'\n';
     auto flags=m.debug_read(0xc0f0,8);std::cout<<"Native reward records:";for(auto b:flags)std::cout<<' '<<unsigned(b);std::cout<<'\n';
     auto after=m.debug_read(0xc000,256);std::cout<<"Changed campaign fields:";for(unsigned n=0;n<256;++n)if(before_reward[n]!=after[n])std::cout<<' '<<std::hex<<0xc000+n<<':'<<unsigned(before_reward[n])<<'>'<<unsigned(after[n]);std::cout<<std::dec<<'\n';
     gbcllr::write_file_atomic("reward-current.state",m.snapshot());gbcllr::write_file_atomic("reward-battery.sav",m.battery());
     if(!granted)throw std::runtime_error("Native reward grant did not complete");
     if(reference)return 0;
     gbcllr::Xtreme2Coop s(o.mods[0].player1_character);s.restore(m.coop_state());auto r=s.reward();
     std::cout<<"Native reward owner="<<r.owner<<" stage="<<r.stage<<" grants="<<r.grants<<'\n';
     if(r.owner!=attacker+1||r.grants!=1||bool(m.debug_read(0xc06f,1)[0]&0x40)==zero||bool(m.debug_read(0xc0a0,1)[0]&1)!=zero)throw std::runtime_error("Boss reward did not belong only to its killing character");
     auto saved=m.snapshot();std::vector<int16_t> expected_pcm,actual_pcm;
     for(unsigned n=0;n<16;++n){m.run_frame(0,0);auto pcm=m.take_audio();expected_pcm.insert(expected_pcm.end(),pcm.begin(),pcm.end());}
     auto expected=m.snapshot();if(!m.restore(saved,error))throw std::runtime_error(error);
     for(unsigned n=0;n<16;++n){m.run_frame(0,0);auto pcm=m.take_audio();actual_pcm.insert(actual_pcm.end(),pcm.begin(),pcm.end());}
     if(m.snapshot()!=expected||actual_pcm!=expected_pcm)throw std::runtime_error("Reward state/PCM replay diverged");
     if(!m.restore(saved,error))throw std::runtime_error(error);
     std::cout<<"Native reward snapshot/PCM replay exact\n";
    }
    gbcllr::Xtreme2Coop state(o.mods[0].player1_character);state.restore(m.coop_state());auto combat=state.combat();
    std::cout<<"Native boss hit events="<<combat.hits[0]<<','<<combat.hits[1]<<" defeated_owner="<<combat.defeated_owner<<'\n';
    m.mod_viewport(384,144);auto view=m.mod_view();std::ofstream picture("boss-hit.ppm",std::ios::binary);picture<<"P6\n"<<view.width<<" 144\n255\n";
    for(auto p:view.pixels){const char rgb[]={char(p>>16),char(p>>8),char(p)};picture.write(rgb,3);}
    gbcllr::write_file_atomic("boss-hit.state",m.snapshot());
    if((!reward&&(m.debug_read(0xb821,1)[0]&127)>=start_hp)||!combat.hits[attacker]||combat.hits[attacker^1]||(reward&&combat.defeated_owner!=attacker+1))throw std::runtime_error("Native boss hit attribution failed");return 0;
   }
   if(mode.starts_with("--forward-")) {
    m.mod_viewport(384,144);
    auto flow=m.debug_read(0xc9b0,8);std::cout<<"Scene flow:";for(auto b:flow)std::cout<<' '<<unsigned(b);std::cout<<'\n';
    auto cp=m.debug_read(0xcb00,0x60);std::cout<<"Checkpoint actor:";for(auto b:cp)std::cout<<' '<<unsigned(b);std::cout<<'\n';
    for(unsigned f=0;f<600;++f) {
     uint8_t first=f<360?1:2,second=f<420?1:2;
     if(f%90<45)first|=32;if(f%75<30)second|=32;
     if(f%60<24)first|=16;if(f%80<24)second|=16;
     try {m.run_frame(first,second);m.take_audio();m.mod_coop_view(f%3);m.mod_view();}
     catch(...) {std::cerr<<"Forward frame="<<f<<'\n';gbcllr::write_file_atomic("before-failure.state",m.snapshot());throw;}
    }
    gbcllr::write_file_atomic("forward-end.state",m.snapshot());
    gbcllr::Xtreme2Coop state(o.mods[0].player1_character);state.restore(m.coop_state());auto actors=state.actors();
    std::cout<<"Forward600 frames passed, pairs="<<state.passes()<<" X/Zero HP="<<actors[0].hp<<','<<actors[1].hp<<" positions="<<actors[0].x<<','<<actors[1].x<<" anchor="<<state.anchor()<<" teleports="<<state.departures()<<','<<state.arrivals()<<'\n';return 0;
   }
   if(mode=="--heart-x") {
    for(unsigned f=0;f<36;++f){m.run_frame(2,0);m.take_audio();}
    unsigned contacts=0;auto stop=m.stats().cycles+140448*180;
    while(m.stats().cycles<stop) {
     auto stat=m.stats();
     if(stat.rom_bank==0x0d&&stat.pc==0x704a) {
      // Existing native fixture supplies travel/height/contact prerequisites.
      // The original actor stream, collision, maximum-HP and ownership writes
      // remain authoritative. P1 has moved away so the nearer actor is P2.
      const uint8_t contact[]{0,108,0,0,216,10};m.debug_write(0xc104,contact);++contacts;
     }
     m.run_until(stat.cycles+1);m.take_audio();
     if(m.debug_read(0xc0de,1)[0]==18) {
      // Finish the actual world-actor scope so its retained body is captured.
      m.run_frame(0,0);m.take_audio();break;
     }
    }
    gbcllr::Xtreme2Coop state(0);state.restore(m.coop_state());auto actors=state.actors();
    std::cout<<"Heart contacts="<<contacts<<" X="<<actors[0].hp<<'/'<<actors[0].max_hp<<" Zero="<<actors[1].hp<<'/'<<actors[1].max_hp<<" records="<<unsigned(m.debug_read(0xc0d9,1)[0])<<','<<unsigned(m.debug_read(0xc0de,1)[0])<<'\n';
    if(!contacts||actors[0].max_hp!=16||actors[1].max_hp!=18||m.debug_read(0xc0d9,1)[0]!=16||m.debug_read(0xc0de,1)[0]!=18)throw std::runtime_error("Native heart collector ownership failed");
    gbcllr::write_file_atomic("heart-owned.state",m.snapshot());return 0;
   }
   if(mode.starts_with("--visual")||mode=="--combat-x") {
    m.mod_viewport(384,144);
    auto movement_start=m.snapshot();
    for(unsigned f=0;f<40;++f){m.run_frame(1,0);m.take_audio();}
    gbcllr::write_file_atomic("movement-ready.state",m.snapshot());
    auto saved_view=m.snapshot();
    // Compare against the original single-player HUD, whose tile workspace
    // is not shared by the two co-op controller contexts.
    auto reference_options=o;reference_options.mods[0].plugin="gbcllr.xtreme2.adaptive-view";reference_options.mods[0].player1_character=0;
    gbcllr::Machine reference(rom,reference_options);
    if(!reference.restore(gbcllr::read_file(argv[3]),error))throw std::runtime_error(error);
    for(unsigned f=0;f<120;++f){reference.run_frame(f<80?0:1);reference.take_audio();}
    reference.write_frame("native-hud.ppm");
    const bool native_hud_reference=mode=="--visual-x";
    for(unsigned mode=0;mode<3;++mode) {
     m.mod_coop_view(mode,1);auto view=m.mod_view();std::ofstream picture("view-"+std::to_string(mode)+".ppm",std::ios::binary);
     picture<<"P6\n"<<view.width<<" "<<view.height<<"\n255\n";
     for(auto p:view.pixels){const char rgb[]={char(p>>16),char(p>>8),char(p)};picture.write(rgb,3);}
     if(m.snapshot()!=saved_view)throw std::runtime_error("Camera presentation advanced or changed the shared world");
     if((mode==0||mode==2)&&native_hud_reference) {
      unsigned different=0;auto native=reference.pixels();
      for(unsigned y=128;y<144;++y)for(unsigned x=0;x<96;++x)different+=view.pixels[y*view.width+x]!=native[y*160+x];
      std::cout<<"Original player HUD pixel differences="<<different<<" view="<<mode<<'\n';
      if(different)throw std::runtime_error("Co-op HUD differs from original player HUD");
     }
    }
    m.mod_coop_view(0);
    if(!m.restore(movement_start,error))throw std::runtime_error(error);
    std::ofstream log("motion.csv");log<<"frame,p1x,p1y,p1hp,p2x,p2y,p2hp\n";
    for(unsigned f=0;f<144;++f) {
     uint8_t p1=f<48?1:f<96?2:0,p2=f<48?0:f<96?1:2;
     if(f>=20&&f<40)p1|=16;if(f>=68&&f<88)p2|=16;
     if(mode=="--combat-x"){m.mod_coop_shared_world(false);m.mod_coop_view(2);p1=f<32?1:f<64?2:0;p2=(f%24<12)?32:0;}
     m.run_frame(p1,p2);m.take_audio();
     gbcllr::Xtreme2Coop state(o.mods[0].player1_character);state.restore(m.coop_state());auto actors=state.actors();
     log<<f<<','<<actors[0].x<<','<<actors[0].y<<','<<actors[0].hp<<','<<actors[1].x<<','<<actors[1].y<<','<<actors[1].hp<<'\n';
     if(f%12==0||f==143||mode=="--combat-x") {
      auto view=m.mod_view();std::ofstream picture("motion-"+std::to_string(f)+".ppm",std::ios::binary);
      picture<<"P6\n"<<view.width<<" "<<view.height<<"\n255\n";
      for(auto p:view.pixels){const char rgb[]={char(p>>16),char(p>>8),char(p)};picture.write(rgb,3);}
     }
    }
    gbcllr::write_file_atomic("motion.state",m.snapshot());
    std::cout<<"Bounded movement/jump frames captured for visual review; not automatic visual acceptance\n";return 0;
   }
   if(!m.restore(seed,error))throw std::runtime_error(error);
   for(unsigned f=0;f<160;++f){m.run_frame(1,2);m.take_audio();}
   gbcllr::Xtreme2Coop state(o.mods[0].player1_character);state.restore(m.coop_state());
   auto a=state.actors();std::cout<<"passes="<<state.passes()<<" P1="<<a[0].x<<","<<a[0].y<<" action="<<a[0].action<<" status="<<a[0].status<<" P2="<<a[1].x<<","<<a[1].y<<" action="<<a[1].action<<" status="<<a[1].status<<"\n";
   if(state.passes()<20||a[0].x<=a[1].x)throw std::runtime_error("Independent native movement failed");
   auto saved=m.snapshot();
   std::vector<int16_t> expected_pcm,actual_pcm;
   for(unsigned f=0;f<16;++f){m.run_frame(2,1);auto pcm=m.take_audio();expected_pcm.insert(expected_pcm.end(),pcm.begin(),pcm.end());}
   auto expected=m.snapshot();if(!m.restore(saved,error))throw std::runtime_error(error);
   for(unsigned f=0;f<16;++f){m.run_frame(2,1);auto pcm=m.take_audio();actual_pcm.insert(actual_pcm.end(),pcm.begin(),pcm.end());}
   if(m.snapshot()!=expected||actual_pcm!=expected_pcm)throw std::runtime_error("Co-op state/PCM replay diverged");
   gbcllr::write_file_atomic("controller-current.state",m.snapshot());
   m.mod_viewport(512,144);auto view=m.mod_view();
   std::ofstream picture("controller-view.ppm",std::ios::binary);
   picture<<"P6\n"<<view.width<<" "<<view.height<<"\n255\n";
   for(auto p:view.pixels){const char rgb[]={char(p>>16),char(p>>8),char(p)};picture.write(rgb,3);}
   std::cout<<"Two native controllers and deterministic replay passed; no save output\n";return 0;
  }
  std::map<std::tuple<unsigned,unsigned,unsigned,unsigned>,unsigned> hits;
  std::map<std::tuple<unsigned,unsigned,unsigned,unsigned>,unsigned> calls;
  const auto stop=m.stats().cycles+140448*2;
  while(m.stats().cycles<stop) {
   auto s=m.stats();auto r=m.debug_registers();unsigned at=s.pc<0x4000?s.pc:s.rom_bank*0x4000+s.pc-0x4000;
   if(s.pc<0x8000&&at+2<rom.size()) {
    if(s.pc==0x675||s.pc==0x10||s.pc==0x28cb) {
     auto stack=m.debug_read(r.sp,2);unsigned caller=stack[0]|unsigned(stack[1])<<8;
     calls[{s.pc,caller,r.de>>8,r.hl}]++;
    }
    unsigned operand=rom[at+1]|unsigned(rom[at+2])<<8;
    if(((rom[at]==0xfa||rom[at]==0xea||rom[at]==0x21)&&operand>=0xc100&&operand<0xc200)||
       (r.hl>=0xc100&&r.hl<0xc200&&rom[at]>=0x70&&rom[at]<=0x7e))
     hits[{s.pc<0x4000?0:s.rom_bank,s.pc,r.sp,r.de>>8}]++;
   }
   m.run_until(s.cycles+1);m.take_audio();
  }
  for(auto& [k,n]:hits) {auto [bank,pc,sp,d]=k;std::cout<<std::hex<<bank<<":"<<pc<<" sp="<<sp<<" d="<<d<<std::dec<<" n="<<n<<"\n";}
  for(auto& [k,n]:calls){auto [pc,caller,d,hl]=k;std::cout<<"call "<<std::hex<<pc<<" caller="<<caller<<" d="<<d<<" hl="<<hl<<std::dec<<" n="<<n<<'\n';}
  auto ram=m.debug_read(0xc000,0x200);for(unsigned a=0;a<ram.size();a+=16){std::cout<<std::hex<<0xc000+a<<":";for(unsigned i=0;i<16;++i)std::cout<<" "<<unsigned(ram[a+i]);std::cout<<"\n";}
 }catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}
}
