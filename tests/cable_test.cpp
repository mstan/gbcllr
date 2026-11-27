#include <gbcllr/cable.hpp>
#define GB_INTERNAL
#define GB_DISABLE_DEBUGGER
#define GB_DISABLE_CHEATS
#define GB_DISABLE_CHEAT_SEARCH
#define GB_DISABLE_REWIND
#define GB_DISABLE_TIMEKEEPING
#include "../third_party/sameboy/Core/gb.h"
#include <algorithm>
#include <cstring>
#include <iostream>
#include <stdexcept>

using namespace gbcllr;
static void check(bool p,const char* message) {if(!p)throw std::runtime_error(message);}
static std::vector<uint8_t> program(bool alternate=false,bool speed=false) {
    std::vector<uint8_t> rom(32768);
    rom[0x100]=0xc3;rom[0x101]=0x50;rom[0x102]=1;rom[0x143]=0x80;
    std::vector<uint8_t> body{0xf3}; // DI; leave LCD/APU running.
    if(speed)body.insert(body.end(),{0x3e,1,0xe0,0x4d,0x10,0});
    if(alternate)body.insert(body.end(),{0xfa,0x00,0xc0,0x18,0xfb}); // 16+12 T
    else body.insert(body.end(),{0x18,0xfe}); // 12 T
    std::copy(body.begin(),body.end(),rom.begin()+0x150);return rom;
}
static void poke(Machine& m,uint16_t a,uint8_t v){m.debug_write(a,{&v,1});}
static uint8_t peek(Machine& m,uint16_t a){return m.debug_read(a,1)[0];}
static uint64_t presentation_ticks(const Machine& m) {
    auto state=m.snapshot();constexpr size_t core=4+64*3+8+8+1+4;
    uint32_t whole=0;
    std::memcpy(&whole,state.data()+core+offsetof(GB_gameboy_t,cycles_since_vblank_callback),sizeof whole);
    return uint64_t(whole)*2+state[core+offsetof(GB_gameboy_t,vblank_half_tick)];
}
static bool same(const std::vector<uint8_t>& a,const std::vector<uint8_t>& b) {
    if(a==b)return true;
    unsigned count=0;
    for(size_t i=0;i<std::min(a.size(),b.size())&&count<20;++i)if(a[i]!=b[i]) {
        std::cerr<<"state difference offset="<<i<<" split="<<unsigned(a[i])<<" baseline="<<unsigned(b[i])<<'\n';++count;
    }
    return false;
}
static void lcd_off_clock(Model model,bool speed) {
    Options o;o.native=false;o.model=model;auto rom=program(false,speed);
    Machine standalone(rom,o);CablePair pair(rom,o,rom,o);
    poke(standalone,0xff40,0);
    for(unsigned i=0;i<2;++i)poke(pair.machine(i),0xff40,0);
    pair.run_until(150000);standalone.run_until(pair.ticks());
    // A run can overshoot its deadline at a CPU instruction boundary. Check
    // each clock against its actual elapsed time, including the speed switch.
    auto expected=pair.ticks()%140448;
    check(presentation_ticks(standalone)==standalone.stats().cycles%140448,"standalone LCD-off presentation period drift");
    for(unsigned i=0;i<2;++i)
        check(presentation_ticks(pair.machine(i))==expected,"paired LCD-off presentation period drift");
}
static void split_equivalence() {
    Options o;o.native=false;
    for(auto model:{Model::dmg,Model::cgb}) {
        o.model=model;auto a=program(),b=program(true);
        CablePair pair(a,o,b,o);Machine left(a,o),right(b,o);
        for(uint64_t deadline:{2000u,4000u,6000u,150000u}) {
            pair.run_until(deadline);left.run_until(pair.ticks());right.run_until(pair.ticks());
            check(same(pair.machine(0).snapshot(),left.snapshot()),"cycle-split left state differs from unbatched baseline");
            check(pair.machine(1).snapshot()==right.snapshot(),"cycle-split right state differs from unbatched baseline");
            check(pair.machine(0).take_audio()==left.take_audio(),"cycle-split left PCM differs");
            check(pair.machine(1).take_audio()==right.take_audio(),"cycle-split right PCM differs");
        }
    }
}
static void wire(Model model,bool fast,bool double_master,bool double_slave,unsigned master) {
    Options o;o.native=false;o.model=model;
    auto a=program(false,master?double_slave:double_master);
    auto b=program(true,master?double_master:double_slave);
    CablePair pair(a,o,b,o);
    if(double_master||double_slave) {
        // The existing single-console contract qualifies KEY1/STOP itself.
        // Prepare the actual speed transition offline, then qualify this
        // cable's clocking and in-flight replay at that established speed.
        Machine warm_left(a,o),warm_right(b,o);
        warm_left.run_until(300000);warm_right.run_until(warm_left.stats().cycles);
        unsigned limit=1000;
        while(warm_left.stats().cycles!=warm_right.stats().cycles&&limit--) {
            auto target=std::max(warm_left.stats().cycles,warm_right.stats().cycles);
            warm_left.run_until(target);warm_right.run_until(target);
        }
        check(warm_left.stats().cycles==warm_right.stats().cycles,"speed preparation has no common boundary");
        std::string error;
        check(pair.machine(0).restore(warm_left.snapshot(),error),error.c_str());
        check(pair.machine(1).restore(warm_right.snapshot(),error),error.c_str());
    }
    pair.run_until(pair.ticks()+128);
    auto& m=pair.machine(master);auto& s=pair.machine(master^1);
    if(model==Model::cgb) {
        check(bool(peek(m,0xff4d)&0x80)==double_master,"master KEY1 transition");
        check(bool(peek(s,0xff4d)&0x80)==double_slave,"slave KEY1 transition");
    }
    poke(m,0xff0f,0);poke(s,0xff0f,0);
    poke(m,0xff01,0xa5);poke(s,0xff01,0x3c);
    poke(s,0xff02,0x80);poke(m,0xff02,fast?0x83:0x81);
    auto start=pair.ticks();auto period=(fast?32u:1024u)/(double_master?2:1);
    // Fast edges can be within an instruction. Paired checkpoints are at a
    // common instruction boundary, so stop early enough to retain an in-flight
    // byte even if reaching that boundary crosses several fast edges.
    if(!fast) {
        pair.run_until(start+3*period);
        check(m.serial_port().shifted_bits==3&&s.serial_port().shifted_bits==3,"paired third-bit alignment");
        check(!(peek(m,0xff0f)&8)&&!(peek(s,0xff0f)&8),"early cable IRQ");
    } else pair.run_until(start+period);
    check(m.serial_port().active&&s.serial_port().active,"mid-byte checkpoint already complete");
    auto checkpoint=pair.snapshot();m.take_audio();s.take_audio();
    auto deadline=start+10*period+512;
    pair.run_until(deadline);
    check(peek(m,0xff01)==0x3c&&peek(s,0xff01)==0xa5,"reciprocal cable byte mismatch");
    check(!m.serial_port().active&&!s.serial_port().active,"cable byte did not finish");
    check((peek(m,0xff0f)&8)&&(peek(s,0xff0f)&8),"eighth-bit cable IRQ missing");
    auto expected=pair.snapshot();
    std::array<std::vector<int16_t>,2> pcm{pair.machine(0).take_audio(),pair.machine(1).take_audio()};
    std::string error;check(pair.restore(checkpoint,error),error.c_str());pair.run_until(deadline);
    check(pair.snapshot()==expected,"paired in-flight replay mismatch");
    for(unsigned i=0;i<2;++i)check(pair.machine(i).take_audio()==pcm[i],"paired in-flight PCM mismatch");
    auto bad=checkpoint;bad.back()^=1;
    check(!pair.restore(bad,error)&&pair.snapshot()==expected,"failed paired restore changed a console");
    std::cout<<"wire model="<<(model==Model::dmg?"dmg":"cgb")<<" fast="<<fast
             <<" master="<<master<<" double="<<double_master<<'/'<<double_slave<<" passed\n";
}
static void native_probe(const char* rom_path,const char* boot_path,const char* state_path) {
    auto rom=read_file(rom_path),state=read_file(state_path);Options n;n.boot_rom=read_file(boot_path);
    n.model=n.boot_rom.size()==2304?Model::cgb:Model::dmg;auto r=n;r.native=false;
    CablePair native(rom,n,rom,n),reference(rom,r,rom,r);
    std::string error;
    for(auto* p:{&native,&reference})for(unsigned i=0;i<2;++i) {
        check(p->machine(i).restore(state,error),error.c_str());
    }
    Machine floor(rom,r);check(floor.restore(state,error),error.c_str());
    auto deadline=floor.stats().cycles+140448;
    native.run_until(deadline);reference.run_until(deadline);floor.run_until(native.ticks());
    auto floor_state=floor.snapshot();auto floor_pcm=floor.take_audio();
    for(auto* p:{&native,&reference})for(unsigned i=0;i<2;++i) {
        check(same(p->machine(i).snapshot(),floor_state),"real-ROM cycle slicing differs from original hardware path");
        check(p->machine(i).take_audio()==floor_pcm,"real-ROM cycle slicing PCM differs from original hardware path");
        poke(p->machine(i),0xff01,i?0x3c:0xa5);poke(p->machine(i),0xff02,i?0x80:0x81);
    }
    for(unsigned f=0;f<4;++f) {
        native.run_frame();reference.run_frame();
        check(native.snapshot()==reference.snapshot(),"paired native/reference state divergence");
        for(unsigned i=0;i<2;++i)
            check(native.machine(i).take_audio()==reference.machine(i).take_audio(),"paired native/reference PCM divergence");
    }
    check(native.machine(0).stats().instructions_native&&native.machine(1).stats().instructions_native,"no generated paired execution");
    std::cout<<"paired real-ROM native/reference state/PCM passed; native="<<native.machine(0).stats().instructions_native
             <<'/'<<native.machine(1).stats().instructions_native<<" digest="<<sha256(native.snapshot())<<'\n';
}
static void native_cycle_probe(const char* rom_path,const char* boot_path,const char* state_path) {
    auto rom=read_file(rom_path),state=read_file(state_path);Options n;n.boot_rom=read_file(boot_path);
    n.model=n.boot_rom.size()==2304?Model::cgb:Model::dmg;auto r=n;r.native=false;
    CablePair native(rom,n,rom,n,CableSchedule::cycle_continuations),reference(rom,r,rom,r,CableSchedule::cycle_continuations);
    std::string error;
    for(auto* pair:{&native,&reference}) {
        for(unsigned i=0;i<2;++i)check(pair->machine(i).restore(state,error),error.c_str());
        poke(pair->machine(0),0xff01,0xa5);poke(pair->machine(1),0xff01,0x3c);
        poke(pair->machine(1),0xff02,0x80);poke(pair->machine(0),0xff02,0x81);
    }
    for(unsigned frame=0;frame<8;++frame) {
        uint8_t left=frame==1?1:frame==2?2:0,right=frame==1?4:frame==2?8:0;
        native.run_frame(left,right);reference.run_frame(left,right);
        check(native.snapshot()==reference.snapshot(),"real-ROM cycle native/reference checkpoint differs");
        for(unsigned i=0;i<2;++i)check(native.machine(i).take_audio()==reference.machine(i).take_audio(),"real-ROM cycle native/reference PCM differs");
        if(frame==3) {
            auto start=native.snapshot();
            for(auto* pair:{&native,&reference}) {pair->machine(0).set_buttons(4);pair->machine(1).set_buttons(8);}
            auto checkpoint=native.snapshot();check(checkpoint==reference.snapshot(),"real input journal differs");
            auto end=native.ticks()+1112;native.run_until(end);reference.run_until(end);
            auto expected=native.snapshot();check(expected==reference.snapshot(),"real-ROM cycle sub-frame differs");
            std::array<std::vector<int16_t>,2> pcm{native.machine(0).take_audio(),native.machine(1).take_audio()};
            for(unsigned i=0;i<2;++i)check(reference.machine(i).take_audio()==pcm[i],"real-ROM cycle sub-frame PCM differs");
            for(auto* pair:{&native,&reference}) {
                check(pair->restore(checkpoint,error),error.c_str());pair->run_until(end);
                check(pair->snapshot()==expected,"real-ROM cycle replay state differs");
                for(unsigned i=0;i<2;++i)check(pair->machine(i).take_audio()==pcm[i],"real-ROM cycle replay PCM differs");
                check(pair->restore(start,error),error.c_str());
            }
        }
    }
    check(native.machine(0).stats().instructions_native&&native.machine(1).stats().instructions_native,"no real native cycle execution");
    std::cout<<"real-ROM cycle native/reference state, input journal, sub-frame restore and PCM passed\n";
}
static void unsupported_phases() {
    Options o;o.native=false;
    auto a=program(),b=program();
    b[0x151]=0;b[0x152]=0x18;b[0x153]=0xfe; // NOP offsets the permanent JR loop.
    CablePair pair(a,o,b,o);auto initial=pair.snapshot();
    bool rejected=false;
    try {pair.run_until(128);}catch(const std::exception& e) {
        rejected=std::string(e.what())=="no common cable instruction boundary within one extra frame";
    }
    check(rejected,"disjoint continuation phases were silently accepted");
    std::string error;check(pair.restore(initial,error),error.c_str());
    check(pair.snapshot()==initial,"paired recovery after unsupported phases differs");
    std::cout<<"disjoint instruction phases explicitly rejected; checkpoint recovery passed\n";
}
static void paused_edge() {
    for(auto model:{Model::dmg,Model::cgb})for(unsigned master=0;master<2;++master)
    for(uint16_t address:{uint16_t(0xff02),uint16_t(0xff04)}) {
        Options o;o.native=false;o.model=model;auto rom=program();CablePair pair(rom,o,rom,o);
        auto& m=pair.machine(master);auto& s=pair.machine(master^1);
        poke(m,0xff0f,0);poke(s,0xff0f,0);
        poke(m,0xff01,0xa5);poke(s,0xff01,0x3c);
        poke(s,0xff02,0x80);poke(m,0xff02,0x81);pair.run_until(900);
        // SC or DIV forces a falling edge while the internal clock is high.
        // Independently specified pre-shift bytes require 4a/79 after
        // exactly one reciprocal bit, with no early completion interrupt.
        check(m.serial_port().shifted_bits==0&&s.serial_port().shifted_bits==0,"paused edge preparation already shifted");
        auto initial=pair.snapshot();m.take_audio();s.take_audio();
        {
            poke(m,address,address==0xff02?0x81:0);
            if(peek(m,0xff01)!=0x4a||peek(s,0xff01)!=0x79)
                std::cerr<<"paused edge model="<<int(model)<<" master="<<master<<" address="<<address
                    <<" bytes="<<unsigned(peek(m,0xff01))<<'/'<<unsigned(peek(s,0xff01))<<'\n';
            check(peek(m,0xff01)==0x4a&&peek(s,0xff01)==0x79,"paused edge did not preserve pre-shift bits");
            check(m.serial_port().shifted_bits==1&&s.serial_port().shifted_bits==1,"paused edge did not clock both consoles once");
            check(!(peek(m,0xff0f)&8)&&!(peek(s,0xff0f)&8),"paused edge raised an early serial IRQ");
            pair.run_until(10000);
            check(peek(m,0xff01)==0x3c&&peek(s,0xff01)==0xa5,"paused edge byte completion differs");
            check(!m.serial_port().active&&!s.serial_port().active,"paused edge transfer did not complete");
            check((peek(m,0xff0f)&8)&&(peek(s,0xff0f)&8),"paused edge eighth-bit IRQ missing");
            auto expected=pair.snapshot();
            auto a=m.take_audio(),b=s.take_audio();
            std::string error;check(pair.restore(initial,error),error.c_str());
            // restore swaps Machine objects; use the new instances below.
            poke(pair.machine(master),address,address==0xff02?0x81:0);pair.run_until(10000);
            check(pair.snapshot()==expected,"paused edge checkpoint replay differs");
            check(pair.machine(master).take_audio()==a&&pair.machine(master^1).take_audio()==b,"paused edge PCM replay differs");
        }
    }
    // An independently advanced peer or an active internal peer cannot be
    // clocked by this synchronized, external-receiver operation.
    for(bool competing:{false,true}) {
        Options o;o.native=false;auto rom=program();CablePair pair(rom,o,rom,o);
        poke(pair.machine(1),0xff02,competing?0x81:0x80);poke(pair.machine(0),0xff02,0x81);
        pair.run_until(600);auto initial=pair.snapshot();
        if(!competing)pair.machine(1).run_until(pair.ticks()+8);
        auto peer=pair.machine(1).snapshot();poke(pair.machine(0),0xff02,0x81);
        check(pair.machine(1).snapshot()==peer,"invalid paused edge mutated the peer");
        bool rejected=false;try {pair.run_until(1000);}catch(const std::exception&) {rejected=true;}
        check(rejected,"invalid paused edge was accepted");
        std::string error;check(pair.restore(initial,error),error.c_str());
        check(pair.snapshot()==initial,"paused edge failure recovery differs");
    }
    std::cout<<"paused SC/DIV edge reciprocal bits, completion/IRQs, state/PCM replay and guarded failures passed\n";
}
static void atomic_second_console() {
    Options o;o.native=false;
    auto left=program(),right=program(true),wrong=right;wrong[0x180]^=1;
    CablePair live(left,o,right,o);live.run_until(1000);auto before=live.snapshot();
    // The first checkpoint is valid and differs from live state. Only the
    // second cartridge identity is wrong, so rejection must not commit the
    // already-parsed first console.
    CablePair foreign(left,o,wrong,o);std::string error;
    check(!live.restore(foreign.snapshot(),error)&&error=="snapshot ROM mismatch","second-console identity was accepted");
    check(live.snapshot()==before,"second-console restore failure changed the first console");
}
static void cycle_disjoint() {
    Options o;o.native=false;auto a=program(),b=program();
    b[0x151]=0;b[0x152]=0x18;b[0x153]=0xfe;
    CablePair pair(a,o,b,o,CableSchedule::cycle_continuations);
    auto idle=pair.snapshot();std::string idle_error;
    check(pair.restore(idle,idle_error),idle_error.c_str());check(pair.snapshot()==idle,"idle cycle checkpoint differs");
    auto initial=pair.machine(0).snapshot();
    poke(pair.machine(0),0xff01,0xa5);poke(pair.machine(1),0xff01,0x3c);
    poke(pair.machine(1),0xff02,0x80);poke(pair.machine(0),0xff02,0x81);
    for(uint64_t target:{128u,900u,4096u,10000u,150000u}) {
        pair.run_until(target);
        check(pair.ticks()>=target&&pair.ticks()<target+64,"cycle schedule did not stop promptly");
    }
    check(peek(pair.machine(0),0xff01)==0x3c&&peek(pair.machine(1),0xff01)==0xa5,"disjoint cycle wire differs");
    check((peek(pair.machine(0),0xff0f)&8)&&(peek(pair.machine(1),0xff0f)&8),"disjoint cycle wire IRQ missing");
    std::string error;check(!pair.machine(0).restore(initial,error),"active individual continuation restore accepted");
    pair.machine(0).set_buttons(1);pair.machine(1).set_buttons(2);
    auto checkpoint=pair.snapshot();
    pair.machine(0).take_audio();pair.machine(1).take_audio();
    pair.run_until(160000);auto expected=pair.snapshot();
    std::array<std::vector<int16_t>,2> pcm{pair.machine(0).take_audio(),pair.machine(1).take_audio()};
    check(pair.restore(checkpoint,error),error.c_str());pair.run_until(160000);
    check(pair.snapshot()==expected,"disjoint continuation replay state differs");
    for(unsigned i=0;i<2;++i)check(pair.machine(i).take_audio()==pcm[i],"disjoint continuation PCM replay differs");
    std::cout<<"cycle-continuation disjoint phases complete reciprocal bytes and exact state/PCM replay\n";
}
static uint64_t word(const std::vector<uint8_t>& data,size_t offset) {
    uint64_t n=0;for(unsigned i=0;i<8;++i)n|=uint64_t(data.at(offset+i))<<(8*i);return n;
}
static void set_word(std::vector<uint8_t>& data,size_t offset,uint64_t n) {
    for(unsigned i=0;i<8;++i)data.at(offset+i)=uint8_t(n>>(8*i));
}
static void reseal(std::vector<uint8_t>& data) {
    auto h=sha256(std::span(data).first(data.size()-64));std::copy(h.begin(),h.end(),data.end()-64);
}
static void cycle_atomic_restore() {
    Options o;o.native=false;auto a=program(),b=program(true),wrong=b;wrong[0x180]^=1;
    CablePair pair(a,o,b,o,CableSchedule::cycle_continuations);
    poke(pair.machine(0),0xff01,0xa5);poke(pair.machine(1),0xff01,0x3c);
    poke(pair.machine(1),0xff02,0x80);poke(pair.machine(0),0xff02,0x81);pair.run_until(3500);
    auto before=pair.snapshot();std::string error;
    Machine orphan(a,o);auto left=word(before,16);
    check(!orphan.restore(std::span(before).subspan(32,size_t(left)),error)&&
        error=="CPU continuation requires a paired checkpoint","partial CPU view independently resumed");
    CablePair foreign(a,o,wrong,o,CableSchedule::cycle_continuations);foreign.run_until(3000);
    check(!pair.restore(foreign.snapshot(),error)&&error=="snapshot ROM mismatch","second cycle ROM accepted");
    check(pair.snapshot()==before,"second cycle ROM failure changed live continuations");
    auto reject=[&](std::vector<uint8_t> bad){reseal(bad);check(!pair.restore(bad,error),"malformed cycle state accepted");
        check(pair.snapshot()==before,"malformed cycle state changed live continuations");};
    size_t descriptor=32+size_t(word(before,16)+word(before,24))+16;
    auto bad=before;set_word(bad,descriptor+8,4000001);reject(std::move(bad));
    bad=before;set_word(bad,descriptor+16,word(bad,descriptor+48)+280897);reject(std::move(bad));
    bad=before;set_word(bad,descriptor+72,word(bad,descriptor+48)+1);reject(std::move(bad));
    auto events=word(before,descriptor+64);check(events>0,"wire replay fixture has no inputs");
    size_t event=descriptor+80+size_t(word(before,descriptor+56));
    bad=before;set_word(bad,event+24,word(bad,event+24)^1);reject(std::move(bad));
    bad=before;bad.insert(bad.end()-64,0);reject(std::move(bad));
    pair.machine(0).take_audio();pair.machine(1).take_audio();pair.run_until(10000);auto expected=pair.snapshot();
    std::array<std::vector<int16_t>,2> pcm{pair.machine(0).take_audio(),pair.machine(1).take_audio()};
    check(pair.restore(before,error),error.c_str());pair.run_until(10000);
    check(pair.snapshot()==expected,"failed cycle restore damaged future execution");
    for(unsigned i=0;i<2;++i)check(pair.machine(i).take_audio()==pcm[i],"failed cycle restore damaged future PCM");
    std::cout<<"cycle replay bounds, independent-resume refusal, event corruption and atomic second-ROM restore passed\n";
}
static std::vector<uint8_t> button_serial_program(bool master,bool fast,bool speed) {
    auto rom=program(false,speed);std::vector<uint8_t> body{0xf3};
    if(speed)body.insert(body.end(),{0x3e,1,0xe0,0x4d,0x10,0});
    body.insert(body.end(),{0x3e,uint8_t(master?0xa5:0x3c),0xe0,1});
    if(!master)body.insert(body.end(),{0x3e,0x80,0xe0,2});
    // Arm the external receiver first. Actual Right input releases the clock
    // owner only after both real KEY1/STOP transitions have settled.
    body.insert(body.end(),{0x3e,0x20,0xe0,0,0xf0,0,0xe6,1,0x20,0xfa});
    if(master)body.insert(body.end(),{0x3e,uint8_t(fast?0x83:0x81),0xe0,2});
    body.insert(body.end(),{0x18,0xfe});
    std::copy(body.begin(),body.end(),rom.begin()+0x150);return rom;
}
static void cycle_mixed(bool fast,bool double_master,unsigned master) {
    Options o;o.native=false;o.model=Model::cgb;
    auto a=button_serial_program(master==0,fast,master==0?double_master:!double_master);
    auto b=button_serial_program(master==1,fast,master==1?double_master:!double_master);
    CablePair pair(a,o,b,o,CableSchedule::cycle_continuations);
    pair.run_until(24000);
    auto& m=pair.machine(master);auto& s=pair.machine(master^1);
    check(bool(peek(m,0xff4d)&0x80)==double_master,"cycle mixed master speed differs");
    check(bool(peek(s,0xff4d)&0x80)!=double_master,"cycle mixed slave speed differs");
    auto armed=[&]{return s.serial_port().active&&!s.serial_port().internal_clock&&!m.serial_port().active&&
        (peek(m,0xff00)&0x30)==0x20&&(peek(s,0xff00)&0x30)==0x20;};
    // KEY1 can change before STOP's remaining halt has completed. Wait for
    // the actual CPU-controlled port/input setup, not just the speed bit.
    while(!armed()&&pair.ticks()<300000)pair.run_until(pair.ticks()+4096);
    check(armed(),"cycle mixed arming differs");
    m.set_buttons(1);s.set_buttons(1);auto begin=pair.ticks();
    for(unsigned wait=1;wait<=128;++wait) {
        pair.run_until(begin+16*wait);
        if(m.serial_port().active&&m.serial_port().shifted_bits)break;
    }
    check(m.serial_port().active&&m.serial_port().shifted_bits>0&&m.serial_port().shifted_bits<8,"cycle mixed checkpoint is not mid-byte");
    auto checkpoint=pair.snapshot();m.take_audio();s.take_audio();pair.run_until(begin+10000);
    check(peek(m,0xff01)==0x3c&&peek(s,0xff01)==0xa5,"cycle mixed reciprocal bytes differ");
    check(!m.serial_port().active&&!s.serial_port().active,"cycle mixed transfer not complete");
    check((peek(m,0xff0f)&8)&&(peek(s,0xff0f)&8),"cycle mixed completion IRQ missing");
    auto expected=pair.snapshot();std::array<std::vector<int16_t>,2> pcm{pair.machine(0).take_audio(),pair.machine(1).take_audio()};
    std::string error;check(pair.restore(checkpoint,error),error.c_str());pair.run_until(begin+10000);
    check(pair.snapshot()==expected,"cycle mixed mid-byte replay state differs");
    for(unsigned i=0;i<2;++i)check(pair.machine(i).take_audio()==pcm[i],"cycle mixed mid-byte replay PCM differs");
    std::cout<<"cycle mixed fast="<<fast<<" double-master="<<double_master<<" master="<<master<<" passed\n";
}
static void competing_clocks() {
    for(unsigned scenario=0;scenario<3;++scenario) {
        Options o;o.native=false;o.model=scenario?Model::cgb:Model::dmg;auto rom=program();
        Machine left(rom,o),right(rom,o);left.run_until(128);right.run_until(145);
        check(left.stats().cycles!=right.stats().cycles,"competing clock fixture lacks a natural phase offset");
        CablePair pair(rom,o,rom,o,CableSchedule::cycle_continuations);std::string error;
        check(pair.machine(0).restore(left.snapshot(),error),error.c_str());check(pair.machine(1).restore(right.snapshot(),error),error.c_str());
        for(unsigned i=0;i<2;++i){poke(pair.machine(i),0xff0f,0);poke(pair.machine(i),0xff01,i?0x3c:0xa5);}
        poke(pair.machine(0),0xff02,scenario?0x83:0x81);poke(pair.machine(1),0xff02,scenario==1?0x83:0x81);
        const auto begin=pair.ticks();const auto period=scenario==1?32:1024;
        pair.run_until(begin+(scenario==2?64:period*3));
        check(pair.machine(0).serial_port().active&&pair.machine(0).serial_port().shifted_bits>0,"competing clock checkpoint is not mid-byte");
        auto checkpoint=pair.snapshot();for(unsigned i=0;i<2;++i)pair.machine(i).take_audio();
        pair.run_until(begin+12000);
        check(peek(pair.machine(0),0xff01)==(scenario==2?0:0x3c)&&peek(pair.machine(1),0xff01)==(scenario==2?0xff:0xa5),"independent clock SO samples differ from rising-edge waveform");
        for(unsigned i=0;i<2;++i)check(!pair.machine(i).serial_port().active&&(peek(pair.machine(i),0xff0f)&8),"competing clock eighth-bit completion/IRQ differs");
        check(pair.machine(0).serial_pin()&&!pair.machine(1).serial_pin(),"competing sources lost their last output bits");
        auto expected=pair.snapshot();std::array<std::vector<int16_t>,2> pcm{pair.machine(0).take_audio(),pair.machine(1).take_audio()};
        check(pair.restore(checkpoint,error),error.c_str());pair.run_until(begin+12000);
        check(pair.snapshot()==expected,"competing clock current-build replay differs");
        for(unsigned i=0;i<2;++i)check(pair.machine(i).take_audio()==pcm[i],"competing clock PCM replay differs");
        std::cout<<"competing internal clocks scenario="<<scenario<<" phase,SO,IRQ,state/PCM passed\n";
    }
}
static void rearmed_clocks() {
    Options o;o.native=false;o.model=Model::cgb;
    auto make=[](uint8_t byte) {
        auto rom=program();
        // A real pad releases both CPUs. Repeated SC writes occur between
        // device intervals while the other internal clock keeps running.
        const uint8_t code[]={0xf3,0x3e,byte,0xe0,1,0x3e,0x20,0xe0,0,
            0xf0,0,0xe6,1,0x20,0xfa,0x3e,0x83,0xe0,2,0,0,0x18,0xfa};
        std::copy(std::begin(code),std::end(code),rom.begin()+0x150);return rom;
    };
    auto a=make(0xa5),b=make(0x3c);Machine left(a,o),right(b,o);left.run_until(128);right.run_until(145);
    CablePair pair(a,o,b,o,CableSchedule::cycle_continuations);std::string error;
    check(pair.machine(0).restore(left.snapshot(),error),error.c_str());check(pair.machine(1).restore(right.snapshot(),error),error.c_str());
    for(unsigned i=0;i<2;++i){poke(pair.machine(i),0xff0f,0);pair.machine(i).set_buttons(1);}
    auto begin=pair.ticks();pair.run_until(begin+800);auto checkpoint=pair.snapshot();
    for(unsigned i=0;i<2;++i)pair.machine(i).take_audio();
    pair.run_until(begin+2500);auto expected=pair.snapshot();
    std::array<std::vector<int16_t>,2> pcm{pair.machine(0).take_audio(),pair.machine(1).take_audio()};
    for(unsigned i=0;i<2;++i)check(pair.machine(i).serial_port().active&&!(peek(pair.machine(i),0xff0f)&8),"SC rearm did not restart the eight-bit counter");
    check(pair.restore(checkpoint,error),error.c_str());pair.run_until(begin+2500);
    check(pair.snapshot()==expected,"rearmed internal clocks replay differs");
    for(unsigned i=0;i<2;++i)check(pair.machine(i).take_audio()==pcm[i],"rearmed internal clocks PCM replay differs");
    std::cout<<"CPU-driven SC rearm with independent clocks passed\n";
}
static void independent_waveforms() {
    struct Edge {uint64_t tick;bool high,bit;};
    struct Wave {
        std::vector<Edge> edges;
        static bool fall(void* p,uint64_t tick,bool bit) noexcept {
            static_cast<Wave*>(p)->edges.push_back({tick,false,bit});return true;
        }
        static void rise(void* p,uint64_t tick,bool bit) noexcept {
            static_cast<Wave*>(p)->edges.push_back({tick,true,bit});
        }
    };
    // Independently collect each oscillator's waveform. The ROM never reads
    // SB/SC/IF, so incoming data cannot change either edge schedule. Compute
    // SI from the peer's last strictly earlier rising edge, then compare the
    // pair. In particular, a later pending edge must not be consumed early.
    for(unsigned speeds=0;speeds<4;++speeds) {
        constexpr unsigned offset=1;
        Options o;o.native=false;o.model=Model::cgb;
        auto a=program(false,speeds&1),b=program(false,speeds&2);
        Machine left(a,o),right(b,o);left.run_until(500000);right.run_until(500000+offset);
        std::array<Machine*,2> single{&left,&right};std::array<Wave,2> waves;
        std::array<std::vector<uint8_t>,2> seeds;std::array<uint64_t,2> origins;
        std::array<bool,2> initial;
        for(unsigned i=0;i<2;++i) {
            waves[i].edges.reserve(16);
            auto& m=*single[i];poke(m,0xff01,i?0x3c:0xa5);poke(m,0xff02,0x83);
            seeds[i]=m.snapshot();origins[i]=m.stats().cycles;initial[i]=m.serial_pin();
            m.link_callbacks({&waves[i],Wave::fall,nullptr,Wave::rise});m.run_until(origins[i]+1024);
        }
        CablePair pair(a,o,b,o,CableSchedule::cycle_continuations);std::string error;
        for(unsigned i=0;i<2;++i)check(pair.machine(i).restore(seeds[i],error),error.c_str());
        const auto origin=std::max(origins[0],origins[1]);pair.run_until(origin+1024);
        for(unsigned i=0;i<2;++i) {
            uint8_t expected=0;unsigned bits=0;
            for(const auto& edge:waves[i].edges)if(!edge.high) {
                bool input=initial[i^1];auto tick=edge.tick-origins[i];
                for(const auto& peer:waves[i^1].edges)
                    if(peer.high&&peer.tick-origins[i^1]<tick)input=peer.bit;
                expected=uint8_t((expected<<1)|input);++bits;
            }
            check(bits==8,"independent waveform fixture lacks eight falling edges");
            if(peek(pair.machine(i),0xff01)!=expected) {
                std::cerr<<"waveform speeds="<<speeds<<" offset="<<offset<<" side="<<i<<'\n';
                throw std::runtime_error("pair consumed a pending independent edge before its timestamp");
            }
        }
    }
    std::cout<<"independent normal/double-speed SO waveforms preserve each edge timestamp\n";
}
static void independent_clocks() {
    for(bool fast:{false,true})for(unsigned master=0;master<2;++master) {
        Options o;o.native=false;o.model=Model::cgb;
        auto a=button_serial_program(master==0,fast,master==0);
        auto b=button_serial_program(master==1,fast,master==1);
        Machine left(a,o),right(b,o);left.run_until(500000);right.run_until(710012);
        auto local_left=left.stats().cycles,local_right=right.stats().cycles;
        check(local_left!=local_right,"independent fixture has equal clocks");
        CablePair pair(a,o,b,o,CableSchedule::cycle_continuations);std::string error;
        check(pair.machine(0).restore(left.snapshot(),error),error.c_str());
        check(pair.machine(1).restore(right.snapshot(),error),error.c_str());
        auto idle=pair.snapshot();check(pair.restore(idle,error),error.c_str());
        auto attached=pair.ticks();pair.run_until(attached);
        check(pair.machine(0).snapshot()==left.snapshot()&&pair.machine(1).snapshot()==right.snapshot(),
            "clock attachment changed guest counters or hardware state");
        auto mapped=pair.snapshot();check(pair.restore(mapped,error),error.c_str());
        check(pair.snapshot()==mapped,"idle mapped clock checkpoint differs");
        pair.run_until(attached+128);pair.machine(0).set_buttons(1);pair.machine(1).set_buttons(1);
        auto begin=pair.ticks();
        for(unsigned wait=1;wait<=128;++wait) {
            pair.run_until(begin+16*wait);
            if(pair.machine(master).serial_port().active&&pair.machine(master).serial_port().shifted_bits)break;
        }
        check(pair.machine(master).serial_port().active&&pair.machine(master).serial_port().shifted_bits>0,
            "independent clock checkpoint is not mid-byte");
        auto checkpoint=pair.snapshot();for(unsigned i=0;i<2;++i)pair.machine(i).take_audio();
        pair.run_until(begin+10000);
        check(peek(pair.machine(master),0xff01)==0x3c&&peek(pair.machine(master^1),0xff01)==0xa5,
            "independent clock reciprocal bytes differ");
        auto elapsed=pair.ticks()-attached;
        check(pair.machine(0).stats().cycles-local_left<=elapsed&&elapsed-(pair.machine(0).stats().cycles-local_left)<16&&
              pair.machine(1).stats().cycles-local_right<=elapsed&&elapsed-(pair.machine(1).stats().cycles-local_right)<16,
            "independent guest clocks were advanced to a shared origin");
        auto expected=pair.snapshot();std::array<std::vector<int16_t>,2> pcm{pair.machine(0).take_audio(),pair.machine(1).take_audio()};
        check(pair.restore(checkpoint,error),error.c_str());pair.run_until(begin+10000);
        check(pair.snapshot()==expected,"independent clock replay state differs");
        for(unsigned i=0;i<2;++i)check(pair.machine(i).take_audio()==pcm[i],"independent clock replay PCM differs");
        std::cout<<"independent origins fast="<<fast<<" master="<<master<<" state/PCM passed\n";
    }
}
static void dormant_windows() {
    // The dormant receiver can run ahead only until its SC write. A master
    // that starts earlier must never deliver old bits to that later receiver.
    for(unsigned delay:{0u,3u,30u,300u,3000u})for(unsigned master=0;master<2;++master) {
        auto cartridge=[](bool source_clock,unsigned wait) {
            auto rom=program();std::vector<uint8_t> body{0xf3,0x3e,source_clock?uint8_t(0xa5):uint8_t(0x3c),0xe0,1};
            body.insert(body.end(),wait,0x00);
            body.insert(body.end(),{0x3e,source_clock?uint8_t(0x81):uint8_t(0x80),0xe0,2,0x18,0xfe});
            std::copy(body.begin(),body.end(),rom.begin()+0x150);return rom;
        };
        auto a=cartridge(master==0,master?delay:0),b=cartridge(master==1,master?0:delay);
        Options o;o.native=false;o.model=Model::cgb;
        CablePair reference(a,o,b,o,CableSchedule::common_instruction);
        CablePair events(a,o,b,o,CableSchedule::cycle_continuations);
        reference.run_until(150000);events.run_until(reference.ticks());
        for(unsigned side=0;side<2;++side) {
            auto& r=reference.machine(side);auto& e=events.machine(side);
            for(uint16_t address:{0xff01,0xff02,0xff0f})
                check(peek(r,address)==peek(e,address),"dormant activation consumed an earlier peer edge");
            check(r.serial_pin()==e.serial_pin()&&r.serial_port().shifted_bits==e.serial_port().shifted_bits,
                  "dormant activation changed SO or partial transfer phase");
            check(r.take_audio()==e.take_audio(),"dormant event horizon changed PCM");
            check(std::equal(r.pixels().begin(),r.pixels().end(),e.pixels().begin()),"dormant event horizon changed pixels");
        }
        auto checkpoint=events.snapshot();auto deadline=events.ticks()+10000;
        events.run_until(deadline);auto expected=events.snapshot();
        std::string error;check(events.restore(checkpoint,error),error.c_str());events.run_until(deadline);
        check(events.snapshot()==expected,"dormant event horizon replay differs");
    }
    std::cout<<"dormant receiver activation preserves delayed bytes, SO, IRQ, pixels, PCM and replay\n";
}
static void dormant_external_replay() {
    for(unsigned receiver=0;receiver<2;++receiver) {
        Options o;o.native=false;o.model=Model::cgb;
        auto a=program(),b=program(true);
        CablePair pair(a,o,b,o,CableSchedule::cycle_continuations);
        poke(pair.machine(receiver),0xff0f,0);
        poke(pair.machine(receiver),0xff02,0x80);
        std::vector<uint8_t> checkpoint;
        for(unsigned frame=0;frame<5;++frame) {
            pair.run_frame();
            for(unsigned i=0;i<2;++i)pair.machine(i).take_audio();
            checkpoint=pair.snapshot();
        }
        auto port=pair.machine(receiver).serial_port();
        check(port.active&&!port.shifted_bits&&!(peek(pair.machine(receiver),0xff0f)&8),
              "dormant source delivered phantom data or IRQ");
        pair.run_frame();auto expected=pair.snapshot();
        std::array<std::vector<int16_t>,2> pcm{pair.machine(0).take_audio(),pair.machine(1).take_audio()};
        std::string error;check(pair.restore(checkpoint,error),error.c_str());pair.run_frame();
        check(pair.snapshot()==expected,"external waiting continuation replay differs");
        for(unsigned i=0;i<2;++i)check(pair.machine(i).take_audio()==pcm[i],"external waiting replay PCM differs");
    }
    std::cout<<"external receiver waiting across replay windows preserves state, PCM and IRQ\n";
}
int main(int argc,char** argv) {try {
    std::cout.setf(std::ios::unitbuf);
    std::cout<<"cable contract started\n";
    if(argc==4) {native_probe(argv[1],argv[2],argv[3]);native_cycle_probe(argv[1],argv[2],argv[3]);return 0;}
    check(argc==1,"usage: cable-test [ROM boot-ROM prepared-state]");
    split_equivalence();
    lcd_off_clock(Model::dmg,false);
    lcd_off_clock(Model::cgb,true);
    std::cout<<"unattached cycle slicing matches original state/PCM\n";
    wire(Model::dmg,false,false,false,0);wire(Model::dmg,false,false,false,1);
    wire(Model::cgb,false,false,false,0);wire(Model::cgb,true,false,false,1);
    wire(Model::cgb,true,true,true,0);
    unsupported_phases();
    paused_edge();
    atomic_second_console();
    cycle_disjoint();
    cycle_atomic_restore();
    competing_clocks();
    independent_waveforms();
    rearmed_clocks();
    independent_clocks();
    dormant_windows();
    dormant_external_replay();
    for(bool fast:{false,true})for(bool double_master:{false,true})for(unsigned master=0;master<2;++master)
        cycle_mixed(fast,double_master,master);
    std::cout<<"cable cycle alignment, reciprocal bytes/IRQs, paired state/PCM replay and atomic restore passed\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
