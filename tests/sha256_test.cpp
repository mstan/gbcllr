#include <gbcllr/machine.hpp>
#include <array>
#include <future>
#include <iostream>
#include <stdexcept>
namespace gbcllr::detail {
std::string portable_sha256(std::span<const uint8_t>);
bool system_sha256(std::span<const uint8_t>,std::array<uint8_t,32>&);
const char* sha256_backend();
}
static void check(bool value,const char* message) {if(!value)throw std::runtime_error(message);}
static void vector(const std::string& input,const char* expected) {
    auto bytes=std::span(reinterpret_cast<const uint8_t*>(input.data()),input.size());
    check(gbcllr::sha256(bytes)==expected,"selected SHA256 known-answer mismatch");
    check(gbcllr::detail::portable_sha256(bytes)==expected,"portable SHA256 known-answer mismatch");
}
int main() {try {
    vector("","e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    vector("abc","ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    vector("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq","248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
    vector(std::string(1000000,'a'),"cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
    std::vector<uint8_t> data(1048576);uint32_t rng=0x651cea5u;
    for(auto& b:data){rng=rng*1664525u+1013904223u;b=uint8_t(rng>>24);}
    for(size_t n:{1,55,56,63,64,65,127,128,129,4096,1048576}) {
        auto input=std::span(data).first(n);
        check(gbcllr::sha256(input)==gbcllr::detail::portable_sha256(input),"SHA256 boundary/multi-block mismatch");
    }
    std::array<std::future<void>,4> threads;
    for(auto& t:threads)t=std::async(std::launch::async,[&]{for(unsigned i=0;i<8;++i) {
        auto input=std::span(data).first(4096+i);
        check(gbcllr::sha256(input)==gbcllr::detail::portable_sha256(input),"concurrent SHA256 differs");
    }});
    for(auto& t:threads)t.get();
    std::array<uint8_t,32> digest;
    std::cout<<"SHA256 known answers, boundaries and concurrent calls pass; system="
             <<gbcllr::detail::system_sha256(data,digest)<<" backend="<<gbcllr::detail::sha256_backend()<<'\n';
    return 0;
} catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
