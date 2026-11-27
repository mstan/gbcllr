#pragma once
#include <gbcllr/machine.hpp>
#include <gbcllr/presentation.hpp>
#include <array>
#include <optional>
#include <memory>
struct DesktopNetStart;
struct DesktopNetLobby;

struct DesktopConfig {
    int scale=4,fullscreen=0,volume=100,source=1,deadzone=15,state_slot=1;
    bool audio=true,linear=false,skip_launcher=false,shortcuts=true;
    bool diagnostics=true; // Also defaults on for profiles predating this key.
    bool color=false;
    bool sgb_requested=false,sgb_current=false,sgb_supported=false,sgb_cli_consumed=false;
    std::optional<gbcllr::Model> original_explicit_model; // CLI intent across launcher/profile loops.
    bool sgb_colors=true,sgb_border=false;
    std::string sgb_boot; // Separate DMG boot preference; native CGB boot is preserved.
    gbcllr::PresentationSettings presentation;
    bool presentation_filter_selected=false; // Current launcher transaction only.
    uint32_t gamepad_instance=0;
    std::string gamepad_guid,rom,boot;
    int partner_source=1,partner_deadzone=15;
    uint32_t partner_instance=0;
    std::string partner_guid;
    std::array<int,10> partner_keys{},partner_pads{};
    unsigned coop_first=0;
    unsigned coop_view=0;
    bool coop_first_saved=false;
    bool coop_enabled=false,coop_enabled_saved=false,coop_shared=true;
    std::array<int,10> keys{},pads{};
    std::array<int,4> shortcut_keys{},shortcut_pads{};
    std::filesystem::path profile;
    std::optional<std::filesystem::path> save_override; // Original caller intent, retained on launcher/retry loops.
    std::filesystem::path package_override;
    std::filesystem::path bundled_package;
    std::optional<gbcllr::NativePackage> package;
    std::optional<gbcllr::NativePackage> dx_package;
    bool adaptive=true,mod_enabled_saved=false,mod_width_saved=false,session_mods=true;
    bool colorization=false,mod_color_saved=false;
    bool weapon_cycle=false,mod_weapon_cycle_saved=false;
    bool smooth_gameplay=false,mod_smooth_saved=false;
    bool no_flicker=false,mod_no_flicker_saved=false;
    bool hide_closed_rooms=false;
    bool gift_enabled=false;
    bool extended_content=false,expanded_encounters=false;
    int gift_choice=1;
    int view_width=-1;
    std::string mod_error;
    std::string link_rom,link_boot,link_save,link_state;
    bool link_lobby_requested=false;
    bool link_retry=false;
    std::string link_error;
    std::shared_ptr<DesktopNetStart> net_start;
    std::shared_ptr<DesktopNetLobby> net_lobby;
    DesktopConfig();
    void load();
    void persist() const;
};

// The launcher rechecks this context after boot and mod choices change.
// A missing boot is allowed only by the explicit skip-launcher path.
struct DesktopBootContext {
    DesktopConfig& config;
    std::optional<gbcllr::Model> explicit_model;
};
gbcllr::Model desktop_boot_model(const DesktopBootContext&,std::string_view rom_identity,
    uint8_t cartridge_color_flag,std::string_view boot_sha);

// Profile and native-save paths are per exact cartridge unless the caller
// explicitly supplies them. Returns false when the player quits the launcher.
bool desktop_prepare(DesktopConfig&,std::filesystem::path& rom,
    std::filesystem::path& boot,std::filesystem::path& save,
    const std::filesystem::path& profile,bool show_launcher,
    bool force_launcher,bool model_explicit,gbcllr::Options&);
std::string desktop_title(const std::string& rom_sha);

// Optional owned-cartridge QA in the actual solo desktop loop. Normal callers
// have no observer; QA can supply input without activating its window.
struct DesktopFrameQa {
    void* context=nullptr;
    uint8_t (*input)(void*,uint32_t,const gbcllr::Machine&)=nullptr;
    bool (*present)(void*,uint32_t,const gbcllr::Machine&,std::span<const int16_t>)=nullptr;
    // SDL_PushEvent does not alter SDL's held-key array. Bounded event tests
    // may provide matching held-key samples without sending OS desktop input.
    const uint8_t* (*keyboard)(void*,const uint8_t*)=nullptr;
};
int desktop(gbcllr::Machine&,const std::filesystem::path&,bool,DesktopConfig&,const DesktopFrameQa&);
