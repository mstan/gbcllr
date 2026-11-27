#include <gbcllr/netplay.hpp>
#include <algorithm>
#include <chrono>
#include <iostream>
#include <stdexcept>

using namespace gbcllr;
static void check(bool p,const char* message) {if(!p)throw std::runtime_error(message);}
static std::vector<uint8_t> program() {
    std::vector<uint8_t> rom(32768);
    rom[0x147]=9;rom[0x149]=2; // ROM + 8KiB battery RAM; no extra devices.
    rom[0x100]=0xc3;rom[0x101]=0x50;rom[0x102]=1;
    // Read the directional pad repeatedly into persistent guest RAM. A wrong
    // predicted row changes the simulation, even after its buttons release.
    const uint8_t code[]={0xf3,0xaf,0xea,0x00,0xc0,
        0x3e,0x20,0xe0,0x00,0xf0,0x00,0x47,0xfa,0x00,0xc0,
        0x80,0xea,0x00,0xc0,0xea,0x00,0xa0,0x18,0xed};
    std::copy(std::begin(code),std::end(code),rom.begin()+0x150);
    return rom;
}
static uint8_t pad(unsigned seat,uint32_t wire) {
    return wire>=9&&wire<14?uint8_t(seat?2:1):wire>=23&&wire<28?uint8_t(seat?1:2):0;
}
template<class F> static bool rejects(F f) {try{f();return false;}catch(const std::runtime_error&){return true;}}
int main(int argc,char** argv) {try {
    check(argc==8,"slot bind peer session mismatch expected-rollback schedule required");
    auto slot=unsigned(std::stoul(argv[1]));bool mismatch=std::stoi(argv[5])!=0,rollback=std::stoi(argv[6])!=0;
    std::array<Options,2> options;for(auto& o:options)o.native=false;
    if(std::stoi(argv[5])==1&&slot)++options[0].seed;
    auto rom=program();NetOptions n;n.local_slot=slot;n.bind=argv[2];n.peer=argv[3];n.session_id=uint32_t(std::stoul(argv[4]));
    for(unsigned i=0;i<2;++i) {n.initial_batteries[i].resize(8192);n.initial_batteries[i][0x20]=uint8_t(37+i);}
    if(std::stoi(argv[5])==3&&slot)++n.initial_batteries[0][0x20];
    const auto batteries=n.initial_batteries;
    auto selection=std::stoul(argv[7]);
    auto schedule=(selection&1)?CableSchedule::cycle_continuations:CableSchedule::common_instruction;
    if(selection&2)n.machine_slots={1,0};
    const auto console_slots=n.machine_slots;
    n.schedule=std::stoi(argv[5])==2&&slot?CableSchedule::common_instruction:schedule;
    n.sample=[slot](uint32_t t){return pad(slot,t);};
    constexpr uint32_t hold=34;
    NetSession* owned=nullptr;unsigned resets=0;
    std::array<bool,hold> shown{};
    std::array<bool,hold> views{};
    std::array<std::vector<int16_t>,hold> audio;
    n.present=[&](uint32_t t,const Machine& m,std::span<const int16_t> pcm) {
        check(owned && &m==&owned->pair().machine(console_slots[slot]),"remote console reached local presentation");
        check(t<hold&&!shown[t],"replayed frame reached presentation");shown[t]=true;audio[t].assign(pcm.begin(),pcm.end());
    };
    n.present_view=[&](uint32_t t,const ModView& view) {
        check(owned&&t<hold&&!views[t],"replayed frame reached local view callback");views[t]=true;
        auto pixels=owned->pair().machine(console_slots[slot]).pixels();
        check(view.width==160&&view.pixels.data()==pixels.data()&&view.pixels.size()==pixels.size(),"stock local view differs from native console");
    };
    n.presentation_reset=[&]{++resets;};
    NetSession net({rom,rom},options,std::move(n));
    owned=&net;
    check(net.diagnostics().starts_with("startup pending"),"pre-start diagnostics entered the rollback driver");
    check(rejects([&]{net.confirmed_battery();}),"pre-start native save accepted");
    check(rejects([&]{net.confirmed_snapshot();}),"pre-start checkpoint accepted");
    check(rejects([&]{net.confirmed_progress();}),"pre-start live progress accepted");
    check(rejects([&]{net.hold_at(0);}),"zero-frame hold can bypass content and initial-state confirmation");
    net.hold_at(hold);
    auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(90);
    bool live_checked=false,refused=false;NetProgress live_progress;
    try {
        while(!net.settled()) {
            if(std::chrono::steady_clock::now()>=deadline)
                throw std::runtime_error("network contract timeout: "+net.diagnostics());
            if(!net.poll())net.wait(1);
            if(net.ready()&&net.stats().tick>0&&!net.settled()) {
                check(rejects([&]{net.confirmed_battery();}),"unconfirmed native save accepted");
                check(rejects([&]{net.confirmed_snapshot();}),"unconfirmed checkpoint accepted");
                live_checked=true;
                auto s=net.stats();
                if(!live_progress.frames&&s.confirmed>0&&s.tick>s.confirmed+1&&!net.replaying())
                    live_progress=net.confirmed_progress();
            }
        }
    } catch(const std::runtime_error&) {if(!mismatch)throw;refused=true;}
    if(mismatch) {
        check(refused&&net.stats().tick==0,"identity mismatch ran guest frames");
        check(rejects([&]{net.confirmed_battery();}),"refused session exported a save");
        check(rejects([&]{net.confirmed_progress();}),"refused session exported live progress");
        std::cout<<"PASS: content mismatch refused before frame zero\n";return 0;
    }
    check(live_checked,"live save guard was not exercised");
    net.quiesce();while(!net.drained()) {
        check(std::chrono::steady_clock::now()<deadline,"network drain contract timeout");
        net.poll();net.wait(1);
    }
    auto stats=net.stats();check(stats.tick==hold&&stats.confirmed+1>=hold,"held state is unconfirmed");
    check(views==shown,"local picture and PCM presentation watermarks differ");
    if(rollback)check(stats.episodes>0&&stats.replayed>0&&resets>0,"rollback/presentation reset was not exercised");
    CablePair offline(rom,options[0],rom,options[1],schedule);
    for(unsigned i=0;i<2;++i) {std::string why;check(offline.machine(i).load_battery(batteries[i],why),"offline native start refused");}
    std::vector<uint8_t> expected_live;
    for(uint32_t t=0;t<hold;++t) {
        std::array<uint8_t,2> pads{};for(unsigned i=0;i<2;++i)pads[console_slots[i]]=t<3?0:pad(i,t);
        offline.run_frame(pads[0],pads[1]);
        for(unsigned i=0;i<2;++i) {
            auto pcm=offline.machine(i).take_audio();
            if(i==console_slots[slot]&&shown[t])check(pcm==audio[t],"local presented PCM differs from the corresponding offline frame");
        }
        if(t+1==live_progress.frames)expected_live=offline.machine(console_slots[slot]).battery();
    }
    check(net.confirmed_snapshot()==offline.snapshot(),"network differs from offline authoritative inputs");
    check(net.confirmed_battery()==offline.machine(console_slots[slot]).battery(),"confirmed native save differs");
    auto progress=net.confirmed_progress();check(progress.frames==hold&&progress.battery==offline.machine(console_slots[slot]).battery(),"settled progress differs");
    if(live_progress.frames)check(live_progress.battery==expected_live,"live export read speculative SRAM instead of its confirmed checkpoint");
    if(rollback)check(live_progress.frames>0,"predicted live native-save cut was not exercised");
    check(std::count(shown.begin(),shown.end(),true)>0,"presentation callback did not run");
    std::cout<<"PASS: exact offline state, live save guards, confirmed hold; episodes="<<stats.episodes<<" replayed="<<stats.replayed<<'\n';
    return 0;
}catch(const std::exception& e){std::cerr<<"netplay contract: "<<e.what()<<'\n';return 1;}}
