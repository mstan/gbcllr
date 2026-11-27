#include "desktop_ui.hpp"
#include "desktop_lcd.hpp"
#include <gbcllr/diagnostics.hpp>
#ifdef RECOMP_LAUNCHER
#include "desktop_mods.hpp"
#endif
#include "desktop_net.hpp"
#ifndef SDL_MAIN_HANDLED
#define SDL_MAIN_HANDLED
#endif
#include <SDL.h>
#include "desktop_review.hpp"
#include <gbcllr/rewind.hpp>
#include <algorithm>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <exception>
#include <functional>
#include <iostream>
#include <memory>
#include <stdexcept>
#ifdef RECOMP_LAUNCHER
#include "recomp_runtime_ui.h"
#include "imgui.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_sdlrenderer2.h"
#include "common/launcher_files.h"
#endif

namespace {
bool pad_binding(SDL_GameController* pad,int code,int deadzone,bool shortcut=false) {
    if(!pad||!SDL_GameControllerGetAttached(pad)||code<=0)return false;
    if(code>=1000) {
        unsigned mask=unsigned(code-1000);
        if(!mask||(mask>>SDL_CONTROLLER_BUTTON_MAX))return false;
        for(unsigned b=0;b<SDL_CONTROLLER_BUTTON_MAX;++b)
            if((mask&(1u<<b))&&!SDL_GameControllerGetButton(pad,SDL_GameControllerButton(b)))return false;
        return true;
    }
    if(shortcut&&!SDL_GameControllerGetButton(pad,SDL_CONTROLLER_BUTTON_BACK))return false;
    if(code<100)return code-1<SDL_CONTROLLER_BUTTON_MAX&&SDL_GameControllerGetButton(pad,SDL_GameControllerButton(code-1));
    int axis=(code-100)/2;
    if(axis>=SDL_CONTROLLER_AXIS_MAX)return false;
    auto value=SDL_GameControllerGetAxis(pad,SDL_GameControllerAxis(axis));
    int threshold=std::max(8000,deadzone*32767/100);
    return (code-100)%2?value>threshold:value<-threshold;
}
SDL_GameController* selected_pad(const DesktopConfig& c,bool partner=false,SDL_GameController* reserved=nullptr) {
    const auto& wanted_guid=partner?c.partner_guid:c.gamepad_guid;
    auto wanted_instance=partner?c.partner_instance:c.gamepad_instance;
    if((partner?c.partner_source:c.source)<2)return nullptr;
    int unique=-1,matches=0;
    for(int i=0;i<SDL_NumJoysticks();++i)if(SDL_IsGameController(i)) {
        char guid[64]{};SDL_JoystickGetGUIDString(SDL_JoystickGetDeviceGUID(i),guid,sizeof guid);
        if(!wanted_guid.empty()&&wanted_guid!=guid)continue;
        auto instance=uint32_t(SDL_JoystickGetDeviceInstanceID(i))+1;
        if(reserved&&instance==uint32_t(SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(reserved)))+1)continue;
        if(wanted_instance&&wanted_instance==instance)return SDL_GameControllerOpen(i);
        unique=i;++matches;
    }
    // The launcher closes SDL; only a unique GUID can safely recover a new ID.
    return matches==1?SDL_GameControllerOpen(unique):nullptr;
}
#ifdef RECOMP_LAUNCHER
struct MenuCallbacks {
    std::function<int(const RecompRuntimeUiItem*,int*)> get;
    std::function<int(const RecompRuntimeUiItem*,int)> set;
    std::function<int(const RecompRuntimeUiItem*)> action,enabled;
    std::function<void()> save;
    std::function<void(bool)> visible;
    std::function<int(const RecompRuntimeUiItem*,char*,size_t)> text_get;
    std::function<int(const RecompRuntimeUiItem*,const char*)> text_set;
    static int get_value(void* p,const RecompRuntimeUiItem* i,int* v){return static_cast<MenuCallbacks*>(p)->get(i,v);}
    static int set_value(void* p,const RecompRuntimeUiItem* i,int v){return static_cast<MenuCallbacks*>(p)->set(i,v);}
    static int run_action(void* p,const RecompRuntimeUiItem* i){return static_cast<MenuCallbacks*>(p)->action(i);}
    static int is_enabled(void* p,const RecompRuntimeUiItem* i){return static_cast<MenuCallbacks*>(p)->enabled(i);}
    static void persist(void* p){static_cast<MenuCallbacks*>(p)->save();}
    static void visibility(void* p,int open){static_cast<MenuCallbacks*>(p)->visible(open!=0);}
    static int get_text(void* p,const RecompRuntimeUiItem* i,char* text,size_t size){auto& f=static_cast<MenuCallbacks*>(p)->text_get;return f?f(i,text,size):0;}
    static int set_text(void* p,const RecompRuntimeUiItem* i,const char* text){auto& f=static_cast<MenuCallbacks*>(p)->text_set;return f?f(i,text):0;}
};
#endif
}

