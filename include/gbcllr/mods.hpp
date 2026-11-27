#pragma once
#include <cstdint>
#include <array>
#include <filesystem>
#include <string>
#include <string_view>
#include <span>
#include <vector>

namespace gbcllr {
// A data package selects an implementation already linked into the executable.
// No code, symbol names, or executable paths are loaded from a package.
struct NativeFeature {
    std::string plugin;
    uint32_t abi = 1;
    int view_width = -1; // Fit; otherwise 160..4096 game pixels at height 144.
    bool colorization = false; // Host-native color; supported adapters only.
    std::string asset_input; // Pinned owned presentation input, where required.
    uint32_t gift = 0; // Gen2 elective one-shot pending input; 0 means none.
    bool expanded_encounters = false; // Optional native Gen1 species encounters.
    unsigned player1_character = 0; // Xtreme2 co-op: 0 X, 1 Zero.
    bool weapon_cycle = false; // Native virtual-L/R selection on supported titles.
    bool smooth_gameplay = false; // Native CPU headroom; physical clocks are unchanged.
    bool no_flicker = false; // Complete native sprite lists; preserve injury flashes.
    bool operator==(const NativeFeature&) const = default;
};
struct ModView {
    unsigned width = 160;
    bool expanded = false;
    // Valid until the next render, resize or machine restore.
    std::span<const uint32_t> pixels;
    int camera_x=0,camera_y=0,left=0,bound_left=0,bound_right=0;
    unsigned sprites=0,captures=0,activation_adjustments=0,spawn_scans=0,spawn_reads=0;
    unsigned uncapped_sprites=0; // Source-emitted pieces beyond original OAM allocation.
    uint64_t logic_updates=0; // Completed original game work, not helper calls.
    unsigned neighbor_rooms=0,neighbor_ticks=0,neighbor_failures=0;
    // Logical game pixels; sampled at 1x by the current adapters. Texture
    // density is a separate future presentation service, not viewport shape.
    unsigned height = 144;
    // Optional host-only room aperture. Zero leaves presentation unchanged;
    // fixed rooms retain their native pixels inside the wider logical canvas.
    unsigned framing_width = 0;
    std::span<const uint32_t> framing_background; // Same-sized terrain, without actors.
};
// Last completed native SGB output plus actual received-command diagnostics.
// Querying this view never advances the guest or changes protocol state.
struct NativeSgbView {
    bool supported=false,active=false,colors_ready=false,border_ready=false;
    bool uniform_attributes=false,colors_qualified=false;
    uint32_t packets=0,border_revision=0;
    uint8_t mask_mode=0;
    std::array<uint32_t,32> commands{};
    std::span<const uint32_t> colors,border,palettes;
    std::span<const uint8_t> attributes;
};

struct NativePackage {
    std::string id,version,target;
    bool default_enabled = false;
    NativeFeature feature;
    std::string author,description,source;
};
NativePackage read_mod_package(const std::filesystem::path&,std::string_view rom);
}
