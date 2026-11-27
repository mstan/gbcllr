#include <gbcllr/diagnostics.hpp>
#include <algorithm>
#include <fstream>
#include <iostream>
#include <stdexcept>
using namespace gbcllr;
static void check(bool ok,const char* text){if(!ok)throw std::runtime_error(text);}
int main(int argc,char** argv){try{
    check(argc==2,"temporary output directory required");auto root=std::filesystem::path(argv[1]);
    std::vector<uint8_t> rom(32768);rom[0x100]=0xc3;rom[0x101]=0x50;rom[0x102]=1;
    const uint8_t loop[]={0xf3,0xaf,0xe0,0x40,0x21,0x00,0xc0,0x34,0x18,0xfd};std::copy(std::begin(loop),std::end(loop),rom.begin()+0x150);
    Options options;options.native=false;Machine live(rom,options),replay(rom,options);
    auto initial=live.snapshot();Diagnostics::Limits limits;limits.segment_frames=3;limits.history_segments=3;limits.history_bytes=initial.size()*4+16384;
    Diagnostics recording(root/"recording",live.diagnostic_metadata());check(recording.recording(),"diagnostics not enabled by default");
    live.frame_observer(recording.observer());
    for(unsigned i=0;i<7;++i){live.mod_viewport(256+i,144);live.mod_coop_view(i%3);live.mod_coop_shared_world(i%2);live.run_frame(LogicalInput{uint8_t(i),uint8_t(i%4)},LogicalInput{uint8_t(i+1),uint8_t((i+2)%4)});live.take_audio();}
    auto before_restore=live.restore_generation();std::string error;check(live.restore(initial,error),error.c_str());check(live.restore_generation()==before_restore+1,"restore generation not advanced");
    for(unsigned i=0;i<4;++i){live.run_frame(LogicalInput{1,3},LogicalInput{2,1});live.take_audio();}
    recording.finish(live);live.frame_observer({});recording.drain();
    unsigned segments=0;for(auto& e:std::filesystem::directory_iterator(root/"recording"))if(e.path().extension()==".replay"){
        auto data=read_file(e.path());check(replay_diagnostic_segment(replay,data,error),error.c_str());++segments;
        data.pop_back();check(!replay_diagnostic_segment(replay,data,error),"truncated replay accepted");
    }check(segments==2,"restore did not retain two independently replayable execution segments");
    auto image=live.pixels();auto report=recording.report(live.snapshot(),image,160,144,"contract report");check(!report.empty(),"report not queued");recording.drain();check(std::filesystem::exists(report),"report missing after drain");
    {
        Diagnostics bounded(root/"bounded",live.diagnostic_metadata(),true,limits);live.frame_observer(bounded.observer());
        for(unsigned i=0;i<21;++i){live.run_frame();live.take_audio();if(i%3==2)bounded.drain();}bounded.finish(live);live.frame_observer({});bounded.drain();
        size_t count=0,total=0;for(auto& e:std::filesystem::directory_iterator(root/"bounded"))if(e.path().extension()==".replay"){++count;total+=e.file_size();}check(count<=3&&total<=limits.history_bytes,"history exceeds its disk bounds");
    }
    {
        auto tiny=limits;tiny.queue_bytes=1;Diagnostics full(root/"full",live.diagnostic_metadata(),true,tiny);live.frame_observer(full.observer());live.run_frame();live.take_audio();full.finish(live);live.frame_observer({});check(!full.recording()&&full.status().find("queue")!=std::string::npos,"queue overflow did not stop recording with warning");
    }
    {
        std::ofstream(root/"not-a-directory")<<"fixture";Diagnostics failed(root/"not-a-directory","storage failure");check(!failed.recording()&&failed.status().find("stopped")!=std::string::npos,"storage failure not reported");
        auto start=live.snapshot();check(replay.restore(start,error),error.c_str());live.frame_observer(failed.observer());live.run_frame(4);replay.run_frame(4);live.frame_observer({});check(live.digest()==replay.digest(),"failed diagnostics changed simulation");
    }
    std::cout<<"diagnostics replay, discontinuity, queue/storage bounds and report passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