static int desktop_loop(gbcllr::Machine* solo,const DesktopNetStart* linked,
    const std::filesystem::path&,bool muted,DesktopConfig& c,const DesktopFrameQa* qa=nullptr) {
    std::filesystem::create_directories(c.profile/"states");int& slot=c.state_slot;
    auto state_path=[&]{return c.profile/"states"/("slot-"+std::to_string(slot)+".state");};
    if(SDL_Init(SDL_INIT_VIDEO|SDL_INIT_AUDIO|SDL_INIT_GAMECONTROLLER))throw std::runtime_error(SDL_GetError());
    struct Quit {~Quit(){SDL_Quit();}} quit;
    const bool network_coop=linked&&linked->options.topology==gbcllr::NetTopology::xtreme2_coop;
    std::string diagnostic_metadata="topology="+std::string(solo?"local":network_coop?"network-coop":"network-cable")+"\n";
    if(solo)diagnostic_metadata+=solo->diagnostic_metadata();
    else for(unsigned i=0;i<(network_coop?1u:2u);++i){
        diagnostic_metadata+="console="+std::to_string(i)+"\n"+gbcllr::diagnostic_machine_metadata(gbcllr::sha256(linked->images[i]),linked->machines[i]);
    }
    diagnostic_metadata+="view="+std::to_string(c.coop_view)+"\nshared_world="+std::to_string(linked?linked->options.coop_shared:c.coop_shared&&c.coop_view==0)+"\n";
    diagnostic_metadata+="audio="+std::to_string(c.audio)+"\nvolume="+std::to_string(c.volume)+"\npresentation="+std::to_string(c.presentation.enabled)+","+std::to_string(c.presentation.palette)+","+std::to_string(c.presentation.correction)+","+std::to_string(c.presentation.effect)+"\n";
    auto diagnostic_folder=c.profile/"diagnostics";
    if(auto override_path=std::getenv("GBCLLR_DIAGNOSTICS_DIR"))diagnostic_folder=override_path;
    gbcllr::Diagnostics diagnostics(diagnostic_folder,diagnostic_metadata,c.diagnostics);
    diagnostics.log("Desktop session starting.");
    if(solo)solo->frame_observer(diagnostics.observer());
    struct DiagnosticBinding {
        gbcllr::Machine* machine;gbcllr::Diagnostics& recorder;
        ~DiagnosticBinding(){if(machine){recorder.finish(*machine);machine->frame_observer({});}recorder.log(std::uncaught_exceptions()?"Desktop session ended with an exception.":"Desktop session stopped.");}
    } diagnostic_binding{solo,diagnostics};
    auto title=desktop_title(solo?solo->rom_identity():gbcllr::sha256(linked->images[network_coop?0:linked->options.machine_slots[linked->options.local_slot]]));
    unsigned network_width=160,network_height=144;
    if(linked&&c.adaptive) {
        const auto& mods=linked->machines[network_coop?0:linked->options.machine_slots[linked->options.local_slot]].mods;
        if(network_coop)network_width=linked->options.coop_width;
        else if(mods.size()==1&&mods[0].plugin=="gbcllr.gen2.adaptive-view")network_width=c.view_width<0?256u:unsigned(c.view_width);
    }
    std::vector<uint32_t> network_pixels(network_width*144,0xff000000u);
    if(solo)solo->mod_oracle_visibility(c.hide_closed_rooms);
    auto frame_view=[&] {if(solo){solo->mod_coop_view(c.coop_view);return solo->mod_view();}gbcllr::ModView view;view.width=network_width;view.height=network_height;view.pixels=network_pixels;return view;};
    const char* activation=SDL_GetHint(SDL_HINT_WINDOW_NO_ACTIVATION_WHEN_SHOWN);
    std::string previous_activation=activation?activation:"";
    if(qa)SDL_SetHint(SDL_HINT_WINDOW_NO_ACTIVATION_WHEN_SHOWN,"1");
    auto initial_view=frame_view();
    unsigned initial_window_width=initial_view.width;
    // A fixed-resolution boot/title picture precedes native world admission.
    // Give adaptive Fit a wide initial canvas rather than locking that first
    // physical aspect into every subsequent world viewport. Explicit native
    // or fixed-width selections retain their existing sizing.
    if(c.package&&c.session_mods&&c.adaptive&&c.view_width<0)
        initial_window_width=std::max(initial_window_width,initial_view.height*16/9);
    auto window=std::unique_ptr<SDL_Window,decltype(&SDL_DestroyWindow)>(SDL_CreateWindow(title.c_str(),SDL_WINDOWPOS_CENTERED,SDL_WINDOWPOS_CENTERED,
        int(initial_window_width)*c.scale,int(initial_view.height)*c.scale,SDL_WINDOW_RESIZABLE|SDL_WINDOW_ALLOW_HIGHDPI|
        ((SDL_getenv("LNG_TEST_HIDDEN")&&SDL_getenv("LNG_SCRIPT"))?SDL_WINDOW_HIDDEN:0)),SDL_DestroyWindow);
    if(qa){if(activation)SDL_SetHint(SDL_HINT_WINDOW_NO_ACTIVATION_WHEN_SHOWN,previous_activation.c_str());else SDL_ResetHint(SDL_HINT_WINDOW_NO_ACTIVATION_WHEN_SHOWN);}
    if(!window)throw std::runtime_error(SDL_GetError());
    auto renderer=std::unique_ptr<SDL_Renderer,decltype(&SDL_DestroyRenderer)>(DesktopLcd::create_renderer(window.get()),SDL_DestroyRenderer);
    if(!renderer)throw std::runtime_error(SDL_GetError());
    DesktopLcd lcd(renderer.get());
    SDL_RendererInfo renderer_info{};
    SDL_GetRendererInfo(renderer.get(),&renderer_info);
    diagnostics.log(std::string("Desktop renderer: ")+(renderer_info.name?renderer_info.name:"unknown")+"; LCD provider: "+(c.presentation.enabled&&c.presentation.effect==3?(lcd.available()?"GPU":"CPU"):"idle"));
    unsigned texture_width=frame_view().width,texture_height=frame_view().height;
    auto texture=std::unique_ptr<SDL_Texture,decltype(&SDL_DestroyTexture)>(SDL_CreateTexture(renderer.get(),SDL_PIXELFORMAT_ARGB8888,SDL_TEXTUREACCESS_STREAMING,int(texture_width),int(texture_height)),SDL_DestroyTexture);
    if(!texture)throw std::runtime_error(SDL_GetError());
    SDL_AudioSpec want{},got{};want.freq=48000;want.format=AUDIO_S16SYS;want.channels=2;want.samples=1024;
    auto audio=SDL_OpenAudioDevice(nullptr,0,&want,&got,0);
    if(!audio&&!muted&&c.audio)throw std::runtime_error(SDL_GetError());
    struct CloseAudio {SDL_AudioDeviceID id;~CloseAudio(){if(id)SDL_CloseAudioDevice(id);}} close{audio};
    if(audio)SDL_PauseAudioDevice(audio,0);
    auto ring=std::make_unique<gbcllr::RewindHistory>(300);
    std::unique_ptr<SDL_GameController,decltype(&SDL_GameControllerClose)> pad(selected_pad(c),SDL_GameControllerClose);
    const bool cooperative=solo&&c.package&&c.coop_enabled&&(c.package->feature.plugin=="gbcllr.xtreme2.coop"||c.package->feature.plugin=="gbcllr.xtreme2.adaptive-view");
    std::unique_ptr<SDL_GameController,decltype(&SDL_GameControllerClose)> partner_pad(cooperative?selected_pad(c,true,pad.get()):nullptr,SDL_GameControllerClose);
    uint32_t tick=0;bool running=true,paused=false,suppress_inputs=true,return_to_launcher=false,closing=false;
    DesktopReview review(solo!=nullptr);
    uint8_t local_buttons=0,local_virtual=0;std::array<bool,4> pad_shortcuts{};
    auto play_audio=[&](std::span<const int16_t> samples) {
        if(audio&&!muted&&c.audio&&(c.volume>0||qa)&&!closing) {
            std::vector<int16_t> scaled;
            if(c.volume<100){scaled.assign(samples.begin(),samples.end());for(auto& v:scaled)v=int16_t(int(v)*c.volume/100);samples=scaled;}
            if(SDL_QueueAudio(audio,samples.data(),samples.size()*sizeof(int16_t)))throw std::runtime_error(SDL_GetError());
        } else if(audio)SDL_ClearQueuedAudio(audio);
    };
#ifdef GBCLLR_NETPLAY
    std::unique_ptr<gbcllr::NetSession> net;
    if(linked) {
        auto options=linked->options;auto observe=options.present;auto observe_view=options.present_view;
        auto log=options.log;options.log=[&,log](const std::string& text){diagnostics.log(text);if(log)log(text);};
        options.sample=[&](uint32_t){return local_buttons;};
        if(network_coop)options.sample_logical=[&](uint32_t){return gbcllr::LogicalInput{local_buttons,local_virtual};};
        options.present=[&,observe](uint32_t frame,const gbcllr::Machine& machine,std::span<const int16_t> samples) {
            tick=frame+1;play_audio(samples);
            if(observe)observe(frame,machine,samples);
        };
        options.present_view=[&,observe_view](uint32_t frame,const gbcllr::ModView& view) {
            network_width=view.width;network_height=view.height;network_pixels.assign(view.pixels.begin(),view.pixels.end());
            if(observe_view)observe_view(frame,view);
        };
        options.presentation_reset=[&]{if(audio)SDL_ClearQueuedAudio(audio);};
        net=std::make_unique<gbcllr::NetSession>(linked->images,linked->machines,std::move(options));
        if(network_coop)net->frame_observer(diagnostics.observer());
    }
    struct CloseNetDiagnostics {
        std::unique_ptr<gbcllr::NetSession>& session;gbcllr::Diagnostics& recorder;bool coop;
        ~CloseNetDiagnostics(){if(session&&coop){try{if(session->ready())recorder.finish(session->machine());session->frame_observer({});}catch(...){}}}
    } close_net_diagnostics{net,diagnostics,network_coop};
#endif
    auto snapshot=[&] {
        ring->store(*solo,tick);
    };
    auto display=[&] {
        if(SDL_SetWindowFullscreen(window.get(),c.fullscreen==1?SDL_WINDOW_FULLSCREEN_DESKTOP:c.fullscreen==2?SDL_WINDOW_FULLSCREEN:0))
            throw std::runtime_error(SDL_GetError());
        if(SDL_SetTextureScaleMode(texture.get(),gbcllr::presentation_linear(c.presentation,c.linear)?SDL_ScaleModeLinear:SDL_ScaleModeNearest))throw std::runtime_error(SDL_GetError());
    };
    display();if(solo)snapshot();
#ifdef RECOMP_LAUNCHER
    ImGui::CreateContext();ImGui::StyleColorsDark();ImGui::GetIO().IniFilename=nullptr;
    char* base_path=SDL_GetBasePath();
    auto font_path=(std::filesystem::path(base_path?base_path:".")/"assets/fonts/LatoLatin-Regular.ttf").string();
    SDL_free(base_path);
    if(!std::filesystem::exists(font_path)||!ImGui::GetIO().Fonts->AddFontFromFileTTF(font_path.c_str(),18.0f)) {
        ImFontConfig font{};font.SizePixels=18;ImGui::GetIO().Fonts->AddFontDefault(&font);
    }
    if(!ImGui_ImplSDL2_InitForSDLRenderer(window.get(),renderer.get())||!ImGui_ImplSDLRenderer2_Init(renderer.get()))
        throw std::runtime_error("shared menu renderer initialization failed");
    struct CloseGui {~CloseGui(){ImGui_ImplSDLRenderer2_Shutdown();ImGui_ImplSDL2_Shutdown();ImGui::DestroyContext();}} close_gui;
    RecompRuntimeUi* ui=nullptr;
#endif
    uint32_t toast_until=0;
    auto notice=[&](const std::string& text) {
        diagnostics.log(text);
#ifdef RECOMP_LAUNCHER
        if(ui){recomp_runtime_ui_set_status(ui,text.c_str());recomp_runtime_ui_set_toast(ui,"Game",text.c_str());toast_until=SDL_GetTicks()+2500;}
#else
        std::cerr<<text<<'\n';
#endif
    };
    auto save_report=[&] {
        try {
            std::vector<uint8_t> state;std::string context;
            if(solo){diagnostics.finish(*solo);state=solo->snapshot();context="checkpoint_kind=machine\nreplay_scope=actual frame executions, including rewind and rollback\n";}
#ifdef GBCLLR_NETPLAY
            else if(net&&net->ready()) {
                context=net->diagnostics();
                if(network_coop){diagnostics.finish(net->machine());state=net->machine().snapshot();context+="\ncheckpoint_kind=machine\nreplay_scope=actual one-world executions, including rollback\n";}
                else {state=net->pair().snapshot();context+="\ncheckpoint_kind=paired_cable\nreplay_scope=current paired checkpoint only; cable input scheduling is not recorded\n";}
            }
#endif
            context+="\npresentation_now="+std::to_string(c.presentation.enabled)+","+std::to_string(c.presentation.palette)+","+std::to_string(c.presentation.correction)+","+std::to_string(c.presentation.effect)+"\ncoop_view_now="+std::to_string(c.coop_view)+"\n";
            auto view=frame_view();auto path=diagnostics.report(std::move(state),view.pixels,view.width,view.height,std::move(context));
            notice(path.empty()?diagnostics.status():"Saving bug report in "+diagnostics.folder().string()+".");
        }catch(const std::exception& e){notice(std::string("Bug report failed: ")+e.what());}
    };
    auto open_diagnostics=[&] {
        auto path=std::filesystem::absolute(diagnostics.folder()).generic_string();
        std::string url="file:///";constexpr char hex[]="0123456789ABCDEF";
        if(!path.empty()&&path[0]=='/')url="file://";
        for(unsigned char ch:path){if(ch==' '||ch=='#'||ch=='%'||ch=='?'){url+='%';url+=hex[ch>>4];url+=hex[ch&15];}else url+=char(ch);}
        if(SDL_OpenURL(url.c_str()))notice(std::string("Could not open diagnostics folder: ")+SDL_GetError());
    };
    auto checkpoint=[&](bool load) {
        if(!solo)return;
        try {
            if(load) {
                std::string error;if(!solo->restore(gbcllr::read_file(state_path()),error))throw std::runtime_error(error);
                tick=0;ring->clear();snapshot();if(audio)SDL_ClearQueuedAudio(audio);suppress_inputs=true;
            } else gbcllr::write_file_atomic(state_path(),solo->snapshot());
            notice(std::string(load?"Loaded":"Saved")+" checkpoint "+std::to_string(slot)+".");
        }catch(const std::exception& e){notice(e.what());}
    };
#ifdef RECOMP_LAUNCHER
    auto colors_provider=desktop_mod_provider(c);
    MenuCallbacks callbacks;
    callbacks.get=[&](const RecompRuntimeUiItem* i,int* v) {
        std::string k=i->key;
        if(k=="mods.sgb.next")*v=c.sgb_requested;
        else if(k=="mods.sgb.colors")*v=c.sgb_colors;
        else if(k=="mods.sgb.border")*v=c.sgb_border;
        else if(k=="mods.oracle.hide-closed-rooms")*v=c.hide_closed_rooms;
        else if(k=="mods.coop.view")*v=int(c.coop_view);
        else if(k.starts_with("mods.colors.")){*v=gbcllr::presentation_value(c.presentation,c.linear,k.substr(12));return int(*v>=0);}
        else if(k==RECOMP_RUNTIME_UI_KEY_FULLSCREEN)*v=c.fullscreen;
        else if(k==RECOMP_RUNTIME_UI_KEY_WINDOW_SCALE)*v=c.scale;
        else if(k==RECOMP_RUNTIME_UI_KEY_VOLUME)*v=c.volume;
        else if(k==RECOMP_RUNTIME_UI_KEY_AUDIO)*v=c.audio;
        else if(k==RECOMP_RUNTIME_UI_KEY_LINEAR_FILTER)*v=c.linear;
        else if(k==RECOMP_RUNTIME_UI_KEY_STATE_SLOT)*v=slot;
        else if(k=="support.recording")*v=c.diagnostics;
        else return 0;return 1;
    };
    callbacks.set=[&](const RecompRuntimeUiItem* i,int v) {
        try {
            std::string k=i->key;
            if(k.starts_with("mods.sgb.")){
                auto key=k.substr(9),text=std::to_string(v);int accepted=key=="next"?
                    colors_provider.feature_enable(colors_provider.ctx,desktop_sgb_package,desktop_sgb_feature,v):
                    colors_provider.feature_set_option(colors_provider.ctx,desktop_sgb_package,desktop_sgb_feature,key.c_str(),text.c_str());
                if(!accepted)return 0;
            }
            else if(k.starts_with("mods.colors.")){
                auto key=k.substr(12);auto text=std::to_string(v);
                int accepted=key=="enabled"?colors_provider.feature_enable(colors_provider.ctx,desktop_presentation_package,desktop_presentation_feature,v):
                    colors_provider.feature_set_option(colors_provider.ctx,desktop_presentation_package,desktop_presentation_feature,key.c_str(),text.c_str());
                if(!accepted)return 0;
                if(SDL_SetTextureScaleMode(texture.get(),gbcllr::presentation_linear(c.presentation,c.linear)?SDL_ScaleModeLinear:SDL_ScaleModeNearest))throw std::runtime_error(SDL_GetError());
            }
            else if(k=="mods.oracle.hide-closed-rooms") {
                if(!c.package)return 0;auto value=std::to_string(v);
                if(!colors_provider.feature_set_option(colors_provider.ctx,c.package->id.c_str(),"adaptive-view","hide-closed-rooms",value.c_str()))return 0;
            }
            else if(k=="mods.coop.view") {
                if(!c.package)return 0;auto value=std::to_string(v);
                if(!colors_provider.feature_set_option(colors_provider.ctx,c.package->id.c_str(),"coop","coop-view",value.c_str()))return 0;
            }
            else if(k==RECOMP_RUNTIME_UI_KEY_FULLSCREEN){c.fullscreen=v;display();}
            else if(k==RECOMP_RUNTIME_UI_KEY_WINDOW_SCALE){c.scale=v;if(!c.fullscreen)SDL_SetWindowSize(window.get(),int(frame_view().width)*v,int(frame_view().height)*v);}
            else if(k==RECOMP_RUNTIME_UI_KEY_VOLUME)c.volume=v;
            else if(k==RECOMP_RUNTIME_UI_KEY_AUDIO)c.audio=v!=0;
            else if(k==RECOMP_RUNTIME_UI_KEY_LINEAR_FILTER){c.linear=v!=0;display();}
            else if(k==RECOMP_RUNTIME_UI_KEY_STATE_SLOT)slot=v;
            else if(k=="support.recording")c.diagnostics=v!=0;
            else return 0;
            if(audio)SDL_ClearQueuedAudio(audio);c.persist();return 1;
        }catch(const std::exception& e){notice(e.what());return 0;}
    };
    callbacks.action=[&](const RecompRuntimeUiItem* i) {
        std::string k=i->key;
        if(k==RECOMP_RUNTIME_UI_KEY_RESUME)recomp_runtime_ui_close(ui);
        else if(k==RECOMP_RUNTIME_UI_KEY_SAVE_STATE)checkpoint(false);
        else if(k==RECOMP_RUNTIME_UI_KEY_LOAD_STATE)checkpoint(true);
        else if(k==RECOMP_RUNTIME_UI_KEY_QUIT)running=false;
        else if(k=="support.report")save_report();
        else if(k=="support.folder")open_diagnostics();
#ifdef GBCLLR_NETPLAY
        else if(k=="linked.lobby") {
            if(!cooperative&&!network_coop&&c.link_rom.empty())notice("Choose the other cartridge first.");
            else {c.link_lobby_requested=true;return_to_launcher=true;running=false;}
        } else if(k=="linked.browse") {
            char path[4096]{};const char* patterns[]={"*.gb","*.gbc"};
            if(launcher_pick_file("Other cartridge",patterns,2,"Game Boy cartridge",path,sizeof path)){c.link_rom=path;c.persist();}
        }
#endif
        return 0;
    };
    callbacks.enabled=[&](const RecompRuntimeUiItem* i){std::string key=i->key;std::error_code error;
        if(key.starts_with("mods.sgb.")){
            if(!solo||!c.sgb_supported)return 0;auto state=solo->sgb_view();
            if(key=="mods.sgb.colors")return int(state.colors_ready);
            if(key=="mods.sgb.border")return int(state.border_ready);
        }
        if(key.starts_with("mods.colors.")&&key!="mods.colors.enabled"&&!c.presentation.enabled)return 0;
        if(!solo&&((key.starts_with("linked.")&&!(network_coop&&key=="linked.lobby"))||key==RECOMP_RUNTIME_UI_KEY_SAVE_STATE||key==RECOMP_RUNTIME_UI_KEY_LOAD_STATE||key==RECOMP_RUNTIME_UI_KEY_STATE_SLOT))return 0;
        return key==RECOMP_RUNTIME_UI_KEY_LOAD_STATE?int(std::filesystem::exists(state_path(),error)):1;};
    callbacks.save=[&]{try{c.persist();}catch(const std::exception& e){notice(e.what());}};
    callbacks.visible=[&](bool open){paused=open;suppress_inputs=true;if(audio)SDL_ClearQueuedAudio(audio);};
    auto link_text=[&](const std::string& key)->std::string* {
        if(key=="linked.rom")return &c.link_rom;if(key=="linked.boot")return &c.link_boot;
        if(key=="linked.save")return &c.link_save;if(key=="linked.state")return &c.link_state;return nullptr;
    };
    callbacks.text_get=[&](const RecompRuntimeUiItem* item,char* text,size_t size) {
        auto value=link_text(item->key);if(!value||!size)return 0;std::snprintf(text,size,"%s",value->c_str());return 1;
    };
    callbacks.text_set=[&](const RecompRuntimeUiItem* item,const char* text) {
        auto value=link_text(item->key);if(!value||!solo||!text||std::strlen(text)>4095)return 0;*value=text;c.persist();return 1;
    };
    RecompRuntimeUiStandardConfig menu{};menu.menu.title=title.c_str();menu.menu.subtitle="SETTINGS";
    menu.menu.theme=c.color?"gbc":"gb";menu.menu.callbacks={&callbacks,MenuCallbacks::get_value,MenuCallbacks::set_value,MenuCallbacks::run_action,
        MenuCallbacks::is_enabled,MenuCallbacks::persist,MenuCallbacks::visibility,MenuCallbacks::get_text,MenuCallbacks::set_text};
    menu.menu.accept_label=c.source>=2?"A":"Enter";menu.menu.back_label=c.source>=2?"B":"Esc";
    menu.features=RECOMP_RUNTIME_UI_STANDARD_FULLSCREEN|RECOMP_RUNTIME_UI_STANDARD_WINDOW_SCALE|
        RECOMP_RUNTIME_UI_STANDARD_VOLUME|RECOMP_RUNTIME_UI_STANDARD_AUDIO|
        RECOMP_RUNTIME_UI_STANDARD_RESUME|RECOMP_RUNTIME_UI_STANDARD_SAVE_STATE|RECOMP_RUNTIME_UI_STANDARD_LOAD_STATE|
        RECOMP_RUNTIME_UI_STANDARD_STATE_SLOT|RECOMP_RUNTIME_UI_STANDARD_QUIT;
    // The runtime Mods rows use the same finite catalog and provider as the
    // launcher, with live final-frame application and per-cartridge settings.
    std::vector<RecompRuntimeUiItem> extra_items;
    const char* coop_views[]={"Unified","Independent","Split screen"};
    if(cooperative||network_coop)extra_items.push_back({"mods.coop.view","Mods/X + Zero co-op","Co-op view","Switch cameras without advancing the shared world.",RECOMP_RUNTIME_UI_CHOICE,0,2,1,coop_views,3});
    if(c.package&&c.package->feature.plugin=="gbcllr.oracle.adaptive-view")extra_items.push_back({"mods.oracle.hide-closed-rooms","Mods/Adaptive widescreen","Hide closed rooms","Hide adjacent rooms until their doors open.",RECOMP_RUNTIME_UI_BOOL,0,1,1});
    extra_items.push_back({"mods.colors.enabled","Mods/Display colors","Display colors and filters",nullptr,RECOMP_RUNTIME_UI_BOOL,0,1,1});
    std::vector<std::string> colors_keys;colors_keys.reserve(gbcllr::presentation_options().size());
    for(const auto& option:gbcllr::presentation_options()){
        colors_keys.push_back(std::string("mods.colors.")+option.key);
        extra_items.push_back({colors_keys.back().c_str(),"Mods/Display colors",option.label,nullptr,RECOMP_RUNTIME_UI_CHOICE,0,int(option.choices.size())-1,1,option.choices.data(),option.choices.size()});
    }
    if(c.sgb_supported){
        extra_items.push_back({"mods.sgb.colors","Mods/Super Game Boy","Cartridge colors","Live after a DMG/SGB start and cartridge palettes arrive.",RECOMP_RUNTIME_UI_BOOL,0,1,1});
        extra_items.push_back({"mods.sgb.border","Mods/Super Game Boy","Cartridge border","Live original 256x224 layout after cartridge border data arrives.",RECOMP_RUNTIME_UI_BOOL,0,1,1});
        extra_items.push_back({"mods.sgb.next","Mods/Super Game Boy","SGB session on next start","Save in game, quit and reopen. Current play never restarts here.",RECOMP_RUNTIME_UI_BOOL,0,1,1});
    }
#ifdef GBCLLR_NETPLAY
    const RecompRuntimeUiItem linked_items[]={
        {"linked.rom","Linked play","Other cartridge","Both devices need the same two cartridges and starting saves.",RECOMP_RUNTIME_UI_TEXT},
        {"linked.browse","Linked play","Choose other cartridge",nullptr,RECOMP_RUNTIME_UI_ACTION},
        {"linked.boot","Linked play","Other boot ROM (optional)","Blank uses this console's boot ROM.",RECOMP_RUNTIME_UI_TEXT},
        {"linked.save","Linked play","Other native save (optional)","Blank starts the other cartridge with no saved progress.",RECOMP_RUNTIME_UI_TEXT},
        {"linked.state","Linked play","Prepared paired checkpoint (optional)","Uses a separate linked-native.sav so original native saves are preserved.",RECOMP_RUNTIME_UI_TEXT},
        {"linked.lobby","Linked play","Open linked-play lobby","Choose Online or LAN, then restart both consoles from the selected starting progress.",RECOMP_RUNTIME_UI_ACTION}
    };
    if(solo&&cooperative)extra_items.push_back({"linked.lobby","Co-op","Open co-op lobby","Play X and Zero together using the host campaign.",RECOMP_RUNTIME_UI_ACTION});
    else if(network_coop)extra_items.push_back({"linked.lobby","Co-op","Return to lobby","Finish the session and retain confirmed host progress.",RECOMP_RUNTIME_UI_ACTION});
    else if(solo)extra_items.insert(extra_items.end(),std::begin(linked_items),std::end(linked_items));
#endif
    extra_items.push_back({"support.report","Support","Save bug report (F10)","Save a local ZIP with recent play, checkpoint, settings, log and screenshot.",RECOMP_RUNTIME_UI_ACTION});
    extra_items.push_back({"support.folder","Support","Open diagnostics folder",nullptr,RECOMP_RUNTIME_UI_ACTION});
    extra_items.push_back({"support.recording","Support","Automatic recording on next start","Bounded local replay history. No files are uploaded.",RECOMP_RUNTIME_UI_BOOL,0,1,1});
    menu.extra_items=extra_items.data();menu.extra_item_count=extra_items.size();
    auto menu_owner=std::unique_ptr<RecompRuntimeUi,decltype(&recomp_runtime_ui_destroy)>(recomp_runtime_ui_create_standard(&menu),recomp_runtime_ui_destroy);
    ui=menu_owner.get();if(!ui)throw std::runtime_error("shared menu allocation failed");
#endif
    auto toggle_menu=[&] {
#ifdef RECOMP_LAUNCHER
        if(paused)recomp_runtime_ui_close(ui);else recomp_runtime_ui_open(ui);
#else
        paused=!paused;suppress_inputs=true;if(audio)SDL_ClearQueuedAudio(audio);
#endif
    };
    gbcllr::PresentationTransform presentation_transform;
    gbcllr::RoomFraming room_framing;
    auto border_texture=std::unique_ptr<SDL_Texture,decltype(&SDL_DestroyTexture)>(nullptr,SDL_DestroyTexture);
    uint32_t shown_border_revision=UINT32_MAX;const uint32_t* shown_border_pixels=nullptr;
    double next=double(SDL_GetPerformanceCounter())/SDL_GetPerformanceFrequency();
    bool fullscreen_enter=false;
    std::string diagnostic_status=diagnostics.status();
    if(!diagnostics.recording()&&c.diagnostics)notice(diagnostic_status);
    while(running) {
        auto status=diagnostics.status();if(status!=diagnostic_status){diagnostic_status=status;notice(status);}
        auto previous_tick=tick;
        bool toggle=false;SDL_Event e;
        while(SDL_PollEvent(&e)) {
            if(e.type==SDL_RENDER_DEVICE_RESET||e.type==SDL_RENDER_TARGETS_RESET)lcd.reset();
#ifdef RECOMP_LAUNCHER
            ImGui_ImplSDL2_ProcessEvent(&e);
#endif
            if(e.type==SDL_QUIT)running=false;
            if(e.type==SDL_WINDOWEVENT&&e.window.event==SDL_WINDOWEVENT_FOCUS_LOST){suppress_inputs=true;local_buttons=0;}
            if((e.type==SDL_CONTROLLERDEVICEADDED||e.type==SDL_CONTROLLERDEVICEREMOVED)&&c.source>=2&&
                (!pad||!SDL_GameControllerGetAttached(pad.get())))pad.reset(selected_pad(c,false,partner_pad.get()));
            if(cooperative&&(e.type==SDL_CONTROLLERDEVICEADDED||e.type==SDL_CONTROLLERDEVICEREMOVED)&&
                (!partner_pad||!SDL_GameControllerGetAttached(partner_pad.get())))partner_pad.reset(selected_pad(c,true,pad.get()));
            if(e.type==SDL_KEYDOWN||e.type==SDL_KEYUP) {
                bool down=e.type==SDL_KEYDOWN;int key=e.key.keysym.scancode;
                if(key==SDL_SCANCODE_F10){if(down&&!e.key.repeat)save_report();continue;}
                if(key==SDL_SCANCODE_RETURN||key==SDL_SCANCODE_KP_ENTER){
                    if(!down)fullscreen_enter=false;
                    if(down&&(e.key.keysym.mod&KMOD_ALT)){
                        fullscreen_enter=true;
                        if(!e.key.repeat){c.fullscreen=c.fullscreen?0:1;display();c.persist();}
                        continue;
                    }
                    if(down&&fullscreen_enter)continue;
                }
                if(key==c.shortcut_keys[0]) {
#ifdef RECOMP_LAUNCHER
                    if(paused&&key==SDL_SCANCODE_ESCAPE){recomp_runtime_ui_handle_input(ui,RECOMP_RUNTIME_UI_INPUT_BACK,down,e.key.repeat);continue;}
#endif
                    if(down&&!e.key.repeat)toggle=true;continue;
                }
                if(paused) {
#ifdef RECOMP_LAUNCHER
                    int action=key==SDL_SCANCODE_UP?RECOMP_RUNTIME_UI_INPUT_UP:key==SDL_SCANCODE_DOWN?RECOMP_RUNTIME_UI_INPUT_DOWN:
                        key==SDL_SCANCODE_LEFT?RECOMP_RUNTIME_UI_INPUT_LEFT:key==SDL_SCANCODE_RIGHT?RECOMP_RUNTIME_UI_INPUT_RIGHT:
                        key==SDL_SCANCODE_RETURN?RECOMP_RUNTIME_UI_INPUT_ACCEPT:key==SDL_SCANCODE_ESCAPE?RECOMP_RUNTIME_UI_INPUT_BACK:-1;
                    if(action>=0&&!recomp_runtime_ui_wants_text_input(ui))recomp_runtime_ui_handle_input(ui,RecompRuntimeUiInput(action),down,e.key.repeat);
#endif
                } else if(c.shortcuts&&down&&!e.key.repeat) {
                    if(key==c.shortcut_keys[2])checkpoint(false);
                    if(key==c.shortcut_keys[3])checkpoint(true);
                }
            }
#ifdef RECOMP_LAUNCHER
            if(paused&&pad&&(e.type==SDL_CONTROLLERBUTTONDOWN||e.type==SDL_CONTROLLERBUTTONUP)&&
                e.cbutton.which==SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(pad.get()))) {
                int b=e.cbutton.button,action=b==SDL_CONTROLLER_BUTTON_DPAD_UP?RECOMP_RUNTIME_UI_INPUT_UP:
                    b==SDL_CONTROLLER_BUTTON_DPAD_DOWN?RECOMP_RUNTIME_UI_INPUT_DOWN:b==SDL_CONTROLLER_BUTTON_DPAD_LEFT?RECOMP_RUNTIME_UI_INPUT_LEFT:
                    b==SDL_CONTROLLER_BUTTON_DPAD_RIGHT?RECOMP_RUNTIME_UI_INPUT_RIGHT:b==SDL_CONTROLLER_BUTTON_A?RECOMP_RUNTIME_UI_INPUT_ACCEPT:
                    b==SDL_CONTROLLER_BUTTON_B?RECOMP_RUNTIME_UI_INPUT_BACK:-1;
                if(action>=0)recomp_runtime_ui_handle_input(ui,RecompRuntimeUiInput(action),e.type==SDL_CONTROLLERBUTTONDOWN,0);
            }
#endif
        }
        const auto* keys=SDL_GetKeyboardState(nullptr);
        if(qa&&qa->keyboard)keys=qa->keyboard(qa->context,keys);
        auto pressed=[&](int key){return key>0&&keys[key]&&!(fullscreen_enter&&(key==SDL_SCANCODE_RETURN||key==SDL_SCANCODE_KP_ENTER));};
        std::array<bool,4> shortcuts{};
        for(unsigned i=0;i<4;++i)shortcuts[i]=pad_binding(pad.get(),c.shortcut_pads[i],c.deadzone,true);
        if(shortcuts[0]&&!pad_shortcuts[0])toggle=true;
        if(toggle)toggle_menu();
        if(!paused&&c.shortcuts) {
            if(shortcuts[2]&&!pad_shortcuts[2])checkpoint(false);
            if(shortcuts[3]&&!pad_shortcuts[3])checkpoint(true);
        }
        pad_shortcuts=shortcuts;
        int width=0,height=0;SDL_GetRendererOutputSize(renderer.get(),&width,&height);
        if(solo){solo->mod_viewport(unsigned(std::max(0,width)),unsigned(std::max(0,height)));solo->mod_oracle_visibility(c.hide_closed_rooms);}
#ifdef GBCLLR_NETPLAY
        if(net)net->presentation_viewport(c.adaptive?(c.view_width<0?unsigned(std::max(0,width)):unsigned(c.view_width)):160u,
            c.adaptive&&c.view_width<0?unsigned(std::max(0,height)):144u);
        if(net&&network_coop)net->presentation_coop_view(c.coop_view);
#endif
        uint8_t buttons=0,partner_buttons=0,virtual_buttons=0,partner_virtual=0;constexpr unsigned bits[]={2,3,1,0,4,5,7,6};
        for(unsigned i=0;i<8;++i)if((c.source==1&&pressed(c.keys[i]))||
            (c.source>=2&&pad_binding(pad.get(),c.pads[i],c.deadzone)))buttons|=1<<bits[i];
        if(cooperative)for(unsigned i=0;i<8;++i)if((c.partner_source==1&&pressed(c.partner_keys[i]))||
            (c.partner_source>=2&&pad_binding(partner_pad.get(),c.partner_pads[i],c.partner_deadzone)))partner_buttons|=1<<bits[i];
        for(unsigned i=0;i<2;++i)if((c.source==1&&pressed(c.keys[i+8]))||
            (c.source>=2&&pad_binding(pad.get(),c.pads[i+8],c.deadzone)))virtual_buttons|=1<<i;
        if(cooperative)for(unsigned i=0;i<2;++i)if((c.partner_source==1&&pressed(c.partner_keys[i+8]))||
            (c.partner_source>=2&&pad_binding(partner_pad.get(),c.partner_pads[i+8],c.partner_deadzone)))partner_virtual|=1<<i;
        if(suppress_inputs){if(!(buttons|partner_buttons|virtual_buttons|partner_virtual))suppress_inputs=false;buttons=partner_buttons=virtual_buttons=partner_virtual=0;}
        if(!qa&&!(SDL_GetWindowFlags(window.get())&SDL_WINDOW_INPUT_FOCUS))buttons=partner_buttons=virtual_buttons=partner_virtual=0;
        if(qa&&qa->input&&solo)buttons=qa->input(qa->context,tick,*solo);
        if(review.active()&&!review.hold&&c.source==0)buttons=review.held_buttons;
        local_buttons=paused?0:buttons;
        local_virtual=paused?0:virtual_buttons;
        std::vector<int16_t> frame_pcm;
#ifdef GBCLLR_NETPLAY
        if(net) {
            if(!running&&!closing){closing=true;running=true;local_buttons=local_virtual=0;if(audio)SDL_ClearQueuedAudio(audio);if(net->ready())net->quiesce();}
            if(closing)local_buttons=local_virtual=0;
            try {
                if(!net->ready()&&closing)running=false;
                else {net->poll();if(net->drained())running=false;}
            }catch(const std::exception& error){c.link_error=error.what();notice(error.what());std::cerr<<error.what()<<'\n';running=false;}
        } else
#endif
        {
        bool rewind=c.shortcuts&&((c.shortcut_keys[1]>0&&keys[c.shortcut_keys[1]])||shortcuts[1]);
        if(!paused&&!review.hold&&rewind&&tick) {
            auto data=ring->peek(tick-1);
            if(data) {std::string error;if(!solo->restore(*data,error))throw std::runtime_error(error);--tick;ring->drop_after(tick);}
            if(audio)SDL_ClearQueuedAudio(audio);
        } else if(!paused&&!review.hold) {
            solo->mod_coop_shared_world(c.coop_shared&&c.coop_view==0);
            solo->run_frame(gbcllr::LogicalInput{buttons,virtual_buttons},gbcllr::LogicalInput{partner_buttons,partner_virtual});++tick;snapshot();
            auto samples=solo->take_audio();play_audio(samples);if(qa)frame_pcm=std::move(samples);
        }
        }
        auto view=room_framing.apply(frame_view(),tick);auto sgb=solo?solo->sgb_view():gbcllr::NativeSgbView{};
        bool classic=c.sgb_border&&sgb.border_ready;
        if(classic){view.width=160;view.height=144;view.expanded=false;view.pixels=solo->pixels();}
        if(c.sgb_colors)view=presentation_transform.cartridge_sgb_colors(view,sgb);
        unsigned canvas_width=classic?256:view.width,canvas_height=classic?224:view.height;
        double scale=std::min(width/double(canvas_width),height/double(canvas_height));
        SDL_Rect canvas{int((width-canvas_width*scale)/2),int((height-canvas_height*scale)/2),std::max(1,int(canvas_width*scale)),std::max(1,int(canvas_height*scale))};
        SDL_Rect dest=canvas;
        if(classic){dest.x+=int(48*scale);dest.y+=int(40*scale);dest.w=std::max(1,int(160*scale));dest.h=std::max(1,int(144*scale));}
        // Prepare colors once at source size. The owned shader performs the
        // destination sampling; failed/unsupported devices use the CPU service.
        bool native_lcd=c.presentation.enabled&&c.presentation.effect==3&&lcd.available()&&dest.w<=4096&&dest.h<=4096;
        auto settings=c.presentation;
        if(native_lcd)settings.effect=0;
        auto presented=presentation_transform.apply(view,settings,unsigned(dest.w),unsigned(dest.h));
        if(presented.width!=texture_width||presented.height!=texture_height) {
            texture.reset(SDL_CreateTexture(renderer.get(),SDL_PIXELFORMAT_ARGB8888,SDL_TEXTUREACCESS_STREAMING,int(presented.width),int(presented.height)));
            if(!texture)throw std::runtime_error(SDL_GetError());
            texture_width=presented.width;texture_height=presented.height;
            if(SDL_SetTextureScaleMode(texture.get(),gbcllr::presentation_linear(c.presentation,c.linear)?SDL_ScaleModeLinear:SDL_ScaleModeNearest))throw std::runtime_error(SDL_GetError());
        }
        if(!native_lcd)SDL_UpdateTexture(texture.get(),nullptr,presented.pixels.data(),int(presented.width)*4);
        SDL_SetRenderDrawColor(renderer.get(),0,0,0,255);SDL_RenderClear(renderer.get());
        if(classic){
            if(!border_texture){border_texture.reset(SDL_CreateTexture(renderer.get(),SDL_PIXELFORMAT_ARGB8888,SDL_TEXTUREACCESS_STREAMING,256,224));if(!border_texture)throw std::runtime_error(SDL_GetError());SDL_SetTextureScaleMode(border_texture.get(),SDL_ScaleModeNearest);}
            if(shown_border_revision!=sgb.border_revision||shown_border_pixels!=sgb.border.data()){
                SDL_UpdateTexture(border_texture.get(),nullptr,sgb.border.data(),256*4);shown_border_revision=sgb.border_revision;shown_border_pixels=sgb.border.data();
            }
            SDL_RenderCopy(renderer.get(),border_texture.get(),nullptr,&canvas);
        }
        if(!native_lcd||!lcd.draw(presented,dest)){
            if(native_lcd){
                presented=presentation_transform.apply(view,c.presentation,unsigned(dest.w),unsigned(dest.h));
                if(presented.width!=texture_width||presented.height!=texture_height){
                    texture.reset(SDL_CreateTexture(renderer.get(),SDL_PIXELFORMAT_ARGB8888,SDL_TEXTUREACCESS_STREAMING,int(presented.width),int(presented.height)));
                    if(!texture)throw std::runtime_error(SDL_GetError());
                    texture_width=presented.width;texture_height=presented.height;
                    SDL_SetTextureScaleMode(texture.get(),SDL_ScaleModeLinear);
                }
                SDL_UpdateTexture(texture.get(),nullptr,presented.pixels.data(),int(presented.width)*4);
            }
            SDL_RenderCopy(renderer.get(),texture.get(),nullptr,&dest);
        }
#ifdef RECOMP_LAUNCHER
        if(toast_until&&SDL_TICKS_PASSED(SDL_GetTicks(),toast_until)){recomp_runtime_ui_set_toast(ui,nullptr,nullptr);toast_until=0;}
        ImGui_ImplSDLRenderer2_NewFrame();ImGui_ImplSDL2_NewFrame();ImGui::NewFrame();recomp_runtime_ui_render_imgui(ui);
        ImGui::Render();ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(),renderer.get());
