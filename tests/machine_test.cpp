#include <gbcllr/machine.hpp>
#define GB_INTERNAL
#define GB_DISABLE_DEBUGGER
#define GB_DISABLE_CHEATS
#define GB_DISABLE_CHEAT_SEARCH
#define GB_DISABLE_REWIND
#define GB_DISABLE_TIMEKEEPING
#include "../third_party/sameboy/Core/gb.h"
#include <cstring>
#include <iostream>
#include <stdexcept>

void check(bool pass,const char* message){if(!pass)throw std::runtime_error(message);}
int main(){try {
    using namespace gbcllr;
    // Arithmetic elapsed clocks must preserve delayed reload, DIV-reset
    // phase and APU/serial edge ordering under partial and long advances.
    for(auto model:{GB_MODEL_DMG_B,GB_MODEL_CGB_E})for(bool fast:{false,true}) {
        GB_gameboy_t reference{},elapsed{};
        GB_init(&reference,model);GB_init(&elapsed,model);
        for(auto* hw:{&reference,&elapsed}) {
            hw->cgb_double_speed=fast;
            hw->apu_output.sample_rate=48000;
            GB_apu_set_sample_callback(hw,[](GB_gameboy_t*,GB_sample_t*){});
            hw->io_registers[GB_IO_TAC]=5;
            hw->io_registers[GB_IO_TIMA]=247;
            hw->io_registers[GB_IO_TMA]=239;
            hw->io_registers[GB_IO_IF]=0;
        }
        unsigned rng=0x943103;
        for(unsigned i=0;i<20000;i++) {
            rng=rng*1664525u+1013904223u;
            if(i%113==0)for(auto* hw:{&reference,&elapsed}) {
                GB_set_internal_div_counter(hw,uint16_t(rng>>8));
                hw->div_cycles=0;hw->div_state=0;
                hw->io_registers[GB_IO_TAC]=uint8_t(rng>>24)&7;
                hw->serial_mask=(i&1)?4:128;
                hw->io_registers[GB_IO_SC]=0x83;
                hw->io_registers[GB_IO_SB]=0xa5;
            }
            uint8_t amount=uint8_t(1+(rng>>24)%124);
            GB_timers_reference(&reference,amount);
            GB_timers_run(&elapsed,amount);
            check(!std::memcmp(&elapsed,&reference,offsetof(GB_gameboy_t,llr_reset_seed)),
                  "elapsed timer operation differs from hardware clock oracle");
        }
        GB_free(&reference);GB_free(&elapsed);
    }
    // DIV clocks TIMA to overflow after 16 CPU clocks. Its delayed reload
    // requests the IRQ four clocks later; HALT then exits at the next poll.
    // Coalescing must preserve that delay, both speed modes and IME-off wake.
    for(bool fast:{false,true})for(bool ime:{false,true})for(unsigned phase=0;phase<6;++phase) {
        GB_gameboy_t reference{},wait{};
        for(auto* hw:{&reference,&wait}) {
            GB_init(hw,GB_MODEL_CGB_E);
            hw->io_registers[GB_IO_LCDC]=0;
            hw->io_registers[GB_IO_IF]=0;
            hw->io_registers[GB_IO_TAC]=5;
            hw->io_registers[GB_IO_TIMA]=255;
            hw->io_registers[GB_IO_TMA]=0;
            hw->div_cycles=0;hw->div_counter=0;
            hw->cgb_double_speed=fast;hw->halted=true;hw->just_halted=false;
            hw->ime=ime;hw->ime_toggle=false;hw->interrupt_enable=4;
            hw->pc=0xc000;hw->sp=0xd000;hw->dma_current_dest=0xa1;
            hw->llr_cycle_budget=10000;
            if(phase==1){hw->joyp_switching_delay=12;hw->io_registers[GB_IO_JOYP]=0x2f;}
            if(phase==2){hw->key_bounce_timing[GB_KEY_RIGHT]=128;hw->keys[0][GB_KEY_RIGHT]=true;hw->io_registers[GB_IO_JOYP]=0x2f;}
            if(phase==3){hw->ime_toggle=true;hw->ime=!ime;}
            if(phase==4){hw->dma_current_dest=0;hw->dma_current_src=0xc000;}
            if(phase==5){hw->key_bounce_timing[GB_KEY_RIGHT]=4;hw->key_bounce_timing[GB_KEY_A]=12;hw->io_registers[GB_IO_JOYP]=0x2f;}
        }
        wait.llr_native_wait_allowed=true;
        unsigned expected=0,actual=0;
        while(reference.halted)expected+=GB_run(&reference);
        while(wait.halted)actual+=GB_run(&wait);
        check(actual==expected,"native HALT changed timer reload wake time");
        check(actual==(ime?44u:24u)*(fast?1:2),"timer reload/IRQ CPU clock oracle differs");
        check(wait.pc==(ime?0x50:0xc000),"HALT did not dispatch or preserve IME-off PC");
        check(wait.sp==(ime?0xcffe:0xd000),"HALT changed interrupt stack ownership");
        check(!std::memcmp(&wait,&reference,offsetof(GB_gameboy_t,llr_reset_seed)),
              "native HALT changed hardware state at the wake boundary");
#if GBCLLR_NATIVE_WAIT && GBCLLR_FRAME_AUDIO
        check(wait.llr_wait_costs.calls>0,"native HALT timer fixture did not use the candidate");
#endif
        GB_free(&reference);GB_free(&wait);
    }
#if GBCLLR_NATIVE_WAIT && GBCLLR_FRAME_AUDIO
    // Independent instruction execution remains the oracle for the complete
    // STAT predicate wait, including speed modes and IRQ exit boundaries.
    for(bool fast:{false,true})for(unsigned interrupt:{0u,2u,4u}) {
        std::vector<uint8_t> image(32768);image[0x143]=0x80;
        const uint8_t loop[]={0xf0,0x41,0xe6,2,0x20,0xfa};
        std::copy(std::begin(loop),std::end(loop),image.begin()+0x19);
        GB_gameboy_t reference{},wait{};
        for(auto* hw:{&reference,&wait}) {
            GB_init(hw,GB_MODEL_CGB_E);GB_load_rom_from_buffer(hw,image.data(),image.size());
            hw->boot_rom_finished=true;hw->cgb_double_speed=fast;hw->disable_rendering=true;
            hw->llr_frame_picture_enabled=true;hw->io_registers[GB_IO_LCDC]=0x91;
            hw->dma_current_dest=0xa1;hw->pc=0x19;hw->sp=0xd000;
        }
        unsigned prime=0;
        while(!((reference.io_registers[GB_IO_STAT]&2)&&reference.display_cycles< -96)&&prime++<20000)
            GB_advance_cycles(&reference,4);
        check(prime<20000,"STAT wait fixture did not reach a stable transfer interval");
        for(unsigned n=0;n<prime;++n)GB_advance_cycles(&wait,4);
        reference.ime=true;reference.interrupt_enable=uint8_t(interrupt);
        if(interrupt==2)reference.io_registers[GB_IO_STAT]|=8;
        if(interrupt==4){reference.io_registers[GB_IO_TAC]=5;reference.io_registers[GB_IO_TIMA]=248;reference.div_counter=0;reference.div_cycles=0;reference.div_state=0;}
        reference.io_registers[GB_IO_IF]=0;
        std::memcpy(&wait,&reference,offsetof(GB_gameboy_t,llr_reset_seed));
        std::memcpy(wait.ram,reference.ram,reference.ram_size);std::memcpy(wait.vram,reference.vram,0x4000);
        wait.llr_native_poll_allowed=true;
        wait.llr_cycle_budget=10000;wait.cycles_since_run=0;
        wait.llr_fetch_offset=0x19;wait.address_bus=0x19;
        check(GB_read_memory(&wait,wait.pc++)==0xf0,"STAT wait opcode fixture differs");wait.pending_cycles=4;
        auto retired=GB_native_stat_poll_wait(&wait);check(retired>=6,"STAT wait fixture did not execute the native operation");
        unsigned actual=wait.cycles_since_run,expected=0;GB_instruction_end(&wait);
        auto in_loop=[](const GB_gameboy_t& hw){return hw.pc==0x19||hw.pc==0x1b||hw.pc==0x1d;};
        for(unsigned n=0;in_loop(reference)&&n<10000;++n)expected+=GB_run(&reference);
        for(unsigned n=0;in_loop(wait)&&n<10000;++n)actual+=GB_run(&wait);
        check(!in_loop(reference)&&!in_loop(wait)&&actual==expected,"native STAT wait changed its exit/IRQ clock boundary");
        check(!std::memcmp(&wait,&reference,offsetof(GB_gameboy_t,llr_reset_seed)),"native STAT wait differs from instruction/hardware oracle");
        GB_free(&reference);GB_free(&wait);
    }
#endif
    check(sha256({})=="e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855","SHA empty");
    std::string abc="abc";check(sha256({reinterpret_cast<const uint8_t*>(abc.data()),abc.size()})=="ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad","SHA abc");
    // A window-register write must stay in I/O, including its CGB side effect.
    // A donor fall-through previously wrote before the HRAM array instead.
    for(auto model:{GB_MODEL_DMG_B,GB_MODEL_CGB_E}) {
        GB_gameboy_t hw{};GB_init(&hw,model);
        std::array<uint8_t,sizeof hw.camera_registers> unrelated{};
        unrelated.fill(0xa5);std::memcpy(hw.camera_registers,unrelated.data(),unrelated.size());
        std::array<uint8_t,sizeof hw.hram> hram{};
        hram.fill(0x5a);std::memcpy(hw.hram,hram.data(),hram.size());
        for(uint8_t value:{0,7,166,255}) {
            GB_write_memory(&hw,0xff4b,value);
            check(hw.io_registers[GB_IO_WX]==value,"WX register write lost");
            check(!std::memcmp(hw.camera_registers,unrelated.data(),unrelated.size()),"WX write changed unrelated cartridge-controller memory");
            check(!std::memcmp(hw.hram,hram.data(),hram.size()),"WX write changed HRAM");
        }
        GB_free(&hw);
    }
    std::vector<uint8_t> rom(32768);rom[0x100]=0xc3;rom[0x101]=0x50;rom[0x102]=1;
    // LCD off, enable APU, pulse channel, increment WRAM, loop.
    std::vector<uint8_t> body={0xf3,0xaf,0xe0,0x40,0x3e,0x80,0xe0,0x26,0x3e,0x77,0xe0,0x24,0x3e,0x11,0xe0,0x25,0x3e,0x80,0xe0,0x11,0x3e,0xf3,0xe0,0x12,0x3e,0x87,0xe0,0x14,0x21,0x00,0xc0,0x34,0x18,0xfd};
    std::copy(body.begin(),body.end(),rom.begin()+0x150);
    Options opts;opts.native=false;
    {
        Machine alu(rom,opts);
        const uint8_t off=0;alu.debug_write(0xff40,{&off,1});
        // Independent byte-result and nibble/full-borrow oracle. Exercise the
        // real decoder and timed register/immediate reads, including underflow.
        for(uint8_t opcode:{0x90,0x98,0xd6,0xde})for(unsigned carry=0;carry<2;++carry)
            for(unsigned a=0;a<256;++a)for(unsigned b=0;b<256;++b) {
                const uint8_t instruction[]={opcode,uint8_t(b)};
                alu.debug_write(0xc000,instruction);
                Registers registers{};registers.af=uint16_t((a<<8)|(carry?0x10:0));
                registers.bc=uint16_t(b<<8);registers.sp=0xd000;registers.pc=0xc000;
                alu.debug_registers(registers);alu.run_until(alu.stats().cycles+1);
                unsigned borrow=(opcode==0x98||opcode==0xde)?carry:0;
                uint8_t result=uint8_t(int(a)-int(b)-int(borrow));
                unsigned flags=0x40|(result==0?0x80:0)|((a%16)<(b%16)+borrow?0x20:0)|(a<b+borrow?0x10:0);
                check(alu.debug_registers().af==uint16_t((unsigned(result)<<8)|flags),"SUB/SBC result or borrow flags differ from byte arithmetic");
            }
    }
    Machine a(rom,opts),b(rom,opts);
    check(a.digest()==b.digest(),"independent initialization differs");
    for(int i=0;i<12;++i){a.run_frame(uint8_t(i));b.run_frame(uint8_t(i));}
    check(a.digest()==b.digest(),"interleaved machines diverged");
    auto point=a.snapshot();a.take_audio();b.take_audio();
    for(int i=0;i<8;++i)a.run_frame(0x10);
    auto expected=a.digest();auto pcm=a.take_audio();
    std::string error;check(a.restore(point,error),error.c_str());
    for(int i=0;i<8;++i)a.run_frame(0x10);
    check(a.digest()==expected,"restore replay state differs");
    check(a.take_audio()==pcm,"restore replay PCM differs");
    auto current=a.digest();point.back()^=1;
    check(!a.restore(point,error),"corrupt snapshot accepted");
    check(a.digest()==current,"failed load changed live state");
    check(!a.restore(std::span<const uint8_t>(point).first(25),error),"truncated snapshot accepted");
    check(a.digest()==current,"truncated load changed live state");
    // A valid checksum is not allocation validation. A forged native-prefix
    // VRAM length plus a matching, enlarged payload must not write beyond the
    // separately allocated machine's real VRAM buffer.
    auto forged=a.snapshot();
    constexpr size_t core_length_at=4+64*3+8+8+1;
    constexpr size_t core_at=core_length_at+4;
    auto get32=[&](size_t offset) {uint32_t n;std::memcpy(&n,forged.data()+offset,4);return n;};
    auto put32=[&](size_t offset,uint32_t n) {std::memcpy(forged.data()+offset,&n,4);};
    auto core_length=get32(core_length_at);
    check(core_length==offsetof(GB_gameboy_t,llr_reset_seed),"snapshot regression-test donor layout mismatch");
    auto ram_length_at=core_at+core_length;
    auto vram_length_at=ram_length_at+4+get32(ram_length_at);
    auto old_vram=get32(vram_length_at);constexpr uint32_t forged_vram=65536;
    check(old_vram<forged_vram,"snapshot regression-test VRAM size");
    put32(core_at+offsetof(GB_gameboy_t,vram_size),forged_vram);
    put32(vram_length_at,forged_vram);
    forged.insert(forged.begin()+vram_length_at+4+old_vram,forged_vram-old_vram,0);
    auto checksum=sha256(std::span<const uint8_t>(forged).first(forged.size()-64));
    std::copy(checksum.begin(),checksum.end(),forged.end()-64);
    check(!a.restore(forged,error),"forged VRAM allocation length accepted");
    check(a.digest()==current,"forged VRAM load changed live state");
    // Checksum-valid malformed PPU queue indices must reject before the
    // donor can index its fixed-size FIFO during subsequent execution.
    for(size_t fifo:{offsetof(GB_gameboy_t,bg_fifo),offsetof(GB_gameboy_t,oam_fifo)})
        for(auto [field,value]:{std::pair{offsetof(GB_fifo_t,read_end),uint8_t(GB_FIFO_LENGTH)},
                               std::pair{offsetof(GB_fifo_t,size),uint8_t(GB_FIFO_LENGTH+1)},
                               std::pair{offsetof(GB_fifo_t,fifo)+offsetof(GB_fifo_item_t,pixel),uint8_t(4)},
                               std::pair{offsetof(GB_fifo_t,fifo)+offsetof(GB_fifo_item_t,palette),uint8_t(8)},
                               std::pair{offsetof(GB_fifo_t,fifo)+offsetof(GB_fifo_item_t,bg_priority),uint8_t(2)}}) {
            auto invalid=a.snapshot();invalid[core_at+fifo+field]=value;
            auto hash=sha256(std::span(invalid).first(invalid.size()-64));
            std::copy(hash.begin(),hash.end(),invalid.end()-64);
            check(!a.restore(invalid,error),"checksum-valid invalid PPU FIFO accepted");
            check(a.digest()==current,"invalid PPU FIFO load changed live state");
        }
    for(auto [offset,value]:{std::pair{offsetof(GB_gameboy_t,n_visible_objs),uint8_t(11)},
                             std::pair{offsetof(GB_gameboy_t,orig_n_visible_objs),uint8_t(11)},
                             std::pair{offsetof(GB_gameboy_t,visible_objs),uint8_t(40)}}) {
        auto invalid=a.snapshot();invalid[core_at+offset]=value;
        auto hash=sha256(std::span(invalid).first(invalid.size()-64));
        std::copy(hash.begin(),hash.end(),invalid.end()-64);
        check(!a.restore(invalid,error),"checksum-valid invalid PPU object bounds accepted");
        check(a.digest()==current,"invalid PPU object load changed live state");
    }
    {
        auto invalid=a.snapshot();size_t extra=core_at+core_length;
        for(unsigned i=0;i<3;++i){uint32_t size;std::memcpy(&size,invalid.data()+extra,4);extra+=4+size;}
        extra+=offsetof(GB_apu_output_t,sample_callback);
        invalid[extra]=2; // Individually encoded sweep continuation boolean.
        auto hash=sha256(std::span(invalid).first(invalid.size()-64));
        std::copy(hash.begin(),hash.end(),invalid.end()-64);
        check(!a.restore(invalid,error),"noncanonical continuation boolean accepted");
        check(a.digest()==current,"invalid continuation boolean changed live state");
        invalid=a.snapshot();
        const size_t pin=extra+sizeof(bool)+sizeof(unsigned)+4*sizeof(bool)+sizeof(uint8_t)+sizeof(unsigned)+sizeof(uint8_t);
        invalid.at(pin)=2;hash=sha256(std::span(invalid).first(invalid.size()-64));
        std::copy(hash.begin(),hash.end(),invalid.end()-64);
        check(!a.restore(invalid,error),"noncanonical serial SO latch accepted");
        check(a.digest()==current,"invalid SO latch changed live state");
    }
    check(a.stats().cycles>=140448*20,"LCD-off cycle progress failed");
    auto observed=a.digest();
    auto ram=a.debug_read(0xc000,0x2000);
    check(ram.size()==0x2000&&a.digest()==observed,"debug RAM inspection changed guest state");
    const uint8_t cheat[]={0x42,0x99};a.debug_write(0xc010,cheat);
    check(a.debug_read(0xc010,2)==std::vector<uint8_t>({0x42,0x99}),"guest RAM cheat write failed");
    auto registers=a.debug_registers();registers.pc=0x150;a.debug_registers(registers);
    check(a.stats().pc==0x150,"QA PC placement failed");
    bool rejected=false;try {a.debug_read(0xffff,2);}catch(const std::exception&){rejected=true;}
    check(rejected,"out-of-range debug read accepted");
    rejected=false;try {a.debug_write(0xffff,cheat);}catch(const std::exception&){rejected=true;}
    check(rejected,"out-of-range debug write accepted");
    a.run_until(a.stats().cycles+1000000);
    auto stepped_ticks=a.stats().cycles;a.run_frame();
    check(a.stats().cycles>stepped_ticks,"frame stalled after debugger stepping");
    rom[0x147]=0x10;rom[0x149]=3; // MBC3 + RTC + battery, 32 KiB RAM
    Machine rtc_a(rom,opts),rtc_b(rom,opts);
    auto battery=rtc_a.battery();
    check(battery.size()==32768+48,"MBC3 battery length");
    battery[5]=0x99;battery[32768]=17;battery[32768+4]=31;
    check(rtc_a.load_battery(battery,error),error.c_str());
    check(rtc_b.load_battery(battery,error),error.c_str());
    check(rtc_a.digest()==rtc_b.digest(),"battery import depends on wall clock or global state");
    auto saved=rtc_a.battery();check(saved[5]==0x99&&saved[32768]==17,"battery RAM/RTC roundtrip");
    auto rtc_before=rtc_a.digest();
    check(!rtc_a.load_battery(std::span<const uint8_t>(battery).first(battery.size()-1),error),"invalid RTC suffix accepted");
    check(rtc_a.digest()==rtc_before,"failed battery import changed guest");
    check(rtc_a.load_battery(std::span<const uint8_t>(battery).first(32768),error),"raw SRAM import rejected");
    check(rtc_a.load_battery(std::span<const uint8_t>(battery).first(32768+44),error),"VBA32 RTC import rejected");
    // The ten-title set also uses plain MBC1/MBC5 battery RAM. Exercise their
    // real bus enable/write and native byte formats, separate from MBC3 RTC.
    for(uint8_t mapper:{0x03,0x1b}) {
        rom[0x147]=mapper;Machine saved_machine(rom,opts),loaded_machine(rom,opts);
        const uint8_t enable=0x0a;const uint8_t progress[]={0x19,0x82,0x00,0xff};
        saved_machine.debug_write(0x0000,{&enable,1});saved_machine.debug_write(0xa010,progress);
        auto native=saved_machine.battery();check(native.size()==32768,"plain battery RAM size");
        check(std::equal(std::begin(progress),std::end(progress),native.begin()+0x10),"bus progress missing from native save");
        check(loaded_machine.load_battery(native,error),error.c_str());
        loaded_machine.debug_write(0x0000,{&enable,1});
        check(loaded_machine.debug_read(0xa010,sizeof progress)==std::vector<uint8_t>(std::begin(progress),std::end(progress)),"fresh plain battery progress differs");
        auto before=loaded_machine.snapshot();auto invalid=native;invalid.push_back(0);
        check(!loaded_machine.load_battery(invalid,error),"oversized plain battery accepted");
        check(!loaded_machine.load_battery(std::span(native).first(native.size()-1),error),"short plain battery accepted");
        check(loaded_machine.snapshot()==before&&loaded_machine.battery()==native,"failed plain battery import changed progress");
    }
    std::cout<<"machine isolation, state, PCM replay, invalid-load atomicity and cycle cap passed\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
