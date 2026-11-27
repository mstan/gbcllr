// Manual owned-cartridge pilot linked to the same build's real desktop objects.
// The setup may position Link; all measured crossings use only controller input.
#ifndef SDL_MAIN_HANDLED
#define SDL_MAIN_HANDLED
#endif
#include "../src/desktop_ui.hpp"
#include <SDL.h>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace gbcllr {void execute_commands(Machine&,const std::filesystem::path&,bool*);}
namespace gbcllr::detail {const char* sha256_backend();}
static void check(bool value,const char* why){if(!value)throw std::runtime_error(why);}
struct OracleRoute {
    const char* title;const char* rom;const char* battery;
    unsigned group_address,room_address,health_address,group,west,east,west_turn,east_turn,legs;
};
constexpr OracleRoute ages{"Ages","0b56b78a9e45452e98c33edd111234931f1e034dc097f6f23082eb8db6055474",
    "949df080b68841ae418cde7e57f2e794d563f6d22e7abc82dfe9073035be16ec",0xcc2d,0xcc30,0xc6aa,3,0xf8,0xf9,32,128,24};
// Shorter safe edge travel; repeat the genuine original loader 96 times.
constexpr OracleRoute seasons{"Seasons","862a51368fb30539279d336b3fe193b43876d2cb15c87a36f5da517804ab3971",
    "88a9d8dad4c30e9af8b7ab8b7a44f09b3a3b36f485c0ce3cfa6f85a5a83e8089",0xcc49,0xcc4c,0xc6a2,0,0x97,0x98,136,24,96};
