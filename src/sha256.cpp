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
    std::array<uint8_t,32> digest;
    (void)rmod_sha256_bytes(data.data(),data.size(),digest.data());
    constexpr char hex[]="0123456789abcdef";
    std::string result(64,'0');
    for(size_t i=0;i<digest.size();++i){result[i*2]=hex[digest[i]>>4];result[i*2+1]=hex[digest[i]&15];}
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
