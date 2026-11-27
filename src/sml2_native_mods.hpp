#pragma once
#include <gbcllr/mods.hpp>
#include <memory>
namespace gbcllr {
namespace detail {
constexpr unsigned sml2_window_address(unsigned map,int x,int y){return map+((unsigned(y)>>3)&31)*32+((unsigned(x)>>3)&31);}
}
struct ModContext;
struct ModBackgroundRow;
struct ModPixelSink;
class Sml2NativeMods {
public:
 explicit Sml2NativeMods(std::span<const NativeFeature>,bool model_cgb);
 ~Sml2NativeMods();
 uint8_t read(ModContext&,uint16_t,uint8_t);
 void scanline(ModContext&,uint8_t);
 bool observes_pixels()const;
 bool first_line(ModContext&,std::span<const uint32_t> native);
 void framebuffer_cleared();
 void background_row(ModContext&,const ModBackgroundRow&);
 ModPixelSink pixel_sink(uint8_t);
 void viewport(unsigned,unsigned);
 ModView render(ModContext&,std::span<const uint32_t>);
 std::vector<uint8_t> snapshot()const;
 void restore(std::span<const uint8_t>);
private:
 struct Impl;
 std::unique_ptr<Impl> impl_;
};
}
