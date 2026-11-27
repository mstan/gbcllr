#include <gbcllr/diagnostics.hpp>
#include <recomp_diagnostics/session.hpp>
#include <chrono>
#include <cstdlib>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <stdexcept>

#ifndef GBCLLR_DIAGNOSTICS_BUILD
#define GBCLLR_DIAGNOSTICS_BUILD "unidentified-current-build"
#endif
namespace gbcllr {
namespace {
using Bytes=std::vector<uint8_t>;
void u32(Bytes& b,uint32_t n){for(unsigned i=0;i<4;++i)b.push_back(uint8_t(n>>(8*i)));}
void u64(Bytes& b,uint64_t n){u32(b,uint32_t(n));u32(b,uint32_t(n>>32));}
uint32_t get32(std::span<const uint8_t>& b){if(b.size()<4)throw std::runtime_error("truncated diagnostic replay");uint32_t n=0;for(unsigned i=0;i<4;++i)n|=uint32_t(b[i])<<(8*i);b=b.subspan(4);return n;}
void append(Bytes& b,std::string_view text){b.insert(b.end(),text.begin(),text.end());}
Bytes bytes(std::string_view text){return {text.begin(),text.end()};}
std::string stamp(){auto now=std::chrono::system_clock::now();auto value=std::chrono::system_clock::to_time_t(now);std::tm tm{};
#ifdef _WIN32
    gmtime_s(&tm,&value);
#else
    gmtime_r(&value,&tm);
#endif
    std::ostringstream out;out<<std::put_time(&tm,"%Y%m%d-%H%M%S")<<'-'<<std::chrono::duration_cast<std::chrono::microseconds>(now.time_since_epoch()).count();return out.str();}
}
std::string diagnostics_build(){return GBCLLR_DIAGNOSTICS_BUILD;}
std::string diagnostic_machine_metadata(std::string_view identity,const Options& o){
    std::string text="rom_sha256="+std::string(identity)+"\nmodel="+(o.model==Model::cgb?std::string("cgb"):std::string("dmg"))+"\nnative="+std::to_string(o.native)+"\nsgb_service="+std::to_string(o.sgb_service)+"\nseed="+std::to_string(o.seed)+"\nrtc_epoch="+std::to_string(o.rtc_epoch)+"\nboot_sha256="+sha256(o.boot_rom)+"\n";
    for(const auto& m:o.mods)text+="mod="+m.plugin+",abi="+std::to_string(m.abi)+",width="+std::to_string(m.view_width)+",color="+std::to_string(m.colorization)+",gift="+std::to_string(m.gift)+",encounters="+std::to_string(m.expanded_encounters)+",character="+std::to_string(m.player1_character)+",weapon_cycle="+std::to_string(m.weapon_cycle)+",smooth="+std::to_string(m.smooth_gameplay)+",no_flicker="+std::to_string(m.no_flicker)+"\n";
    return text;
}

// Machine checkpoints, input encoding and the GBCLLRR1 codec stay here. The
// shared session owns segment/restore bookkeeping and all storage operations.
struct Diagnostics::Impl {
    std::filesystem::path root;
    std::string metadata;
    std::unique_ptr<recomp::diagnostics::DiagnosticSession> storage;
    recomp::diagnostics::DiagnosticSession::BlobCodec codec;
    Impl(std::filesystem::path path,std::string text,bool recording,Limits limits)
        :root(std::move(path)),metadata(std::move(text)) {
        using Session=recomp::diagnostics::DiagnosticSession;
        auto options=Session::gb_profile(root,stamp());
        options.recording=recording;
        options.queue_bytes=limits.queue_bytes;
        options.history_bytes=limits.history_bytes;
        options.segment_frames=limits.segment_frames;
        options.history_segments=limits.history_segments;
        options.report_segments=limits.report_segments;
        options.blob_header_bytes=metadata.size()+28+64;
        options.log_header="gbcllr local diagnostics\nbuild="+diagnostics_build()+"\n"+metadata+"\n";
        storage=std::make_unique<Session>(std::move(options));
        codec=[metadata=metadata](const Bytes& start,const Bytes& inputs,uint64_t generation,unsigned frames,const Bytes& end) {
            Bytes b=bytes("GBCLLRR1");
            u32(b,uint32_t(metadata.size()));u32(b,uint32_t(start.size()));
            u32(b,frames);u64(b,generation);append(b,metadata);
            b.insert(b.end(),start.begin(),start.end());
            b.insert(b.end(),inputs.begin(),inputs.end());
            append(b,sha256(end));return b;
        };
    }
    void observe(const Machine& machine,const FrameObservation& frame,bool before) noexcept {
        try {
            auto checkpoint=[&machine]{return machine.snapshot();};
            if(before) {
                Bytes input={frame.player1.buttons,frame.player1.virtual_buttons,frame.player2.buttons,frame.player2.virtual_buttons};
                u32(input,frame.width);u32(input,frame.height);u32(input,frame.coop_view);u32(input,frame.coop_actor);
                u32(input,unsigned(frame.shared_world)|(unsigned(frame.hide_closed_rooms)<<1));
                storage->capture_begin(frame.generation,std::move(input),checkpoint,codec);
            } else storage->capture_end(checkpoint);
        } catch(const std::exception& e) { storage->stop(e.what()); }
          catch(...) { storage->stop("recording allocation failed"); }
    }
};
Diagnostics::Diagnostics(std::filesystem::path path,std::string metadata,bool record):Diagnostics(std::move(path),std::move(metadata),record,Limits{}){}
Diagnostics::Diagnostics(std::filesystem::path path,std::string metadata,bool record,Limits limits):impl_(std::make_unique<Impl>(std::move(path),std::move(metadata),record,limits)){}
Diagnostics::~Diagnostics()=default;
FrameObserver Diagnostics::observer()noexcept{return {this,[](void* p,const Machine& m,const FrameObservation& f,bool before)noexcept{static_cast<Diagnostics*>(p)->impl_->observe(m,f,before);},[](void* p,const Machine& m)noexcept{static_cast<Diagnostics*>(p)->finish(m);}};}
void Diagnostics::log(std::string_view text)noexcept{try{impl_->storage->log(text);}catch(...){}}
void Diagnostics::finish(const Machine& machine)noexcept{try{impl_->storage->finish_blob([&machine]{return machine.snapshot();});}catch(...){}}
std::filesystem::path Diagnostics::report(Bytes state,std::span<const uint32_t> pixels,unsigned width,unsigned height,std::string context)noexcept {
    try {
        auto text="gbcllr current-build diagnostic report\nbuild="+diagnostics_build()+"\n"+impl_->metadata+"\n"+context+
            "\nContains execution snapshots and inputs; no source ROM or boot ROM.\nCheckpoints are build specific and may contain in-game progress.\n";
        return impl_->storage->report(std::move(state),pixels.data(),pixels.size(),width,height,std::move(text));
    } catch(...) {return {};}
}
std::string Diagnostics::status()const{return impl_->storage->message();}
bool Diagnostics::recording()const noexcept{return impl_->storage->recording();}
const std::filesystem::path& Diagnostics::folder()const noexcept{return impl_->root;}
void Diagnostics::drain(){impl_->storage->drain();}
bool replay_diagnostic_segment(Machine& m,std::span<const uint8_t> b,std::string& error){try{
    if(b.size()<28+64||std::string_view(reinterpret_cast<const char*>(b.data()),8)!="GBCLLRR1")throw std::runtime_error("invalid diagnostic replay");b=b.subspan(8);auto metadata=get32(b),state=get32(b),frames=get32(b);get32(b);get32(b);if(frames>600||metadata>1024*1024||state>b.size()||size_t(metadata)+state+size_t(frames)*24+64!=b.size())throw std::runtime_error("invalid diagnostic replay lengths");b=b.subspan(metadata);if(!m.restore(b.first(state),error))return false;b=b.subspan(state);
    for(unsigned i=0;i<frames;++i){LogicalInput p1{b[0],b[1]},p2{b[2],b[3]};b=b.subspan(4);auto w=get32(b),h=get32(b),view=get32(b),actor=get32(b),flags=get32(b);m.mod_viewport(w,h);m.mod_coop_view(view,actor);m.mod_coop_shared_world(flags&1);m.mod_oracle_visibility(flags&2);m.run_frame(p1,p2);if(i+1<frames)m.take_audio();}
    auto expected=std::string(reinterpret_cast<const char*>(b.data()),b.size());if(expected!=sha256({})&&m.digest()!=expected){m.take_audio();if(m.digest()!=expected)throw std::runtime_error("diagnostic replay final state mismatch");}error.clear();return true;
}catch(const std::exception& e){error=e.what();return false;}}
std::filesystem::path diagnostic_failure(std::string_view text)noexcept{try{std::filesystem::path root;if(auto p=std::getenv("GBCLLR_DIAGNOSTICS_DIR"))root=p;else root=std::filesystem::temp_directory_path()/"gbcllr-diagnostics";Diagnostics log(root,"startup failure",false);log.log(text);log.drain();return log.status().starts_with("Diagnostics stopped")?std::filesystem::path{}:root;}catch(...){return {};}}
}
