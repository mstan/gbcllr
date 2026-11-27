#include <gbcllr/machine.hpp>
#include <bit>
#include <cstdio>
#include <array>
#include <algorithm>
#include <limits>
#if defined(GBCLLR_SHA256_BCRYPT)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <bcrypt.h>
#elif defined(GBCLLR_SHA256_OPENSSL)
#include <openssl/sha.h>
#endif

namespace gbcllr {
namespace detail {
#if defined(GBCLLR_SHA256_OPENSSL_DLL)
namespace {
struct OpenSslProvider {
    using Hash=unsigned char* (*)(const unsigned char*,size_t,unsigned char*);
    HMODULE module=nullptr;
    Hash hash=nullptr;
    OpenSslProvider() {
        // Search the verified package and Windows system dependencies only;
        // never load an optional service from cwd or an arbitrary PATH entry.
        std::wstring path(32768,L'\0');
        auto length=GetModuleFileNameW(nullptr,path.data(),DWORD(path.size()));
        if(!length||length>=path.size())return;
        path.resize(length);auto slash=path.find_last_of(L"\\/");
        if(slash==std::wstring::npos)return;
        path.resize(slash+1);path+=L"libcrypto-3-x64.dll";
        module=LoadLibraryExW(path.c_str(),nullptr,
            LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_SYSTEM32);
        if(module)hash=reinterpret_cast<Hash>(GetProcAddress(module,"SHA256"));
        if(module&&!hash){FreeLibrary(module);module=nullptr;}
    }
    ~OpenSslProvider(){if(module)FreeLibrary(module);}
};
const OpenSslProvider& openssl_provider(){static const OpenSslProvider provider;return provider;}
}
#endif
const char* sha256_backend() {
#if defined(GBCLLR_SHA256_OPENSSL_DLL)
    if(openssl_provider().hash)return "app-local-openssl3";
#endif
#if defined(GBCLLR_SHA256_BCRYPT)
    return "bcrypt";
#elif defined(GBCLLR_SHA256_OPENSSL)
    return "openssl";
#else
    return "portable";
#endif
}
bool system_sha256(std::span<const uint8_t> data,std::array<uint8_t,32>& digest) {
#if defined(GBCLLR_SHA256_OPENSSL_DLL)
    // Caller-owned output makes concurrent calls independent. OpenSSL chooses
    // supported CPU instructions internally; the algorithm/state bytes remain
    // SHA256 and a missing/failed optional service falls through to BCrypt.
    if(auto hash=openssl_provider().hash;hash&&hash(data.data(),data.size(),digest.data()))return true;
#endif
#if defined(GBCLLR_SHA256_BCRYPT)
    // The provider handle is immutable after initialization. Each call owns
    // its own hash object, so callers on different machines/threads do not
    // share mutable hash state. Provider failure retains the portable path.
    struct Provider {
        BCRYPT_ALG_HANDLE handle=nullptr;
        Provider() {if(BCryptOpenAlgorithmProvider(&handle,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0)handle=nullptr;}
        ~Provider() {if(handle)BCryptCloseAlgorithmProvider(handle,0);}
    };
    static Provider provider;
    BCRYPT_HASH_HANDLE hash=nullptr;
    if(!provider.handle||BCryptCreateHash(provider.handle,&hash,nullptr,0,nullptr,0,0)<0)return false;
    bool ok=true;
    while(!data.empty()) {
        auto n=ULONG(std::min<size_t>(data.size(),std::numeric_limits<ULONG>::max()));
        if(BCryptHashData(hash,const_cast<PUCHAR>(data.data()),n,0)<0){ok=false;break;}
        data=data.subspan(n);
    }
    if(ok)ok=BCryptFinishHash(hash,digest.data(),ULONG(digest.size()),0)>=0;
    BCryptDestroyHash(hash);
    return ok;
#elif defined(GBCLLR_SHA256_OPENSSL)
    return SHA256(data.data(),data.size(),digest.data())!=nullptr;
#else
    return false;
#endif
}
std::string portable_sha256(std::span<const uint8_t> data) {
    constexpr uint32_t k[] = {
      0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
      0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
      0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
      0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
      0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
      0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
      0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
      0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2};
    uint32_t h[] = {0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};
    std::vector<uint8_t> padded(data.begin(), data.end());
    padded.push_back(0x80);
    while (padded.size() % 64 != 56) padded.push_back(0);
    uint64_t bits = uint64_t(data.size()) * 8;
    for (int i=7; i>=0; --i) padded.push_back(uint8_t(bits >> (i*8)));
    for (size_t offset=0; offset<padded.size(); offset+=64) {
        uint32_t w[64];
        for (int i=0;i<16;++i) {
            auto p=padded.data()+offset+i*4;
            w[i]=uint32_t(p[0])<<24|uint32_t(p[1])<<16|uint32_t(p[2])<<8|p[3];
        }
        for (int i=16;i<64;++i) {
            auto a=w[i-15],b=w[i-2];
            w[i]=w[i-16]+(std::rotr(a,7)^std::rotr(a,18)^(a>>3))+w[i-7]
                +(std::rotr(b,17)^std::rotr(b,19)^(b>>10));
        }
        auto a=h[0],b=h[1],c=h[2],d=h[3],e=h[4],f=h[5],g=h[6],v=h[7];
        for(int i=0;i<64;++i) {
            auto t=v+(std::rotr(e,6)^std::rotr(e,11)^std::rotr(e,25))
                +((e&f)^(~e&g))+k[i]+w[i];
            auto u=(std::rotr(a,2)^std::rotr(a,13)^std::rotr(a,22))+((a&b)^(a&c)^(b&c));
            v=g;g=f;f=e;e=d+t;d=c;c=b;b=a;a=t+u;
        }
        uint32_t values[]={a,b,c,d,e,f,g,v};
        for(int i=0;i<8;++i) h[i]+=values[i];
    }
    std::string result;
    for(auto x:h) {char s[9]; std::snprintf(s,sizeof s,"%08x",x); result+=s;}
    return result;
}
}
std::string sha256(std::span<const uint8_t> data) {
    std::array<uint8_t,32> digest;
    if(!detail::system_sha256(data,digest))return detail::portable_sha256(data);
    constexpr char hex[]="0123456789abcdef";
    std::string result(64,'0');
    for(size_t i=0;i<digest.size();++i){result[i*2]=hex[digest[i]>>4];result[i*2+1]=hex[digest[i]&15];}
    return result;
}
}
