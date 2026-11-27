#include "desktop_ui.hpp"
#include "identity.h"
#include "native_mods.hpp"
#include "megaman_titles.hpp"
#include <SDL.h>
#include <algorithm>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#ifdef RECOMP_LAUNCHER
#include "recomp_launcher.h"
#include "launcher_profile.h"
#include "desktop_mods.hpp"
#ifdef GBCLLR_NETPLAY
#include "desktop_net.hpp"
#endif
#endif

DesktopConfig::DesktopConfig() {
    keys={SDL_SCANCODE_UP,SDL_SCANCODE_DOWN,SDL_SCANCODE_LEFT,SDL_SCANCODE_RIGHT,
        SDL_SCANCODE_Z,SDL_SCANCODE_X,SDL_SCANCODE_RETURN,SDL_SCANCODE_RSHIFT,SDL_SCANCODE_Q,SDL_SCANCODE_E};
    const int buttons[]={SDL_CONTROLLER_BUTTON_DPAD_UP,SDL_CONTROLLER_BUTTON_DPAD_DOWN,
        SDL_CONTROLLER_BUTTON_DPAD_LEFT,SDL_CONTROLLER_BUTTON_DPAD_RIGHT,
        SDL_CONTROLLER_BUTTON_A,SDL_CONTROLLER_BUTTON_B,SDL_CONTROLLER_BUTTON_START,SDL_CONTROLLER_BUTTON_BACK,
        SDL_CONTROLLER_BUTTON_LEFTSHOULDER,SDL_CONTROLLER_BUTTON_RIGHTSHOULDER};
    for(unsigned i=0;i<10;++i)pads[i]=buttons[i]+1;
    shortcut_keys={SDL_SCANCODE_ESCAPE,SDL_SCANCODE_BACKSPACE,SDL_SCANCODE_F5,SDL_SCANCODE_F8};
    partner_keys={SDL_SCANCODE_W,SDL_SCANCODE_S,SDL_SCANCODE_A,SDL_SCANCODE_D,
        SDL_SCANCODE_G,SDL_SCANCODE_H,SDL_SCANCODE_T,SDL_SCANCODE_Y,SDL_SCANCODE_U,SDL_SCANCODE_O};
    partner_pads=pads;
    shortcut_pads[0]=1000+(1<<SDL_CONTROLLER_BUTTON_START)+(1<<SDL_CONTROLLER_BUTTON_BACK);
}
void DesktopConfig::load() {
    std::ifstream file(profile/"desktop.cfg");std::string line;
    while(std::getline(file,line)) {
        std::istringstream in(line);std::string key;int n;
        if(!(in>>key))continue;
        if(key=="rom")in>>std::quoted(rom);
        else if(key=="boot")in>>std::quoted(boot);
        else if(key=="sgb_boot")in>>std::quoted(sgb_boot);
        else if(key=="link_rom")in>>std::quoted(link_rom);
        else if(key=="link_boot")in>>std::quoted(link_boot);
        else if(key=="link_save")in>>std::quoted(link_save);
        else if(key=="link_state")in>>std::quoted(link_state);
        else if(key=="controller_guid")in>>std::quoted(gamepad_guid);
        else if(key=="partner_guid")in>>std::quoted(partner_guid);
        else if(key=="partner_keys"||key=="partner_pads") {auto& a=key=="partner_keys"?partner_keys:partner_pads;for(auto& v:a)if(in>>n)v=std::max(0,n);}
        else if(key=="keys"||key=="pads") {auto& a=key=="keys"?keys:pads;for(auto& v:a)if(in>>n)v=std::max(0,n);}
        else if(key=="shortcut_keys"||key=="shortcut_pads") {auto& a=key=="shortcut_keys"?shortcut_keys:shortcut_pads;for(auto& v:a)if(in>>n)v=std::max(0,n);}
        else if(in>>n) {
            if(key=="scale")scale=std::clamp(n,1,8);
            else if(key=="fullscreen")fullscreen=std::clamp(n,0,2);
            else if(key=="volume")volume=std::clamp(n,0,100);
            else if(key=="source")source=std::clamp(n,0,2);
            else if(key=="partner_source")partner_source=std::clamp(n,0,2);
            else if(key=="partner_deadzone")partner_deadzone=std::clamp(n,0,100);
            else if(key=="coop_first"){coop_first=unsigned(std::clamp(n,0,1));coop_first_saved=true;}
            else if(key=="coop_view")coop_view=unsigned(std::clamp(n,0,2));
            else if(key=="coop_enabled"){coop_enabled=n!=0;coop_enabled_saved=true;}
            else if(key=="coop_shared")coop_shared=n!=0;
            else if(key=="deadzone")deadzone=std::clamp(n,0,100);
            else if(key=="state_slot")state_slot=std::clamp(n,1,10);
            else if(key=="audio")audio=n!=0;
            else if(key=="linear")linear=n!=0;
            else if(key=="sgb_requested")sgb_requested=n!=0;
            else if(key=="sgb_colors")sgb_colors=n!=0;
            else if(key=="sgb_border")sgb_border=n!=0;
            else if(key.starts_with("presentation_"))gbcllr::presentation_set(presentation,linear,key.substr(13),n);
            else if(key=="skip_launcher")skip_launcher=n!=0;
            else if(key=="shortcuts")shortcuts=n!=0;
            else if(key=="diagnostics")diagnostics=n!=0;
            else if(key=="adaptive_view"){adaptive=n!=0;mod_enabled_saved=true;}
            else if(key=="colorization"){colorization=n!=0;mod_color_saved=true;}
            else if(key=="weapon_cycle"){weapon_cycle=n!=0;mod_weapon_cycle_saved=true;}
            else if(key=="smooth_gameplay"){smooth_gameplay=n!=0;mod_smooth_saved=true;}
            else if(key=="no_flicker"){no_flicker=n!=0;mod_no_flicker_saved=true;}
            else if(key=="hide_closed_rooms")hide_closed_rooms=n!=0;
            else if(key=="gift_enabled")gift_enabled=n!=0;
            else if(key=="extended_content")extended_content=n!=0;
            else if(key=="expanded_encounters")expanded_encounters=n!=0;
            else if(key=="gift_choice"&&n>=1&&n<=74)gift_choice=n;
            else if(key=="view_width"&&(n==-1||(n>=160&&n<=4096))){view_width=n;mod_width_saved=true;}
        }
    }
    for(auto& key:keys)if(key>=SDL_NUM_SCANCODES)key=0;
    for(auto& key:partner_keys)if(key>=SDL_NUM_SCANCODES)key=0;
    for(auto& key:shortcut_keys)if(key>=SDL_NUM_SCANCODES)key=0;
}
void DesktopConfig::persist() const {
    std::ostringstream out;
    out<<"gbcllr-desktop 1\nscale "<<scale<<"\nfullscreen "<<fullscreen<<"\nvolume "<<volume
       <<"\nsource "<<source<<"\ndeadzone "<<deadzone<<"\nstate_slot "<<state_slot<<"\naudio "<<audio<<"\nlinear "<<linear
       <<"\nskip_launcher "<<skip_launcher<<"\nshortcuts "<<shortcuts<<"\ndiagnostics "<<diagnostics
       <<"\nrom "<<std::quoted(rom)<<"\nboot "<<std::quoted(boot)<<"\ncontroller_guid "<<std::quoted(gamepad_guid)<<'\n';
    out<<"sgb_requested "<<sgb_requested<<"\nsgb_colors "<<sgb_colors<<"\nsgb_border "<<sgb_border<<"\nsgb_boot "<<std::quoted(sgb_boot)<<'\n';
    out<<"presentation_enabled "<<presentation.enabled<<"\npresentation_palette "<<presentation.palette
       <<"\npresentation_correction "<<presentation.correction<<"\npresentation_effect "<<presentation.effect<<'\n';
    out<<"link_rom "<<std::quoted(link_rom)<<"\nlink_boot "<<std::quoted(link_boot)
       <<"\nlink_save "<<std::quoted(link_save)<<"\nlink_state "<<std::quoted(link_state)<<'\n';
    auto bindings=[&](const char* name,const auto& values){out<<name;for(auto v:values)out<<' '<<v;out<<'\n';};
    bindings("keys",keys);bindings("pads",pads);bindings("shortcut_keys",shortcut_keys);bindings("shortcut_pads",shortcut_pads);
    bindings("partner_keys",partner_keys);bindings("partner_pads",partner_pads);
    out<<"partner_source "<<partner_source<<"\npartner_deadzone "<<partner_deadzone<<"\npartner_guid "<<std::quoted(partner_guid)<<'\n';
    if(package&&(package->feature.plugin=="gbcllr.xtreme2.coop"||package->feature.plugin=="gbcllr.xtreme2.adaptive-view"))
        out<<"coop_first "<<coop_first<<'\n'<<"coop_view "<<coop_view<<'\n'<<"coop_enabled "<<coop_enabled<<'\n'<<"coop_shared "<<coop_shared<<'\n';
    if(package)out<<"adaptive_view "<<adaptive<<"\nview_width "<<view_width<<"\ncolorization "<<colorization
        <<"\ngift_enabled "<<gift_enabled<<"\ngift_choice "<<gift_choice
        <<"\nextended_content "<<extended_content<<"\nexpanded_encounters "<<expanded_encounters
        <<"\nhide_closed_rooms "<<hide_closed_rooms<<"\nsmooth_gameplay "<<smooth_gameplay<<"\nweapon_cycle "<<weapon_cycle<<"\nno_flicker "<<no_flicker<<'\n';
    auto text=out.str();gbcllr::write_file_atomic(profile/"desktop.cfg",{reinterpret_cast<const uint8_t*>(text.data()),text.size()});
}
std::string desktop_title(const std::string& sha) {
    if(auto title=gbcllr::megaman_title_for_rom(sha))return std::string(title->name);
    if(sha=="5ca7ba01642a3b27b0cc0b5349b52792795b62d3ed977e98a09390659af96b7b")return "Pokemon Red";
    if(sha=="2a951313c2640e8c2cb21f25d1db019ae6245d9c7121f754fa61afd7bee6452d")return "Pokemon Blue";
    if(sha=="8cbaa499397e4f1a679c992ea9382a2dd7942ab398b48c19829c2d9529de47bf")return "Pokemon Yellow";
    if(sha=="fb0016d27b1e5374e1ec9fcad60e6628d8646103b5313ca683417f52b97e7e4e")return "Pokemon Gold";
    if(sha=="72b190859a59623cbef6c49d601f8de52c1d2331b4f08a8d2acc17274fc19a8c")return "Pokemon Silver";
    if(sha=="fdcc3c8c43813cf8731fc037d2a6d191bac75439c34b24ba1c27526e6acdc8a2")return "Pokemon Crystal";
    if(sha=="0b56b78a9e45452e98c33edd111234931f1e034dc097f6f23082eb8db6055474")return "The Legend of Zelda: Oracle of Ages";
    if(sha=="862a51368fb30539279d336b3fe193b43876d2cb15c87a36f5da517804ab3971")return "The Legend of Zelda: Oracle of Seasons";
    if(sha=="e37a14a8e61bd64f2226dacc26a7ee75dd90abfcf88c5016eb97b76ecda0ddca")return "Mega Man Xtreme 2";
    if(sha=="42045a1642f77b568534ab85b9db93c8a33d2b95c38ae84c987f497e8d52f97f")return "Super Mario Land 2: 6 Golden Coins";
    return "Game Boy";
}
namespace {
std::vector<gbcllr::NativeFeature> selected_features(const DesktopConfig& c) {
    if(!c.package||!c.session_mods)return {};
    auto f=c.package->feature;
    if(auto title=gbcllr::megaman_title_for_plugin(f.plugin)) {
        std::vector<gbcllr::NativeFeature> selected;
        if(c.adaptive||c.weapon_cycle||c.smooth_gameplay||c.no_flicker) {
            f.plugin=title->adaptive;f.colorization=false;f.asset_input.clear();
            f.view_width=c.adaptive?c.view_width:160;
            f.weapon_cycle=c.weapon_cycle;f.smooth_gameplay=c.smooth_gameplay;f.no_flicker=c.no_flicker;
            selected.push_back(std::move(f));
        }
        if(c.colorization&&c.dx_package)selected.push_back(c.dx_package->feature);
        return selected;
    }
    const bool gen2=f.plugin=="gbcllr.gen2.adaptive-view";
    const bool extended=f.plugin=="gbcllr.gen1.extended";
    const bool xtreme2=f.plugin=="gbcllr.xtreme2.adaptive-view"||f.plugin=="gbcllr.xtreme2.coop";
    if(c.sgb_requested&&gen2)return {}; // Preserve the Color-session choice; DMG adapter is unqualified.
    if(!c.adaptive&&!(gen2&&c.gift_enabled)&&!(extended&&c.extended_content)&&!(xtreme2&&(c.weapon_cycle||c.smooth_gameplay||c.coop_enabled)))return {};
    f.weapon_cycle=xtreme2&&c.weapon_cycle;f.smooth_gameplay=xtreme2&&c.smooth_gameplay;
    if(xtreme2){f.plugin=c.coop_enabled?"gbcllr.xtreme2.coop":"gbcllr.xtreme2.adaptive-view";f.player1_character=c.coop_enabled?c.coop_first:0;}
    f.view_width=c.adaptive?c.view_width:160;
    if(extended) {
        if(c.extended_content)f.expanded_encounters=c.expanded_encounters;
        else {
            f.plugin="gbcllr.gen1.adaptive-view";f.asset_input.clear();f.expanded_encounters=false;
        }
    }
    if(f.plugin=="gbcllr.sml2.adaptive-view")f.colorization=c.colorization;
    if(gen2)f.gift=c.gift_enabled?uint32_t(c.gift_choice):0;
    return {f};
}
}
gbcllr::Model desktop_boot_model(const DesktopBootContext& context,std::string_view identity,
    uint8_t color_flag,std::string_view boot_sha) {
    using gbcllr::Model;
    auto explicit_model=context.config.sgb_requested?std::optional<Model>{Model::dmg}:context.explicit_model;
    std::optional<Model> boot;
    if(!boot_sha.empty()) {
        if(boot_sha=="cf053eccb4ccafff9e67339d4e78e98dce7d1ed59be819d2a1ba2232c6fce1c7")boot=Model::dmg;
        else if(boot_sha=="b4f2e416a35eef52cba161b159c7c8523a92594facb924b3ede0d722867c50c7")boot=Model::cgb;
        else throw std::runtime_error("Choose a qualified DMG or CGB boot ROM.");
    }
    if(boot&&explicit_model&&boot!=explicit_model)
        throw std::runtime_error("Boot ROM does not match the explicit hardware model.");
    auto features=selected_features(context.config);
    auto validate=[&](Model model) {
        if(!features.empty()) {
            gbcllr::NativeMods checked(identity,features,model==Model::cgb);
        }
    };
    auto model=explicit_model.value_or(boot.value_or((color_flag&0x80)?Model::cgb:Model::dmg));
    if(color_flag==0xc0&&model==Model::dmg)
        throw std::runtime_error("This cartridge requires the Game Boy Color boot ROM.");
    try {validate(model);}
    catch(const std::exception&) {
        if(color_flag==0xc0)throw;
        auto alternative=model==Model::cgb?Model::dmg:Model::cgb;
        // Consult the actual adapter, rather than duplicating its supported
        // model/profile table. Stock-only carts retain both supported models.
        validate(alternative);
        if(!boot&&!explicit_model)model=alternative;
        else throw std::runtime_error(alternative==Model::dmg?
            "Enabled native enhancements require the DMG boot ROM.":
            "Enabled native enhancements require the Game Boy Color boot ROM.");
    }
    return model;
}
#ifdef RECOMP_LAUNCHER
namespace {
int persist_setup(void* ctx,const char* rom,const char* boot) {
    try {auto& c=*static_cast<DesktopConfig*>(ctx);c.rom=rom?rom:"";if(c.sgb_requested)c.sgb_boot=boot?boot:"";else c.boot=boot?boot:"";c.persist();return 0;}
    catch(...){return 1;}
}
}
#endif
bool desktop_prepare(DesktopConfig& c,std::filesystem::path& rom,std::filesystem::path& boot,
    std::filesystem::path& save,const std::filesystem::path& profile,bool show,bool force,
    bool model_explicit,gbcllr::Options& options) {
    if(!c.save_override)c.save_override=save;
    save=*c.save_override; // A previously resolved default is not a CLI override.
    const bool explicit_save=!save.empty();
    std::string identity;
    if(!rom.empty())identity=gbcllr::sha256(gbcllr::read_file(rom));
    else if(GBCLLR_PROGRAM_COUNT)identity=gbcllr_program_sha256[0];
    else throw std::runtime_error("--rom is required for a ROM-free desktop build");
    if(options.native&&GBCLLR_PROGRAM_COUNT) {
        bool compiled=false;for(unsigned i=0;i<GBCLLR_PROGRAM_COUNT;++i)compiled|=identity==gbcllr_program_sha256[i];
        if(!compiled)throw std::runtime_error("this build does not include the selected cartridge");
    }
    if(profile.empty()) {
        char* base=SDL_GetPrefPath("RetroPortingToolKit","gbcllr");
        if(!base)throw std::runtime_error(SDL_GetError());c.profile=std::filesystem::path(base)/identity;SDL_free(base);
    } else c.profile=std::filesystem::absolute(profile);
    std::filesystem::create_directories(c.profile);c.load();
    if(!c.sgb_cli_consumed){
        if(options.sgb_service)c.sgb_requested=true;
        c.original_explicit_model=model_explicit?std::optional(options.model):std::nullopt;
        c.sgb_cli_consumed=true;
    }
    auto profile_image=!rom.empty()?gbcllr::read_file(rom):c.rom.empty()?std::vector<uint8_t>{}:gbcllr::read_file(c.rom);
    c.sgb_supported=profile_image.size()>0x146&&profile_image[0x146]==3;
    c.net_start.reset();c.link_retry=false;
    char* executable_base=SDL_GetBasePath();std::filesystem::path assets=executable_base?executable_base:".";SDL_free(executable_base);
    const bool gen2_gold=identity=="fb0016d27b1e5374e1ec9fcad60e6628d8646103b5313ca683417f52b97e7e4e";
    const bool gen2_silver=identity=="72b190859a59623cbef6c49d601f8de52c1d2331b4f08a8d2acc17274fc19a8c";
    const bool gen2_crystal=identity=="fdcc3c8c43813cf8731fc037d2a6d191bac75439c34b24ba1c27526e6acdc8a2";
    std::string gen1_package;
    std::string oracle_package;
    if(identity=="0b56b78a9e45452e98c33edd111234931f1e034dc097f6f23082eb8db6055474")oracle_package="assets/mods/ages-adaptive.gbmod";
    if(identity=="862a51368fb30539279d336b3fe193b43876d2cb15c87a36f5da517804ab3971")oracle_package="assets/mods/seasons-adaptive.gbmod";
    if(identity=="5ca7ba01642a3b27b0cc0b5349b52792795b62d3ed977e98a09390659af96b7b")gen1_package="assets/mods/red-adaptive.gbmod";
    if(identity=="2a951313c2640e8c2cb21f25d1db019ae6245d9c7121f754fa61afd7bee6452d")gen1_package="assets/mods/blue-adaptive.gbmod";
    if(identity=="8cbaa499397e4f1a679c992ea9382a2dd7942ab398b48c19829c2d9529de47bf")gen1_package="assets/mods/yellow-adaptive.gbmod";
#if GBCLLR_GEN1_EXTENDED_ASSETS
    if(identity=="5ca7ba01642a3b27b0cc0b5349b52792795b62d3ed977e98a09390659af96b7b")gen1_package="assets/mods/red-extended.gbmod";
    if(identity=="2a951313c2640e8c2cb21f25d1db019ae6245d9c7121f754fa61afd7bee6452d")gen1_package="assets/mods/blue-extended.gbmod";
#endif
    const auto* megaman=gbcllr::megaman_title_for_rom(identity);
    c.bundled_package=megaman?assets/("assets/mods/"+std::string(megaman->key)+"-adaptive.gbmod"):assets/(!oracle_package.empty()?oracle_package:!gen1_package.empty()?gen1_package:gen2_gold?"assets/mods/gold-adaptive.gbmod":gen2_silver?"assets/mods/silver-adaptive.gbmod":gen2_crystal?"assets/mods/crystal-adaptive.gbmod":identity=="42045a1642f77b568534ab85b9db93c8a33d2b95c38ae84c987f497e8d52f97f"?"assets/mods/sml2-adaptive.gbmod":"assets/mods/xtreme2-adaptive.gbmod");
    c.dx_package.reset();
    if(megaman) {
        auto dx_path=c.profile/"native-dx-package.gbmod";
        if(!std::filesystem::exists(dx_path))dx_path=assets/("assets/mods/"+std::string(megaman->key)+"-dx.gbmod");
        if(std::filesystem::exists(dx_path))c.dx_package=gbcllr::read_mod_package(dx_path,identity);
    }
    auto installed=c.profile/"native-package.gbmod";
    auto package_path=!c.package_override.empty()?c.package_override:std::filesystem::exists(installed)?installed:c.bundled_package;
    if(!c.package_override.empty()||((megaman||!oracle_package.empty()||!gen1_package.empty()||gen2_gold||gen2_silver||gen2_crystal||identity=="e37a14a8e61bd64f2226dacc26a7ee75dd90abfcf88c5016eb97b76ecda0ddca"||identity=="42045a1642f77b568534ab85b9db93c8a33d2b95c38ae84c987f497e8d52f97f")&&std::filesystem::exists(package_path))) {
        c.package=gbcllr::read_mod_package(package_path,identity);
        if(c.package->feature.plugin=="gbcllr.xtreme2.coop"&&!c.coop_first_saved)c.coop_first=c.package->feature.player1_character;
        if(c.package->feature.plugin=="gbcllr.xtreme2.coop"&&!c.coop_enabled_saved)
            c.coop_enabled=c.mod_enabled_saved?c.adaptive:c.package->default_enabled;
        if(!c.mod_enabled_saved)c.adaptive=c.package->feature.plugin=="gbcllr.xtreme2.coop"||c.package->default_enabled;
        if(!c.mod_width_saved)c.view_width=c.package->feature.view_width;
        if(!c.mod_color_saved)c.colorization=c.package->feature.colorization;
        if(!c.mod_weapon_cycle_saved)c.weapon_cycle=c.package->feature.weapon_cycle;
        if(!c.mod_smooth_saved)c.smooth_gameplay=c.package->feature.smooth_gameplay;
        if(!c.mod_no_flicker_saved)c.no_flicker=c.package->feature.no_flicker;
        if(megaman&&!c.mod_color_saved)c.colorization=c.dx_package&&c.dx_package->default_enabled;
    }
    if(!rom.empty())c.rom=std::filesystem::absolute(rom).string();
    if(!boot.empty()){if(options.sgb_service)c.sgb_boot=std::filesystem::absolute(boot).string();else c.boot=std::filesystem::absolute(boot).string();}
    DesktopBootContext boot_context{c,c.original_explicit_model};
    {
        try {
            if(!c.rom.empty()) {
                auto image=gbcllr::read_file(c.rom);
                options.model=desktop_boot_model(boot_context,identity,image.size()>0x143?image[0x143]:0,
                    (c.sgb_requested?c.sgb_boot:c.boot).empty()?std::string{}:gbcllr::sha256(gbcllr::read_file(c.sgb_requested?c.sgb_boot:c.boot)));
            }
        }catch(const std::exception&){} // The setup wizard can repair moved inputs.
    }
    c.color=options.model==gbcllr::Model::cgb;
    if(save.empty())save=c.profile/"native.sav";else save=std::filesystem::absolute(save);
#ifdef RECOMP_LAUNCHER
    if(show&&(force||!c.skip_launcher)) {
        RecompLauncherCSettings s{};s.window_scale=c.scale;s.fullscreen=c.fullscreen;s.volume=c.volume;
        s.enable_audio=c.audio;s.audio_freq=48000;s.linear_filter=c.linear;s.player_src[0]=c.source;
        s.deadzone[0]=c.deadzone;s.skip_launcher=c.skip_launcher;s.assist_tools=c.shortcuts;
        std::copy(c.keys.begin(),c.keys.end(),s.player_key_bind[0]);std::copy(c.pads.begin(),c.pads.end(),s.player_pad_bind[0]);
        s.player_src[1]=c.partner_source;s.deadzone[1]=c.partner_deadzone;
        std::copy(c.partner_keys.begin(),c.partner_keys.end(),s.player_key_bind[1]);std::copy(c.partner_pads.begin(),c.partner_pads.end(),s.player_pad_bind[1]);
        std::snprintf(s.player_gamepad_guid[1],sizeof s.player_gamepad_guid[1],"%s",c.partner_guid.c_str());
        std::copy(c.shortcut_keys.begin(),c.shortcut_keys.end(),s.assist_key_bind);std::copy(c.shortcut_pads.begin(),c.shortcut_pads.end(),s.assist_pad_bind);
        const bool launcher_sgb=c.sgb_requested;const auto launcher_boot=c.sgb_requested?c.sgb_boot:c.boot;
        std::snprintf(s.bios_path,sizeof s.bios_path,"%s",launcher_boot.c_str());
        std::snprintf(s.player_gamepad_guid[0],sizeof s.player_gamepad_guid[0],"%s",c.gamepad_guid.c_str());
        auto defaults=s;DesktopConfig fresh;defaults.window_scale=fresh.scale;defaults.fullscreen=0;defaults.volume=100;
        defaults.enable_audio=1;defaults.linear_filter=0;defaults.skip_launcher=0;defaults.player_src[0]=1;
        defaults.deadzone[0]=fresh.deadzone;defaults.assist_tools=1;defaults.player_gamepad_guid[0][0]=0;defaults.player_gamepad_instance[0]=0;
        std::copy(fresh.keys.begin(),fresh.keys.end(),defaults.player_key_bind[0]);std::copy(fresh.pads.begin(),fresh.pads.end(),defaults.player_pad_bind[0]);
        defaults.player_src[1]=fresh.partner_source;defaults.deadzone[1]=fresh.partner_deadzone;
        std::copy(fresh.partner_keys.begin(),fresh.partner_keys.end(),defaults.player_key_bind[1]);std::copy(fresh.partner_pads.begin(),fresh.partner_pads.end(),defaults.player_pad_bind[1]);
        defaults.player_gamepad_guid[1][0]=0;defaults.player_gamepad_instance[1]=0;
        std::copy(fresh.shortcut_keys.begin(),fresh.shortcut_keys.end(),defaults.assist_key_bind);std::copy(fresh.shortcut_pads.begin(),fresh.shortcut_pads.end(),defaults.assist_pad_bind);
        auto title=desktop_title(identity),config=(c.profile/"desktop.cfg").string(),native=save.string(),cache=(c.profile/"rom.cfg").string();
        RecompLauncherCGameInfo gi{};launcher_profile_apply(options.model==gbcllr::Model::cgb?"gbc":"gb",&gi);
        c.presentation_filter_selected=false;
        auto mod_provider=desktop_mod_provider(c);gi.mods=&mod_provider;
        gi.name=title.c_str();gi.has_screen_kind=0;gi.has_bios=1;
        if(c.package&&(c.package->feature.plugin=="gbcllr.xtreme2.coop"||c.package->feature.plugin=="gbcllr.xtreme2.adaptive-view"))gi.native_player_count=gi.num_players=2;
        gi.bios_verify_for_rom=desktop_verify_boot_for_rom;gi.bios_verify_ctx=&boot_context;
        // Extended selection resolves its save after the launcher returns.
        // Until the panel can follow that choice, keep import/delete hidden.
        gi.sram_path=c.package&&c.package->feature.plugin=="gbcllr.gen1.extended"?nullptr:native.c_str();gi.config_path=config.c_str();gi.rom_cache_path=cache.c_str();gi.default_settings=&defaults;
        gi.settings_bindings=1;gi.has_assist_tools=1;gi.assist_tools_note="Enable rewind and checkpoint shortcuts. The menu shortcut always works.";
        const char* virtual_labels[]={"Virtual L","Virtual R"};gi.virtual_button_labels=virtual_labels;gi.virtual_button_count=2;
        const char* labels[]={"Menu","Rewind","Save state","Load state"};gi.assist_binding_labels=labels;gi.assist_binding_count=4;
        gi.credits_text="Game Boy hardware: SameBoy by Lior Halphon and contributors.\nShared launcher: recomp-ui.\nRewind: rbengine.\ngbcllr is an experimental native framework.";
        auto revision=RecompLauncherCRomRevision{title.c_str(),identity.c_str(),nullptr,nullptr,1,nullptr};
        gi.rom_revisions=&revision;gi.num_rom_revisions=1;gi.needs_setup=1;
        gi.persist_setup=persist_setup;gi.persist_setup_ctx=&c;
#ifdef GBCLLR_NETPLAY
        if(c.link_lobby_requested||c.coop_enabled)desktop_net_lobby(c,gi);
#endif
        char selected[4096]{};char* base=SDL_GetBasePath();std::string assets=base?base:".";SDL_free(base);
        auto caller_directory=std::filesystem::current_path();
        int result=recomp_launcher_run_window(title.c_str(),&s,&gi,assets.c_str(),c.rom.c_str(),selected,sizeof selected);
        std::filesystem::current_path(caller_directory);
        c.scale=s.window_scale;c.fullscreen=s.fullscreen;c.volume=s.volume;c.audio=s.enable_audio!=0;if(!c.presentation_filter_selected)c.linear=s.linear_filter!=0;
        c.source=s.player_src[0];c.deadzone=s.deadzone[0];c.skip_launcher=s.skip_launcher!=0;c.shortcuts=s.assist_tools!=0;
        c.gamepad_instance=s.player_gamepad_instance[0];c.gamepad_guid=s.player_gamepad_guid[0];
        c.partner_source=s.player_src[1];c.partner_deadzone=s.deadzone[1];
        c.partner_instance=s.player_gamepad_instance[1];c.partner_guid=s.player_gamepad_guid[1];
        std::copy_n(s.player_key_bind[1],10,c.partner_keys.begin());std::copy_n(s.player_pad_bind[1],10,c.partner_pads.begin());
        if(c.sgb_requested==launcher_sgb||std::string(s.bios_path)!=launcher_boot){if(c.sgb_requested)c.sgb_boot=s.bios_path;else c.boot=s.bios_path;}
        std::copy_n(s.player_key_bind[0],10,c.keys.begin());std::copy_n(s.player_pad_bind[0],10,c.pads.begin());
        std::copy_n(s.assist_key_bind,4,c.shortcut_keys.begin());std::copy_n(s.assist_pad_bind,4,c.shortcut_pads.begin());
        if(result==0)c.rom=selected;c.persist();if(result!=0)return false;
#ifdef GBCLLR_NETPLAY
        if(s.netplay_launch.enabled) {
            try {desktop_net_launch(c,s.netplay_launch,options,save);c.link_error.clear();}
            catch(const std::exception& error) {c.link_error=error.what();c.link_retry=true;c.link_lobby_requested=true;return true;}
        }
#endif
        c.link_lobby_requested=false;
    }
#else
    (void)show;(void)force;
#endif
    rom=c.rom;boot=c.sgb_requested?c.sgb_boot:c.boot;
    if(rom.empty())throw std::runtime_error("choose the cartridge before starting");
    auto image=gbcllr::read_file(rom);
    if(gbcllr::sha256(image)!=identity)throw std::runtime_error("desktop profile cartridge identity differs");
    options.model=desktop_boot_model(boot_context,identity,image.size()>0x143?image[0x143]:0,
        boot.empty()?std::string{}:gbcllr::sha256(gbcllr::read_file(boot)));
    options.sgb_service=c.sgb_requested;c.sgb_current=options.sgb_service;
    if(options.sgb_service&&!c.sgb_supported)throw std::runtime_error("This cartridge does not advertise SGB features.");
    c.color=options.model==gbcllr::Model::cgb;
    if(!explicit_save&&c.package&&c.session_mods&&c.package->feature.plugin=="gbcllr.gen1.extended"&&c.extended_content){
        // Resolve the ordinary save and final launcher selection first. Keep
        // its progress intact; initialize the Extended save only once.
        auto extended_save=c.profile/"extended-native.sav";
        if(!std::filesystem::exists(extended_save)&&std::filesystem::exists(save))
            std::filesystem::copy_file(save,extended_save,std::filesystem::copy_options::skip_existing);
        save=std::move(extended_save);
    }
    if(auto selected=selected_features(c);!selected.empty()) {
        const bool gen2=selected.front().plugin=="gbcllr.gen2.adaptive-view";
        options.mods.insert(options.mods.end(),std::make_move_iterator(selected.begin()),std::make_move_iterator(selected.end()));
        // The UI action queues a single delivery for this execution. The
        // serialized module owns waiting/consumed status from here; future
        // launches do not silently repeat an inventory gift.
        if(gen2&&c.gift_enabled){c.gift_enabled=false;c.persist();}
    }
    return true;
}
