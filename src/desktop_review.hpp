#pragma once
// Optional local review controls. Normal launches do not poll files or change
// their input/pause behavior. All requests execute on the desktop frame thread.
#include <gbcllr/machine.hpp>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>

class DesktopReview {
    std::filesystem::path root;
    uint64_t serial=0;
    uint32_t next_poll=0;
    static std::string quoted(std::string_view value) {
        std::string out="\"";
        for(unsigned char ch:value) {
            if(ch=='\\'||ch=='\"'){out+='\\';out+=char(ch);}
            else if(ch<32){out+=" ";}
            else out+=char(ch);
        }
        return out+'\"';
    }
    void reply(uint32_t tick,const gbcllr::Machine& machine,bool menu,std::string_view error={}) {
        std::ostringstream text;
        text<<"{\"id\":"<<serial<<",\"ok\":"<<(error.empty()?"true":"false")
            <<",\"paused\":"<<((hold||menu)?"true":"false")<<",\"frame\":"<<tick
            <<",\"cycles\":"<<machine.stats().cycles<<",\"rom\":"<<quoted(machine.rom_identity());
        text<<",\"renderer\":"<<quoted(renderer_name?renderer_name:"")
            <<",\"lcd_gpu_draws\":"<<lcd_gpu_draws<<",\"lcd_upload_bytes\":"<<lcd_upload_bytes;
        if(!error.empty())text<<",\"error\":"<<quoted(error);
        text<<"}\n";auto bytes=text.str();
        gbcllr::write_file_atomic(root/("reply-"+std::to_string(serial)+".json"),{reinterpret_cast<const uint8_t*>(bytes.data()),bytes.size()});
    }
public:
    const char* renderer_name=nullptr;
    uint64_t lcd_gpu_draws=0,lcd_upload_bytes=0;
    bool hold=false,wants_play=false;
    uint8_t held_buttons=0;
    bool active()const{return !root.empty();}
    explicit DesktopReview(bool single) {
        const char* value=std::getenv("GBCLLR_REVIEW_DIR");
        if(single&&value&&*value){root=value;std::filesystem::create_directories(root);hold=true;}
    }
    template<class Capture,class Reset,class Resize> bool poll(gbcllr::Machine& machine,uint32_t& tick,
        bool& running,bool menu,Capture capture,Reset reset,Resize resize) {
        wants_play=false;
        if(root.empty()||!SDL_TICKS_PASSED(SDL_GetTicks(),next_poll))return false;
        next_poll=SDL_GetTicks()+50;
        // Request IDs, rather than filesystem timestamp resolution, determine
        // freshness. Consecutive GUI actions can share one timestamp.
        std::string line;
        {std::ifstream file(root/("request-"+std::to_string(serial+1)+".txt"));std::getline(file,line);}
        if(line.size()>8192)return false;
        std::istringstream input(line);uint64_t id=0;std::string op,path;
        if(!(input>>id>>op)||id!=serial+1)return false;
        serial=id;bool changed=false;
        try {
            if(op=="state"){}
            else if(op=="pause"){hold=true;changed=true;}
            else if(op=="play"){
                unsigned buttons=0;
                if(input>>buttons){if(buttons>255)throw std::runtime_error("Invalid review buttons");}
                held_buttons=uint8_t(buttons);hold=false;wants_play=true;changed=true;
            }
            else if(op=="quit"){hold=true;running=false;changed=true;}
            else if(op=="resize") {
                unsigned width=0,height=0;
                if(!(input>>width>>height)||width<160||height<144||width>4096||height>2160)throw std::runtime_error("Invalid review window size");
                resize(width,height);
            }
            else if(op=="step") {
                unsigned count=0,buttons=0;
                if(!(input>>count>>buttons)||!count||count>300||buttons>255)throw std::runtime_error("Invalid review step");
                hold=true;
                for(unsigned i=0;i<count;++i){machine.run_frame(uint8_t(buttons));machine.take_audio();++tick;}
                reset();changed=true;
            } else {
                if(!(input>>std::quoted(path))||path.empty())throw std::runtime_error("Missing review file");
                if(op=="loadstate") {
                    std::string error;
                    if(!machine.restore(gbcllr::read_file(path),error))throw std::runtime_error(error);
                    tick=0;hold=true;reset();changed=true;
                } else if(op=="savestate")gbcllr::write_file_atomic(path,machine.snapshot());
                else if(op=="wram")gbcllr::write_file_atomic(path,machine.debug_read(0xc000,8192));
                else if(op=="view") {
                    auto view=machine.mod_view();std::ostringstream header;header<<"P6\n"<<view.width<<' '<<view.height<<"\n255\n";
                    auto text=header.str();std::vector<uint8_t> bytes(text.begin(),text.end());
                    for(auto pixel:view.pixels){bytes.push_back(uint8_t(pixel>>16));bytes.push_back(uint8_t(pixel>>8));bytes.push_back(uint8_t(pixel));}
                    gbcllr::write_file_atomic(path,bytes);
                }
                else if(op=="screenshot")capture(std::filesystem::path(path));
                else throw std::runtime_error("Unknown review request");
            }
            reply(tick,machine,menu&&!wants_play);
        }catch(const std::exception& error){reply(tick,machine,menu,error.what());}
        return changed;
    }
};
