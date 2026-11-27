#include <gbcllr/machine.hpp>
#include "../src/native_mods.hpp"
#include "gen1_extended_data.hpp"
#include <algorithm>
#include <cstring>
#include <iostream>
#include <stdexcept>
using namespace gbcllr;
static void check(bool v,const char* message){if(!v)throw std::runtime_error(message);}
static void byte(Machine& m,uint16_t a,uint8_t v){m.debug_write(a,{&v,1});}
static void enter(Machine& m,unsigned bank,uint16_t pc,Registers registers){
 byte(m,0xff50,1);byte(m,0xff40,0);byte(m,0xffff,0);byte(m,0xff0f,0);
 byte(m,0x2000,uint8_t(bank));byte(m,0xffb8,uint8_t(bank));byte(m,0xdff0,0);byte(m,0xdff1,0xc0);
 registers.pc=pc;registers.sp=0xdff0;registers.ime=0;m.debug_registers(registers);m.debug_wake();
}
static void finish(Machine& m,uint16_t stop=0xc000){auto limit=m.stats().cycles+4000000;while(m.stats().pc!=stop&&m.stats().cycles<limit)m.run_until(m.stats().cycles+1);check(m.stats().pc==stop,"original Dex/save function failed to return");}
static uint8_t flag(Machine& m,uint16_t base,uint8_t index,uint8_t action){enter(m,3,0x7669,{0,uint16_t((action<<8)|index),0x1234,base});finish(m);return uint8_t(m.debug_registers().bc);}
static void save_function(Machine& m,uint16_t pc){enter(m,28,pc,{});finish(m);}
static void call(Machine& m,uint16_t pc,uint8_t species,uint16_t stop=0xc000,bool back=false,unsigned side=0){
 byte(m,0xff50,1);byte(m,0xff40,0);byte(m,0xffff,0);byte(m,0xff0f,0);
 uint8_t bank=pc>=0x4000?15:1;byte(m,0x2000,bank);byte(m,0xffb8,bank);
 byte(m,0xd0b5,species);byte(m,0xd11e,pc==0x1537?17:species);
 byte(m,0xcf91,species);byte(m,0xd0aa,0);
 if(pc==0x376b){byte(m,0xd0b6,1);byte(m,0xd0b7,7);}
 if(pc==0x7103)byte(m,0xcfd9,species);
 if(pc==0x6abe){byte(m,0xfff3,uint8_t(side));byte(m,0xd733,0);byte(m,0xccdc,species);byte(m,0xccdd,species);}
 if(pc==0x7ba8||pc==0x7b89)byte(m,0xfff3,uint8_t(side));
 if(pc==0x6449)byte(m,0xcfcf,species);
 if(pc==0x63a5){byte(m,0xfff3,uint8_t(side));byte(m,side?0xcfcf:0xcfd5,species);byte(m,0xd0d7,0);byte(m,0xd0d8,100);byte(m,0xd05b,0);byte(m,0xd05f,0);}
 byte(m,0xdff0,0);byte(m,0xdff1,0xc0);
 m.debug_registers({0xa5b0,0x1212,uint16_t(pc==0x1665?0x9000:0xcafe),uint16_t(pc==0x1627?(back?13:11):0x4567),0xdff0,pc,0});m.debug_wake();
 auto start=m.stats().cycles,deadline=start+16000000;bool replayed=false;
 while(m.stats().pc!=stop&&m.stats().cycles<deadline){
  m.run_until(m.stats().cycles+1);
  if(pc==0x1627&&!replayed&&m.stats().pc==0x2694&&m.debug_read(0xd0ab,1)[0]>20){auto state=m.snapshot();auto target=m.stats().cycles+128;m.run_until(target);auto expected=m.digest();std::string error;check(m.restore(state,error),"mid-decoder restore failed");m.run_until(target);check(m.digest()==expected,"mid-decoder replay differs");replayed=true;}
 }
 if(m.stats().pc!=stop)throw std::runtime_error("Native function failed to reach its boundary: entry="+std::to_string(pc)+" species="+std::to_string(species)+" stopped_pc="+std::to_string(m.stats().pc)+" bank="+std::to_string(m.stats().rom_bank));
 if(pc==0x1627)check(replayed,"sprite qualifier did not reach its active compressed-byte checkpoint");
}
static void move_copy(Machine& m,unsigned bank,uint16_t site,uint16_t target,uint8_t id,unsigned side){
 byte(m,0xff50,1);byte(m,0xff40,0);byte(m,0xffff,0);byte(m,0xff0f,0);byte(m,0x2000,uint8_t(bank));byte(m,0xffb8,uint8_t(bank));byte(m,0xfff3,uint8_t(side));
 byte(m,0xdff0,0);byte(m,0xdff1,0xc0);
 m.debug_registers({0x0e00,6,target,uint16_t(0x4000+(id-1)*6),0xdff0,site,0});m.debug_wake();auto limit=m.stats().cycles+100000;
 while(m.stats().pc!=site+3&&m.stats().cycles<limit)m.run_until(m.stats().cycles+1);
 check(m.stats().pc==site+3,"original six-byte move copy did not return to its caller");
}
int main(int argc,char** argv){try{
 check(argc==4,"usage: gen1-extended-qa RED BLUE DMG_BOOT");
 unsigned cases=0,sprites=0,generic_names=0,move_cases=0,type_cases=0,copy_cases=0,flag_cases=0,save_cases=0,dex_cases=0,count_cases=0,reverse_cases=0,back_cases=0,learn_cases=0,cry_cases=0,type_name_cases=0,icon_cases=0,encounter_cases=0,area_cases=0,battle_role_cases=0;uint64_t native_instructions=0;
 for(int cart=1;cart<=2;++cart){
  auto rom=read_file(argv[cart]);Options base;base.boot_rom=read_file(argv[3]);
  NativeFeature f;f.plugin="gbcllr.gen1.extended";f.abi=1;f.asset_input=gen1_extended_data::asset_identity;
  Options enhanced=base;enhanced.mods.push_back(f);
  {
   // OAM/HDMA reads have no CPU stack context. Even a coincident original
   // caller PC must retain physical data and leave all native output untouched.
   NativeMods module(sha256(rom),enhanced.mods,false);std::array<uint8_t,0x8000> ram{};ram.fill(0xa5);
   const auto species=gen1_extended_data::mons[0].species;
   ram[0x10b5]=species;ram[0x111e]=species;ram[0xf91]=species;ram[0x10b6]=1;
   ModContext c{};c.rom=rom.data();c.rom_size=rom.size();c.wram=ram.data();
   struct Read {uint16_t bank,site,delta,address;uint8_t value;};
   for(auto read:std::array{Read{1,0x15b3,1,0xdff0,0x7f},Read{1,0x2fce,1,0xdff0,0x7f},
       Read{15,0x7b8c,3,0xcfd2,166},Read{15,0x63fa,1,0x6474,0xa5},
       Read{1,0x3771,2,0x3772,196},Read{1,0x162f,1,0xd0c4,0x40},
       Read{16,0x501d,1,uint16_t(0x5024+species-1),0xa5},Read{44,0x37b8,1,0x4000,0xa5},
       Read{1,0x4b0f,3,0xcfcb,0xa5}}){
    c.rom_bank=read.bank;c.instruction_pc=read.site;c.pc=uint16_t(read.site+read.delta);
    auto before=module.snapshot();auto physical=ram;
    check(module.read(c,read.address,read.value)==read.value,"DMA saw virtual CPU data");
    check(ram==physical&&module.snapshot()==before,"DMA changed native CPU-service output");
   }
  }
  std::vector<std::string> reference_results;
  for(bool native:{false,true}){
   size_t result_index=0;
   auto matches_reference=[&](Machine& m){if(!native)reference_results.push_back(m.digest());else check(result_index<reference_results.size()&&reference_results[result_index]==m.digest(),"generated/native feature result differs from instruction interpreter");++result_index;};
   base.native=native;enhanced.native=native;
   Machine original(rom,base),candidate(rom,enhanced);
   const auto original_seed=original.snapshot(),candidate_seed=candidate.snapshot();std::string error;
   auto expanded_options=enhanced;expanded_options.mods[0].expanded_encounters=true;Machine expanded(rom,expanded_options);const auto expanded_seed=expanded.snapshot();
   check(!candidate.restore(expanded_seed,error)&&candidate.snapshot()==candidate_seed,"ordinary profile accepted expanded-encounter state");
   check(!expanded.restore(candidate_seed,error)&&expanded.snapshot()==expanded_seed,"expanded profile accepted ordinary-encounter state");
   {
    NativeMods module(sha256(rom),enhanced.mods,false);auto clean=module.snapshot();
    for(unsigned index:{0u,18u,31u,32u,50u,63u}){auto invalid=clean;invalid[index]|=index%32==31?0x80:1;bool refused=false;try{module.restore(invalid);}catch(const std::exception&){refused=true;}check(refused&&module.snapshot()==clean,"invalid extra Dex state was not rejected atomically");}
   }
   for(uint16_t array:{0xd2f7,0xd30a}){
    auto ram_before=candidate.debug_read(0xd2f7,0x50),battery_before=candidate.battery();
    for(unsigned index=151;index<=250;++index){
     check(flag(candidate,array,uint8_t(index),2)==0,"new Dex flag was initially set");
     flag(candidate,array,uint8_t(index),1);check(flag(candidate,array,uint8_t(index),2)==(1u<<(index&7)),"original FlagAction did not set/test expanded Dex flag");
     matches_reference(candidate);++flag_cases;
    }
    check(candidate.debug_read(0xd2f7,0x50)==ram_before,"expanded Dex flags spilled into original seen/bag data");
    check(candidate.battery()==battery_before,"unsaved expanded Dex changes committed battery storage");
    auto set_state=candidate.snapshot();flag(candidate,array,200,0);check(flag(candidate,array,200,2)==0,"expanded reset failed");check(candidate.restore(set_state,error),"extra Dex rewind restore failed");check(flag(candidate,array,200,2)==1,"extra Dex rewind lost a live flag");
    for(unsigned index=151;index<=250;++index){flag(candidate,array,uint8_t(index),0);check(flag(candidate,array,uint8_t(index),2)==0,"expanded flag reset left set bit");++flag_cases;}
    for(uint8_t index:{uint8_t(0),uint8_t(149),uint8_t(150)}){
     check(original.restore(original_seed,error)&&candidate.restore(candidate_seed,error),"original flags seed failed");
     for(uint8_t action:{uint8_t(1),uint8_t(2),uint8_t(0)})check(flag(original,array,index,action)==flag(candidate,array,index,action),"extended flags changed original151 result");
     check(original.debug_read(0xd2f7,0x50)==candidate.debug_read(0xd2f7,0x50),"extended flags changed original151 RAM");
    }
   }
   check(candidate.restore(candidate_seed,error)&&original.restore(original_seed,error),"foreign flags seed failed");
   for(uint8_t action:{uint8_t(1),uint8_t(2),uint8_t(0)})check(flag(original,0xd800,200,action)==flag(candidate,0xd800,200,action),"expanded flag service consumed a foreign bitfield");
   check(original.debug_read(0xd800,32)==candidate.debug_read(0xd800,32),"foreign flag write was changed");
   // Pause between original read and write: debug writes retain their explicit
   // physical bus contract; current-state replay owns the new canonical flags.
   byte(candidate,0xd310,0x5a);enter(candidate,3,0x7669,{0,0x01c8,0x1234,0xd2f7});finish(candidate,0x7690);
   check(candidate.debug_read(0xd310,1)[0]==0x5a,"physical debug read invoked a paused CPU flag service");
   finish(candidate,0x7692);auto midway=candidate.snapshot();
   byte(candidate,0xd310,0x5a);check(candidate.debug_read(0xd310,1)[0]==0x5a,"debug write was intercepted as a native flag write");
   check(candidate.restore(midway,error),"mid-flag restore failed");finish(candidate);auto expected=candidate.digest();check(candidate.restore(midway,error),"mid-flag second restore failed");finish(candidate);check(candidate.digest()==expected,"mid-write flag replay differs");
   for(uint16_t saver:{0x778c,0x780f}){
    check(candidate.restore(candidate_seed,error),"save seed failed");flag(candidate,0xd2f7,151,1);flag(candidate,0xd30a,250,1);
    auto unsaved=candidate.battery();save_function(candidate,saver);auto committed=candidate.battery();
    check(committed!=unsaved,"native save did not commit");
    for(size_t i=0;i<0x2000;++i)if(i<0x1f00||i>=0x1f4c)check(committed[i]==unsaved[i],"extra Dex save changed sprite/Hall of Fame SRAM");
    flag(candidate,0xd2f7,151,0);flag(candidate,0xd30a,249,1);check(candidate.battery()==committed,"unsaved flags modified committed progress");
    Machine fresh(rom,enhanced);check(fresh.load_battery(committed,error),"native battery-only Dex import failed");save_function(fresh,0x76bd);
    check(flag(fresh,0xd2f7,151,2)==0x80&&flag(fresh,0xd30a,250,2)==4&&flag(fresh,0xd30a,249,2)==0,"fresh native party/Dex load lost saved or imported unsaved flags");
    enter(fresh,3,0x7850,{});finish(fresh);check(flag(fresh,0xd2f7,151,2)==0&&flag(fresh,0xd30a,250,2)==0,"New Game retained live expanded flags");check(fresh.battery()==committed,"New Game overwrote previous native save before SAVE");
    auto bad=committed;bad[0x1f48]^=1;Machine broken(rom,enhanced);check(broken.load_battery(bad,error),"test corrupt native import failed");bool refused=false;try{save_function(broken,0x76bd);}catch(const std::exception&){refused=true;}check(refused,"corrupt extended native save was silently accepted");
    ++save_cases;
   }
   check(candidate.restore(candidate_seed,error),"data seed after flag/save qualification failed");
   struct Count {unsigned bank;uint16_t site,base;};
   constexpr Count counters[]={{1,0x5e43,0xd2f7},{7,0x5250,0xd2f7},{7,0x6968,0xd2f7},{16,0x4143,0xd2f7},{17,0x4176,0xd2f7},{18,0x47b3,0xd2f7},{22,0x5044,0xd2f7},{16,0x412f,0xd30a},{17,0x4169,0xd30a}};
   for(unsigned stage:{0u,1u,2u}){
    check(candidate.restore(candidate_seed,error),"count seed failed");
    std::vector<uint8_t> empty_flags(19);candidate.debug_write(0xd2f7,empty_flags);candidate.debug_write(0xd30a,empty_flags);
    if(stage==1){flag(candidate,0xd2f7,215,1);flag(candidate,0xd30a,215,1);}
    if(stage==2){std::vector<uint8_t> original_flags(19,255);candidate.debug_write(0xd2f7,original_flags);candidate.debug_write(0xd30a,original_flags);for(unsigned index=151;index<=250;++index){flag(candidate,0xd2f7,uint8_t(index),1);flag(candidate,0xd30a,uint8_t(index),1);}}
    auto physical=candidate.debug_read(0xd2f7,0x50),battery=candidate.battery();
    for(const auto& counter:counters){enter(candidate,counter.bank,counter.site,{});finish(candidate,counter.site+8);auto got=candidate.debug_read(0xd11e,1)[0];if(got!=(stage==2?251:stage))throw std::runtime_error("original count consumer differs: bank="+std::to_string(counter.bank)+" site="+std::to_string(counter.site)+" stage="+std::to_string(stage)+" got="+std::to_string(got));matches_reference(candidate);++count_cases;}
    if(stage){enter(candidate,16,0x417b,{});finish(candidate,0x4191);check(candidate.debug_read(0xcd3d,1)[0]==(stage==2?251:216),"original highest-seen scan lost expanded last index");matches_reference(candidate);++count_cases;}
    enter(candidate,17,0x4169,{});finish(candidate,0x4191);check(candidate.debug_registers().hl==(stage==2?0x41ff:0x41d2),"original rating scan exceeded its final expanded threshold");matches_reference(candidate);++count_cases;
    enter(candidate,18,0x47b3,{});finish(candidate,0x47ca);check(candidate.debug_registers().hl==(stage==2?0x47d5:0x47d0),"original diploma branch used the wrong total");matches_reference(candidate);++count_cases;
    if(stage==2){for(unsigned removed=1;removed<=2;++removed){flag(candidate,0xd2f7,uint8_t(251-removed),0);enter(candidate,18,0x47b3,{});finish(candidate,0x47ca);check(candidate.debug_registers().hl==(removed==1?0x47d5:0x47d0),"original diploma250 boundary differs");matches_reference(candidate);++count_cases;}flag(candidate,0xd2f7,249,1);flag(candidate,0xd2f7,250,1);}
    check(candidate.debug_read(0xd2f7,0x50)==physical&&candidate.battery()==battery,"Dex count/list read changed native flags/bag/battery");
   }
   for(const auto& mon:gen1_extended_data::mons){check(candidate.restore(candidate_seed,error),"reverse Dex seed failed");byte(candidate,0xd11e,mon.dex);enter(candidate,16,0x4ff9,{});finish(candidate);check(candidate.debug_read(0xd11e,1)[0]==mon.species,"original reverse Dex lookup selected wrong native species");matches_reference(candidate);++reverse_cases;}
   for(uint8_t dex:{uint8_t(1),uint8_t(25),uint8_t(151)}){check(candidate.restore(candidate_seed,error)&&original.restore(original_seed,error),"original reverse Dex seed failed");for(Machine* m:{&original,&candidate}){byte(*m,0xd11e,dex);enter(*m,16,0x4ff9,{});finish(*m);}check(original.debug_read(0xd11e,1)==candidate.debug_read(0xd11e,1),"expanded reverse table changed original species");}
   check(candidate.restore(candidate_seed,error),"category seed after counts failed");
   for(const auto& mon:gen1_extended_data::mons)if(mon.species>=200){
    check(candidate.restore(candidate_seed,error),"wild role seed failed");byte(candidate,0xcfd8,mon.species);byte(candidate,0xd059,0);
    auto ready=candidate.snapshot();enter(candidate,15,0x6f50,{});finish(candidate,0x6f8b);auto expected=candidate.digest();matches_reference(candidate);++battle_role_cases;
    check(candidate.restore(ready,error),"wild role replay restore failed");enter(candidate,15,0x6f50,{});finish(candidate,0x6f8b);check(candidate.digest()==expected,"native wild classification replay differs");
    check(candidate.restore(candidate_seed,error)&&original.restore(original_seed,error),"original trainer role seed failed");
    for(Machine* m:{&original,&candidate}){byte(*m,0xcfd8,mon.species);byte(*m,0xd059,mon.species);enter(*m,15,0x6f50,{});finish(*m,0x6f58);}
    check(original.debug_read(0xc000,0x2000)==candidate.debug_read(0xc000,0x2000)&&original.stats().cycles==candidate.stats().cycles,"Extended data changed original trainer role");matches_reference(candidate);++battle_role_cases;
   }
   for(unsigned map=0;map<gen1_extended_data::habitat_maps.size();++map){
    check(candidate.restore(candidate_seed,error)&&expanded.restore(expanded_seed,error),"wild map seed failed");
    for(Machine* m:{&candidate,&expanded}){byte(*m,0xd35e,uint8_t(map));enter(*m,3,0x4eb8,{});finish(*m);}
    check(candidate.debug_read(0xd887,0x32)==expanded.debug_read(0xd887,0x32)&&candidate.stats().cycles==expanded.stats().cycles,"optional encounters modified original wild loading/rates/levels");
    auto ordinary=candidate.snapshot(),extra=expanded.snapshot();
    for(unsigned water=0;water<2;++water){
     unsigned index=gen1_extended_data::habitat_maps[map][water];if(index>=gen1_extended_data::habitats.size())continue;
     const auto& habitat=gen1_extended_data::habitats[index];unsigned base_address=water?0xd8a5:0xd888;
     for(unsigned slot:{0u,7u,8u,9u})for(unsigned parity:{0u,1u}){
      check(candidate.restore(ordinary,error)&&expanded.restore(extra,error),"wild consumer seed failed");
      unsigned expected=candidate.debug_read(uint16_t(base_address+slot*2+1),1)[0],added=slot>=8?habitat.species[slot-8]:0;
      for(Machine* m:{&candidate,&expanded}){byte(*m,0xffd3,uint8_t(parity));byte(*m,0xd0db,0);enter(*m,4,0x78e5,{0,uint16_t(slot*2),0,uint16_t(base_address)});finish(*m);}
      check(candidate.debug_read(0xcf91,1)[0]==expected&&expanded.debug_read(0xcf91,1)[0]==(added&&!parity?added:expected),"native rare encounter option/parity differs");
      auto a=candidate.debug_read(0xc000,0x2000),b=expanded.debug_read(0xc000,0x2000);b[0xf91]=a[0xf91];b[0xfd8]=a[0xfd8];check(a==b&&candidate.stats().cycles==expanded.stats().cycles,"native encounter changed unrelated RAM or CPU clocks");
      matches_reference(expanded);++encounter_cases;
     }
    }
   }
   for(const auto& mon:gen1_extended_data::mons){
    check(candidate.restore(candidate_seed,error),"party icon seed failed");enter(candidate,28,0x58e9,{uint16_t(mon.species<<8),0,0,0});finish(candidate);
    check(uint8_t(candidate.debug_registers().af>>8)==mon.icon*4,"original party icon lookup differs from owned data");matches_reference(candidate);++icon_cases;
    check(candidate.restore(candidate_seed,error)&&expanded.restore(expanded_seed,error),"area seed failed");
    std::vector<uint8_t> expected;
    for(unsigned map=0;map<gen1_extended_data::habitat_maps.size();++map)for(unsigned water=0;water<2;++water){unsigned index=gen1_extended_data::habitat_maps[map][water];if(index<gen1_extended_data::habitats.size()){const auto& habitat=gen1_extended_data::habitats[index];if(std::find(habitat.species.begin(),habitat.species.end(),mon.species)!=habitat.species.end())expected.push_back(uint8_t(map));}}
    check(!expected.empty(),"added species has no authored native habitat");expected.push_back(255);
    for(Machine* m:{&candidate,&expanded}){byte(*m,0xd11e,mon.species);enter(*m,3,0x69cb,{});finish(*m);}
    check(candidate.debug_read(0xcee9,1)[0]==255&&expanded.debug_read(0xcee9,expected.size())==expected,"native area search lost optional habitat or changed disabled behavior");matches_reference(expanded);++area_cases;
   }
   for(unsigned species=1;species<=190;++species){
    unsigned table=16*0x4000+0x5024-0x4000+species-1;if(!rom[table])continue;
    check(candidate.restore(candidate_seed,error)&&expanded.restore(expanded_seed,error),"original area seed failed");
    for(Machine* m:{&candidate,&expanded}){byte(*m,0xd11e,uint8_t(species));enter(*m,3,0x69cb,{});finish(*m);}
    check(candidate.debug_read(0xcee9,255)==expanded.debug_read(0xcee9,255)&&candidate.stats().cycles==expanded.stats().cycles,"optional encounters changed an original species area list");
   }
   for(const auto& mon:gen1_extended_data::mons){
    check(candidate.restore(candidate_seed,error),"evolution direct-header seed failed");byte(candidate,0xd0b5,mon.species);
    enter(candidate,14,0x6e54,{0,28,0xd0b8,uint16_t(0x43de + (mon.dex-1)*28)});finish(candidate,0x6e57);
    auto header=candidate.debug_read(0xd0b8,28);check(std::equal(mon.header.begin(),mon.header.end(),header.begin()),"original evolution direct-header copy differs from native data");matches_reference(candidate);++cases;
    for(unsigned level:{1u,29u,50u,100u}){
     check(candidate.restore(candidate_seed,error),"learnset seed failed");
     std::array<uint8_t,4> expected{};std::copy_n(mon.header.begin()+15,4,expected.begin());candidate.debug_write(0xd173,expected);
     size_t at=mon.evo_offset;while(gen1_extended_data::evolution_bytes[at])at+=gen1_extended_data::evolution_bytes[at]==2?4:3;++at;
     while(gen1_extended_data::evolution_bytes[at]&&gen1_extended_data::evolution_bytes[at]<=level){
      auto move=gen1_extended_data::evolution_bytes[at+1];at+=2;
      if(std::find(expected.begin(),expected.end(),move)!=expected.end())continue;
      auto empty=std::find(expected.begin(),expected.end(),0);if(empty!=expected.end())*empty=move;
      else{std::move(expected.begin()+1,expected.end(),expected.begin());expected.back()=move;}
     }
     byte(candidate,0xcf91,mon.species);byte(candidate,0xd127,uint8_t(level));byte(candidate,0xcee9,0);
     // Original WriteMonMoves restores these architectural arguments through
     // GetPredefRegisters, then parses the native immutable data itself.
     const uint8_t predef[]={0xc1,0x73,0xd1,0x73,0x00,0x04};candidate.debug_write(0xcc4f,predef);
     enter(candidate,14,0x6fb8,{});finish(candidate);auto got=candidate.debug_read(0xd173,4);
     if(!std::equal(expected.begin(),expected.end(),got.begin()))throw std::runtime_error("original learnset differs: species="+std::to_string(mon.species)+" level="+std::to_string(level));
     matches_reference(candidate);++learn_cases;
    }
    check(candidate.restore(candidate_seed,error),"cry seed failed");enter(candidate,1,0x13d9,{uint16_t(mon.species<<8),0,0,0});finish(candidate);
    check(candidate.debug_read(0xc0f1,2)==std::vector<uint8_t>({mon.cry[1],mon.cry[2]})&&uint8_t(candidate.debug_registers().af>>8)==uint8_t(mon.cry[0]*3+0x14),"original cry resource/modifiers differ");matches_reference(candidate);++cry_cases;
   }
   for(unsigned n=0;n<2;++n){
    check(candidate.restore(candidate_seed,error),"type-name seed failed");enter(candidate,9,0x7d89,{uint16_t((n?27:9)<<8),0,0,0xc000});finish(candidate);
    const auto& name=gen1_extended_data::type_names[n];auto end=std::find(name.begin(),name.end(),0x50);auto got=candidate.debug_read(0xc000,size_t(end-name.begin()));
    check(std::equal(name.begin(),end,got.begin()),"original PrintType/PlaceString differs from native type name");matches_reference(candidate);++type_name_cases;
   }
   for(uint8_t species:{uint8_t(1),uint8_t(0x54),uint8_t(0x99)}){
    check(original.restore(original_seed,error)&&candidate.restore(candidate_seed,error),"original cry seed failed");
    for(Machine* m:{&original,&candidate}){enter(*m,1,0x13d9,{uint16_t(species<<8),0,0,0});finish(*m);}
    check(original.debug_read(0xc000,0x2000)==candidate.debug_read(0xc000,0x2000)&&original.debug_read(0xff00,256)==candidate.debug_read(0xff00,256)&&original.stats().cycles==candidate.stats().cycles,"native cry service changed original species");
   }
   for(uint8_t type:{uint8_t(0),uint8_t(8),uint8_t(26)}){
    check(original.restore(original_seed,error)&&candidate.restore(candidate_seed,error),"original type-name seed failed");
    for(Machine* m:{&original,&candidate}){enter(*m,9,0x7d89,{uint16_t(type<<8),0,0,0xc000});finish(*m);}
    check(original.debug_read(0xc000,0x2000)==candidate.debug_read(0xc000,0x2000)&&original.stats().cycles==candidate.stats().cycles,"native type-name service changed original type");
   }
   for(const auto& mon:gen1_extended_data::mons){
    check(candidate.restore(candidate_seed,error),"Dex-entry seed failed");byte(candidate,0xcf91,mon.species);byte(candidate,0xd11e,mon.species);
    enter(candidate,16,0x435e,{});finish(candidate,0x436d);check(candidate.debug_registers().de==0x6000,"original Dex table caller did not select native entry window");
    auto begin=gen1_extended_data::dex_bytes.begin()+mon.dex_offset,end=std::find(begin,begin+mon.dex_size,0x50);check(end!=begin+mon.dex_size,"Dex category is unterminated");auto category=size_t(end-begin);
    byte(candidate,0xc403,0x66);finish(candidate,0x4373);auto category_pixels=candidate.debug_read(uint16_t(category>10?0xc3f8:0xc3f9),category);check(std::equal(begin,end,category_pixels.begin())&&candidate.debug_read(0xc403,1)[0]==0x66,"native Dex category lost data or overwrote its right border");
    enter(candidate,16,0x1955,{0,0,0x6000,0xc000});finish(candidate);auto printed=candidate.debug_read(0xc000,category);check(std::equal(begin,end,printed.begin()),"original PlaceString differs from owned Dex category");
    for(unsigned inches=0;inches<2;++inches){uint8_t blank=0x7f;candidate.debug_write(0xc010,{&blank,1});candidate.debug_write(0xc011,{&blank,1});enter(candidate,16,0x3c5f,{0,uint16_t(inches?0x8102:0x0102),uint16_t(0x6000+category+1+inches),0xc010});finish(candidate);auto number=candidate.debug_read(0xc010,2);auto value=begin[category+1+inches];check(number[1]==0xf6+value%10&&number[0]==(value>=10?0xf6+value/10:inches?0xf6:0x7f),"original Dex height number differs from owned entry");}
    enter(candidate,16,0x43e7,{0,0,uint16_t(0x6000+category+4),0xff8b});finish(candidate,0x43e9);check(candidate.debug_read(0xff8b,1)[0]==begin[category+4],"original Dex upper weight read differs");
    enter(candidate,16,0x43ec,{0,0,uint16_t(0x6000+category+3),0xff8c});finish(candidate,0x43ee);check(candidate.debug_read(0xff8c,1)[0]==begin[category+3],"original Dex lower weight read differs");
    matches_reference(candidate);++dex_cases;
   }
   for(const auto& mon:gen1_extended_data::mons)for(uint16_t pc:{0x1537,0x2f9e}){
    check(original.restore(original_seed,error)&&candidate.restore(candidate_seed,error),"seed restore failed");
    auto native_before=candidate.stats().instructions_native;call(original,pc,mon.species);call(candidate,pc,mon.species);
    auto a=original.debug_read(0xc000,0x2000),b=candidate.debug_read(0xc000,0x2000);
    unsigned start=pc==0x1537?0x10b8:0xd6d,count=pc==0x1537?28:11;
    if(pc==0x1537){check(std::equal(mon.header.begin(),mon.header.end(),b.begin()+start),"native Extended base stats differ");}
    else check(std::equal(mon.name.begin(),mon.name.end(),b.begin()+start),"native Extended name differs");
    std::copy_n(a.begin()+start,count,b.begin()+start);check(a==b,"adapter changed unrelated original RAM");
    auto ar=original.debug_registers(),br=candidate.debug_registers();
    check(ar.af==br.af&&ar.bc==br.bc&&ar.de==br.de&&ar.hl==br.hl&&ar.sp==br.sp&&ar.pc==br.pc&&ar.ime==br.ime,"adapter changed original register return contract");
    // New Dex table data naturally changes the original AddNTimes loop count.
    // Compare exact hardware time/state with the same interpreter workload;
    // invalid old-ROM MissingNo data is not a timing reference for new content.
    matches_reference(candidate);
    check(original.battery()==candidate.battery(),"data caller changed native persistent storage");
    if(native)native_instructions+=candidate.stats().instructions_native-native_before;
    auto state=candidate.snapshot();check(candidate.restore(state,error)&&candidate.snapshot()==state,"current Extended restore differs");
    auto stock_before=original.snapshot();check(!original.restore(state,error),"stock machine accepted Extended snapshot");check(original.snapshot()==stock_before,"incompatible Extended restore changed stock machine");++cases;
   }
   for(const auto& mon:gen1_extended_data::mons){
    check(candidate.restore(candidate_seed,error),"generic name seed restore failed");call(candidate,0x376b,mon.species);
    auto name=candidate.debug_read(0xcd6d,11);check(std::equal(mon.name.begin(),mon.name.end(),name.begin()),"native generic name path treated expanded species as a TM");matches_reference(candidate);++generic_names;
   }
   for(const auto& move:gen1_extended_data::moves)for(unsigned side:{0u,1u}){
    check(candidate.restore(candidate_seed,error),"move caller seed restore failed");call(candidate,0x6abe,move.id,0xc000,false,side);
    auto data=candidate.debug_read(side?0xcfcc:0xcfd2,6),name=candidate.debug_read(0xcd6d,20);
    check(std::equal(move.data.begin(),move.data.end(),data.begin()),"native current-move metadata differs from owned donor profile");
    check(std::equal(move.name.begin(),move.name.end(),name.begin()),"native variable move name differs from owned donor data");
    for(uint16_t animation_entry:{0x7ba8,0x7b89}){call(candidate,animation_entry,move.id,0x7bbc,false,side);check(candidate.debug_read(0xd07c,1)[0]==move.animation,"native current-move animation did not select the donor's original animation alias");check(candidate.debug_read(side?0xcfcc:0xcfd2,1)[0]==move.id,"animation alias changed canonical move identity");}
    matches_reference(candidate);++move_cases;
   }
   constexpr uint8_t types[]={0,1,2,3,4,5,7,8,9,20,21,22,23,24,25,26,27};
   for(auto attack:types)for(auto defense:types){
    if(attack!=9&&attack!=27&&defense!=9&&defense!=27)continue;
    unsigned factor=10,ai=16;
    for(size_t at=0;at+2<gen1_extended_data::type_effects.size();at+=3)if(gen1_extended_data::type_effects[at]==attack&&gen1_extended_data::type_effects[at+1]==defense){factor=gen1_extended_data::type_effects[at+2];ai=factor;break;}
    check(candidate.restore(candidate_seed,error),"AI type seed restore failed");byte(candidate,0xd019,defense);byte(candidate,0xd01a,defense);call(candidate,0x6449,attack);
    check(candidate.debug_read(0xd11e,1)[0]==ai,"native AI Dark/Steel effectiveness differs from source table");matches_reference(candidate);++type_cases;
    for(unsigned side:{0u,1u}){
     check(candidate.restore(candidate_seed,error),"damage type seed restore failed");uint8_t own=attack?0:1;
     byte(candidate,0xd019,side?defense:own);byte(candidate,0xd01a,side?defense:own);byte(candidate,0xcfea,side?own:defense);byte(candidate,0xcfeb,side?own:defense);
     call(candidate,0x63a5,attack,0xc000,false,side);auto damage=candidate.debug_read(0xd0d7,2);
     check(damage[0]==0&&damage[1]==factor*10,"native player/enemy Dark/Steel damage differs from source table");matches_reference(candidate);++type_cases;
    }
   }
   for(uint8_t move:{uint8_t(1),uint8_t(33),uint8_t(165)}){
    check(original.restore(original_seed,error)&&candidate.restore(candidate_seed,error),"original move seed restore failed");call(original,0x6abe,move);call(candidate,0x6abe,move);
    check(original.debug_read(0xcfd2,6)==candidate.debug_read(0xcfd2,6),"new-move adapter changed original move metadata");
    auto a=original.debug_read(0xcd6d,20),b=candidate.debug_read(0xcd6d,20);auto end=std::find(a.begin(),a.end(),0x50);check(end!=a.end()&&std::equal(a.begin(),end+1,b.begin()),"new-move adapter changed an original visible move name");
   }
   struct Consumer {unsigned bank;uint16_t site,target;};
   constexpr Consumer consumers[]={{15,0x6aeb,0},{15,0x6338,0},{1,0x6ea4,0xcee9},{14,0x703f,0xcee9},{3,0x748e,0xcd6d},{3,0x76d9,0xcd6d},{3,0x66b4,0xcd6d},{14,0x5894,0xcfcc}};
   for(const auto& consumer:consumers)for(const auto& move:gen1_extended_data::moves)for(unsigned side=0;side<(consumer.target?1u:2u);++side){
    check(candidate.restore(candidate_seed,error),"move consumer seed restore failed");uint16_t target=consumer.target?consumer.target:side?0xcfcc:0xcfd2;move_copy(candidate,consumer.bank,consumer.site,target,move.id,side);
    auto result=candidate.debug_read(target,6);check(std::equal(move.data.begin(),move.data.end(),result.begin()),"native move PP/relearn/heal/item/AI consumer differs from donor metadata");matches_reference(candidate);++copy_cases;
   }
   check(candidate.restore(candidate_seed,error),"wrong-destination seed restore failed");auto untouched=candidate.debug_read(0xcd6d,6);move_copy(candidate,3,0x66b4,0xcd4d,166,0);check(candidate.debug_read(0xcd6d,6)==untouched,"move adapter accepted a foreign DE output extent");
   for(const auto& mon:gen1_extended_data::mons){
    check(candidate.restore(candidate_seed,error),"sprite seed restore failed");
    call(candidate,0x1537,mon.species);
    for(bool back:{false,true}){
     call(candidate,0x1627,mon.species,0xc000,back);
     const auto& picture=back?mon.back:mon.front;unsigned dim=gen1_extended_data::sprite_bytes[picture.offset],w=dim>>4,h=dim&15;
     std::array<uint8_t,784> expected{};
     for(unsigned x=0;x<w;++x)for(unsigned y=0;y<h;++y)for(unsigned row=0;row<8;++row){unsigned src=((x+(8-w)/2)*7+y+7-h)*16+row*2,dst=x*h*8+y*8+row;expected[dst]=picture.expected[src];expected[392+dst]=picture.expected[src+1];}
     auto decoded=candidate.debug_read(0xa188,784);check(std::equal(expected.begin(),expected.end(),decoded.begin()),"native sprite decoder differs from owned raw planes");
    }
    call(candidate,0x1665,mon.species,0x1848);
    auto pixels=candidate.debug_read(0xa188,784);
    check(std::equal(mon.front.expected.begin(),mon.front.expected.end(),pixels.begin()),"native decoder/front alignment differs from owned raw sprite");
    auto current=candidate.snapshot();check(candidate.restore(current,error)&&candidate.snapshot()==current,"front sprite current-load differs");
    call(candidate,0x1384,mon.species,0x1665);
    check(candidate.debug_read(0xcf91,1)[0]==mon.species,"native front entry rejected a valid expanded Dex number");
    check(candidate.debug_read(0xd0aa,1)[0]==1,"native front entry did not set its original flip flag");
    call(candidate,0x1537,mon.species);call(candidate,0x7103,mon.species,0x1848);
    auto back_pixels=candidate.debug_read(0xa188,784);check(std::equal(mon.back.expected.begin(),mon.back.expected.end(),back_pixels.begin()),"original merged native48px back differs from owned raw sprite");
    auto back_state=candidate.snapshot();check(candidate.restore(back_state,error)&&candidate.snapshot()==back_state,"native48 back current-load differs");++back_cases;
    matches_reference(candidate);
    ++sprites;
   }
   for(uint8_t species:{uint8_t(1),uint8_t(0x54),uint8_t(0x99)}){
    check(original.restore(original_seed,error)&&candidate.restore(candidate_seed,error),"original back seed failed");
    for(Machine* m:{&original,&candidate}){call(*m,0x1537,species);call(*m,0x7103,species,0x1848);}
    check(original.debug_read(0xa000,1176)==candidate.debug_read(0xa000,1176)&&original.debug_read(0xc000,0x2000)==candidate.debug_read(0xc000,0x2000)&&original.stats().cycles==candidate.stats().cycles,"native48 adapter changed an original scaled back");
   }
   // Original species keep their complete caller contract byte-for-byte.
   for(uint8_t species:{uint8_t(1),uint8_t(0x54),uint8_t(0x99)})for(uint16_t pc:{0x1537,0x2f9e}){
    check(original.restore(original_seed,error)&&candidate.restore(candidate_seed,error),"stock seed restore failed");call(original,pc,species);call(candidate,pc,species);
    check(original.debug_read(0xc000,0x2000)==candidate.debug_read(0xc000,0x2000),"Extended adapter changed original species");++cases;
    check(original.debug_read(0xff00,256)==candidate.debug_read(0xff00,256)&&original.stats().cycles==candidate.stats().cycles,"Extended adapter changed original species IO/bus timing");
   }
   // Inspect the physical cartridge after every native provider has run.
   // This bypasses read hooks and covers both ROM0 and every switchable bank.
   for(Machine* m:{&candidate,&expanded}){byte(*m,0xff50,1);auto first=m->debug_read(0,0x4000);
    check(std::equal(first.begin(),first.end(),rom.begin()),"native module modified cartridge ROM0 backing bytes");
    for(unsigned bank=1;bank<rom.size()/0x4000;++bank){byte(*m,0x2000,uint8_t(bank));auto physical=m->debug_read(0x4000,0x4000);check(std::equal(physical.begin(),physical.end(),rom.begin()+bank*0x4000),"native module modified switchable cartridge backing bytes");}
   }
  }
 }
 check(native_instructions>0,"actual caller qualification did not execute generated instructions");
 std::cout<<cases<<" actual Red/Blue data/name calls, "<<sprites<<" front/back raw decoder/alignment/valid-entry, "<<generic_names<<" expanded generic names, "<<move_cases<<" native player/enemy move-data/name/animation, "<<type_cases<<" Dark/Steel AI/damage, "<<copy_cases<<" exact original move-data consumers, "<<flag_cases<<" expanded flag cases, "<<save_cases<<" original save/load/NewGame cases, "<<dex_cases<<" original Dex table/category/height/weight, "<<count_cases<<" count/highest-seen, "<<reverse_cases<<" reverse Dex, "<<back_cases<<" merged native48px back, "<<learn_cases<<" native learnset, "<<cry_cases<<" cry, "<<type_name_cases<<" type-name, "<<icon_cases<<" party-icon, "<<encounter_cases<<" optional encounter and "<<area_cases<<" area and "<<battle_role_cases<<" wild/trainer-role cases passed; "<<native_instructions<<" generated data-caller instructions; native/reference exact state and active decoder restore. Broader actual gameplay remains unqualified; native48px VRAM/PPU,evolution/learning and gameplay SAVE/Continue qualification is separate.\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
