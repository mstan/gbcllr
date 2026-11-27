#include <gbcllr/machine.hpp>
#include "../src/native_mods.hpp"
#include "../src/xtreme2_coop.hpp"
#include <algorithm>
#include <cstring>
#include <iostream>
#include <stdexcept>

#ifdef GBCLLR_QA_SHA_WRAP
static uint64_t hash_calls;
#ifdef _WIN32
#define SHA_SPAN "St4spanIKhLy18446744073709551615EE"
#else
#define SHA_SPAN "St4spanIKhLm18446744073709551615EE"
#endif
#if _GLIBCXX_USE_CXX11_ABI
#define SHA_ABI "_ZN6gbcllr6sha256B5cxx11E" SHA_SPAN
#else
#define SHA_ABI "_ZN6gbcllr6sha256E" SHA_SPAN
#endif
extern "C" std::string real_sha(std::span<const uint8_t>)
 __asm__("__real_" SHA_ABI);
extern "C" std::string wrap_sha(std::span<const uint8_t>)
 __asm__("__wrap_" SHA_ABI);
extern "C" std::string wrap_sha(std::span<const uint8_t> data){++hash_calls;return real_sha(data);}
#undef SHA_ABI
#undef SHA_SPAN
#endif
static void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
namespace gbcllr {
// An already-open private-room fixture. This tests archive provenance and
// hash work; it does not claim to validate gameplay or menu admission.
struct RewindChildFixture {
 static void attach(NativeMods& mods,std::unique_ptr<Machine> child,std::span<const uint32_t> native){
  auto& coop=*mods.coop_;coop.state_.menu.active=1;
  coop.menu_native_.assign(native.begin(),native.end());coop.menu_copy_=std::move(child);
 }
 static Machine& child(NativeMods& mods){return *mods.coop_->menu_copy_;}
};
}
int main(int argc,char** argv){try{
 using namespace gbcllr;
 check(argc==2||argc==3,"Co-op package and optional owned Xtreme2 ROM required");
 constexpr auto game="e37a14a8e61bd64f2226dacc26a7ee75dd90abfcf88c5016eb97b76ecda0ddca";
 auto feature=read_mod_package(argv[1],game).feature;
 Options options;options.native=false;options.model=Model::cgb;
 std::vector<uint8_t> rom(32768);
 const uint8_t loop[]={0xaf,0xe0,0x40,0x21,0x00,0xc0,0x34,0x18,0xfd};
 std::copy(std::begin(loop),std::end(loop),rom.begin()+0x100);
 if(argc==3){rom=read_file(argv[2]);check(sha256(rom)==game,"Owned cartridge differs");options.model=Model::cgb;options.mods={feature};}
 Machine owner(rom,options);auto child_options=options;child_options.mods.clear();child_options.reference_picture=true;
 auto child=std::make_unique<Machine>(rom,child_options);
 std::array features{feature};NativeMods original(game,features);RewindChildFixture::attach(original,std::move(child),owner.pixels());
 for(unsigned f=0;f<4;++f){auto& live=RewindChildFixture::child(original);live.run_frame();live.take_audio();}
#ifdef GBCLLR_QA_SHA_WRAP
 hash_calls=0;
#endif
 auto file=original.snapshot();
#ifdef GBCLLR_QA_SHA_WRAP
 check(hash_calls>0,"Sealed child checksum positive control was not observed");
#endif
 auto capture=[](const NativeMods& mods){std::vector<uint8_t> out;mods.checkpoint(&out,[](void* p,const uint8_t* b,size_t n){auto& v=*static_cast<std::vector<uint8_t>*>(p);v.insert(v.end(),b,b+n);});return out;};
#ifdef GBCLLR_QA_SHA_WRAP
 auto before=hash_calls;
#endif
 auto local=capture(original);uint32_t size=0;std::memcpy(&size,local.data(),4);check(size==local.size()-4,"Local module size differs");
#ifdef GBCLLR_QA_SHA_WRAP
 check(hash_calls==before,"Local checkpoint hashed its private child");
#endif
 NativeMods restored(game,features);restored.restore(std::span<const uint8_t>(local).subspan(4),false);
#ifdef GBCLLR_QA_SHA_WRAP
 before=hash_calls;
#endif
 check(capture(restored)==local,"Unmaterialized local child did not retain its codec");
#ifdef GBCLLR_QA_SHA_WRAP
 check(hash_calls==before,"Unmaterialized local child was sealed during capture");
#endif
 restored.restore_coop_menu(owner);check(restored.snapshot()==file,"Materialized local child differs from sealed file state");
 auto& live=RewindChildFixture::child(restored);Machine::RewindState checkpoint;live.checkpoint(checkpoint);std::string error;
 std::vector<int16_t> pcm,replay;
 for(unsigned f=0;f<8;++f){live.run_frame();auto a=live.take_audio();pcm.insert(pcm.end(),a.begin(),a.end());}
 auto final=live.snapshot();check(live.restore(checkpoint,error),error.c_str());
 for(unsigned f=0;f<8;++f){live.run_frame();auto a=live.take_audio();replay.insert(replay.end(),a.begin(),a.end());}
 check(live.snapshot()==final&&pcm==replay,"Private child state/PCM replay differs");
 auto unchanged=restored.snapshot(),corrupt=unchanged;const auto identity=live.rom_identity();
 auto at=std::search(corrupt.begin(),corrupt.end(),identity.begin(),identity.end());check(at!=corrupt.end(),"Child identity absent");*at^=1;
 bool refused=false;try{restored.restore(corrupt);}catch(const std::exception&){refused=true;}
 check(refused&&restored.snapshot()==unchanged,"Invalid sealed child changed module state");
 std::cout<<"PASS private child capture/provenance, sealed rejection and exact state/PCM replay";
#ifdef GBCLLR_QA_SHA_WRAP
 std::cout<<"; positive file checksum control and zero local-capture SHA calls";
#endif
 std::cout<<'\n';return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
