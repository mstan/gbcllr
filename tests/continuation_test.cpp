#include "../src/continuation.hpp"
#include <array>
#include <cfenv>
#include <cstdint>
#include <iostream>
#if defined(__SSE__)
#include <xmmintrin.h>
#include <emmintrin.h>
#endif
using gbcllr::detail::Continuation;
static void check(bool condition,const char* message) {if(!condition)throw std::runtime_error(message);}
template<class F> static void preserving(F&& call,uint64_t seed) {
#if defined(_WIN64) && defined(__GNUC__) && defined(__x86_64__)
    // Live MS x64 nonvolatiles on both sides of real host/context switches.
    register uint64_t r12 asm("r12")=seed,r15 asm("r15")=~seed;
    register __m128i x6 asm("xmm6")=_mm_set1_epi64x(seed);
    register __m128i x15 asm("xmm15")=_mm_set1_epi64x(~seed);
    asm volatile("":"+r"(r12),"+r"(r15),"+x"(x6),"+x"(x15));
    call();
    asm volatile("":"+r"(r12),"+r"(r15),"+x"(x6),"+x"(x15));
    std::array<uint64_t,2> a{},b{};_mm_storeu_si128(reinterpret_cast<__m128i*>(a.data()),x6);_mm_storeu_si128(reinterpret_cast<__m128i*>(b.data()),x15);
    check(r12==seed&&r15==~seed&&a[0]==seed&&a[1]==seed&&b[0]==~seed&&b[1]==~seed,"MS x64 nonvolatile register corruption");
#else
    call();
#endif
}
struct FloatingState {
    int rounding=std::fegetround();
#if defined(__SSE__)
    unsigned control=_mm_getcsr()&0xffc0;
#endif
    bool operator==(const FloatingState&) const = default;
};
struct Guard {int& count;~Guard(){++count;}};
int main() {try {
    auto original=std::fegetround();std::fesetround(FE_TONEAREST);
    auto parent=FloatingState{};
    int left_visits=0,right_visits=0,unwound=0;
    Continuation *left_ptr=nullptr,*right_ptr=nullptr;
    auto body=[&](Continuation*& self,int mode,int& visits) {
        Guard guard{unwound};std::array<uint64_t,8192> local{};
        for(size_t i=0;i<local.size();++i)local[i]=i*0x102030405060708u+mode;
        check(std::fesetround(mode)==0,"cannot set context rounding mode");
        auto own=FloatingState{};
        for(unsigned round=0;round<2;++round) {
            ++visits;preserving([&]{self->yield();},uint64_t(mode)+round);
            check(FloatingState{}==own,"context floating control state changed");
            for(size_t i=0;i<local.size();++i)
                check(local[i]==i*0x102030405060708u+mode,"suspended stack local changed");
        }
        ++visits;
    };
    {
        Continuation left([&]{body(left_ptr,FE_DOWNWARD,left_visits);});left_ptr=&left;
        Continuation right([&]{body(right_ptr,FE_UPWARD,right_visits);});right_ptr=&right;
        std::cout<<"continuation="<<left.backend()<<'\n';
        for(unsigned round=0;round<3;++round) {
            preserving([&]{left.resume();},round);check(FloatingState{}==parent,"left changed host floating control state");
            preserving([&]{right.resume();},~uint64_t(round));check(FloatingState{}==parent,"right changed host floating control state");
        }
        check(left_visits==3&&right_visits==3&&unwound==2,"context completion did not unwind locals");
    }
    {
        Continuation failed([&]{Guard guard{unwound};throw std::runtime_error("guest test error");});
        bool caught=false;try{failed.resume();}catch(const std::runtime_error& e){caught=std::string(e.what())=="guest test error";}
        check(caught&&unwound==3,"context exception transport/unwind failed");
    }
    {Continuation unused([&]{throw std::runtime_error("unstarted context executed");});}
    check(FloatingState{}==parent,"context teardown changed host floating control state");
    std::fesetround(original);
    std::cout<<"stack locals, floating controls, exceptions and idle teardown passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
