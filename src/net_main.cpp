#include <gbcllr/netplay.hpp>
#include <chrono>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <thread>

int main(int argc,char** argv) {try {
    std::array<std::filesystem::path,2> rom,boot,package;
    std::array<gbcllr::Options,2> machines;
    gbcllr::NetOptions options;
    std::filesystem::path initial,inputs,output,battery,captures;
    uint32_t frames=600,timeout=120,view_width=256;bool unpaced=false,frames_set=false;
    for(int i=1;i<argc;++i) {
        std::string a=argv[i];auto value=[&] {if(++i>=argc)throw std::runtime_error("missing value for "+a);return std::string(argv[i]);};
        auto number=[&](uint32_t maximum) {auto s=value();size_t n=0;auto v=std::stoul(s,&n,0);if(n!=s.size()||v>maximum)throw std::runtime_error("numeric argument out of range");return uint32_t(v);};
        if(a=="--left-rom"||a=="--right-rom")rom[a=="--right-rom"]=value();
        else if(a=="--left-boot"||a=="--right-boot")boot[a=="--right-boot"]=value();
        else if(a=="--left-mod"||a=="--right-mod")package[a=="--right-mod"]=value();
        else if(a=="--view-width")view_width=number(4096);
        else if(a=="--left-model"||a=="--right-model") {auto v=value();if(v!="dmg"&&v!="cgb")throw std::runtime_error("model must be dmg or cgb");machines[a=="--right-model"].model=v=="cgb"?gbcllr::Model::cgb:gbcllr::Model::dmg;}
        else if(a=="--slot")options.local_slot=number(1);
        else if(a=="--bind")options.bind=value();
        else if(a=="--peer")options.peer=value();
        else if(a=="--session")options.session_id=number(UINT32_MAX);
        else if(a=="--delay")options.delay=number(8);
        else if(a=="--prediction")options.prediction=number(16);
        else if(a=="--frames") {frames=number(1000000);frames_set=true;}
        else if(a=="--timeout")timeout=number(3600);
        else if(a=="--pair-state")initial=value();
        else if(a=="--inputs")inputs=value();
        else if(a=="--output-state")output=value();
        else if(a=="--export-confirmed-save")battery=value();
        else if(a=="--capture-dir")captures=value();
        else if(a=="--unpaced")unpaced=true;
        else if(a=="--reference")machines[0].native=machines[1].native=false;
        else if(a=="--cycle-continuations")options.schedule=gbcllr::CableSchedule::cycle_continuations;
        else if(a=="--help") {
            std::cout<<"gbcllr-net --left-rom FILE --right-rom FILE --slot 0|1 --bind IP:PORT --peer IP:PORT\n"
              "  [--left-boot FILE --right-boot FILE] [--pair-state FILE] [--reference] [--cycle-continuations]\n"
              "  [--left-mod FILE --right-mod FILE] [--view-width N]\n"
              "  [--inputs FILE] [--frames N] [--delay N] [--prediction N] [--session ID]\n"
              "  [--output-state FILE] [--export-confirmed-save NEW_FILE] [--capture-dir DIR]\n"
              "Headless experimental two-peer cable rollback. Inputs contain only frames COUNT LEFT_MASK RIGHT_MASK.\n"
              "Only the local pad is sampled/sent; the library supplies remote rows. Headless audio is discarded.\n";
            return 0;
        } else throw std::runtime_error("unknown option: "+a);
    }
    if(rom[0].empty()||rom[1].empty()||!frames||!timeout)throw std::runtime_error("two cartridges and positive frame/timeout limits are required");
    if(!battery.empty()&&std::filesystem::exists(battery))throw std::runtime_error("confirmed-save export requires a new file; existing native saves are preserved");
    if(!initial.empty())options.initial_pair=gbcllr::read_file(initial);
    std::vector<uint8_t> pads;
    if(!inputs.empty()) {
        std::ifstream file(inputs);if(!file)throw std::runtime_error("cannot read network input profile");
        std::string line;
        while(std::getline(file,line)) {
            std::istringstream in(line);std::string op;if(!(in>>op)||op.starts_with('#'))continue;
            uint64_t count=0;unsigned left=0,right=0;std::string extra;
            if(op!="frames"||!(in>>count>>left>>right)||in>>extra||left>255||right>255||count>1000000-pads.size())
                throw std::runtime_error("network profiles accept bounded frames COUNT LEFT_MASK RIGHT_MASK only");
            pads.insert(pads.end(),size_t(count),uint8_t(options.local_slot?right:left));
        }
        if(!frames_set)frames=uint32_t(pads.size()+options.delay);
    }
    options.sample=[&,delay=options.delay](uint32_t t) {return t>=delay&&uint64_t(t-delay)<pads.size()?pads[t-delay]:uint8_t(0);};
    if(!frames)throw std::runtime_error("input profile must run at least one frame");
    for(unsigned i=0;i<2;++i)if(!boot[i].empty())machines[i].boot_rom=gbcllr::read_file(boot[i]);
    using Clock=std::chrono::steady_clock;
    Clock::time_point first_live{},last_live{};uint32_t first_frame=0,last_frame=0;
    options.present=[&](uint32_t frame,const gbcllr::Machine&,std::span<const int16_t>) {
        auto now=Clock::now();
        if(first_live==Clock::time_point{}) {first_live=now;first_frame=frame;}
        last_live=now;last_frame=frame;
    };
    std::array images{gbcllr::read_file(rom[0]),gbcllr::read_file(rom[1])};
    unsigned view_frames=0,expanded_frames=0,last_width=160;std::vector<uint32_t> last_view;
    for(unsigned i=0;i<2;++i)if(!package[i].empty())machines[i].mods.push_back(gbcllr::read_mod_package(package[i],gbcllr::sha256(images[i])).feature);
    if(!package[0].empty()||!package[1].empty())options.present_view=[&](uint32_t,const gbcllr::ModView& view){++view_frames;expanded_frames+=view.expanded;last_width=view.width;if(!captures.empty())last_view.assign(view.pixels.begin(),view.pixels.end());};
    gbcllr::NetSession net(std::move(images),std::move(machines),std::move(options));
    net.presentation_viewport(view_width,144);
    net.hold_at(frames);
    auto start=std::chrono::steady_clock::now(),next=start;
    while(!net.settled()) {
        if(std::chrono::steady_clock::now()-start>std::chrono::seconds(timeout))throw std::runtime_error("network frame/hold timeout: "+net.diagnostics());
        bool ran=net.poll();
        if(ran&&!net.replaying()&&!unpaced) {
            next+=std::chrono::microseconds(16743);
            if(next>std::chrono::steady_clock::now())std::this_thread::sleep_until(next);
            else next=std::chrono::steady_clock::now();
        } else if(!ran)net.wait(2);
    }
    auto settled=Clock::now();
    net.quiesce();
    while(!net.drained()) {
        if(std::chrono::steady_clock::now()-start>std::chrono::seconds(timeout))throw std::runtime_error("network drain timeout");
        net.poll();net.wait(2);
    }
    auto state=net.confirmed_snapshot();
    if(!output.empty())gbcllr::write_file_atomic(output,state);
    if(!battery.empty()) {auto data=net.confirmed_battery();if(!data.empty())gbcllr::write_file_atomic(battery,data);}
    if(!captures.empty()) {std::filesystem::create_directories(captures);for(unsigned i=0;i<2;++i)net.pair().machine(i).write_frame(captures/(i?"right.ppm":"left.ppm"));}
    if(!captures.empty()&&!last_view.empty()) {
        auto header="P6\n"+std::to_string(last_width)+" 144\n255\n";
        std::vector<uint8_t> image(header.begin(),header.end());image.reserve(image.size()+last_view.size()*3);
        for(auto pixel:last_view){image.push_back(uint8_t(pixel>>16));image.push_back(uint8_t(pixel>>8));image.push_back(uint8_t(pixel));}
        gbcllr::write_file_atomic(captures/"local-view.ppm",image);
    }
    auto s=net.stats();auto final_hash=gbcllr::sha256(state);auto finished=Clock::now();
    std::cout<<"{\"tick\":"<<s.tick<<",\"confirmed\":"<<s.confirmed<<",\"episodes\":"<<s.episodes
        <<",\"predicted\":"<<s.predicted<<",\"replayed\":"<<s.replayed<<",\"profile\":"<<s.profile<<",\"transcript\":"<<s.transcript
        <<",\"module_profile\":"<<s.module_profile
        <<",\"continuation\":\""<<net.pair().continuation_backend()<<"\",\"pair_sha256\":\""<<final_hash<<"\",\"seconds\":"
        <<std::chrono::duration<double>(finished-start).count()
        <<",\"steady_frames\":"<<last_frame-first_frame
        <<",\"view_frames\":"<<view_frames<<",\"expanded_frames\":"<<expanded_frames<<",\"view_width\":"<<last_width
        <<",\"steady_seconds\":"<<std::chrono::duration<double>(last_live-first_live).count()
        <<",\"startup_first_frame_seconds\":"<<std::chrono::duration<double>(first_live-start).count()
        <<",\"settle_seconds\":"<<std::chrono::duration<double>(settled-last_live).count()
        <<",\"drain_export_seconds\":"<<std::chrono::duration<double>(finished-settled).count()<<",\"consoles\":[";
    for(unsigned i=0;i<2;++i) {
        auto m=net.pair().machine(i).stats();
        std::cout<<(i?",":"")<<"{\"native\":"<<m.instructions_native<<",\"fallback\":"<<m.instructions_fallback<<'}';
    }
    std::cout<<"]}\n";
    return 0;
}catch(const std::exception& e){std::cerr<<"gbcllr-net: "<<e.what()<<'\n';return 1;}}
