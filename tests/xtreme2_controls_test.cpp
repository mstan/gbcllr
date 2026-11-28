#include "../src/xtreme2_controls.hpp"
#include "../src/native_mods.hpp"
#include <gbcllr/machine.hpp>
#include "native_contract_rom.hpp"
#include <iostream>
#include <stdexcept>
using namespace gbcllr;
static void check(bool pass,const char* text){if(!pass)throw std::runtime_error(text);}
int main(){try{
 // A cartridge sees exactly its original eight buttons even when either
 // logical seat also holds virtual actions. Ordinary snapshots stay equal.
 auto rom=native_contract_rom();Options options;
 Machine a(rom,options),b(rom,options);
 native_contract_entry(a,0x180);native_contract_entry(b,0x180);
 for(unsigned i=0;i<3;++i){a.run_frame(16);b.run_frame(LogicalInput{16,3},LogicalInput{255,3});}
 check(a.snapshot()==b.snapshot(),"Virtual inputs entered cartridge hardware/state");
 bool rejected=false;try{b.run_frame(LogicalInput{0,4});}catch(const std::exception&){rejected=true;}
 check(rejected,"Unknown virtual button accepted");
 check(a.stats().instructions_native>0&&a.stats().instructions_fallback==0&&b.stats().instructions_fallback==0,"input fixture left native execution");
 std::array<uint8_t,0x8000> ram{};ram[0x101]=2;ram[0x121]=16;ram[0x9b0]=1;
 ram[0x6f]=0x40; // Acquired weapon2 with zero energy; selection remains valid.
 ModContext c{};c.wram=ram.data();c.rom_bank=5;
 const std::array<uint16_t,6> original{0x12b0,0x3456,0x789a,0xbcde,0xcff0,0x4001};auto cpu=original;
 Xtreme2Controls controls(true);controls.inputs(virtual_r,0);
 Xtreme2Controls fresh_a(true),fresh_b(true);check(fresh_a.snapshot()==fresh_b.snapshot(),"Fresh virtual controls contain indeterminate state");
 check(controls.dispatch(c,cpu,ram,0,true),"Owned weapon was not selected");
 check(cpu[5]==0x675&&cpu[3]==0x7b33&&cpu[4]==original[4]-2&&ram[cpu[4]-0xc000]==1&&ram[cpu[4]-0xc000+1]==0x40,"Selection lacks a real native far-call frame");
 auto continuation=controls.snapshot();Xtreme2Controls replay(true);replay.restore(continuation);
 c.rom_bank=0x28;cpu[5]=0x7b33;controls.instruction(c,cpu);
 check(cpu[0]>>8==2,"Native selection did not skip locked entries");
 auto other=cpu;replay.instruction(c,other);check(other==cpu&&replay.snapshot()==controls.snapshot(),"Selection continuation replay differs");
 c.rom_bank=5;cpu[5]=0x4001;cpu[4]=original[4];controls.instruction(c,cpu);
 check(cpu==original&&!controls.active(),"Native reload did not restore caller registers");
 controls.inputs(virtual_r,0);check(!controls.dispatch(c,cpu,ram,0,true),"Held shoulder repeated");
 controls.inputs(0,0);controls.inputs(3,0);check(!controls.dispatch(c,cpu,ram,0,true),"Opposed shoulders selected a weapon");
 controls.inputs(0,0);controls.inputs(virtual_l,0);check(!controls.dispatch(c,cpu,ram,1,true),"Player1 shoulder entered Player2");
 check(controls.dispatch(c,cpu,ram,0,true),"Backward ownership scan failed");
 c.rom_bank=0x28;cpu[5]=0x7b33;controls.instruction(c,cpu);check(cpu[0]>>8==2,"Backward scan did not wrap through owned weapons");
 c.rom_bank=5;cpu=original;controls.instruction(c,cpu);
 controls.inputs(0,0);controls.inputs(virtual_r,0);ram[0xc9]=1;
 check(!controls.dispatch(c,cpu,ram,0,true)&&ram[0x131]==0,"Zero shoulder activated a technique");ram[0xc9]=0;
 for(unsigned scene:{0xcu,0x14u,0xc2u}){
  controls.inputs(0,0);controls.inputs(virtual_r,0);ram[scene]=1;controls.instruction(c,cpu);ram[scene]=0;
  check(!controls.dispatch(c,cpu,ram,0,true),"Scene shoulder was queued after the scene");
  controls.inputs(virtual_r,0);check(!controls.dispatch(c,cpu,ram,0,true),"Held scene shoulder became a new edge");
 }
 controls.inputs(0,0);controls.inputs(virtual_r,0);controls.discard_pending();
 check(!controls.dispatch(c,cpu,ram,0,true),"Menu shoulder was queued after resume");
 controls.inputs(0,0);controls.inputs(virtual_r,0);ram[0x5f]=6;ram[0x2d]=1;
 check(!controls.dispatch(c,cpu,ram,0,true),"Mounted shoulder entered an on-foot weapon reload");ram[0x5f]=ram[0x2d]=0;
 Xtreme2Controls disabled;disabled.inputs(virtual_r,0);check(!disabled.dispatch(c,cpu,ram,0,true),"Disabled native enhancement changed equipment");
 std::cout<<"Virtual input isolation, native-call continuation, ownership, edges and blocked-input contracts pass\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
