#include <gbcllr/machine.hpp>
#include <gbcllr/diagnostics.hpp>
#include <chrono>
#include <algorithm>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <sstream>
#include <cstdlib>

namespace gbcllr {void execute_commands(Machine&,const std::filesystem::path&,bool*);}

#ifdef GBCLLR_SDL
#include "desktop_ui.hpp"
#ifndef SDL_MAIN_HANDLED
#define SDL_MAIN_HANDLED
#endif
#include <SDL.h>
#include "desktop_graphics.hpp"
#ifdef GBCLLR_NETPLAY
#include "desktop_net.hpp"
int desktop_network(const DesktopNetStart&,bool,DesktopConfig&);
#endif
int desktop(gbcllr::Machine&, const std::filesystem::path&, bool,DesktopConfig&);
#endif
int main(int argc,char** argv) {
    bool show_window_errors=false;
    try {
        gbcllr::Options opts;
        std::filesystem::path rom,frame,trace,save,boot,commands,profile,mod_package;
        std::vector<std::filesystem::path> additional_mods;
        uint64_t frames=600;
        bool window=false,muted=false,skip_launcher=false,force_launcher=false,model_explicit=false;
        bool link_lobby=false;
        for(int i=1;i<argc;++i) {
            std::string a=argv[i];
            auto value=[&]() -> std::string {if(++i>=argc)throw std::runtime_error("missing value for "+a);return argv[i];};
            if(a=="--rom")rom=value();
            else if(a=="--frames")frames=std::stoull(value());
            else if(a=="--model") {auto m=value();if(m!="dmg"&&m!="cgb")throw std::runtime_error("model must be dmg or cgb");opts.model=m=="cgb"?gbcllr::Model::cgb:gbcllr::Model::dmg;model_explicit=true;}
            else if(a=="--boot-rom")boot=value();
            else if(a=="--reference")opts.native=false;
            else if(a=="--sgb-service")opts.sgb_service=true;
            else if(a=="--frame")frame=value();
            else if(a=="--trace")trace=value();
            else if(a=="--save")save=value();
            else if(a=="--commands")commands=value();
            else if(a=="--mod-package") {auto path=value();if(mod_package.empty())mod_package=path;else additional_mods.emplace_back(path);}
            else if(a=="--window")window=true;
            else if(a=="--launcher"){window=true;force_launcher=true;skip_launcher=false;}
            else if(a=="--link-lobby"){window=true;force_launcher=true;skip_launcher=false;link_lobby=true;}
            else if(a=="--skip-launcher"){skip_launcher=true;force_launcher=false;}
            else if(a=="--profile")profile=value();
            else if(a=="--mute")muted=true;
            else if(a=="--help") {
                std::cout<<"gbcllr-run --rom FILE [--frames N] [--model dmg|cgb] [--boot-rom FILE]\n"
                  "  [--reference] [--frame FILE.ppm] [--trace FILE] [--save FILE] [--window] [--mute]\n"
                  "  [--commands FILE] (offline cheat/teleport/state QA; commands run before frames/window)\n";
                std::cout<<"  [--launcher] [--skip-launcher] [--profile DIR] (isolated desktop preferences/saves/states)\n";
                std::cout<<"  [--sgb-service] (opt-in preboot native SGB protocol; DMG colors, cartridge border)\n";
                std::cout<<"  [--mod-package FILE.gbmod] (exact-ROM native enhancement manifest)\n";
                std::cout<<"  [--link-lobby] (LAN / Direct IP with the profile's selected other cartridge)\n";
                return 0;
            } else throw std::runtime_error("unknown option: "+a);
        }
        show_window_errors=window;
        if(window&&!additional_mods.empty())throw std::runtime_error("Choose additional native mods in the launcher; repeated --mod-package is for headless QA");
#if !defined(GBCLLR_SDL) || !defined(GBCLLR_NETPLAY) || !defined(RECOMP_LAUNCHER)
        if(link_lobby)throw std::runtime_error("this build has no shared linked-play launcher");
#endif
#ifdef GBCLLR_SDL
        if(window)gbcllr::desktop_graphics::prepare();
        DesktopConfig desktop_config;
        desktop_config.package_override=mod_package;
        desktop_config.link_lobby_requested=link_lobby;
        for(;;) {
        if(window)opts.mods.clear();
        if(window&&!desktop_prepare(desktop_config,rom,boot,save,profile,!skip_launcher,force_launcher,model_explicit,opts))return 0;
#ifdef GBCLLR_NETPLAY
        if(window&&desktop_config.link_retry){force_launcher=true;skip_launcher=false;continue;}
        if(window&&desktop_config.net_start) {
            int result=0;
            try {result=desktop_network(*desktop_config.net_start,muted,desktop_config);}
            catch(const std::exception& error){gbcllr::diagnostic_failure(error.what());desktop_config.link_error=error.what();result=2;}
            if(result==1||result==2){desktop_config.link_lobby_requested=true;force_launcher=true;skip_launcher=false;continue;}
            return result;
        }
#endif
#endif
        if(rom.empty())throw std::runtime_error("--rom is required");
        opts.boot_rom=boot.empty()?std::vector<uint8_t>{}:gbcllr::read_file(boot);
        auto image=gbcllr::read_file(rom);
        if(!mod_package.empty()&&!window)opts.mods.push_back(gbcllr::read_mod_package(mod_package,gbcllr::sha256(image)).feature);
        for(const auto& package:additional_mods)opts.mods.push_back(gbcllr::read_mod_package(package,gbcllr::sha256(image)).feature);
        gbcllr::Machine m(image,opts);
        if(!save.empty() && std::filesystem::exists(save)) {
            std::string error;if(!m.load_battery(gbcllr::read_file(save),error))throw std::runtime_error(error);
        }
        m.trace_enabled(!trace.empty());
        auto start=std::chrono::steady_clock::now();
        bool qa_rewound=false;
        int desktop_result=0;
        if(!commands.empty())gbcllr::execute_commands(m,commands,&qa_rewound);
        if(qa_rewound&&!save.empty())throw std::runtime_error("native-save output is disabled after QA rewind; omit --save");
        if(window) {
#ifdef GBCLLR_SDL
            desktop_result=desktop(m,save,muted,desktop_config);
#else
            throw std::runtime_error("this build has no SDL desktop host");
#endif
        } else for(uint64_t f=0;f<frames;++f) {m.run_frame();m.take_audio();}
        double seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();
        if(!frame.empty())m.write_frame(frame);
        if(!trace.empty()) {std::ofstream out(trace);for(auto offset:m.take_trace())out<<std::hex<<offset<<'\n';if(!out)throw std::runtime_error("trace write failed");}
        if(!save.empty()) {auto data=m.battery();if(!data.empty())gbcllr::write_file_atomic(save,data);}
#ifdef GBCLLR_SDL
        if(desktop_result==1) {force_launcher=true;skip_launcher=false;commands.clear();continue;}
#endif
        auto stats=m.stats();auto r=m.registers();
        std::cout<<"{\"rom_sha256\":\""<<m.rom_identity()<<"\",\"digest\":\""<<m.digest()
          <<"\",\"cycles\":"<<stats.cycles<<",\"native\":"<<stats.instructions_native
          <<",\"fallback\":"<<stats.instructions_fallback<<",\"seconds\":"<<seconds
          <<",\"pc\":"<<stats.pc<<",\"rom_bank\":"<<stats.rom_bank
          <<",\"chained\":"<<stats.instructions_chained<<",\"registers\":[";
        for(size_t i=0;i<r.size();++i)std::cout<<(i?",":"")<<unsigned(r[i]);
        std::cout<<"],\"serial_hex\":\"";
        constexpr char hex[]="0123456789abcdef";
        for(unsigned char c:m.serial_output())std::cout<<hex[c>>4]<<hex[c&15];
        std::cout<<"\",\"cartridge_ram_hex\":\"";
        auto ram=m.cartridge_ram();
        for(auto c:ram.first(std::min(ram.size(),size_t(256))))std::cout<<hex[c>>4]<<hex[c&15];
        std::cout<<"\"}\n";
        return 0;
#ifdef GBCLLR_SDL
        }
#endif
    } catch(const std::exception& e) {
        auto diagnostic_folder=gbcllr::diagnostic_failure(e.what());std::cerr<<"gbcllr: "<<e.what()<<'\n';
#ifdef GBCLLR_SDL
        if(show_window_errors&&!std::getenv("LNG_TEST_HIDDEN")){
            std::string message=e.what();if(!diagnostic_folder.empty())message+="\n\nLocal diagnostic logs: "+diagnostic_folder.string();
            SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR,"Game could not continue",message.c_str(),nullptr);
        }
#endif
        return 1;
    }
}
