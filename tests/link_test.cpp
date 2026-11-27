#include <gbcllr/machine.hpp>
#define GB_INTERNAL
#define GB_DISABLE_DEBUGGER
#define GB_DISABLE_CHEATS
#define GB_DISABLE_CHEAT_SEARCH
#define GB_DISABLE_REWIND
#define GB_DISABLE_TIMEKEEPING
#include "../third_party/sameboy/Core/gb.h"
#include <algorithm>
#include <array>
#include <iostream>
#include <memory>
#include <stdexcept>

using namespace gbcllr;
static void check(bool pass,const char* message) {if(!pass)throw std::runtime_error(message);}
static void serial_callback_guards() {
    auto gb=std::make_unique<GB_gameboy_t>();
    gb->llr_tick_base=100;gb->cycles_since_run=7;
    // Both entry points must accept an unbound port, including active SC.
    GB_serial_state_changed(gb.get());
    GB_serial_observation(gb.get());
    gb->io_registers[GB_IO_SC]=0x80;
    GB_serial_observation(gb.get());
    struct Log {unsigned count=0,event=0;uint64_t tick=0;} log;
    gb->llr_cycle_context=&log;
    gb->llr_cycle_callback=[](void* context,uint64_t tick,unsigned event) {
        auto& row=*static_cast<Log*>(context);++row.count;row.tick=tick;row.event=event;
    };
    gb->io_registers[GB_IO_SC]=0;
    GB_serial_observation(gb.get());
    check(!log.count,"inactive SC observation called the cycle owner");
    GB_serial_state_changed(gb.get());
#if GBCLLR_CABLE_ACTIVE_HORIZONS
    check(log.count==1&&log.tick==107&&log.event==6,"state-change callback lost its timestamp/event");
#else
    check(!log.count,"ordinary serial profile called the horizon owner");
#endif
    gb->io_registers[GB_IO_SC]=0x80;GB_serial_observation(gb.get());
#if GBCLLR_CABLE_ACTIVE_HORIZONS
    check(log.count==2&&log.tick==107&&log.event==5,"active observation lost its timestamp/event");
#if GBCLLR_HEADROOM_COUNTERS
    check(gb->llr_headroom.cycle_callback_calls==2,"callback counter escaped its guard");
#else
    check(!gb->llr_headroom.cycle_callback_calls,"disabled counters were updated");
#endif
#else
    check(!log.count&&!gb->llr_headroom.cycle_callback_calls,"ordinary serial profile counted a callback");
#endif
    gb->llr_cycle_callback=nullptr;
    GB_serial_state_changed(gb.get());GB_serial_observation(gb.get());
}
static std::vector<uint8_t> program(bool speed_switch=false) {
    std::vector<uint8_t> rom(32768);
    rom[0x100]=0xc3;rom[0x101]=0x50;rom[0x102]=1;rom[0x143]=0x80;
    const uint8_t loop[]={0xf3,0xaf,0xe0,0x40,0x18,0xfe};
    std::copy(std::begin(loop),std::end(loop),rom.begin()+0x150);
    if(speed_switch) {
        const uint8_t body[]={0xf3,0xaf,0xe0,0x40,0x3e,1,0xe0,0x4d,0x10,0,0x18,0xfe};
        std::copy(std::begin(body),std::end(body),rom.begin()+0x150);
    }
    return rom;
}
static void poke(Machine& m,uint16_t address,uint8_t value) {m.debug_write(address,{&value,1});}
static uint8_t peek(Machine& m,uint16_t address) {return m.debug_read(address,1)[0];}
struct Exchange {
    std::array<uint64_t,8> ticks{};
    std::array<bool,8> bits{};
    std::array<uint64_t,8> rise_ticks{};
    std::array<bool,8> rise_bits{};
    unsigned rises=0;
    unsigned count=0;
    uint8_t input=0x3c;
    bool overflow=false;
    static bool edge(void* context,uint64_t tick,bool bit) noexcept {
        auto& e=*static_cast<Exchange*>(context);
        if(e.count>=8){e.overflow=true;return true;}
        unsigned i=e.count++;e.ticks[i]=tick;e.bits[i]=bit;
        return bool(e.input&(0x80>>i));
    }
    static void rise(void* context,uint64_t tick,bool bit) noexcept {
        auto& e=*static_cast<Exchange*>(context);
        if(e.rises>=8){e.overflow=true;return;}
        e.rise_ticks[e.rises]=tick;e.rise_bits[e.rises++]=bit;
    }
};
static void output_latch() {
    Options o;o.native=false;Machine m(program(),o);
    check(!m.serial_pin(),"reset serial SO latch is not low");
    poke(m,0xff01,0xa5);poke(m,0xff02,0x80);
    check(!m.serial_pin(),"SB/SC write changed the SO latch without a rising edge");
    for(unsigned i=0;i<8;++i) {
        auto bit=m.serial_clock_edge(true);check(bit.accepted&&bit.data_out==bool(0xa5&(0x80>>i)),"rising edge did not latch SO");
        check(m.serial_port().shifted_bits==i,"rising edge shifted serial data");
        auto pin=m.serial_pin();m.serial_clock_edge(false,false);check(m.serial_pin()==pin,"falling edge changed latched SO");
    }
    check(m.serial_pin()&&!m.serial_port().active,"completion did not preserve the last SO bit");
    auto saved=m.snapshot();poke(m,0xff01,0);check(m.serial_pin(),"idle SB write changed SO");
    std::string error;check(m.restore(saved,error)&&m.serial_pin(),"snapshot lost the SO latch");
}
static void external_clock() {
    Options o;o.native=false;Machine m(program(),o);
    poke(m,0xff0f,0);poke(m,0xff01,0xa5);poke(m,0xff02,0x80);
    auto state=m.serial_port();check(state.active&&!state.internal_clock&&state.shifted_bits==0,"external port setup");
    std::vector<uint8_t> checkpoint;std::string error;
    for(unsigned i=0;i<8;++i) {
        auto bit=m.serial_clock(0x3c&(0x80>>i));
        check(bit.accepted&&bit.data_out==bool(0xa5&(0x80>>i)),"external outgoing bit");
        if(i<7)check(m.serial_port().active&&!(peek(m,0xff0f)&8),"external completion before eighth bit");
        if(i==2)checkpoint=m.snapshot();
    }
    check(!m.serial_port().active&&peek(m,0xff01)==0x3c&&(peek(m,0xff0f)&8),"external byte/IRQ completion");
    auto expected=m.snapshot();
    check(!m.serial_clock(false).accepted&&m.snapshot()==expected,"inactive port consumed clock");
    check(m.restore(checkpoint,error),error.c_str());
    for(unsigned i=3;i<8;++i)m.serial_clock(0x3c&(0x80>>i));
    check(m.snapshot()==expected,"external in-flight restore lost serial shift state");
    poke(m,0xff02,0x81);auto before=m.snapshot();
    check(!m.serial_clock(false).accepted&&m.snapshot()==before,"external edge modified internal-clock port");
}
static void internal_clock(Model model,bool fast,bool double_speed) {
    Options o;o.native=false;o.model=model;
    Machine m(program(double_speed),o);
    // Execute a real KEY1/STOP transition, including its halted interval.
    if(double_speed){m.run_until(300000);check(peek(m,0xff4d)&0x80,"CGB did not enter double speed");}
    Exchange e;m.link_callbacks({&e,Exchange::edge,nullptr,Exchange::rise});
    poke(m,0xff0f,0);poke(m,0xff01,0xa5);poke(m,0xff02,fast?0x83:0x81);
    const auto start=m.stats().cycles;
    while(e.count<3&&m.stats().cycles<start+12000)m.run_until(m.stats().cycles+8);
    check(e.count==3,"internal clock did not reach third bit");
    auto checkpoint=m.snapshot();auto checkpoint_rises=e.rises;m.take_audio();
    m.run_until(start+12000);auto expected=m.snapshot();auto pcm=m.take_audio();
    check(e.count==8&&!e.overflow&&!m.serial_port().active,"internal serial completion");
    check(peek(m,0xff01)==0x3c&&(peek(m,0xff0f)&8),"internal sampled byte/IRQ");
    const auto expected_ticks=e.ticks;auto expected_bits=e.bits;
    const auto expected_rise_ticks=e.rise_ticks;auto expected_rise_bits=e.rise_bits;
    uint64_t period=fast?32:1024;if(double_speed)period/=2;
    for(unsigned i=0;i<8;++i) {
        check(e.bits[i]==bool(0xa5&(0x80>>i)),"internal outgoing bit order");
        check(e.rises==8&&e.rise_bits[i]==e.bits[i]&&e.ticks[i]-e.rise_ticks[i]==period/2,"SO rising/falling half-cycle phase differs");
        if(i)check(e.ticks[i]-e.ticks[i-1]==period,"serial edge timestamp period");
    }
    std::string error;check(m.restore(checkpoint,error),error.c_str());
    // The future paired snapshot owns peer/wire state. This raw machine
    // snapshot deliberately does not rewind the host callback context.
    e.count=3;e.rises=checkpoint_rises;
    m.run_until(start+12000);
    check(e.count==8&&e.ticks==expected_ticks&&e.bits==expected_bits,"restored binding/timestamp continuation");
    check(e.rise_ticks==expected_rise_ticks&&e.rise_bits==expected_rise_bits,"restored SO phase differs");
    check(m.snapshot()==expected&&m.take_audio()==pcm,"internal in-flight state/PCM replay");
    m.link_callbacks({});
    poke(m,0xff01,0);poke(m,0xff02,fast?0x83:0x81);
    m.run_until(m.stats().cycles+12000);
    check(peek(m,0xff01)==0xff,"unattached serial input is not high");
}
struct Infrared {
    std::array<uint64_t,4> ticks{};
    std::array<bool,4> values{};
    unsigned count=0;
    static void changed(void* context,uint64_t tick,bool on) noexcept {
        auto& ir=*static_cast<Infrared*>(context);
        if(ir.count<4){ir.ticks[ir.count]=tick;ir.values[ir.count++]=on;}
    }
};
static void infrared() {
    Options o;o.model=Model::cgb;o.native=false;Machine m(program(),o);
    m.run_until(64);Infrared ir;m.link_callbacks({&ir,nullptr,Infrared::changed});
    auto at=m.stats().cycles;poke(m,0xff56,0xc1);
    check(ir.count==1&&ir.ticks[0]==at&&ir.values[0],"IR output edge timestamp");
    poke(m,0xff56,0xc1);check(ir.count==1,"unchanged IR output emitted edge");
    poke(m,0xff56,0xc0);check(ir.count==2&&!ir.values[1],"IR off edge");
    m.infrared_input(false);m.run_until(at+50000);
    check(peek(m,0xff56)&2,"dark IR sensor reads light");
    m.infrared_input(true);auto checkpoint=m.snapshot();m.take_audio();
    auto deadline=m.stats().cycles+1000;m.run_until(deadline);
    check(!(peek(m,0xff56)&2),"IR light did not reach hardware sensor");
    auto expected=m.snapshot();auto pcm=m.take_audio();std::string error;
    check(m.restore(checkpoint,error),error.c_str());m.run_until(deadline);
    check(m.snapshot()==expected&&m.take_audio()==pcm,"IR sensor in-flight replay");
    at=m.stats().cycles;poke(m,0xff56,0xc1);
    check(ir.count==3&&ir.ticks[2]==at&&ir.values[2],"IR binding lost on restore");
    m.infrared_input(false);poke(m,0xff56,0xc0);m.run_until(m.stats().cycles+50000);
    check(peek(m,0xff56)&2,"IR sensor did not decay to dark");
    auto rom=program();
    const uint8_t output[]={0xf3,0xaf,0xe0,0x40,0x3e,0xc1,0xe0,0x56,0x3e,0xc0,0xe0,0x56,0x18,0xfe};
    std::copy(std::begin(output),std::end(output),rom.begin()+0x150);
    Machine cpu(rom,o);Infrared emitted;
    cpu.link_callbacks({&emitted,nullptr,Infrared::changed});cpu.run_until(1000);
    check(emitted.count==2&&emitted.values[0]&&!emitted.values[1],"CPU-driven IR emission");
    check(emitted.ticks[1]-emitted.ticks[0]==40,"CPU-driven IR edge timing");
}
static void native_probe(const char* rom_path,const char* boot_path) {
    auto rom=read_file(rom_path);Options o;o.boot_rom=read_file(boot_path);
    o.model=o.boot_rom.size()==2304?Model::cgb:Model::dmg;
    Machine native(rom,o);o.native=false;Machine reference(rom,o);
    Exchange n,r;native.link_callbacks({&n,Exchange::edge,nullptr});
    reference.link_callbacks({&r,Exchange::edge,nullptr});
    for(unsigned frame=0;frame<1200;++frame) {
        if(frame==600)for(auto* m:{&native,&reference}) {
            poke(*m,0xff01,0xa5);poke(*m,0xff02,0x81);
        }
        native.run_frame();reference.run_frame();
        check(native.snapshot()==reference.snapshot(),"bound-port native/reference state divergence");
        check(native.take_audio()==reference.take_audio(),"bound-port native/reference PCM divergence");
        check(n.count==r.count&&n.ticks==r.ticks&&n.bits==r.bits&&n.overflow==r.overflow,
              "bound-port native/reference callback divergence");
    }
    check(native.stats().instructions_native&&n.count==8,"bound-port probe did not exercise generated code and a byte");
    std::cout<<"native/reference bound-port state, PCM and edge timestamps passed; native="
             <<native.stats().instructions_native<<" chained="<<native.stats().instructions_chained<<'\n';
}
int main(int argc,char** argv){try {
    serial_callback_guards();
    external_clock();
    output_latch();
    internal_clock(Model::dmg,false,false);
    internal_clock(Model::cgb,false,false);
    internal_clock(Model::cgb,true,false);
    internal_clock(Model::cgb,false,true);
    internal_clock(Model::cgb,true,true);
    // IR qualification is outside the owner's current scope.
    if(argc==2)check(std::string(argv[1])=="--serial-only","usage: link-test [--serial-only | ROM boot-ROM]");
    else if(argc!=1) {
        check(argc==3,"usage: link-test [ROM boot-ROM] (optional native/reference callback probe)");
        native_probe(argv[1],argv[2]);
    }
    std::cout<<"serial bit/byte/IRQ, DMG/CGB normal/fast/double-speed timestamps and in-flight replay passed\n";
    return 0;
} catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
