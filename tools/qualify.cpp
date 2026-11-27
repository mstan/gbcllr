#include <gbcllr/machine.hpp>
#include <gbcllr/cable.hpp>
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <algorithm>
#include <fstream>
#include <sstream>

static int cable(int argc,char** argv) {
    if(argc!=8&&argc!=10)throw std::runtime_error("usage: gbcllr-qualify --cable LEFT RIGHT dmg|cgb BOOT CHECKPOINT INPUTS [LEFT_PACKAGE RIGHT_PACKAGE]");
    std::string model=argv[4];
    if(model!="dmg"&&model!="cgb")throw std::runtime_error("invalid cable model");
    gbcllr::Options opts;opts.model=model=="cgb"?gbcllr::Model::cgb:gbcllr::Model::dmg;
    opts.boot_rom=gbcllr::read_file(argv[5]);
    auto left=gbcllr::read_file(argv[2]),right=gbcllr::read_file(argv[3]);
    auto ropts=opts;
    if(argc==10){opts.mods.push_back(gbcllr::read_mod_package(argv[8],gbcllr::sha256(left)).feature);ropts.mods.push_back(gbcllr::read_mod_package(argv[9],gbcllr::sha256(right)).feature);}
    gbcllr::CablePair native(left,opts,right,ropts,gbcllr::CableSchedule::cycle_continuations);
    opts.native=ropts.native=false;
    gbcllr::CablePair ref(left,opts,right,ropts,gbcllr::CableSchedule::cycle_continuations);
    auto initial=gbcllr::read_file(argv[6]);std::string error;
    if(!native.restore(initial,error)||!ref.restore(initial,error))throw std::runtime_error(error);
    std::ifstream input(argv[7]);if(!input)throw std::runtime_error("cannot read cable inputs");
    std::vector<std::array<uint8_t,2>> pads;std::string line;
    while(std::getline(input,line)) {
        std::istringstream in(line);std::string op,extra;if(!(in>>op)||op.starts_with('#'))continue;
        uint64_t count=0;unsigned a=0,b=0;
        if(op!="frames"||!(in>>count>>a>>b)||in>>extra||a>255||b>255||count>1000000-pads.size())
            throw std::runtime_error("cable inputs require bounded frames COUNT LEFT_MASK RIGHT_MASK");
        pads.insert(pads.end(),size_t(count),{uint8_t(a),uint8_t(b)});
    }
    if(pads.empty())throw std::runtime_error("cable inputs must advance frames");
    double seconds=0;
    for(size_t frame=0;frame<pads.size();++frame) {
        auto begin=std::chrono::steady_clock::now();native.run_frame(pads[frame][0],pads[frame][1]);
        seconds+=std::chrono::duration<double>(std::chrono::steady_clock::now()-begin).count();
        ref.run_frame(pads[frame][0],pads[frame][1]);
        if(native.snapshot()!=ref.snapshot())throw std::runtime_error("cable state divergence at frame "+std::to_string(frame));
        for(unsigned side=0;side<2;++side)
            if(native.machine(side).take_audio()!=ref.machine(side).take_audio())
                throw std::runtime_error("cable PCM divergence at frame "+std::to_string(frame));
        if(frame%97==40) {
            auto checkpoint=native.snapshot();auto deadline=native.ticks()+1112;
            native.run_until(deadline);auto expected=native.snapshot();
            auto a=native.machine(0).take_audio(),b=native.machine(1).take_audio();
            if(!native.restore(checkpoint,error))throw std::runtime_error(error);
            native.run_until(deadline);
            if(native.snapshot()!=expected||native.machine(0).take_audio()!=a||native.machine(1).take_audio()!=b)
                throw std::runtime_error("cable mid-frame state/PCM replay divergence");
            if(!native.restore(checkpoint,error))throw std::runtime_error(error);
        }
    }
    for(unsigned side=0;side<2;++side)if(!native.machine(side).stats().instructions_native)
        throw std::runtime_error("cable cartridge has no generated execution");
    std::cout<<"{\"frames\":"<<pads.size()<<",\"continuation\":\""<<native.continuation_backend()
        <<"\",\"pair_sha256\":\""<<gbcllr::sha256(native.snapshot())<<"\",\"native_seconds\":"<<seconds
        <<",\"state_pcm_replay\":\"pass\"}\n";
    return 0;
}