#endif
        review.renderer_name=renderer_info.name;review.lcd_gpu_draws=lcd.draws();review.lcd_upload_bytes=lcd.upload_bytes();
        if(solo&&review.poll(*solo,tick,running,paused,[&](const std::filesystem::path& path){
            int w=0,h=0;SDL_GetRendererOutputSize(renderer.get(),&w,&h);
            auto surface=std::unique_ptr<SDL_Surface,decltype(&SDL_FreeSurface)>(SDL_CreateRGBSurfaceWithFormat(0,w,h,32,SDL_PIXELFORMAT_ARGB8888),SDL_FreeSurface);
            if(!surface||SDL_RenderReadPixels(renderer.get(),nullptr,SDL_PIXELFORMAT_ARGB8888,surface->pixels,surface->pitch)||SDL_SaveBMP(surface.get(),path.string().c_str()))throw std::runtime_error(SDL_GetError());
        },[&]{ring->clear();snapshot();},[&](unsigned w,unsigned h){SDL_SetWindowSize(window.get(),int(w),int(h));})) {
            suppress_inputs=true;local_buttons=0;if(audio)SDL_ClearQueuedAudio(audio);
            if(review.wants_play&&paused)toggle_menu();
        }
        SDL_RenderPresent(renderer.get());
        const gbcllr::Machine* observed=solo;
