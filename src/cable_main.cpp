#include <gbcllr/cable.hpp>
#include <chrono>
#include <iostream>
#include <stdexcept>

namespace gbcllr {void execute_commands(CablePair&,const std::filesystem::path&);}
int main(int argc,char** argv) {try {
    std::array<std::filesystem::path,2> rom,boot,state,package;
    std::array<gbcllr::Options,2> options;
    auto schedule=gbcllr::CableSchedule::common_instruction;
    std::filesystem::path commands,output;
    uint64_t frames=0;
    for(int i=1;i<argc;++i) {
        std::string a=argv[i];
        auto value=[&] {if(++i>=argc)throw std::runtime_error("missing value for "+a);return std::string(argv[i]);};
        if(a=="--left-rom"||a=="--right-rom")rom[a=="--right-rom"]=value();
        else if(a=="--left-boot"||a=="--right-boot")boot[a=="--right-boot"]=value();
        else if(a=="--left-state"||a=="--right-state")state[a=="--right-state"]=value();
        else if(a=="--left-mod"||a=="--right-mod")package[a=="--right-mod"]=value();
        else if(a=="--left-model"||a=="--right-model") {
            unsigned side=a=="--right-model";auto model=value();
            if(model!="dmg"&&model!="cgb")throw std::runtime_error("model must be dmg or cgb");
            options[side].model=model=="cgb"?gbcllr::Model::cgb:gbcllr::Model::dmg;
        } else if(a=="--reference")options[0].native=options[1].native=false;
        else if(a=="--cycle-continuations")schedule=gbcllr::CableSchedule::cycle_continuations;
        else if(a=="--commands")commands=value();
        else if(a=="--frames")frames=std::stoull(value());
        else if(a=="--output-state")output=value();
        else if(a=="--help") {
            std::cout<<"gbcllr-cable --left-rom FILE --right-rom FILE [--left-boot FILE --right-boot FILE]\n"
              "  [--left-state FILE --right-state FILE] [--left-model dmg|cgb --right-model dmg|cgb]\n"
              "  [--reference] [--cycle-continuations] [--commands FILE] [--frames N] [--output-state FILE]\n"
              "  [--left-mod FILE --right-mod FILE] (qualified Gen2 cycle profile only)\n"
              "Headless experimental cable QA. Profiles: select 0|1; frames COUNT LEFT_MASK RIGHT_MASK.\n"
              "Other paused commands match gbcllr-run. States save/load both consoles atomically.\n";
            return 0;
        } else throw std::runtime_error("unknown option: "+a);
    }
    if(rom[0].empty()||rom[1].empty())throw std::runtime_error("both cartridge paths are required");
    for(unsigned i=0;i<2;++i)if(!boot[i].empty())options[i].boot_rom=gbcllr::read_file(boot[i]);
    std::array images{gbcllr::read_file(rom[0]),gbcllr::read_file(rom[1])};
    for(unsigned i=0;i<2;++i)if(!package[i].empty())options[i].mods.push_back(gbcllr::read_mod_package(package[i],gbcllr::sha256(images[i])).feature);
    gbcllr::CablePair pair(images[0],options[0],images[1],options[1],schedule);
    for(unsigned i=0;i<2;++i)if(!state[i].empty()) {
        std::string error;
        if(!pair.machine(i).restore(gbcllr::read_file(state[i]),error))throw std::runtime_error(error);
    }
    auto started=std::chrono::steady_clock::now();
    if(!commands.empty())gbcllr::execute_commands(pair,commands);
    for(uint64_t i=0;i<frames;++i) {pair.run_frame();pair.machine(0).take_audio();pair.machine(1).take_audio();}
    if(!output.empty())gbcllr::write_file_atomic(output,pair.snapshot());
    const auto execution=pair.execution_stats();
    std::cout<<"{\"continuation\":\""<<pair.continuation_backend()<<"\",\"continuation_yields\":"<<execution.continuation_yields
             <<",\"idle_yields_elided\":"<<execution.idle_yields_elided
             <<",\"wire_peer_bits\":"<<execution.wire_peer_bits<<",\"native_exchanges\":"<<execution.native_exchanges
             <<",\"exchange_handoffs_elided\":"<<execution.exchange_handoffs_elided
             <<",\"ticks\":"<<pair.ticks()<<",\"pair_sha256\":\""<<gbcllr::sha256(pair.snapshot())<<"\",\"seconds\":"
             <<std::chrono::duration<double>(std::chrono::steady_clock::now()-started).count()<<",\"consoles\":[";
    for(unsigned i=0;i<2;++i) {
        auto& m=pair.machine(i);auto s=m.stats();
        std::cout<<(i?",":"")<<"{\"rom_sha256\":\""<<m.rom_identity()<<"\",\"pc\":"<<s.pc
                 <<",\"bank\":"<<s.rom_bank<<",\"native\":"<<s.instructions_native<<",\"fallback\":"<<s.instructions_fallback<<'}';
    }
    std::cout<<"]}\n";return 0;
}catch(const std::exception& e){std::cerr<<"gbcllr-cable: "<<e.what()<<'\n';return 1;}}
