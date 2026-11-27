#pragma once
#include <gbcllr/mods.hpp>
#include <algorithm>
#include <vector>

namespace gbcllr {
// Keep a fixed-resolution menu on the selected gameplay canvas. Native menu
// art stays proportional and its background spans the output. Edge pixels
// may contain a scrolling scene, so they must not be repeated into margins.
inline ModView megaman_pause_canvas(ModView view,unsigned width,std::vector<uint32_t>& canvas) {
    if(view.width!=160||width<=160||view.pixels.size()!=size_t(160)*view.height)return view;
    canvas.assign(size_t(width)*view.height,view.pixels.back());
    for(unsigned y=0;y<view.height;++y) {
        std::copy_n(view.pixels.begin()+size_t(y)*160,160,canvas.begin()+size_t(y)*width+(width-160)/2);
    }
    view.width=width;view.expanded=true;view.pixels=canvas;return view;
}
}
