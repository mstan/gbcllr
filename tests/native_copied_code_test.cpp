#include "native_contract_rom.hpp"
#include <algorithm>
#include <array>
#include <iostream>
#include <stdexcept>
using namespace gbcllr;
static void check(bool value,const char* text){if(!value)throw std::runtime_error(text);}
int main(){try{
    auto rom=native_contract_rom();Machine machine(rom,Options{});
    // Observe DMA bytes without a coincident LCD OAM-read barrier.
    const std::array<uint8_t,1> lcd_off{0};machine.debug_write(0xff40,lcd_off);
    std::array<uint8_t,160> objects{};for(unsigned i=0;i<objects.size();++i)objects[i]=uint8_t(i^0x5a);
    machine.debug_write(0xc100,objects);
    // The original routine is copied into writable HRAM at a different base.
    // Its live DMA-page operand changes, without modifying the source image.
    auto copied=std::vector<uint8_t>(rom.begin()+0x200,rom.begin()+0x20a);copied[1]=0xc1;
    machine.debug_write(0xffa0,copied);
    const std::array<uint8_t,2> target{0xc0,0x01};machine.debug_write(0xfff0,target);
    auto registers=machine.debug_registers();registers.pc=0xffa0;registers.sp=0xfff0;registers.ime=0;
    machine.debug_registers(registers);auto start=machine.snapshot();std::string error;
    machine.run_frame();machine.take_audio();auto result=machine.snapshot();
    check(machine.debug_read(0xfe00,objects.size())==std::vector<uint8_t>(objects.begin(),objects.end()),"copied native DMA lost its live source page");
    check(machine.stats().instructions_native>0&&machine.stats().instructions_fallback==0,"copied routine left native execution");
    check(machine.restore(start,error),error.c_str());machine.run_frame();machine.take_audio();
    check(machine.snapshot()==result,"copied native routine current-load replay differs");
    Machine thunk(rom,Options{});thunk.debug_write(0xff40,lcd_off);thunk.debug_write(0xfff0,target);
    registers=thunk.debug_registers();registers.pc=0x230;registers.sp=0xfff0;registers.ime=0;
    thunk.debug_registers(registers);thunk.run_frame();thunk.take_audio();
    check(thunk.debug_read(0xc4b7,3)==std::vector<uint8_t>({0xc3,0xc0,0x01}),"original native thunk installer failed");
    const std::array<uint8_t,1> changed_target{0xd0};thunk.debug_write(0xc4b8,changed_target);
    native_contract_entry(thunk,0xc4b7);auto thunk_start=thunk.snapshot();
    thunk.run_frame();thunk.take_audio();auto thunk_result=thunk.snapshot();
    check(thunk.stats().pc==0x1d0&&thunk.stats().instructions_fallback==0,"native writable jump lost its live target");
    check(thunk.restore(thunk_start,error),error.c_str());thunk.run_frame();thunk.take_audio();
    check(thunk.snapshot()==thunk_result,"writable native thunk current-load replay differs");
    check(native_contract_rom()==rom,"native copied execution modified its original input");
    std::cout<<"Source-bound copied HRAM DMA and writable JP thunks, live operands and current-load replay pass\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
