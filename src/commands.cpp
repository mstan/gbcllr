#include <gbcllr/machine.hpp>
#include <gbcllr/cable.hpp>
#include <gbcllr/diagnostics.hpp>
#include <gbcllr/rewind.hpp>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>

namespace gbcllr {
static bool commands(Machine* single,CablePair* pair,const std::filesystem::path& path) {
    std::ifstream input(path);
    if(!input)throw std::runtime_error("cannot read QA commands: "+path.string());
    std::unique_ptr<RewindHistory> history;
    uint32_t history_tick=0;
    bool was_rewound=false;
    auto remember=[&] {
        if(!history)return;
        history->store(*single,history_tick);
    };
    auto reset_history=[&] {
        if(!history)return;
        history->clear();history_tick=0;remember();
    };
    std::string line;unsigned line_number=0,selected=0;
    while(std::getline(input,line)) {
        ++line_number;std::istringstream command(line);std::string operation;
        if(!(command>>operation)||operation.starts_with('#'))continue;
        auto number=[&](uint64_t maximum) {
            std::string text;if(!(command>>text))throw std::runtime_error("missing numeric argument");
            size_t used=0;auto value=std::stoull(text,&used,0);
            if(used!=text.size()||value>maximum)throw std::runtime_error("numeric argument out of range");
            return value;
        };
        auto file=[&] {
            std::string name;if(!(command>>std::quoted(name)))throw std::runtime_error("missing file path");
            return path.parent_path()/std::filesystem::path(name);
        };
        try {
            auto& m=pair?pair->machine(selected):*single;
            if(operation=="select") {
                if(!pair)throw std::runtime_error("select requires a cable pair");
                selected=unsigned(number(1));
            } else if(operation=="require-rom") {
                std::string expected;
                if(!(command>>expected)||expected!=m.rom_identity())throw std::runtime_error("QA profile ROM identity mismatch");
            } else if(operation=="frames") {
                auto count=number(10000000);auto buttons=number(255);
                auto other=pair?number(255):0;
                for(uint64_t i=0;i<count;++i) {
                    if(pair) {pair->run_frame(uint8_t(buttons),uint8_t(other));pair->machine(0).take_audio();pair->machine(1).take_audio();}
                    else {
                        if(history&&history_tick==UINT32_MAX)reset_history();
                        m.run_frame(uint8_t(buttons));m.take_audio();
                        if(history){++history_tick;remember();}
                    }
                }
            } else if(operation=="headroom-costs") {
                std::cout<<m.headroom_costs()<<'\n';
                if(pair){auto c=pair->execution_stats();std::cout<<"{\"continuation_yields\":"<<c.continuation_yields<<",\"idle_yields_elided\":"<<c.idle_yields_elided<<"}\n";}
            } else if(operation=="headroom-report") {
                m.headroom_report(file().string());
            } else if(operation=="audio-costs") {
                std::cout<<m.audio_costs()<<'\n';
            } else if(operation=="wait-costs") {
                std::cout<<m.wait_costs()<<'\n';
            } else if(operation=="picture-costs") {
                std::cout<<m.picture_costs()<<'\n';
            } else if(operation=="capture-audio") {
                if(pair)throw std::runtime_error("audio capture requires a single console");
                auto count=number(3600);auto buttons=number(255);auto output=file();
                if(!count)throw std::runtime_error("audio capture requires at least one frame");
                if(std::filesystem::exists(output))throw std::runtime_error("audio capture requires a new file");
                std::vector<uint8_t> pcm;
                m.take_audio(); // Capture subsequent frames, not previously queued PCM.
                for(uint64_t i=0;i<count;++i) {
                    m.run_frame(uint8_t(buttons));
                    for(auto sample:m.take_audio()) {
                        auto value=uint16_t(sample);
                        pcm.push_back(uint8_t(value));pcm.push_back(uint8_t(value>>8));
                    }
                }
                std::vector<uint8_t> wav;
                auto tag=[&](const char* text) {for(unsigned i=0;i<4;++i)wav.push_back(uint8_t(text[i]));};
                auto little=[&](uint32_t value,unsigned bytes) {for(unsigned i=0;i<bytes;++i)wav.push_back(uint8_t(value>>(8*i)));};
                tag("RIFF");little(uint32_t(pcm.size()+36),4);tag("WAVE");
                tag("fmt ");little(16,4);little(1,2);little(2,2);
                little(48000,4);little(48000*4,4);little(4,2);little(16,2);
                tag("data");little(uint32_t(pcm.size()),4);
                wav.insert(wav.end(),pcm.begin(),pcm.end());write_file_atomic(output,wav);
                reset_history();
            } else if(operation=="history") {
                if(pair)throw std::runtime_error("QA rewind history requires a single machine");
                auto depth=number(300);
                if(depth<2)throw std::runtime_error("QA rewind history depth must be 2..300");
                history=std::make_unique<RewindHistory>(size_t(depth));history_tick=0;remember();
            } else if(operation=="rewind") {
                if(pair)throw std::runtime_error("QA rewind requires a single machine");
                auto count=number(UINT32_MAX);
                std::string extra;if(command>>extra)throw std::runtime_error("unexpected trailing argument");
                if(!history||!count||count>history_tick)throw std::runtime_error("QA rewind target is unavailable");
                auto target=history_tick-uint32_t(count);
                auto data=history->peek(target);
                if(!data)throw std::runtime_error("QA rewind target is unavailable");
                std::string error;
                if(!m.restore(*data,error))throw std::runtime_error(error);
                history->drop_after(target);history_tick=target;was_rewound=true;
            } else if(operation=="step") {
                if(pair)throw std::runtime_error("individual CPU stepping is unavailable in paired profiles; use frames");
                auto count=number(10000000);
                for(uint64_t i=0;i<count;++i)m.debug_step_instruction();
                reset_history();
            } else if(operation=="buttons") {
                if(pair)throw std::runtime_error("individual held inputs require a single console");
                m.set_buttons(uint8_t(number(255)));
                reset_history();
            } else if(operation=="register-byte") {
                std::string name;command>>name;
                const std::string names="afbcdehl";
                auto index=name.size()==1?names.find(name[0]):std::string::npos;
                if(index==std::string::npos)throw std::runtime_error("unknown byte register");
                auto value=uint16_t(number(255));auto r=m.debug_registers();
                uint16_t* pairs[]{&r.af,&r.bc,&r.de,&r.hl};auto& word=*pairs[index/2];
                word=index%2?uint16_t((word&0xff00)|value):uint16_t((word&0xff)|(value<<8));
                m.debug_registers(r);reset_history();
            } else if(operation=="poke") {
                auto address=number(65535);std::vector<uint8_t> values;
                while(command>>std::ws&&!command.eof())values.push_back(uint8_t(number(255)));
                if(values.empty())throw std::runtime_error("poke requires bytes");
                m.debug_write(uint16_t(address),values);
                reset_history();
            } else if(operation=="pc") {
                auto r=m.debug_registers();r.pc=uint16_t(number(65535));m.debug_registers(r);
                reset_history();
            } else if(operation=="registers") {
                Registers r{};
                r.af=uint16_t(number(65535));r.bc=uint16_t(number(65535));
                r.de=uint16_t(number(65535));r.hl=uint16_t(number(65535));
                r.sp=uint16_t(number(65535));r.pc=uint16_t(number(65535));r.ime=uint8_t(number(1));
                m.debug_registers(r);
                reset_history();
            } else if(operation=="wake") {
                m.debug_wake();
                reset_history();
            } else if(operation=="until-pc") {
                if(pair)throw std::runtime_error("individual PC waits are unavailable in paired profiles; use prepared states");
                auto address=number(65535);auto limit=number(10000000);
                int bank=-1;if(command>>std::ws&&!command.eof())bank=int(number(511));
                auto matches=[&] {auto s=m.stats();return s.pc==address&&(bank<0||s.rom_bank==bank);};
                while(!matches()&&limit--)m.debug_step_instruction();
                if(!matches())throw std::runtime_error("PC/bank wait exceeded its instruction limit");
                reset_history();
            } else if(operation=="expect") {
                auto address=number(65535);std::vector<uint8_t> expected;
                while(command>>std::ws&&!command.eof())expected.push_back(uint8_t(number(255)));
                if(expected.empty())throw std::runtime_error("expect requires bytes");
                if(m.debug_read(uint16_t(address),expected.size())!=expected)throw std::runtime_error("guest memory assertion failed");
            } else if(operation=="coop-view") {
                if(pair)throw std::runtime_error("Co-op cameras require a single shared machine");
                auto mode=number(2),actor=number(1);m.mod_coop_view(unsigned(mode),unsigned(actor));
            } else if(operation=="view-width") {
                auto width=number(65536),height=number(65536);m.mod_viewport(unsigned(width),unsigned(height));
            } else if(operation=="sgb-info") {
                auto output=file();const auto v=m.sgb_view();std::ostringstream json;
                json<<"{\"supported\":"<<v.supported<<",\"active\":"<<v.active<<",\"colors_ready\":"<<v.colors_ready
                    <<",\"colors_qualified\":"<<v.colors_qualified<<",\"border_ready\":"<<v.border_ready<<",\"uniform_attributes\":"<<v.uniform_attributes
                    <<",\"packets\":"<<v.packets<<",\"border_revision\":"<<v.border_revision<<",\"mask\":"<<unsigned(v.mask_mode)<<",\"commands\":[";
                for(size_t i=0;i<v.commands.size();++i)json<<(i?",":"")<<v.commands[i];json<<"]}\n";
                auto text=json.str();write_file_atomic(output,{reinterpret_cast<const uint8_t*>(text.data()),text.size()});
            } else if(operation=="capture-sgb-colors"||operation=="capture-sgb-border") {
                auto output=file();const auto v=m.sgb_view();const bool border=operation=="capture-sgb-border";
                if(border?!v.border_ready:!v.colors_ready)throw std::runtime_error("No qualified cartridge-authored SGB output is ready");
                auto pixels=border?v.border:v.colors;unsigned width=border?256:160,height=border?224:144;
                std::ostringstream header;header<<"P6\n"<<width<<' '<<height<<"\n255\n";auto text=header.str();std::vector<uint8_t> bytes(text.begin(),text.end());
                for(auto pixel:pixels){bytes.push_back(uint8_t(pixel>>16));bytes.push_back(uint8_t(pixel>>8));bytes.push_back(uint8_t(pixel));}write_file_atomic(output,bytes);
            } else if(operation=="replay-diagnostic") {
                if(pair)throw std::runtime_error("diagnostic replay requires a single console");
                std::string error;if(!replay_diagnostic_segment(m,read_file(file()),error))throw std::runtime_error(error);
                reset_history();
            } else if(operation=="capture-view") {
                auto output=file();auto view=m.mod_view();
                std::ostringstream header;header<<"P6\n"<<view.width<<' '<<view.height<<"\n255\n";
                auto text=header.str();std::vector<uint8_t> bytes(text.begin(),text.end());
                for(auto pixel:view.pixels){bytes.push_back(uint8_t(pixel>>16));bytes.push_back(uint8_t(pixel>>8));bytes.push_back(uint8_t(pixel));}
                write_file_atomic(output,bytes);
                std::cout<<"{\"view_width\":"<<view.width<<",\"view_height\":"<<view.height<<",\"expanded\":"<<(view.expanded?"true":"false")
                 <<",\"camera\":["<<view.camera_x<<','<<view.camera_y<<"],\"bounds\":["<<view.bound_left<<','<<view.bound_right
                 <<"],\"left\":"<<view.left<<",\"sprites\":"<<view.sprites<<",\"captures\":"<<view.captures
                 <<",\"activation_adjustments\":"<<view.activation_adjustments<<",\"spawn_scans\":"<<view.spawn_scans<<",\"spawn_reads\":"<<view.spawn_reads<<",\"logic_updates\":"<<view.logic_updates
                 <<",\"neighbor_rooms\":"<<view.neighbor_rooms<<",\"neighbor_ticks\":"<<view.neighbor_ticks<<",\"neighbor_failures\":"<<view.neighbor_failures<<"}\n";
            } else if(operation=="expect-view") {
                auto width=number(4096),expanded=number(1);auto view=m.mod_view();
                if(view.width!=width||view.expanded!=bool(expanded))throw std::runtime_error("native view assertion failed");
            } else if(operation=="expect-view-size") {
                auto width=number(4096),height=number(4096),expanded=number(1);auto view=m.mod_view();
                if(view.width!=width||view.height!=height||view.expanded!=bool(expanded))throw std::runtime_error("native view shape assertion failed");
            } else if(operation=="dump") {
                auto address=number(65535);auto size=number(65536);auto output=file();
                auto bytes=m.debug_read(uint16_t(address),size);write_file_atomic(output,bytes);
            } else if(operation=="peek") {
                auto address=number(65535);auto size=number(65536);
                auto bytes=m.debug_read(uint16_t(address),size);
                constexpr char hex[]="0123456789abcdef";
                std::cout<<"{\"address\":"<<address<<",\"data_hex\":\"";
                for(auto value:bytes)std::cout<<hex[value>>4]<<hex[value&15];
                std::cout<<"\"}\n";
            } else if(operation=="export-save") {
                if(was_rewound)throw std::runtime_error("native-save export is disabled after QA rewind");
                auto output=file();
                if(std::filesystem::exists(output))throw std::runtime_error("native-save export requires a new file");
                auto data=m.battery();
                if(data.empty())throw std::runtime_error("cartridge has no native-save data");
                write_file_atomic(output,data);
            } else if(operation=="save-state") {
                auto output=file();auto data=pair?pair->snapshot():m.snapshot();write_file_atomic(output,data);
            } else if(operation=="load-state") {
                auto state=file();std::string error;
                auto data=read_file(state);
                if(!(pair?pair->restore(data,error):m.restore(data,error)))throw std::runtime_error(error);
                reset_history();
            } else if(operation=="capture")m.write_frame(file());
            else throw std::runtime_error("unknown QA command: "+operation);
            std::string extra;if(command>>extra)throw std::runtime_error("unexpected trailing argument");
        } catch(const std::exception& e) {
            throw std::runtime_error(path.string()+":"+std::to_string(line_number)+": "+e.what());
        }
    }
    return was_rewound;
}
void execute_commands(Machine& m,const std::filesystem::path& path,bool* was_rewound) {
    const auto result=commands(&m,nullptr,path);if(was_rewound)*was_rewound=result;
}
void execute_commands(CablePair& pair,const std::filesystem::path& path) {commands(nullptr,&pair,path);}
}
