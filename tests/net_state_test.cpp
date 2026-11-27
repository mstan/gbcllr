#include <gbcllr/cable.hpp>
#include <gbcllr/netplay.hpp>
#include "../src/net_state.hpp"
#include <algorithm>
#include <bit>
#include <iostream>
#include <stdexcept>
extern "C" uint32_t rnet_checksum(const void*,size_t);
using namespace gbcllr;
static void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
static uint64_t word(const std::vector<uint8_t>& x,size_t at){uint64_t n=0;for(unsigned i=0;i<8;++i)n|=uint64_t(x.at(at+i))<<(i*8);return n;}
static void put(std::vector<uint8_t>& x,size_t at,uint64_t n){for(unsigned i=0;i<8;++i)x.at(at+i)=uint8_t(n>>(i*8));}
static void seal(std::vector<uint8_t>& x,size_t at,size_t size){auto hash=sha256(std::span(x).subspan(at,size-64));std::copy(hash.begin(),hash.end(),x.begin()+at+size-64);}
static void rejects(const std::vector<uint8_t>& x){bool bad=false;try{detail::simulation_state(x,true);}catch(const std::runtime_error&){bad=true;}check(bad,"invalid qualified initial checkpoint accepted");}
static void same_digests(const std::vector<uint8_t>& x,bool cycle) {
    auto left=size_t(word(x,16)),right=size_t(word(x,24)),tail=32+left+right;
    uint32_t parts[]{rnet_checksum(x.data(),32),rnet_checksum(x.data()+tail,x.size()-tail-64)};
    std::array<uint32_t,4> old{rnet_checksum(x.data(),x.size()),rnet_checksum(x.data()+32,left),
        rnet_checksum(x.data()+32+left,right),cycle?rnet_checksum(parts,sizeof parts):parts[0]};
    check(detail::simulation_digest(x,cycle)==old,"one-pass digest differs from established network checksums");
}
int main(){try{
    if(std::endian::native!=std::endian::little){std::cout<<"qualified little-endian projection is not selected\n";return 0;}
    std::vector<uint8_t> rom(32768);Options o;o.native=false;
    {Machine single(rom,o);auto state=single.snapshot(),projected=single.simulation_snapshot(),expected=state;
     std::fill_n(expected.begin()+68,64,0);std::fill_n(expected.end()-64,64,0);
     check(projected==expected&&single.snapshot()==state,"Single-world comparison changed guest bytes or mutated native state");
     std::string why;check(!single.restore(projected,why)&&single.snapshot()==state,"Comparison bytes were accepted as a restorable checkpoint");
     const uint8_t value=42;single.debug_write(0xc000,{&value,1});check(single.simulation_snapshot()!=projected,"Single-world comparison omitted guest memory");}
    {auto sgb=o;sgb.sgb_service=true;bool rejected=false,transport=false;NetOptions settings;
     settings.start_transport=[&](auto*){transport=true;return -1;};
     try{NetSession unavailable({rom,rom},{sgb,o},settings);}catch(const std::exception& e){rejected=std::string(e.what()).find("not qualified for connected play")!=std::string::npos;}
     check(rejected&&!transport,"unqualified SGB profile reached connected startup");}
    CablePair pair(rom,o,rom,o,CableSchedule::cycle_continuations);
    auto write=[](Machine& m,uint16_t address,uint8_t value){m.debug_write(address,{&value,1});};
    write(pair.machine(0),0xff01,0xa5);write(pair.machine(1),0xff01,0x3c);
    write(pair.machine(1),0xff02,0x80);write(pair.machine(0),0xff02,0x81);pair.run_until(3500);
    auto state=pair.snapshot();auto before=state;
    auto projected=detail::simulation_state(state,true);
    check(state==before&&projected==detail::simulation_state(state,false),"projection mutated native checkpoint");
    const auto left=size_t(word(state,16)),right=size_t(word(state,24));
    same_digests(state,true);same_digests(projected,true);
    // Check header, machine, continuation and checksum boundaries independently.
    for(size_t at:{size_t(0),size_t(15),size_t(32),size_t(32+left-1),size_t(32+left),size_t(32+left+right),state.size()-65,state.size()-64,state.size()-1}) {
        auto changed=state;changed.at(at)^=0xa5;same_digests(changed,true);
    }
    CablePair ordinary(rom,o,rom,o);same_digests(ordinary.snapshot(),false);
    const size_t descriptor=32+left+right+16;
    auto foreign=state;foreign[32+68]^=1;seal(foreign,32,left);seal(foreign,0,foreign.size());
    check(detail::simulation_state(foreign,false)==projected,"ABI envelope changed simulation identity");
    rejects(foreign); // Each host must still supply its own ABI-bound file.
    for(size_t at:{size_t(8),size_t(32+4),size_t(32+132),size_t(32+196),descriptor+16}){
        auto changed=state;changed.at(at)^=1;
        check(detail::simulation_state(changed,false)!=projected,"guest or continuation state omitted from identity");
    }
    auto events=word(state,descriptor+64);check(events>0,"projection fixture lacks actual wire input");
    auto changed=state;changed.at(descriptor+80+size_t(word(state,descriptor+56))+24)^=1;
    check(detail::simulation_state(changed,false)!=projected,"wire input omitted from identity");
    changed=state;changed.back()^=1;rejects(changed);
    changed=state;changed[32]=7;seal(changed,32,left);seal(changed,0,changed.size());rejects(changed);
    for(auto [at,value]:{std::pair{size_t(16),UINT64_MAX},std::pair{descriptor+56,UINT64_MAX},std::pair{descriptor+64,uint64_t(65537)}}){
        changed=state;put(changed,at,value);seal(changed,0,changed.size());rejects(changed);
    }
    changed=state;changed.resize(25);rejects(changed);
    std::cout<<"qualified state identity retains guest/wire payload,local ABI validation and bounded parser failures\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
