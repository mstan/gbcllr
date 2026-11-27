#include <gbcllr/machine.hpp>
#include "../src/native_mods.hpp"
#include "../src/xtreme2_coop.hpp"
#include <algorithm>
#include <cstring>
#include <iostream>
#include <map>
#include <queue>
#include <stdexcept>
using namespace gbcllr;
static void check(bool pass,const char* text){if(!pass)throw std::runtime_error(text);}
int main(int argc,char** argv){try{
 check(argc==4||argc==5,"ROM BOOT plain-gameplay-STATE [--coop-x|--coop-zero] required");
 auto rom=read_file(argv[1]),seed=read_file(argv[3]);const auto sha=sha256(rom);
 Options options;options.model=Model::cgb;options.boot_rom=read_file(argv[2]);
 options.mods={read_mod_package(std::filesystem::path(__FILE__).parent_path().parent_path()/"mods/xtreme2-adaptive.gbmod",sha).feature};
 const bool coop=argc==5;unsigned first=0;
 bool prepare=false,prepare_options=false;
 if(coop){std::string mode=argv[4];prepare=mode.starts_with("--net-");prepare_options=mode=="--net-options-x";check(mode=="--coop-x"||mode=="--coop-zero"||mode=="--net-x"||mode=="--net-zero"||prepare_options,"Invalid roster");first=mode.ends_with("-zero");}
 const auto base=NativeMods(sha,options.mods).snapshot().size();
 check(seed.size()>base+73,"Invalid fixture size");size_t at=seed.size()-64-5-base-4;uint32_t size=0,version=0;
 std::memcpy(&size,seed.data()+at,4);std::memcpy(&version,seed.data()+at+4,4);
 check(size==base&&version==2,"A plain native-view gameplay fixture is required");
 check(sha256(std::span<const uint8_t>(seed).first(seed.size()-64))==std::string(reinterpret_cast<const char*>(seed.data()+seed.size()-64),64),"Invalid fixture checksum");
 options.mods[0].weapon_cycle=true;if(coop){options.mods[0].plugin="gbcllr.xtreme2.coop";options.mods[0].player1_character=first;}
 auto initial=NativeMods(sha,options.mods).snapshot();
 seed.insert(seed.begin()+at+4+base,initial.begin()+base,initial.end());size=uint32_t(initial.size());std::memcpy(seed.data()+at,&size,4);std::copy_n(initial.data(),4,seed.data()+at+4);
 auto checksum=sha256(std::span<const uint8_t>(seed).first(seed.size()-64));std::copy(checksum.begin(),checksum.end(),seed.end()-64);
 Machine machine(rom,options);std::string error;check(machine.restore(seed,error),error.c_str());
 const unsigned x=coop?first:0;
 auto step=[&](unsigned frames,uint8_t left=0,uint8_t right=0){for(unsigned i=0;i<frames;++i){machine.run_frame(LogicalInput{0,left},LogicalInput{0,right});machine.take_audio();}};
 auto weapons=[&]{if(!coop)return std::array<uint8_t,2>{machine.debug_read(0xc131,1)[0],0};Xtreme2Coop state(first);state.restore(machine.coop_state());return state.weapons();};
 step(120);check(machine.debug_read(0xc9b0,1)[0]&&!machine.debug_read(0xc00c,1)[0]&&!machine.debug_read(0xc0c2,1)[0],"Fixture is not ordinary playable gameplay");
 // Supply availability only. Native selection and reload remain the sole
 // result under test; this is not an acquisition/progression claim.
 std::array<uint8_t,9> records{};for(unsigned i=0;i<9;++i)records[i]=uint8_t(0x40+(i*3)%29);
 machine.debug_write(0xc06e,records);const auto energy=machine.debug_read(0xc06d,10);
 if(prepare){
  if(prepare_options){
   auto frame=[&](unsigned count,uint8_t input=0){for(unsigned i=0;i<count;++i){machine.run_frame(0,input);machine.take_audio();}};
   auto menu=[&](){auto state=std::make_unique<Xtreme2Coop>(first);state->restore(machine.coop_state());return state->menus();};
   frame(2,128);frame(118);check(menu().ready==3,"Original menus did not initialize");
   std::map<unsigned,std::vector<uint8_t>> paths;std::queue<unsigned> pending;paths[menu().cursor[1]]={};pending.push(menu().cursor[1]);
   while(!pending.empty()&&!paths.contains(15)){auto at=pending.front();pending.pop();for(unsigned d=0;d<4;++d){auto next=rom[0x1d*0x4000+0x46c1-0x4000+4*at+d];if(next!=255&&!paths.contains(next)){paths[next]=paths[at];paths[next].push_back(uint8_t(1u<<d));pending.push(next);}}}
   check(paths.contains(15),"Original Options entry is unreachable");for(auto button:paths[15]){frame(2,button);frame(12);}check(menu().cursor[1]==15,"Original menu cursor did not reach Options");
   frame(2,16);frame(90);check(menu().page[1]==4,"Original Options page did not open");
  }
  write_file_atomic("network-entry.state",machine.snapshot());std::cout<<"Prepared current-build native co-op/shoulder start; availability supplied, no network outcome set\n";return 0;
 }
 auto press=[&](uint8_t bits){step(8,x?0:bits,x?bits:0);step(4);};
 unsigned current=weapons()[x];check(current<10,"Invalid starting weapon");
 for(unsigned i=0;i<10;++i){current=(current+1)%10;press(virtual_r);auto selected=weapons();check(selected[x]==current,"Native next-weapon selection differs");if(coop)check(!selected[x^1],"X selection changed Zero's technique state");check(machine.debug_read(0xc06d,10)==energy,"Shoulder selection spent/refilled weapon energy");
  if(!coop){auto table=rom.data()+0x28*0x4000+0x7aa5-0x4000+current*2;auto native=machine.debug_read(0xc1c9,2);check(std::equal(native.begin(),native.end(),table),"Native weapon animation table was not reloaded");}}
 step(8,x?0:virtual_r,x?virtual_r:0);current=(current+1)%10;auto held=weapons();check(held[x]==current,"Fresh shoulder did not select once");step(24,x?0:virtual_r,x?virtual_r:0);check(weapons()==held,"A held shoulder repeated");step(4);
 press(3);check(weapons()==held,"Opposed shoulders changed equipment");
 press(virtual_l);current=(current+9)%10;check(weapons()[x]==current,"Native previous-weapon selection differs");
 // Lock all specials except2, leaving its existing energy untouched.
 auto sparse=energy;for(unsigned i=1;i<10;++i)sparse[i]&=uint8_t(~0x40);sparse[2]|=0x40;machine.debug_write(0xc06d,sparse);
 for(unsigned i=0;i<3;++i){press(virtual_r);current=current==0?2:0;check(weapons()[x]==current,"Native ownership skip/wrap differs");}
 if(coop){auto before=weapons();step(8,x?virtual_r:0,x?0:virtual_r);step(4);check(weapons()==before,"Zero's shoulder activated a technique or changed X equipment");}
 if(coop){
  auto before=weapons();
  auto button=[&](unsigned frames,uint8_t native,uint8_t extra){for(unsigned i=0;i<frames;++i){machine.run_frame(LogicalInput{x?uint8_t(0):native,x?uint8_t(0):extra},LogicalInput{x?native:uint8_t(0),x?extra:uint8_t(0)});machine.take_audio();}};
  button(2,128,0);button(118,0,0);Xtreme2Coop menu(first);menu.restore(machine.coop_state());check(menu.menus_active(),"Original personal menus did not open");
  button(8,0,virtual_r);button(2,128,virtual_r);button(118,0,virtual_r);menu.restore(machine.coop_state());
  check(!menu.menus_active()&&weapons()==before,"A shoulder held in the menu was queued after resume");step(4);
 }
 step(1,x?0:virtual_l,x?virtual_l:0);auto point=machine.snapshot();auto replay=[&]{step(1,x?0:virtual_l,x?virtual_l:0);for(unsigned i=0;i<16;++i)machine.run_frame(LogicalInput{},LogicalInput{});auto pcm=machine.take_audio();auto view=machine.mod_view();return std::pair{pcm,std::vector<uint32_t>(view.pixels.begin(),view.pixels.end())};};
 auto result=replay();auto expected=machine.snapshot();check(machine.restore(point,error),error.c_str());check(replay()==result&&machine.snapshot()==expected,"Native shoulder state/PCM/view replay differs");
 write_file_atomic("shoulder-cycle.state",machine.snapshot());machine.write_frame("shoulder-cycle.ppm");
 check(machine.rom_identity()==sha&&sha256(read_file(argv[1]))==sha,"Cartridge bytes changed");
 std::cout<<"Native X cycling through10 entries, locked skips, wrap, energy, per-seat isolation and replay pass; roster="<<(coop?(first?"Zero/X":"X/Zero"):"single X")<<'\n';return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