struct Pilot {
    const OracleRoute& route;
    struct Position {unsigned game,cutscene,group,room,scroll,phase,x,y,health;};
    struct Witness {uint32_t frame;uint64_t cycles;Position position;};
    std::vector<Witness> changes;
    std::vector<std::pair<std::string,std::vector<uint32_t>>> pictures;
    Position position{};
    unsigned legs=0,last_room=0xff;
    uint8_t buttons=1;
    uint64_t first_time=0,last_time=0,first_cycles=0,last_cycles=0,pcm=0,nonzero_pcm=0,read_ticks=0;
    uint32_t shown=0;
    static uint8_t input(void* context,uint32_t frame,const gbcllr::Machine& machine) {
        auto& p=*static_cast<Pilot*>(context);
        if(frame%8==0) {
            // Offline debug reads clone the machine. Sample once per eight
            // frames; their small additional cost stays inside measured time.
            auto begin=SDL_GetPerformanceCounter();auto ram=machine.debug_read(0xc200,0xf00);
            p.read_ticks+=SDL_GetPerformanceCounter()-begin;
            auto at=[&](unsigned address){return unsigned(ram.at(address-0xc200));};
            p.position={at(0xc2ee),at(0xc2ef),at(p.route.group_address),at(p.route.room_address),at(0xcd00),at(0xcd04),at(0xd00d),at(0xd00b),at(p.route.health_address)};
            auto v=p.position;
            // Original scrolling runs cutscene00 before normal cutscene01
            // control resumes; that native transition is part of this test.
            if(v.game!=2||v.cutscene>1||v.group!=p.route.group)throw std::runtime_error("pilot left ordinary Oracle gameplay at frame "+std::to_string(frame)+
                " game="+std::to_string(v.game)+" cutscene="+std::to_string(v.cutscene)+" group="+std::to_string(v.group));
            check(v.room==p.route.west||v.room==p.route.east,"pilot left its two original rooms");
            check(v.health>0,"pilot lost native active gameplay health");
            if(v.room!=p.last_room){p.changes.push_back({frame,machine.stats().cycles,v});p.last_room=v.room;}
            bool ready=v.cutscene==1&&v.scroll==1&&v.phase==2;
            if(ready&&((p.buttons==1&&v.room==p.route.east&&v.x>=p.route.east_turn)||(p.buttons==2&&v.room==p.route.west&&v.x<=p.route.west_turn))) {
                ++p.legs;p.buttons=p.buttons==1?2:1;
                if(p.legs==1||p.legs==2)p.pictures.push_back({p.legs==1?"first-east":"first-return",{machine.pixels().begin(),machine.pixels().end()}});
            }
        }
        check(frame<20000,"stock crossing pilot made no bounded route progress");
        return p.buttons;
    }
    static bool present(void* context,uint32_t frame,const gbcllr::Machine& machine,std::span<const int16_t> samples) {
        auto& p=*static_cast<Pilot*>(context);auto now=SDL_GetPerformanceCounter();
        check(frame==p.shown+1,"desktop omitted or repeated a solo frame");
        if(!p.shown) {
            p.first_time=now;p.first_cycles=machine.stats().cycles;p.read_ticks=0;
            auto window=SDL_GetWindowFromID(1);check(window,"normal desktop window missing");
            auto flags=SDL_GetWindowFlags(window);
            check(!(flags&(SDL_WINDOW_HIDDEN|SDL_WINDOW_MINIMIZED|SDL_WINDOW_FULLSCREEN|SDL_WINDOW_FULLSCREEN_DESKTOP|SDL_WINDOW_INPUT_FOCUS)),"QA window was hidden, fullscreen, minimized or activated");
            SDL_RendererInfo info{};check(SDL_GetRendererInfo(SDL_GetRenderer(window),&info)==0,"renderer identity missing");
            std::string video=SDL_GetCurrentVideoDriver(),audio=SDL_GetCurrentAudioDriver();
            check(video!="dummy"&&video!="offscreen"&&audio!="dummy"&&audio!="disk","pilot requires normal video and audio devices");
            int w=0,h=0;SDL_GetRendererOutputSize(SDL_GetRenderer(window),&w,&h);
            std::cout<<"desktop video="<<SDL_GetCurrentVideoDriver()<<" audio="<<SDL_GetCurrentAudioDriver()
                     <<" renderer="<<info.name<<" size="<<w<<'x'<<h<<" volume=0 normal_device=1\n"<<std::flush;
        }
        p.last_time=now;p.last_cycles=machine.stats().cycles;++p.shown;p.pcm+=samples.size();
        for(auto sample:samples)p.nonzero_pcm+=sample!=0;
        return p.legs<p.route.legs;
    }
};
int main(int argc,char** argv){try {
    using namespace gbcllr;
    check(argc==6,"ROM BOOT NATIVE_BATTERY NEW_PROFILE SETUP_COMMANDS required");
    auto rom=read_file(argv[1]),battery=read_file(argv[3]);
    std::cout<<"snapshot_sha_backend="<<detail::sha256_backend()<<'\n';
    const auto sha=sha256(rom);const OracleRoute* route=sha==ages.rom?&ages:sha==seasons.rom?&seasons:nullptr;
    check(route,"pilot requires a qualified Oracle cartridge");check(sha256(battery)==route->battery,"pilot native-save provenance differs");
    DesktopConfig c;c.profile=argv[4];check(!std::filesystem::exists(c.profile),"pilot needs a new disposable profile");
    std::filesystem::create_directories(c.profile);c.rom=argv[1];c.boot=argv[2];c.volume=0;c.audio=true;
    c.scale=4;c.fullscreen=0;c.shortcuts=false;c.source=0;c.adaptive=false;c.session_mods=false;
    Options options;options.model=Model::cgb;options.boot_rom=read_file(argv[2]);
    Machine machine(rom,options);std::string error;if(!machine.load_battery(battery,error))throw std::runtime_error(error);
    bool ignored=false;execute_commands(machine,argv[5],&ignored);machine.take_audio();
    machine.write_frame(c.profile/"start.ppm");auto start=machine.stats();
    Pilot pilot{*route};DesktopFrameQa qa{&pilot,Pilot::input,Pilot::present};
    auto result=desktop(machine,c.profile/"never-export.sav",false,c,qa);
    check(result==0&&pilot.legs==route->legs&&pilot.changes.size()==route->legs+1,"pilot did not finish its actual stock round trips");
    check(machine.debug_read(uint16_t(route->health_address),1)[0]>0,"pilot ended without native active gameplay health");
    check(pilot.pcm>0&&pilot.nonzero_pcm>0,"emulated music/audio was inactive");
    check(read_file(argv[3])==battery&&!std::filesystem::exists(c.profile/"never-export.sav"),"pilot exported or changed native saves");
    auto end=machine.stats();machine.write_frame(c.profile/"finish.ppm");
    for(auto& [name,pixels]:pilot.pictures) {
        std::ofstream out(c.profile/(name+".ppm"),std::ios::binary);out<<"P6\n160 144\n255\n";
        for(auto pixel:pixels){char rgb[]={char(pixel>>16),char(pixel>>8),char(pixel)};out.write(rgb,3);}
    }
    double frequency=double(SDL_GetPerformanceFrequency());
    double elapsed=(pilot.last_time-pilot.first_time)/frequency,guest=(pilot.last_cycles-pilot.first_cycles)/8388608.0;
    std::ofstream out(c.profile/"runtime.json");out.precision(12);
    out<<"{\"title\":\""<<route->title<<"\",\"package\":\"none\",\"presented_frames\":"<<pilot.shown<<",\"presentation_intervals\":"<<pilot.shown-1
       <<",\"elapsed_seconds\":"<<elapsed<<",\"guest_seconds\":"<<guest<<",\"elapsed_minus_guest_seconds\":"<<elapsed-guest
       <<",\"guest_per_elapsed\":"<<guest/elapsed<<",\"completed_crossings\":"<<pilot.legs<<",\"pcm_samples\":"<<pilot.pcm
       <<",\"nonzero_pcm_samples\":"<<pilot.nonzero_pcm<<",\"debug_read_seconds_included\":"<<pilot.read_ticks/frequency
       <<",\"instructions_native\":"<<end.instructions_native-start.instructions_native<<",\"instructions_fallback\":"<<end.instructions_fallback-start.instructions_fallback
       <<",\"room_observations\":[";
    for(size_t i=0;i<pilot.changes.size();++i){auto& row=pilot.changes[i];auto v=row.position;if(i)out<<',';
        out<<"{\"frame\":"<<row.frame<<",\"cycles\":"<<row.cycles<<",\"group\":"<<v.group<<",\"room\":"<<v.room
           <<",\"x\":"<<v.x<<",\"y\":"<<v.y<<",\"scroll\":"<<v.scroll<<",\"phase\":"<<v.phase<<",\"health\":"<<v.health<<'}';}
    out<<"]}\n";
    std::cout<<"PASS stock "<<route->title<<" crossings="<<pilot.legs<<" presented="<<pilot.shown<<" elapsed="<<elapsed<<" guest="<<guest
             <<" speed="<<guest/elapsed<<" debug_read_seconds_included="<<pilot.read_ticks/frequency<<'\n';return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