#ifdef GBCLLR_NETPLAY
        if(net&&net->ready()&&tick!=previous_tick)observed=network_coop?&net->machine():&net->pair().machine(net->local_console());
#endif
        if(qa&&qa->present&&observed&&!qa->present(qa->context,tick,*observed,frame_pcm))running=false;
        // Replayed/catch-up frames and startup polling do not consume an
        // additional display interval or queue duplicate PCM.
#ifdef GBCLLR_NETPLAY
        if(net&&tick==previous_tick){SDL_Delay(1);continue;}
#endif
        next+=70224.0/4194304.0;double now=double(SDL_GetPerformanceCounter())/SDL_GetPerformanceFrequency();
        if(next>now)SDL_Delay(uint32_t((next-now)*1000));else if(now-next>0.25)next=now;
        if(audio&&SDL_GetQueuedAudioSize(audio)>48000*4/4)SDL_ClearQueuedAudio(audio);
    }
#ifdef GBCLLR_NETPLAY
    if(net)try {
        desktop_net_save(*linked,*net);
    }catch(const std::exception& error){c.link_error=error.what();notice(std::string("Native save preserved: ")+error.what());}
#endif
    diagnostics.log("LCD GPU draws="+std::to_string(lcd.draws())+" upload_bytes="+std::to_string(lcd.upload_bytes()));
    c.persist();return linked&&!c.link_error.empty()?2:return_to_launcher?1:0;
}
int desktop(gbcllr::Machine& machine,const std::filesystem::path& save,bool muted,DesktopConfig& config) {
    return desktop_loop(&machine,nullptr,save,muted,config);
}
int desktop(gbcllr::Machine& machine,const std::filesystem::path& save,bool muted,DesktopConfig& config,const DesktopFrameQa& qa) {
    return desktop_loop(&machine,nullptr,save,muted,config,&qa);
}
#ifdef GBCLLR_NETPLAY
int desktop_network(const DesktopNetStart& start,bool muted,DesktopConfig& config) {
    return desktop_loop(nullptr,&start,start.local_save,muted,config);
}
int desktop_network(const DesktopNetStart& start,bool muted,DesktopConfig& config,const DesktopFrameQa& qa) {
    return desktop_loop(nullptr,&start,start.local_save,muted,config,&qa);
}
#endif
