#include <gbcllr/diagnostics.hpp>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <cstdio>
#include <ctime>
#include <deque>
#include <fstream>
#include <functional>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <thread>

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
std::string number(uint64_t n){std::ostringstream out;out<<std::setfill('0')<<std::setw(12)<<n;return out.str();}
void write(const std::filesystem::path& path,std::span<const uint8_t> data){std::ofstream out(path,std::ios::binary|std::ios::trunc);if(!out||!out.write(reinterpret_cast<const char*>(data.data()),std::streamsize(data.size())))throw std::runtime_error("diagnostics storage write failed");}
uint32_t crc(std::span<const uint8_t> data){uint32_t c=~0u;for(auto v:data){c^=v;for(unsigned i=0;i<8;++i)c=(c>>1)^(0xedb88320u&uint32_t(-int(c&1)));}return ~c;}
struct Zip {
    std::ofstream out;Bytes central;uint32_t offset=0,count=0;
    explicit Zip(const std::filesystem::path& p):out(p,std::ios::binary|std::ios::trunc){if(!out)throw std::runtime_error("cannot create diagnostic report");}
    void put(std::string_view name,std::span<const uint8_t> data){if(data.size()>128*1024*1024)throw std::runtime_error("diagnostic entry too large");
        Bytes h;u32(h,0x04034b50);h.insert(h.end(),{20,0,0,0,0,0,0,0,0,0});auto sum=crc(data);u32(h,sum);u32(h,uint32_t(data.size()));u32(h,uint32_t(data.size()));h.push_back(uint8_t(name.size()));h.push_back(uint8_t(name.size()>>8));h.insert(h.end(),{0,0});append(h,name);
        out.write(reinterpret_cast<const char*>(h.data()),std::streamsize(h.size()));out.write(reinterpret_cast<const char*>(data.data()),std::streamsize(data.size()));if(!out)throw std::runtime_error("diagnostic ZIP write failed");
        u32(central,0x02014b50);central.insert(central.end(),{20,0,20,0,0,0,0,0,0,0,0,0});u32(central,sum);u32(central,uint32_t(data.size()));u32(central,uint32_t(data.size()));central.push_back(uint8_t(name.size()));central.push_back(uint8_t(name.size()>>8));central.insert(central.end(),12,0);u32(central,offset);append(central,name);
        offset+=uint32_t(h.size()+data.size());++count;
    }
    void close(){out.write(reinterpret_cast<const char*>(central.data()),std::streamsize(central.size()));Bytes e;u32(e,0x06054b50);u32(e,0);e.push_back(uint8_t(count));e.push_back(uint8_t(count>>8));e.push_back(uint8_t(count));e.push_back(uint8_t(count>>8));u32(e,uint32_t(central.size()));u32(e,offset);e.insert(e.end(),{0,0});out.write(reinterpret_cast<const char*>(e.data()),std::streamsize(e.size()));out.close();if(!out)throw std::runtime_error("diagnostic ZIP finalization failed");}
};
Bytes screenshot(std::span<const uint32_t> pixels,unsigned width,unsigned height){if(!width||!height||width>4096||height>4096||pixels.size()!=size_t(width)*height)return {};auto b=bytes("P6\n"+std::to_string(width)+" "+std::to_string(height)+"\n255\n");b.reserve(b.size()+pixels.size()*3);for(auto p:pixels){b.push_back(uint8_t(p>>16));b.push_back(uint8_t(p>>8));b.push_back(uint8_t(p));}return b;}
}
std::string diagnostics_build(){return GBCLLR_DIAGNOSTICS_BUILD;}
std::string diagnostic_machine_metadata(std::string_view identity,const Options& o){
    std::string text="rom_sha256="+std::string(identity)+"\nmodel="+(o.model==Model::cgb?std::string("cgb"):std::string("dmg"))+"\nnative="+std::to_string(o.native)+"\nsgb_service="+std::to_string(o.sgb_service)+"\nseed="+std::to_string(o.seed)+"\nrtc_epoch="+std::to_string(o.rtc_epoch)+"\nboot_sha256="+sha256(o.boot_rom)+"\n";
    for(const auto& m:o.mods)text+="mod="+m.plugin+",abi="+std::to_string(m.abi)+",width="+std::to_string(m.view_width)+",color="+std::to_string(m.colorization)+",gift="+std::to_string(m.gift)+",encounters="+std::to_string(m.expanded_encounters)+",character="+std::to_string(m.player1_character)+",weapon_cycle="+std::to_string(m.weapon_cycle)+",smooth="+std::to_string(m.smooth_gameplay)+",no_flicker="+std::to_string(m.no_flicker)+"\n";
    return text;
}
struct Diagnostics::Impl {
    struct Job {size_t size;std::function<void()> run;};
    struct Segment {Bytes start,inputs;uint64_t generation=0,id=0;unsigned frames=0;bool pending=false;};
    std::filesystem::path root,log_path;std::string metadata,session=stamp();Limits limits;
    mutable std::mutex mutex;std::condition_variable wake,idle;std::deque<Job> queue;size_t queued=0;bool stopping=false,working=false;
    std::atomic<bool> active{false},failed{false};std::string message="Local diagnostics ready.";std::thread worker;
    std::unique_ptr<Segment> current;uint64_t next_segment=0,next_report=0;
    std::deque<std::pair<std::filesystem::path,size_t>> history;size_t history_size=0,log_size=0;
    bool failure_logged=false;
    Impl(std::filesystem::path path,std::string text,bool record,Limits cap):root(std::move(path)),metadata(std::move(text)),limits(cap){
        limits.segment_frames=std::clamp(limits.segment_frames,1u,600u);
        try {std::filesystem::create_directories(root);log_path=root/("session-"+session+".log");write(log_path,bytes("gbcllr local diagnostics\nbuild="+diagnostics_build()+"\n"+metadata+"\n"));
            // Only owned flat diagnostics names are eligible; no recursive walk.
            std::vector<std::filesystem::directory_entry> old;std::vector<std::filesystem::path> logs;
            for(auto& e:std::filesystem::directory_iterator(root))if(e.is_regular_file()&&!e.is_symlink()){
                auto n=e.path().filename().string();if(n.starts_with("segment-")&&n.ends_with(".replay"))old.push_back(e);if(n.starts_with("session-")&&n.ends_with(".log"))logs.push_back(e.path());
            }
            std::sort(old.begin(),old.end(),[](auto& a,auto& b){return a.path()<b.path();});for(auto& e:old){auto size=size_t(e.file_size());history.emplace_back(e.path(),size);history_size+=size;}prune();
            std::sort(logs.begin(),logs.end());while(logs.size()>8){std::filesystem::remove(logs.front());logs.erase(logs.begin());}
            active=record;worker=std::thread([this]{work();});
        }catch(const std::exception& e){fail(e.what());}
    }
    ~Impl(){if(worker.joinable()){{std::lock_guard lock(mutex);stopping=true;}wake.notify_one();worker.join();}}
    void fail(std::string_view text) noexcept {active=false;failed=true;try{std::lock_guard lock(mutex);message="Diagnostics stopped: "+std::string(text);std::fprintf(stderr,"%s\n",message.c_str());}catch(...){}}
    bool enqueue(size_t size,std::function<void()> action,bool manual=false) noexcept {try{
        {std::lock_guard lock(mutex);if((failed&&!manual)||!worker.joinable())return false;if(size>limits.queue_bytes||queued>limits.queue_bytes-size){active=false;failed=true;message="Diagnostics stopped: background writer queue is full.";std::fprintf(stderr,"%s\n",message.c_str());return false;}queued+=size;queue.push_back({size,std::move(action)});}wake.notify_one();return true;
    }catch(...){fail("writer queue allocation failed");return false;}}
    void work() noexcept {for(;;){Job job;{std::unique_lock lock(mutex);wake.wait(lock,[&]{return stopping||!queue.empty();});if(queue.empty()){if(stopping)return;continue;}job=std::move(queue.front());queue.pop_front();working=true;}
        try {job.run();}catch(const std::exception& e){fail(e.what());}catch(...){fail("unknown writer failure");}
        if(failed&&!failure_logged){failure_logged=true;try{std::string warning;{std::lock_guard lock(mutex);warning="WARNING: "+message+"\n";}std::ofstream out(log_path,std::ios::binary|std::ios::app);out<<warning;}catch(...){}}
        {std::lock_guard lock(mutex);queued-=job.size;working=false;if(queue.empty())idle.notify_all();}
    }}
    void prune(){while(history.size()>limits.history_segments||history_size>limits.history_bytes){auto item=history.front();history.pop_front();history_size-=item.second;std::filesystem::remove(item.first);}}
    void close(Bytes end){if(!current||!current->frames){current.reset();return;}auto segment=std::shared_ptr<Segment>(current.release());if(segment->pending)end.clear();segment->inputs.resize(size_t(segment->frames)*24);auto cost=segment->start.size()+segment->inputs.size()+end.size()+metadata.size()+1024;
        enqueue(cost,[this,segment,end=std::move(end)]{Bytes b=bytes("GBCLLRR1");u32(b,uint32_t(metadata.size()));u32(b,uint32_t(segment->start.size()));u32(b,segment->frames);u64(b,segment->generation);append(b,metadata);b.insert(b.end(),segment->start.begin(),segment->start.end());b.insert(b.end(),segment->inputs.begin(),segment->inputs.end());append(b,sha256(end));auto path=root/("segment-"+session+"-"+number(segment->id)+".replay");write(path,b);history.emplace_back(path,b.size());history_size+=b.size();prune();});
    }
    void observe(const Machine& machine,const FrameObservation& frame,bool before) noexcept {if(!active)return;try{
        if(before){if(current&&current->generation!=frame.generation){
                // The preceding final state was discarded by restore. Preserve
                // the accepted executions with an explicitly unchecked digest.
                close({});
            }
            if(!current){current=std::make_unique<Segment>();current->start=machine.snapshot();current->generation=frame.generation;current->id=next_segment++;}
            current->pending=true;
            auto& b=current->inputs;b.insert(b.end(),{frame.player1.buttons,frame.player1.virtual_buttons,frame.player2.buttons,frame.player2.virtual_buttons});u32(b,frame.width);u32(b,frame.height);u32(b,frame.coop_view);u32(b,frame.coop_actor);u32(b,unsigned(frame.shared_world)|(unsigned(frame.hide_closed_rooms)<<1));
        }else if(current){current->pending=false;++current->frames;if(current->frames>=limits.segment_frames)close(machine.snapshot());}
    }catch(const std::exception& e){fail(e.what());}catch(...){fail("recording allocation failed");}}
};
Diagnostics::Diagnostics(std::filesystem::path path,std::string metadata,bool record):Diagnostics(std::move(path),std::move(metadata),record,Limits{}){}
Diagnostics::Diagnostics(std::filesystem::path path,std::string metadata,bool record,Limits limits):impl_(std::make_unique<Impl>(std::move(path),std::move(metadata),record,limits)){}
Diagnostics::~Diagnostics()=default;
FrameObserver Diagnostics::observer()noexcept{return {this,[](void* p,const Machine& m,const FrameObservation& f,bool before)noexcept{static_cast<Diagnostics*>(p)->impl_->observe(m,f,before);},[](void* p,const Machine& m)noexcept{static_cast<Diagnostics*>(p)->finish(m);}};}
void Diagnostics::log(std::string_view text)noexcept{try{auto line=stamp()+" "+std::string(text.substr(0,4096))+"\n";impl_->enqueue(line.size(),[p=impl_.get(),line=std::move(line)]{if(p->log_size>1024*1024)return;std::ofstream out(p->log_path,std::ios::binary|std::ios::app);if(!out||!out.write(line.data(),std::streamsize(line.size())))throw std::runtime_error("session log write failed");p->log_size+=line.size();});}catch(...){impl_->fail("log allocation failed");}}
void Diagnostics::finish(const Machine& machine)noexcept{try{if(impl_->current)impl_->close(machine.snapshot());}catch(const std::exception& e){impl_->fail(e.what());}}
std::filesystem::path Diagnostics::report(Bytes state,std::span<const uint32_t> pixels,unsigned width,unsigned height,std::string context)noexcept{try{
    auto image=screenshot(pixels,width,height);auto path=impl_->root/("bug-report-"+impl_->session+"-"+number(++impl_->next_report)+".zip");auto cost=state.size()+image.size()+context.size()+4096;
    if(!impl_->enqueue(cost,[p=impl_.get(),path,state=std::move(state),image=std::move(image),context=std::move(context)]{
        auto temporary=path;temporary+=".partial";try{Zip zip(temporary);zip.put("report.txt",bytes("gbcllr current-build diagnostic report\nbuild="+diagnostics_build()+"\n"+p->metadata+"\n"+context+"\nContains execution snapshots and inputs; no source ROM or boot ROM.\nCheckpoints are build specific and may contain in-game progress.\n"));if(!state.empty())zip.put("current.state",state);if(!image.empty())zip.put("screenshot.ppm",image);zip.put("session.log",read_file(p->log_path));
            auto begin=p->history.size()>p->limits.report_segments?p->history.size()-p->limits.report_segments:0;for(size_t i=begin;i<p->history.size();++i)zip.put("replays/"+p->history[i].first.filename().string(),read_file(p->history[i].first));zip.close();std::filesystem::rename(temporary,path);
            // Keep two user exports; never touch personal settings or saves.
            std::vector<std::filesystem::path> reports;for(auto& e:std::filesystem::directory_iterator(p->root))if(e.is_regular_file()&&!e.is_symlink()){auto n=e.path().filename().string();if(n.starts_with("bug-report-")&&n.ends_with(".zip"))reports.push_back(e.path());}std::sort(reports.begin(),reports.end());while(reports.size()>2){std::filesystem::remove(reports.front());reports.erase(reports.begin());}
            std::lock_guard lock(p->mutex);p->message="Saved bug report: "+path.filename().string();
        }catch(...){std::error_code ec;std::filesystem::remove(temporary,ec);throw;}
    },true))return {};return path;
}catch(const std::exception& e){impl_->fail(e.what());return {};}}
std::string Diagnostics::status()const{std::lock_guard lock(impl_->mutex);return impl_->message;}
bool Diagnostics::recording()const noexcept{return impl_->active;}
const std::filesystem::path& Diagnostics::folder()const noexcept{return impl_->root;}
void Diagnostics::drain(){if(!impl_->worker.joinable())return;std::unique_lock lock(impl_->mutex);impl_->idle.wait(lock,[&]{return impl_->queue.empty()&&!impl_->working;});}
bool replay_diagnostic_segment(Machine& m,std::span<const uint8_t> b,std::string& error){try{
    if(b.size()<28+64||std::string_view(reinterpret_cast<const char*>(b.data()),8)!="GBCLLRR1")throw std::runtime_error("invalid diagnostic replay");b=b.subspan(8);auto metadata=get32(b),state=get32(b),frames=get32(b);get32(b);get32(b);if(frames>600||metadata>1024*1024||state>b.size()||size_t(metadata)+state+size_t(frames)*24+64!=b.size())throw std::runtime_error("invalid diagnostic replay lengths");b=b.subspan(metadata);if(!m.restore(b.first(state),error))return false;b=b.subspan(state);
    for(unsigned i=0;i<frames;++i){LogicalInput p1{b[0],b[1]},p2{b[2],b[3]};b=b.subspan(4);auto w=get32(b),h=get32(b),view=get32(b),actor=get32(b),flags=get32(b);m.mod_viewport(w,h);m.mod_coop_view(view,actor);m.mod_coop_shared_world(flags&1);m.mod_oracle_visibility(flags&2);m.run_frame(p1,p2);if(i+1<frames)m.take_audio();}
    auto expected=std::string(reinterpret_cast<const char*>(b.data()),b.size());if(expected!=sha256({})&&m.digest()!=expected){m.take_audio();if(m.digest()!=expected)throw std::runtime_error("diagnostic replay final state mismatch");}error.clear();return true;
}catch(const std::exception& e){error=e.what();return false;}}
std::filesystem::path diagnostic_failure(std::string_view text)noexcept{try{std::filesystem::path root;if(auto p=std::getenv("GBCLLR_DIAGNOSTICS_DIR"))root=p;else root=std::filesystem::temp_directory_path()/"gbcllr-diagnostics";Diagnostics log(root,"startup failure",false);log.log(text);log.drain();return log.status().starts_with("Diagnostics stopped")?std::filesystem::path{}:root;}catch(...){return {};}}
}