int main(int argc,char** argv) {try {
    if(argc>1&&std::string(argv[1])=="--cable")return cable(argc,argv);
    if(argc<2||argc>8)throw std::runtime_error("usage: gbcllr-qualify ROM [frames=600] [dmg|cgb] [boot-ROM] [checkpoint] [native-package] [additional-package]");
    auto rom=gbcllr::read_file(argv[1]);
    unsigned frames=argc>2?std::stoul(argv[2]):600;
    gbcllr::Options opts;
    if(argc>3) {std::string model=argv[3];if(model!="dmg"&&model!="cgb")throw std::runtime_error("invalid model");opts.model=model=="cgb"?gbcllr::Model::cgb:gbcllr::Model::dmg;}
    if(argc>4)opts.boot_rom=gbcllr::read_file(argv[4]);
    if(argc>6)opts.mods.push_back(gbcllr::read_mod_package(argv[6],gbcllr::sha256(rom)).feature);
    if(argc>7)opts.mods.push_back(gbcllr::read_mod_package(argv[7],gbcllr::sha256(rom)).feature);
    gbcllr::Machine native(rom,opts);opts.native=false;gbcllr::Machine ref(rom,opts);
    if(argc>5) {
        auto checkpoint=gbcllr::read_file(argv[5]);std::string error;
        if(!native.restore(checkpoint,error)||!ref.restore(checkpoint,error))throw std::runtime_error(error);
    }
    // Keep instrumentation matched. A prior bring-up comparison traced only
    // the native path, which cannot support a performance-regression claim.
    double native_seconds=0,ref_seconds=0;
    std::string last;
    for(unsigned frame=0;frame<frames;++frame) {
        // Deterministic attract/menu interaction; not a gameplay completion gate.
        uint8_t buttons=frame%180==120?0x80:frame%180==150?0x10:0;
        auto begin=std::chrono::steady_clock::now();native.run_frame(buttons);
        gbcllr::ModView nv,rv;
        if(!opts.mods.empty())nv=native.mod_view();
        native_seconds+=std::chrono::duration<double>(std::chrono::steady_clock::now()-begin).count();
        begin=std::chrono::steady_clock::now();ref.run_frame(buttons);
        if(!opts.mods.empty())rv=ref.mod_view();
        ref_seconds+=std::chrono::duration<double>(std::chrono::steady_clock::now()-begin).count();
        if(!opts.mods.empty()&&(nv.width!=rv.width||nv.height!=rv.height||nv.expanded!=rv.expanded||nv.pixels.size()!=rv.pixels.size()||
           !std::equal(nv.pixels.begin(),nv.pixels.end(),rv.pixels.begin())))throw std::runtime_error("native view divergence at frame "+std::to_string(frame));
        auto ns=native.snapshot(),rs=ref.snapshot();
        if(ns!=rs) {
            size_t offset=0;while(offset<ns.size()&&offset<rs.size()&&ns[offset]==rs[offset])++offset;
            throw std::runtime_error("state divergence at frame "+std::to_string(frame)+" snapshot byte "+std::to_string(offset));
        }
        if(native.take_audio()!=ref.take_audio())throw std::runtime_error("PCM divergence at frame "+std::to_string(frame));
        if(frame%97==40) {
            auto checkpoint=native.snapshot();auto deadline=native.stats().cycles+1112;
            native.run_until(deadline);auto expected=native.snapshot();auto pcm=native.take_audio();
            std::string error;if(!native.restore(checkpoint,error))throw std::runtime_error(error);
            native.run_until(deadline);
            if(native.snapshot()!=expected || native.take_audio()!=pcm)throw std::runtime_error("mid-frame replay divergence");
            if(!native.restore(checkpoint,error))throw std::runtime_error(error);
        }
    }
    auto stats=native.stats();
    if(!stats.instructions_native)throw std::runtime_error("no generated instructions executed; this is not a native qualification");
    std::cout<<"{\"frames\":"<<frames<<",\"rom_sha256\":\""<<native.rom_identity()
      <<"\",\"digest\":\""<<native.digest()<<"\",\"native\":"<<stats.instructions_native
      <<",\"fallback\":"<<stats.instructions_fallback<<",\"native_seconds\":"<<native_seconds
      <<",\"chained\":"<<stats.instructions_chained
      <<",\"reference_seconds\":"<<ref_seconds<<",\"state_pcm_replay\":\"pass\"}\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
