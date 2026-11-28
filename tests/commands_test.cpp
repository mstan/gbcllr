#include <gbcllr/machine.hpp>
#include "native_contract_rom.hpp"
#include <algorithm>
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace gbcllr {void execute_commands(Machine&,const std::filesystem::path&,bool*);}
static void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
int main(){try{
    using namespace gbcllr;
    const auto root=std::filesystem::temp_directory_path()/
        ("gbcllr-commands-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    check(std::filesystem::create_directory(root),"test directory already exists");
    struct Cleanup {
        std::filesystem::path root;
        ~Cleanup(){std::error_code e;for(auto name:{"case.commands","base.state","forbidden.sav","capture.wav"})
            std::filesystem::remove(root/name,e);std::filesystem::remove(root,e);}
    } cleanup{root};
    auto rom=native_contract_rom();Options options;
    Machine machine(rom,options),expected(rom,options);const auto base=machine.snapshot();std::string error;
    auto reset=[&]{check(machine.restore(base,error),"reset failed");check(expected.restore(base,error),"oracle reset failed");};
    auto advance=[&](unsigned frames,uint8_t buttons){for(unsigned i=0;i<frames;++i){expected.run_frame(buttons);expected.take_audio();}};
    auto run=[&](const std::string& text){
        const auto path=root/"case.commands";{std::ofstream out(path);out<<text;check(bool(out),"test profile write failed");}
        bool rewound=false;execute_commands(machine,path,&rewound);return rewound;
    };
    auto rejected=[&](const std::string& text){bool caught=false;try{run(text);}catch(const std::exception&){caught=true;}
        check(caught,"invalid QA history operation was accepted");check(machine.snapshot()==expected.snapshot(),"rejected rewind changed live state");};
    advance(4,1);check(run("history 8\nframes 4 1\nrewind 2\nframes 2 1\n"),"rewind was not reported");
    check(machine.snapshot()==expected.snapshot(),"replay after rewind diverged");
    reset();advance(2,1);advance(1,2);
    run("history 8\nframes 4 1\nrewind 2\nframes 1 2\nrewind 1\nframes 1 2\n");
    check(machine.snapshot()==expected.snapshot(),"rewind branch did not use new inputs");
    reset();advance(5,0);rejected("history 3\nframes 5 0\nrewind 3\n");
    reset();advance(3,0);run("history 3\nframes 5 0\nrewind 2\n");
    check(machine.snapshot()==expected.snapshot(),"oldest retained history target differs");
    for(auto text:{"rewind 1\n","history 1\n","history 301\n","history 3\nrewind 0\n"}){
        reset();rejected(text);
    }
    reset();advance(2,0);rejected("history 4\nframes 2 0\nrewind 1 garbage\n");
    reset();advance(2,0);const std::array<uint8_t,1> value{42};expected.debug_write(0xc000,value);
    rejected("history 4\nframes 2 0\npoke 0xc000 42\nrewind 1\n");
    reset();advance(2,0);auto registers=expected.debug_registers();registers.pc=0x150;expected.debug_registers(registers);
    rejected("history 4\nframes 2 0\npc 0x150\nrewind 1\n");
    reset();advance(2,0);expected.debug_wake();
    rejected("history 4\nframes 2 0\nwake\nrewind 1\n");
    reset();write_file_atomic(root/"base.state",base);
    rejected("history 4\nframes 2 0\nload-state \"base.state\"\nrewind 1\n");
    reset();advance(4,0);
    rejected("history 4\nframes 2 0\ncapture-audio 2 0 \"capture.wav\"\nrewind 1\n");
    reset();rejected("history 4\nframes 1 0\nrewind 1\nexport-save \"forbidden.sav\"\n");
    check(!std::filesystem::exists(root/"forbidden.sav"),"rewound battery was written");
    reset();auto edited=expected.debug_registers();edited.de=uint16_t((edited.de&255)|0xab00);expected.debug_registers(edited);
    run("register-byte d 0xab\n");check(machine.snapshot()==expected.snapshot(),"byte edit changed another register or state");
    rejected("register-byte z 1\n");rejected("register-byte d 256\n");
    reset();expected.set_buttons(32);run("buttons 32\n");
    check(machine.snapshot()==expected.snapshot(),"held input changed simulation time or other state");
    reset();run("pc 0x150\npoke 0xc000 0\nstep 1\n");
    check(machine.stats().pc==0x153,"QA step skipped an instruction boundary");
    run("until-pc 0x157 3 0\n");
    check(machine.stats().pc==0x157&&machine.debug_read(0xc000,1)[0]==1,
          "QA PC wait did not stop after the original memory store");
    reset();advance(2,0);expected.set_buttons(32);rejected("history 4\nframes 2 0\nbuttons 32\nrewind 1\n");
    check(machine.stats().instructions_native>0&&machine.stats().instructions_fallback==0,"commands fixture left native execution");
    std::cout<<"QA rbengine rewind replay, branching, eviction, invalid targets, mutation resets and save guard passed\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
