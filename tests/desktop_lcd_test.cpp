// Manual hardware qualification of the actual owned SDL device and CPU
// reference. No cartridge, owner profile, audio device or native dialogs.
#ifndef SDL_MAIN_HANDLED
#define SDL_MAIN_HANDLED
#endif
#include "../src/desktop_lcd.hpp"
#include "../src/desktop_graphics.hpp"
#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <vector>
#include <SDL_opengl.h>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <SDL_system.h>
#include <d3d9.h>
#endif

static void check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
// Exercise the GL handoff with non-default state on an unchanged source. SDL
// may legitimately change its texture state while creating/uploading a source.
static void gl_state_contract(SDL_Renderer* renderer,DesktopLcd& gpu,const gbcllr::PresentationFrame& input,const SDL_Rect& dest){
    SDL_RendererInfo info{};SDL_GetRendererInfo(renderer,&info);
    if(!info.name||std::string_view(info.name)!="opengl")return;
#define PROC(ret,name,args) auto name=reinterpret_cast<ret(APIENTRY*)args>(SDL_GL_GetProcAddress("gl" #name));check(name,"GL state entry point unavailable");
    PROC(void,GetIntegerv,(GLenum,GLint*)) PROC(void,GetBooleanv,(GLenum,GLboolean*)) PROC(void,GetFloatv,(GLenum,GLfloat*))
    PROC(GLboolean,IsEnabled,(GLenum)) PROC(void,Enable,(GLenum)) PROC(void,Disable,(GLenum))
    PROC(void,ActiveTexture,(GLenum)) PROC(void,BindTexture,(GLenum,GLuint)) PROC(void,GenTextures,(GLsizei,GLuint*)) PROC(void,DeleteTextures,(GLsizei,const GLuint*))
    PROC(void,Viewport,(GLint,GLint,GLsizei,GLsizei)) PROC(void,PolygonMode,(GLenum,GLenum))
    PROC(void,ColorMask,(GLboolean,GLboolean,GLboolean,GLboolean)) PROC(void,TexCoord4fv,(const GLfloat*))
#undef PROC
    constexpr GLenum caps[]={GL_BLEND,GL_DEPTH_TEST,GL_STENCIL_TEST,GL_SCISSOR_TEST,GL_CULL_FACE,GL_ALPHA_TEST};
    GLint active=0,binding=0,viewport[4]{},polygon[2]{},program=0;GLboolean enabled[6]{},texture_enabled=0,color[4]{};GLfloat coords[4]{};
    GetIntegerv(GL_ACTIVE_TEXTURE,&active);ActiveTexture(GL_TEXTURE0);GetIntegerv(GL_TEXTURE_BINDING_2D,&binding);texture_enabled=IsEnabled(GL_TEXTURE_2D);
    GetIntegerv(GL_VIEWPORT,viewport);GetIntegerv(GL_POLYGON_MODE,polygon);GetIntegerv(GL_CURRENT_PROGRAM,&program);GetBooleanv(GL_COLOR_WRITEMASK,color);GetFloatv(GL_CURRENT_TEXTURE_COORDS,coords);
    for(unsigned i=0;i<6;++i){enabled[i]=IsEnabled(caps[i]);Enable(caps[i]);}
    GLuint sentinel=0;GenTextures(1,&sentinel);BindTexture(GL_TEXTURE_2D,sentinel);Enable(GL_TEXTURE_2D);
    constexpr GLfloat primed_coords[]={0.25f,0.5f,0.75f,1};TexCoord4fv(primed_coords);
    Viewport(2,3,101,97);PolygonMode(GL_FRONT,GL_LINE);PolygonMode(GL_BACK,GL_POINT);ColorMask(GL_FALSE,GL_TRUE,GL_FALSE,GL_TRUE);ActiveTexture(GL_TEXTURE1);
    auto uploaded=gpu.upload_bytes();check(gpu.draw(input,dest)&&gpu.upload_bytes()==uploaded,"GL state test uploaded unchanged source");
    GLint value=0,values[4]{};GLboolean mask[4]{};GLfloat restored_coords[4]{};
    GetIntegerv(GL_ACTIVE_TEXTURE,&value);check(value==GL_TEXTURE1,"LCD changed active texture unit");ActiveTexture(GL_TEXTURE0);
    GetIntegerv(GL_TEXTURE_BINDING_2D,&value);check(GLuint(value)==sentinel&&IsEnabled(GL_TEXTURE_2D),"LCD changed unit-zero texture ownership");
    GetIntegerv(GL_CURRENT_PROGRAM,&value);check(value==program,"LCD changed current GL program");
    GetIntegerv(GL_VIEWPORT,values);check(values[0]==2&&values[1]==3&&values[2]==101&&values[3]==97,"LCD changed viewport");
    GetIntegerv(GL_POLYGON_MODE,values);check(values[0]==GL_LINE&&values[1]==GL_POINT,"LCD changed polygon modes");
    GetBooleanv(GL_COLOR_WRITEMASK,mask);check(!mask[0]&&mask[1]&&!mask[2]&&mask[3],"LCD changed color write mask");
    GetFloatv(GL_CURRENT_TEXTURE_COORDS,restored_coords);check(std::equal(std::begin(primed_coords),std::end(primed_coords),restored_coords),"LCD changed current texture coordinates");
    for(auto cap:caps)check(IsEnabled(cap),"LCD changed enabled capability");
    for(unsigned i=0;i<6;++i)(enabled[i]?Enable:Disable)(caps[i]);
    Viewport(viewport[0],viewport[1],viewport[2],viewport[3]);PolygonMode(GL_FRONT,GLenum(polygon[0]));PolygonMode(GL_BACK,GLenum(polygon[1]));ColorMask(color[0],color[1],color[2],color[3]);TexCoord4fv(coords);
    BindTexture(GL_TEXTURE_2D,GLuint(binding));(texture_enabled?Enable:Disable)(GL_TEXTURE_2D);DeleteTextures(1,&sentinel);ActiveTexture(GLenum(active));
    std::cout<<"PASS explicit GL state and SDL texture handoff\n";
}
static void gpu_time(SDL_Window* window,SDL_Renderer* renderer,DesktopLcd& gpu){
#ifdef _WIN32
    auto device=std::unique_ptr<IDirect3DDevice9,void(*)(IDirect3DDevice9*)>(SDL_RenderGetD3D9Device(renderer),[](auto* p){if(p)p->Release();});
    if(!device)return;
    struct Queries{
        IDirect3DQuery9 *start=nullptr,*end=nullptr,*frequency=nullptr,*disjoint=nullptr;
        ~Queries(){for(auto p:{start,end,frequency,disjoint})if(p)p->Release();}
    };
    auto read=[](IDirect3DQuery9* query,void* data,DWORD bytes){
        auto begin=SDL_GetTicks();HRESULT result;
        while((result=query->GetData(data,bytes,D3DGETDATA_FLUSH))==S_FALSE){check(SDL_GetTicks()-begin<2000,"GPU query deadline");SDL_Delay(1);}
        check(SUCCEEDED(result),"GPU timestamp read failed");
    };
    std::vector<uint32_t> source(256*144,0xff98abcd);
    for(auto size:{SDL_Point{1280,720},SDL_Point{2560,1440}}){
        SDL_SetWindowSize(window,size.x,size.y);SDL_PumpEvents();SDL_Event event;while(SDL_PollEvent(&event)){}
        check(!SDL_RenderClear(renderer)&&!SDL_RenderFlush(renderer),"timer viewport reset failed");
        Queries q; // Query resources must not straddle SDL's device Reset.
        if(FAILED(device->CreateQuery(D3DQUERYTYPE_TIMESTAMP,&q.start))||FAILED(device->CreateQuery(D3DQUERYTYPE_TIMESTAMP,&q.end))||FAILED(device->CreateQuery(D3DQUERYTYPE_TIMESTAMPFREQ,&q.frequency))||FAILED(device->CreateQuery(D3DQUERYTYPE_TIMESTAMPDISJOINT,&q.disjoint))){std::cout<<"GPU timestamp queries unavailable\n";return;}
        for(unsigned sample=0;sample<4;++sample){
            SDL_SetRenderDrawColor(renderer,0,0,0,255);SDL_RenderClear(renderer);SDL_RenderFlush(renderer);
            // Warm the resource outside the measured GPU interval. Change a
            // source pixel so the following operation includes native upload.
            check(gpu.draw({256,144,source},{0,0,size.x,size.y}),"timer warmup failed");source[sample]^=0x555555;
            check(SUCCEEDED(q.disjoint->Issue(D3DISSUE_BEGIN))&&SUCCEEDED(q.frequency->Issue(D3DISSUE_END))&&SUCCEEDED(q.start->Issue(D3DISSUE_END)),"GPU timestamp begin failed");
            check(gpu.draw({256,144,source},{0,0,size.x,size.y}),"timed native draw failed");
            check(SUCCEEDED(q.end->Issue(D3DISSUE_END))&&SUCCEEDED(q.disjoint->Issue(D3DISSUE_END)),"GPU timestamp end failed");
            SDL_RenderPresent(renderer);
            UINT64 start=0,end=0,frequency=0;BOOL disjoint=TRUE;
            read(q.start,&start,sizeof start);read(q.end,&end,sizeof end);read(q.frequency,&frequency,sizeof frequency);read(q.disjoint,&disjoint,sizeof disjoint);
            check(!disjoint&&frequency&&end>=start,"invalid/disjoint GPU interval");
            std::cout<<"GPU LCD operation "<<size.x<<'x'<<size.y<<" sample="<<sample<<" ms="<<double(end-start)*1000/frequency<<'\n';
        }
    }
#endif
}
int main(int argc,char** argv){try{
    bool fallback=argc>1&&std::string_view(argv[1])=="--fallback";
    bool bootstrap_failure=argc>1&&std::string_view(argv[1])=="--bootstrap-failure";
    bool preserve=argc>1&&std::string_view(argv[1])=="--bootstrap-preserve";
    auto selected=gbcllr::desktop_graphics::Selection::unchanged;
#ifdef __linux__
    if(bootstrap_failure){
        selected=gbcllr::desktop_graphics::prepare([](int,int){return gbcllr::desktop_graphics::probe_context(99,0);});
        check(selected==gbcllr::desktop_graphics::Selection::software,"WSL context failure did not select CPU presentation");
        check(!std::getenv("GALLIUM_DRIVER"),"failed native context changed the parent Mesa driver");
        fallback=true;
    }else
#endif
    selected=gbcllr::desktop_graphics::prepare();
    if(preserve){check(selected==gbcllr::desktop_graphics::Selection::unchanged,"explicit graphics selection was replaced");std::cout<<"PASS explicit graphics selection preserved\n";return 0;}
    SDL_SetHint(SDL_HINT_WINDOW_NO_ACTIVATION_WHEN_SHOWN,"1");
    bool opengl=argc>1&&std::string_view(argv[1])=="--opengl";
    if(fallback||opengl)SDL_SetHint(SDL_HINT_RENDER_DRIVER,fallback?"software":"opengl");
    check(!SDL_Init(SDL_INIT_VIDEO),SDL_GetError());
    struct Quit{~Quit(){SDL_Quit();}} quit;
    auto window=std::unique_ptr<SDL_Window,decltype(&SDL_DestroyWindow)>(SDL_CreateWindow("LCD qualification",0,0,1300,740,SDL_WINDOW_HIDDEN|SDL_WINDOW_RESIZABLE),SDL_DestroyWindow);
    check(bool(window),SDL_GetError());
    auto renderer=std::unique_ptr<SDL_Renderer,decltype(&SDL_DestroyRenderer)>(DesktopLcd::create_renderer(window.get()),SDL_DestroyRenderer);
    check(bool(renderer),SDL_GetError());
    SDL_RendererInfo info{};SDL_GetRendererInfo(renderer.get(),&info);
    std::cout<<"renderer="<<info.name<<'\n';
    DesktopLcd gpu(renderer.get());
    if(fallback){
        check(!gpu.available(),"unsupported software renderer admitted GPU service");
        gbcllr::PresentationTransform cpu;gbcllr::PresentationSettings settings;settings.effect=3;
        std::vector<uint32_t> source={0xffaabbcc,0xff123456,0xff987654,0xff112233};
        gbcllr::ModView view;view.width=view.height=2;view.pixels=source;
        auto frame=cpu.apply(view,settings,4,4);
        auto texture=std::unique_ptr<SDL_Texture,decltype(&SDL_DestroyTexture)>(SDL_CreateTexture(renderer.get(),SDL_PIXELFORMAT_ARGB8888,SDL_TEXTUREACCESS_STATIC,4,4),SDL_DestroyTexture);
        check(bool(texture),SDL_GetError());check(!SDL_UpdateTexture(texture.get(),nullptr,frame.pixels.data(),16),SDL_GetError());
        SDL_Rect dest{0,0,4,4};check(!SDL_RenderCopy(renderer.get(),texture.get(),nullptr,&dest),SDL_GetError());
        std::vector<uint32_t> actual(16);check(!SDL_RenderReadPixels(renderer.get(),&dest,SDL_PIXELFORMAT_ARGB8888,actual.data(),16),SDL_GetError());
        check(std::equal(actual.begin(),actual.end(),frame.pixels.begin(),frame.pixels.end()),"CPU presentation failed through the software renderer");
        std::cout<<"PASS CPU capability fallback"<<(bootstrap_failure?" after native context failure":"")<<'\n';return 0;
    }
    check(gpu.available(),"native LCD shader unavailable");
    bool bounds=false;for(int i=1;i<argc;++i)bounds|=std::string_view(argv[i])=="--bounds";
    if(bounds){
        std::vector<uint32_t> white(4,0xffffffffu);
        check(!gpu.draw({2,2,white},{0,0,4097,144})&&!gpu.available(),"oversized LCD density bypassed fallback");
        gpu.reset();check(gpu.available()&&gpu.draw({2,2,white},{0,0,16,16}),"LCD failed after density fallback reset");
        std::cout<<"PASS LCD density bounds and reset\n";return 0;
    }
    struct Shape{unsigned sw,sh,dw,dh;};
    constexpr Shape shapes[]={{4,1,12,3},{160,144,641,577},{256,144,1280,720},{160,284,360,641},{256,224,513,447},{32,29,17,13}};
    uint64_t compared=0,different=0;unsigned maximum=0,cases=0;
    for(auto shape:shapes)for(unsigned pattern=0;pattern<3;++pattern){
        std::vector<uint32_t> source(size_t(shape.sw)*shape.sh);
        uint32_t rng=0x97123;
        for(auto& pixel:source){rng=rng*1664525u+1013904223u;pixel=pattern==0?0xffffffffu:pattern==1?(0xff000000u|((rng>>30)*85)*0x10101u):0xff000000u|(rng&0xffffffu);}
        gbcllr::ModView view;view.width=shape.sw;view.height=shape.sh;view.pixels=source;
        gbcllr::PresentationSettings settings;settings.effect=3;
        settings.palette=pattern==1?5:0;settings.correction=pattern==2?2:0;
        gbcllr::PresentationTransform reference,prepared;
        auto expected=reference.apply(view,settings,shape.dw,shape.dh);
        auto input_settings=settings;input_settings.effect=0;
        auto input=prepared.apply(view,input_settings,shape.dw,shape.dh);
        SDL_Rect dest{7,9,int(shape.dw),int(shape.dh)};
        SDL_SetRenderDrawColor(renderer.get(),3,5,7,255);check(!SDL_RenderClear(renderer.get()),SDL_GetError());
        auto before=gpu.upload_bytes();check(gpu.draw(input,dest),"actual LCD draw failed");
        auto uploaded=gpu.upload_bytes();check(uploaded-before==source.size()*4,"upload was not native-sized");
        check(gpu.draw(input,dest)&&gpu.upload_bytes()==uploaded,"unchanged source uploaded again");
        if(cases==0)gl_state_contract(renderer.get(),gpu,input,dest);
        std::vector<uint32_t> actual(size_t(shape.dw)*shape.dh);
        check(!SDL_RenderReadPixels(renderer.get(),&dest,SDL_PIXELFORMAT_ARGB8888,actual.data(),shape.dw*4),SDL_GetError());
        for(size_t i=0;i<actual.size();++i)for(unsigned shift:{0u,8u,16u}){
            unsigned a=(actual[i]>>shift)&255,b=(expected.pixels[i]>>shift)&255;
            unsigned delta=a>b?a-b:b-a;maximum=std::max(maximum,delta);different+=delta!=0;++compared;
            check(delta<=1,"GPU LCD differs by more than one channel level from double reference");
        }
        // Subsequent SDL/UI commands must see their original state/cache.
        SDL_Rect overlay{1,1,2,2};SDL_SetRenderDrawColor(renderer.get(),255,0,255,255);SDL_RenderFillRect(renderer.get(),&overlay);
        uint32_t sample[4]{};check(!SDL_RenderReadPixels(renderer.get(),&overlay,SDL_PIXELFORMAT_ARGB8888,sample,8),SDL_GetError());
        for(auto p:sample)check((p&0xffffffu)==0xff00ffu,"native pass corrupted subsequent SDL drawing");
        SDL_RenderPresent(renderer.get());++cases;
    }
    // The owner retains only managed source resources; SDL can reset its
    // default-pool objects on a real window resize without a dangling lease.
    SDL_SetWindowSize(window.get(),800,600);SDL_PumpEvents();SDL_Event event;while(SDL_PollEvent(&event)){}
    SDL_SetRenderDrawColor(renderer.get(),0,0,0,255);check(!SDL_RenderClear(renderer.get()),SDL_GetError());
    std::vector<uint32_t> white(160*144,0xffffffffu);
    check(gpu.draw({160,144,white},{0,0,640,576}),"LCD failed after actual SDL resize/reset");
    gpu.reset();check(gpu.available()&&gpu.draw({160,144,white},{0,0,640,576}),"LCD resource reset failed");
    check(!gpu.draw({0,0,{}},{0,0,640,576})&&!gpu.available(),"failed GPU service remained admitted");
    gpu.reset();check(gpu.available()&&gpu.draw({160,144,white},{0,0,640,576}),"failed service did not recover on device reset");
    std::cout<<"PASS cases="<<cases<<" compared_channels="<<compared<<" changed_channels="<<different<<" max_channel_error="<<maximum<<" gpu_draws="<<gpu.draws()<<" source_upload_bytes="<<gpu.upload_bytes()<<'\n';
    gpu_time(window.get(),renderer.get(),gpu);
    return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
