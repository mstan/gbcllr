#pragma once
#include <gbcllr/mods.hpp>
#include <array>
#include <span>
#include <string_view>
#include <vector>

namespace gbcllr {
// Host-only presentation. Never part of Machine state, cartridge or net identity.
struct PresentationSettings {
    bool enabled=true;
    int palette=0,correction=0,effect=0;
};
struct PresentationPalette {const char* name;std::array<uint32_t,4> colors;};
std::span<const PresentationPalette> presentation_palettes();
struct PresentationOption {
    const char* key;const char* label;const char* description;
    std::span<const char* const> choices;
};
std::span<const PresentationOption> presentation_options();
// filter is the existing nearest/linear host setting.
int presentation_value(const PresentationSettings&,bool filter,std::string_view key);
bool presentation_set(PresentationSettings&,bool& filter,std::string_view key,int value);
bool presentation_linear(const PresentationSettings&,bool filter);
struct PresentationFrame {unsigned width,height;std::span<const uint32_t> pixels;};
class RoomFraming {
public:
    ModView apply(ModView,uint64_t display_frame);
private:
    unsigned width_=0,height_=0,target_=0,from_=0,aperture_=0;
    int native_left_=0,shift_=0,from_shift_=0;
    uint64_t frame_=0,start_=0;
    std::vector<uint32_t> previous_,edges_,pixels_;
};
class PresentationTransform {
public:
    // destination is sampling density only; the caller retains logical geometry.
    ModView cartridge_sgb_colors(const ModView&,const NativeSgbView&);
    PresentationFrame apply(const ModView&,const PresentationSettings&,unsigned destination_width,unsigned destination_height);
private:
    std::vector<uint32_t> colors_,sampled_,sgb_;
    struct LcdAxis {unsigned first,second;double fraction,edge;};
    std::vector<LcdAxis> lcd_x_,lcd_y_;
    unsigned lcd_width_=0,lcd_height_=0;
};
}
