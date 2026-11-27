#include <gbcllr/rewind.hpp>
#include <iostream>
#include <stdexcept>
#include <cstdlib>
#include <new>

// Capture now updates retained state transactionally. Allocation failure after
// an earlier changed page must preserve the previously available checkpoint.
static unsigned fail_after=0;
void* operator new(std::size_t size){if(fail_after&&!--fail_after)throw std::bad_alloc();if(auto* p=std::malloc(size?size:1))return p;throw std::bad_alloc();}
void* operator new[](std::size_t size){return ::operator new(size);}
void operator delete(void* p)noexcept{std::free(p);}
void operator delete[](void* p)noexcept{std::free(p);}
void operator delete(void* p,std::size_t)noexcept{std::free(p);}
void operator delete[](void* p,std::size_t)noexcept{std::free(p);}

static void check(bool pass,const char* message){if(!pass)throw std::runtime_error(message);}
int main(){try{
    using namespace gbcllr;
    std::vector<uint8_t> rom(32768);
    // LCD off, increment WRAM. Restore must also preserve the audio clock.
    const uint8_t body[]={0xaf,0xe0,0x40,0x21,0x00,0xc0,0x34,0x18,0xfd};
    std::copy(std::begin(body),std::end(body),rom.begin()+0x100);
    Options o;o.native=false;Machine m(rom,o),other(rom,o);std::string error;
    Machine::RewindState empty;auto initial=m.snapshot();
    check(!m.restore(empty,error)&&m.snapshot()==initial,"empty rewind changed state");
    for(auto storage:{RewindHistory::Storage::reverse_pages,RewindHistory::Storage::checkpoints}) {
    check(m.restore(initial,error),error.c_str());
    RewindHistory history(3,storage);history.store(m,0);
    std::vector<uint8_t> saved;std::vector<int16_t> pcm;
    for(unsigned f=1;f<=5;++f){m.run_frame();m.take_audio();history.store(m,f);if(f==3)saved=m.snapshot();}
    check(!history.peek(2)&&history.peek(3)&&history.peek(4)&&history.peek(5),"history wrap retained wrong frames");
    auto foreign=other.snapshot();check(!other.restore(*history.peek(3),error)&&other.snapshot()==foreign,"foreign rewind was accepted");
    check(m.restore(*history.peek(3),error),error.c_str());check(m.snapshot()==saved,"rewind differs from external checkpoint");
    for(unsigned f=0;f<4;++f){m.run_frame(uint8_t(f));auto a=m.take_audio();pcm.insert(pcm.end(),a.begin(),a.end());}
    auto expected=m.snapshot();
    check(m.restore(*history.peek(3),error),error.c_str());
    std::vector<int16_t> replay;
    for(unsigned f=0;f<4;++f){m.run_frame(uint8_t(f));auto a=m.take_audio();replay.insert(replay.end(),a.begin(),a.end());}
    check(m.snapshot()==expected&&replay==pcm,"rewind execution/PCM replay differs");
    history.drop_after(3);check(!history.peek(4)&&!history.peek(5)&&history.peek(3),"branch retained abandoned future");
    check(m.restore(*history.peek(3),error),error.c_str());m.run_frame(16);history.store(m,4);
    check(history.peek(4)&&!history.peek(5),"rewind branch did not replace future");
    history.clear();check(!history.peek(3)&&!history.peek(4),"clear retained history");
    history.store(m,10);auto other_state=other.snapshot();history.store(other,11);
    check(!history.peek(10)&&history.peek(11),"history crossed machine owners");
    check(other.restore(*history.peek(11),error)&&other.snapshot()==other_state,"replacement owner checkpoint differs");
    unsigned failures=0;
    for(unsigned fault=1;fault<=20;++fault){
        check(m.restore(initial,error),error.c_str());RewindHistory probe(3,storage);probe.store(m,0);
        m.run_frame();m.take_audio();auto next=m.snapshot();bool failed=false;
        fail_after=fault;
        try{probe.store(m,1);}catch(const std::bad_alloc&){failed=true;}
        fail_after=0;
        if(failed){++failures;check(!probe.peek(1),"failed capture published a frame");}
        check(probe.peek(0)&&m.restore(*probe.peek(0),error)&&m.snapshot()==initial,"failed/complete capture lost prior state");
        if(!failed)check(probe.peek(1)&&m.restore(*probe.peek(1),error)&&m.snapshot()==next,"streamed checkpoint differs");
    }
    if(storage==RewindHistory::Storage::reverse_pages)
        check(failures>=5,"allocation failure paths were not exercised");
    else {
        unsigned initial_failures=0;
        for(unsigned fault=1;fault<=20;++fault) {
            check(m.restore(initial,error),error.c_str());RewindHistory fresh(3,storage);bool failed=false;
            fail_after=fault;
            try{fresh.store(m,0);}catch(const std::bad_alloc&){failed=true;}
            fail_after=0;
            if(failed){++initial_failures;check(!fresh.peek(0),"partial direct ring was published");}
            else check(fresh.peek(0)&&m.restore(*fresh.peek(0),error)&&m.snapshot()==initial,"direct checkpoint initialization differs");
        }
        check(initial_failures>=5,"direct ring allocation failures were not exercised");
    }
    }
    std::cout<<"PASS both history policies: local rewind wrap, branch, owner isolation and exact state/PCM replay\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
