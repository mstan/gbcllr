#pragma once
#include <gbcllr/mods.hpp>
#include "native_sgb_engine.h"
#include <memory>
namespace gbcllr {
class NativeSgbService {
public:
    explicit NativeSgbService(bool dmg);
    uint8_t read(uint8_t base);
    void write(uint8_t value,const uint8_t* vram);
    void complete_frame(std::span<const uint32_t> native);
    bool freezes_frame() const;
    NativeSgbView view() const;
    std::vector<uint8_t> snapshot() const;
    void restore(std::span<const uint8_t>);
private:
    std::unique_ptr<GBSgbState,decltype(&llr_sgb_destroy)> state_{nullptr,llr_sgb_destroy};
    NativeSgbFrame frame_{};
    bool dmg_=false,frame_valid_=false,frame_palette_=false,shades_valid_=false;
    uint8_t frame_mask_=0;
    std::array<uint16_t,16> palette_{};
    std::array<uint8_t,360> attributes_{};
    std::array<uint32_t,160*144> colors_{};
    std::array<uint32_t,16> palette_rgb_{};
    std::array<uint32_t,256*224> border_{};
    NativeSgbBorder published_border_{};
    bool border_ready_=false;
    uint32_t border_revision_=0;
    void rebuild_colors();
    void refresh_border();
};
}
