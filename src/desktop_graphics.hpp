#pragma once
#include <SDL.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>

#ifdef __linux__
#include <cerrno>
#include <chrono>
#include <csignal>
#include <sys/utsname.h>
#include <sys/wait.h>
#include <unistd.h>
extern char** environ;
#endif

namespace gbcllr::desktop_graphics {
enum class Selection { unchanged, native, software };
#ifdef __linux__
inline bool explicit_selection() {
    for(char** entry=environ;entry&&*entry;++entry) {
        for(const char* prefix:{"MESA_","LIBGL_","GALLIUM_","__GL_","__GLX_","DRI_","EGL_"})
            if(!std::strncmp(*entry,prefix,std::strlen(prefix)))return true;
    }
    for(const char* key:{"SDL_RENDER_DRIVER","SDL_VIDEODRIVER","SDL_VIDEO_GL_DRIVER","SDL_OPENGL_ES_DRIVER"})
        if(std::getenv(key))return true;
    return false;
}
inline bool wsl_gpu() {
    utsname host{};
    return !uname(&host)&&(std::strstr(host.release,"microsoft")||std::strstr(host.release,"Microsoft"))&&
           !access("/dev/dxg",R_OK);
}
inline bool probe_context(int major,int minor) {
    if(SDL_Init(SDL_INIT_VIDEO))return false;
    SDL_SetHint(SDL_HINT_WINDOW_NO_ACTIVATION_WHEN_SHOWN,"1");
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION,major);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION,minor);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,SDL_GL_CONTEXT_PROFILE_CORE);
    auto* window=SDL_CreateWindow("Graphics check",0,0,16,16,SDL_WINDOW_HIDDEN|SDL_WINDOW_OPENGL);
    auto context=window?SDL_GL_CreateContext(window):nullptr;
    bool ok=context!=nullptr;
    if(context){
        using GetString=const unsigned char*(*)(unsigned);
        auto get=reinterpret_cast<GetString>(SDL_GL_GetProcAddress("glGetString"));
        auto* renderer=get?reinterpret_cast<const char*>(get(0x1f01)):nullptr;
        ok=renderer&&std::strstr(renderer,"D3D12 (")&&!std::strstr(renderer,"Microsoft Basic")&&
           !std::strstr(renderer,"WARP")&&!std::strstr(renderer,"llvmpipe")&&!std::strstr(renderer,"softpipe");
        if(!ok)std::fprintf(stderr,"WSL native graphics unavailable: renderer=%s\n",renderer?renderer:"unknown");
    }else std::fprintf(stderr,"WSL graphics context unavailable: %s\n",SDL_GetError());
    if(context)SDL_GL_DeleteContext(context);
    if(window)SDL_DestroyWindow(window);
    SDL_Quit();return ok;
}
using Probe=bool(*)(int,int);
inline Selection prepare(Probe probe=probe_context) {
    static bool prepared=false;
    if(prepared)return Selection::unchanged;
    prepared=true;
    if(!wsl_gpu()||explicit_selection())return Selection::unchanged;
    // Mesa fixes its driver when first initialized. Probe in a fresh child,
    // leaving the parent free to retain its original GL driver on failure.
    // This runs before the launcher (which requires a core GL3.3 context).
    auto child=fork();
    if(child==0) {
        if(setenv("GALLIUM_DRIVER","d3d12",0))_exit(1);
        _exit(probe(3,3)?0:1);
    }
    bool ok=false;
    if(child>0) {
        auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(10);
        for(;;) {
            int status=0;auto result=waitpid(child,&status,WNOHANG);
            if(result==child){ok=WIFEXITED(status)&&WEXITSTATUS(status)==0;break;}
            if(result<0&&errno!=EINTR)break;
            if(std::chrono::steady_clock::now()>=deadline) {
                kill(child,SIGKILL);while(waitpid(child,&status,0)<0&&errno==EINTR){}
                break;
            }
            usleep(10000);
        }
    }
    if(ok&&!setenv("GALLIUM_DRIVER","d3d12",0)) {
        std::fprintf(stderr,"WSL graphics: Mesa D3D12 selected before SDL initialization\n");
        return Selection::native;
    }
    // The launcher uses GL directly and keeps the unchanged default Mesa
    // path. Its SDL renderer hint affects only the subsequent game window.
    setenv("SDL_RENDER_DRIVER","software",0);
    std::fprintf(stderr,"WSL graphics: native context unavailable; using game CPU presentation\n");
    return Selection::software;
}
#else
inline Selection prepare(){return Selection::unchanged;}
#endif
}
