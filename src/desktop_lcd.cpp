#include "desktop_lcd.hpp"
#include <algorithm>
#include <cstring>
#include <vector>
#if GBCLLR_GPU_LCD
#include "desktop_lcd_gl.hpp"
class DesktopLcd::GlOwner:public OpenGlLcd {public:using OpenGlLcd::OpenGlLcd;};
#else
class DesktopLcd::GlOwner {};
#endif

#if defined(_WIN32) && GBCLLR_GPU_LCD
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <SDL_system.h>
#include <d3d9.h>
#include <d3dcompiler.h>

namespace {
template<class T> void release(T*& p){if(p){p->Release();p=nullptr;}}
// One fixed program, compiled once per device owner. Palette/correction uses
// the exact integer service on the small source before upload. All four taps
// are explicit point samples: hardware bilinear precision is not the contract.
constexpr char shader_source[]=R"(
sampler2D image : register(s0);
float4 size : register(c0); // source xy, destination zw
float4 main(float2 uv : TEXCOORD0) : COLOR0 {
    float2 source=uv*size.xy;
    float2 base=floor(source-0.5), f=source-0.5-base;
    float2 lo=(clamp(base,0,size.xy-1)+0.5)/size.xy;
    float2 hi=(clamp(base+1,0,size.xy-1)+0.5)/size.xy;
    float3 a=tex2D(image,lo).rgb,b=tex2D(image,float2(hi.x,lo.y)).rgb;
    float3 c=tex2D(image,float2(lo.x,hi.y)).rgb,d=tex2D(image,hi).rgb;
    float2 phase=frac(source);
    float2 edges=min(phase,1-phase)*size.zw/size.xy;
    float edge=saturate(min(edges.x,edges.y));
    float light=0.78+0.22*edge*edge*(3-2*edge);
    float3 rgb=lerp(lerp(a,b,f.x),lerp(c,d,f.x),f.y);
    return float4(floor(rgb*255*light*float3(0.9928,1,0.9856)+0.5)/255,1);
})";
}
struct DesktopLcd::Impl {
    SDL_Renderer* renderer;
    IDirect3DDevice9* device=nullptr;
    IDirect3DPixelShader9* shader=nullptr;
    IDirect3DTexture9* texture=nullptr;
    unsigned width=0,height=0;
    bool tried=false;
    uint64_t count=0,bytes=0;
    std::vector<uint32_t> previous;
    explicit Impl(SDL_Renderer* r):renderer(r){}
    ~Impl(){clear();release(device);}
    void clear(){release(texture);release(shader);previous.clear();width=height=0;}
    void initialize(){
        tried=true;
        if(!device)device=SDL_RenderGetD3D9Device(renderer);
        if(!device)return;
        D3DCAPS9 caps{};
        if(FAILED(device->GetDeviceCaps(&caps))||caps.PixelShaderVersion<D3DPS_VERSION(3,0))return;
        void* compiler=SDL_LoadObject("d3dcompiler_47.dll");
        if(!compiler)return;
        using Compile=HRESULT (WINAPI *)(LPCVOID,SIZE_T,LPCSTR,const D3D_SHADER_MACRO*,ID3DInclude*,LPCSTR,LPCSTR,UINT,UINT,ID3DBlob**,ID3DBlob**);
        auto compile=reinterpret_cast<Compile>(SDL_LoadFunction(compiler,"D3DCompile"));
        ID3DBlob* code=nullptr;ID3DBlob* errors=nullptr;
        if(compile&&SUCCEEDED(compile(shader_source,sizeof(shader_source)-1,"gbcllr_lcd",nullptr,nullptr,"main","ps_3_0",D3DCOMPILE_OPTIMIZATION_LEVEL3|D3DCOMPILE_IEEE_STRICTNESS,0,&code,&errors)))
            device->CreatePixelShader(static_cast<const DWORD*>(code->GetBufferPointer()),&shader);
        release(errors);release(code);SDL_UnloadObject(compiler);
    }
    bool draw(const gbcllr::PresentationFrame& frame,const SDL_Rect& dest){
        if(!shader||!frame.width||!frame.height||frame.pixels.size()!=size_t(frame.width)*frame.height||dest.w<1||dest.h<1||dest.w>4096||dest.h>4096)return false;
        if(SDL_RenderFlush(renderer))return false;
        if(frame.width!=width||frame.height!=height){
            release(texture);previous.clear();width=height=0;
            // SDL's D3D9 device is non-Ex. Managed resources survive its
            // resize/device Reset without retaining any DEFAULT-pool objects.
            if(FAILED(device->CreateTexture(frame.width,frame.height,1,0,D3DFMT_A8R8G8B8,D3DPOOL_MANAGED,&texture,nullptr)))return false;
            width=frame.width;height=frame.height;
        }
        if(previous.size()!=frame.pixels.size()||!std::equal(previous.begin(),previous.end(),frame.pixels.begin())){
            D3DLOCKED_RECT locked{};
            if(FAILED(texture->LockRect(0,&locked,nullptr,0)))return false;
            for(unsigned y=0;y<height;++y)std::memcpy(static_cast<char*>(locked.pBits)+size_t(y)*locked.Pitch,frame.pixels.data()+size_t(y)*width,size_t(width)*4);
            if(FAILED(texture->UnlockRect(0)))return false;
            previous.assign(frame.pixels.begin(),frame.pixels.end());bytes+=size_t(width)*height*4;
        }
        IDirect3DStateBlock9* state=nullptr;
        if(FAILED(device->CreateStateBlock(D3DSBT_ALL,&state)))return false;
        bool ok=true;
        auto set=[&](HRESULT result){ok=ok&&SUCCEEDED(result);};
        int output_width=0,output_height=0;
        if(SDL_GetRendererOutputSize(renderer,&output_width,&output_height)){release(state);return false;}
        D3DVIEWPORT9 viewport{0,0,DWORD(output_width),DWORD(output_height),0,1};
        set(device->SetViewport(&viewport));set(device->SetVertexShader(nullptr));
        set(device->SetPixelShader(shader));set(device->SetTexture(0,texture));
        set(device->SetFVF(D3DFVF_XYZRHW|D3DFVF_TEX1));
        set(device->SetSamplerState(0,D3DSAMP_MINFILTER,D3DTEXF_POINT));
        set(device->SetSamplerState(0,D3DSAMP_MAGFILTER,D3DTEXF_POINT));
        set(device->SetSamplerState(0,D3DSAMP_MIPFILTER,D3DTEXF_NONE));
        set(device->SetSamplerState(0,D3DSAMP_ADDRESSU,D3DTADDRESS_CLAMP));
        set(device->SetSamplerState(0,D3DSAMP_ADDRESSV,D3DTADDRESS_CLAMP));
        set(device->SetSamplerState(0,D3DSAMP_SRGBTEXTURE,FALSE));
        for(auto state_value:{D3DRS_ALPHABLENDENABLE,D3DRS_ALPHATESTENABLE,D3DRS_ZENABLE,D3DRS_STENCILENABLE,D3DRS_SCISSORTESTENABLE,D3DRS_FOGENABLE,D3DRS_LIGHTING,D3DRS_DITHERENABLE,D3DRS_SRGBWRITEENABLE})set(device->SetRenderState(state_value,FALSE));
        set(device->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE));
        set(device->SetRenderState(D3DRS_COLORWRITEENABLE,15));
        float sizes[]={float(width),float(height),float(std::min(dest.w,4096)),float(std::min(dest.h,4096))};
        set(device->SetPixelShaderConstantF(0,sizes,1));
        struct Vertex{float x,y,z,rhw,u,v;};
        float left=dest.x-0.5f,top=dest.y-0.5f,right=left+dest.w,bottom=top+dest.h;
        const Vertex vertices[]={{left,top,0,1,0,0},{right,top,0,1,1,0},{left,bottom,0,1,0,1},{right,bottom,0,1,1,1}};
        if(ok)set(device->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,vertices,sizeof(Vertex)));
        set(state->Apply());
        release(state); // Never retain SDL's default-pool vertex buffers.
        if(ok)++count;
        return ok;
    }
};
#else
struct DesktopLcd::Impl {
    uint64_t count=0,bytes=0;
    explicit Impl(SDL_Renderer*){}
};
#endif
SDL_Renderer* DesktopLcd::create_renderer(SDL_Window* window){
#if GBCLLR_GPU_LCD
    // Respect an explicitly requested backend. Otherwise prefer a provider
    // supported by this build, retaining SDL's normal fallback on failure.
    if(!SDL_GetHint(SDL_HINT_RENDER_DRIVER)){
#ifdef _WIN32
        constexpr const char* preferred="direct3d";
#else
        constexpr const char* preferred="opengl";
#endif
        for(int i=0;i<SDL_GetNumRenderDrivers();++i){SDL_RendererInfo info{};
            if(!SDL_GetRenderDriverInfo(i,&info)&&info.name&&!std::strcmp(info.name,preferred))
                if(auto* r=SDL_CreateRenderer(window,i,SDL_RENDERER_ACCELERATED))return r;
        }
    }
#endif
    auto* r=SDL_CreateRenderer(window,-1,SDL_RENDERER_ACCELERATED);
    return r?r:SDL_CreateRenderer(window,-1,SDL_RENDERER_SOFTWARE);
}
DesktopLcd::DesktopLcd(SDL_Renderer* r):impl_(std::make_unique<Impl>(r)){
#if GBCLLR_GPU_LCD
    gl_=std::make_unique<GlOwner>(r);
#endif
}
DesktopLcd::~DesktopLcd()=default;
bool DesktopLcd::available()const{
#if GBCLLR_GPU_LCD
    if(gl_->available())return true;
#endif
#if defined(_WIN32) && GBCLLR_GPU_LCD
    if(!impl_->tried)impl_->initialize();
    return impl_->shader!=nullptr;
#else
    return false;
#endif
}
bool DesktopLcd::draw(const gbcllr::PresentationFrame& frame,const SDL_Rect& dest){
#if GBCLLR_GPU_LCD
    if(gl_->available()){bool ok=gl_->draw(frame,dest);if(!ok)gl_->invalidate();return ok;}
#endif
#if defined(_WIN32) && GBCLLR_GPU_LCD
    bool ok=impl_->draw(frame,dest);
    if(!ok)impl_->clear(); // Retry only after an explicit device reset.
    return ok;
#else
    return false;
#endif
}
void DesktopLcd::reset(){
#if GBCLLR_GPU_LCD
    gl_->reset();
#endif
#if defined(_WIN32) && GBCLLR_GPU_LCD
    if(!impl_->tried)return;
    auto* current=SDL_RenderGetD3D9Device(impl_->renderer);
    if(current!=impl_->device){
        impl_->clear();release(impl_->device);impl_->device=current;
    }else{
        release(current);release(impl_->texture);impl_->previous.clear();impl_->width=impl_->height=0;
    }
    // Pixel programs survive D3D9 Reset. Recompile only after a failed or
    // replaced device, retaining the same program across ordinary resizes.
    if(!impl_->shader)impl_->initialize();
#endif
}
uint64_t DesktopLcd::draws()const{return impl_->count
#if GBCLLR_GPU_LCD
    +gl_->draws()
#endif
;}
uint64_t DesktopLcd::upload_bytes()const{return impl_->bytes
#if GBCLLR_GPU_LCD
    +gl_->upload_bytes()
#endif
;}
