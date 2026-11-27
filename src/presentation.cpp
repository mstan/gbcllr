#include <gbcllr/presentation.hpp>
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace gbcllr {
namespace {
// Exact legacy gb-recompiled platform_sdl.cpp five tint values and ppu.c
// CGB boot BG palette values. These are global host tints, not BIOS overrides.
// SGB standard palette data: pinned SameBoy Core/sgb.c, Expat license;
// Copyright (c) 2015-2026 Lior Halphon; see third_party/sameboy/LICENSE.
constexpr PresentationPalette palettes[]={
    {"Native / authentic",{0x00000000u,0x00000000u,0x00000000u,0x00000000u}},
    {"DMG",{0xffe0f8d0u,0xff88c070u,0xff346856u,0xff081820u}},
    {"Game Boy Pocket",{0xffc9d2d5u,0xff8b95a0u,0xff4c5562u,0xff181c20u}},
    {"Game Boy Light",{0xffcfe07au,0xff8aa34au,0xff45612au,0xff131d11u}},
    {"Black & White",{0xffffffffu,0xffaaaaaau,0xff555555u,0xff000000u}},
    {"Amber (Phosphor)",{0xffffb000u,0xffcb4f0eu,0xff800000u,0xff330000u}},
    {"CGB Green (tint)",{0xffffffffu,0xff52ff00u,0xffff4100u,0xff000000u}},
    {"CGB Blue (tint)",{0xffffffffu,0xff62a4ffu,0xff0000ffu,0xff000000u}},
    {"CGB Brown (tint)",{0xffffffffu,0xffffac62u,0xff833100u,0xff000000u}},
    {"CGB Pastel Mix (tint)",{0xffffffa4u,0xffff9494u,0xff9494ffu,0xff000000u}},
    {"CGB Dark Green (tint)",{0xffffffffu,0xff7bff31u,0xff0062c5u,0xff000000u}},
    {"CGB Dark Blue (tint)",{0xffffffffu,0xff8b8bdeu,0xff52528bu,0xff000000u}},
    {"CGB Red (tint)",{0xffffffffu,0xffff8383u,0xff943939u,0xff000000u}},
    {"CGB Orange (tint)",{0xffffffffu,0xffffff00u,0xffff0000u,0xff000000u}},
    {"CGB Inverted (tint)",{0xff000000u,0xff008383u,0xffffde00u,0xffffffffu}},
    {"CGB Grayscale (tint)",{0xffffffffu,0xffa4a4a4u,0xff525252u,0xff000000u}},
    {"CGB Dark Brown (tint)",{0xffffe6c5u,0xffcd9c83u,0xff836a29u,0xff5a3108u}},
    {"CGB Yellow (tint)",{0xffffffffu,0xffffff00u,0xff7b4a00u,0xff000000u}},
    {"SGB 1-A (palette only)",{0xffffeecdu,0xffde944au,0xffac2920u,0xff311852u}},
    {"SGB 1-B (palette only)",{0xffdedec5u,0xffcdb473u,0xffb45210u,0xff000000u}},
    {"SGB 1-C (palette only)",{0xffffc5ffu,0xffee9c52u,0xff9c3962u,0xff39399cu}},
    {"SGB 1-D (palette only)",{0xffffffacu,0xffc5834au,0xffff0000u,0xff521800u}},
    {"SGB 1-E (palette only)",{0xffffdeb4u,0xff7bc57bu,0xff6a8b41u,0xff5a3920u}},
    {"SGB 1-F (palette only)",{0xffdeeeffu,0xffe68b52u,0xffac0000u,0xff004110u}},
    {"SGB 1-G (palette only)",{0xff000052u,0xff00a4eeu,0xff7b7b00u,0xffffff5au}},
    {"SGB 1-H (palette only)",{0xffffeee6u,0xffffbd8bu,0xff834100u,0xff311800u}},
    {"SGB 2-A (palette only)",{0xfff6cda4u,0xffc58b4au,0xff297b00u,0xff000000u}},
    {"SGB 2-B (palette only)",{0xffffffffu,0xffffee52u,0xffff3100u,0xff52005au}},
    {"SGB 2-C (palette only)",{0xffffc5ffu,0xffee8b8bu,0xff7b31eeu,0xff29299cu}},
    {"SGB 2-D (palette only)",{0xffffffa4u,0xff00ff00u,0xffff3100u,0xff000052u}},
    {"SGB 2-E (palette only)",{0xffffcd83u,0xff94b4e6u,0xff291062u,0xff100810u}},
    {"SGB 2-F (palette only)",{0xffd5ffffu,0xffff9452u,0xffa40000u,0xff180000u}},
    {"SGB 2-G (palette only)",{0xff6abd39u,0xffe65241u,0xffe6bd83u,0xff001800u}},
    {"SGB 2-H (palette only)",{0xffffffffu,0xffbdbdbdu,0xff737373u,0xff000000u}},
    {"SGB 3-A (palette only)",{0xffffd59cu,0xff73c5c5u,0xffff6229u,0xff314a62u}},
    {"SGB 3-B (palette only)",{0xffdedec5u,0xffe68320u,0xff005200u,0xff001010u}},
    {"SGB 3-C (palette only)",{0xffe6accdu,0xffffff7bu,0xff00bdffu,0xff20205au}},
    {"SGB 3-D (palette only)",{0xfff6ffbdu,0xffe6ac7bu,0xff08cd00u,0xff000000u}},
    {"SGB 3-E (palette only)",{0xffffffc5u,0xffe6b46au,0xffb47b20u,0xff524a73u}},
    {"SGB 3-F (palette only)",{0xff7b7bcdu,0xffff6affu,0xffffd500u,0xff414141u}},
    {"SGB 3-G (palette only)",{0xff62de52u,0xffffffffu,0xffcd3139u,0xff390000u}},
    {"SGB 3-H (palette only)",{0xffe6ffa4u,0xff7bcd39u,0xff4a8b18u,0xff081800u}},
    {"SGB 4-A (palette only)",{0xfff6ac6au,0xff7bacffu,0xffd500d5u,0xff00007bu}},
    {"SGB 4-B (palette only)",{0xfff6eef6u,0xffeea462u,0xff417b39u,0xff180808u}},
    {"SGB 4-C (palette only)",{0xffffe6e6u,0xffdea4d5u,0xff9ca4e6u,0xff080000u}},
    {"SGB 4-D (palette only)",{0xffffffbdu,0xff94cdcdu,0xff4a6a7bu,0xff08204au}},
    {"SGB 4-E (palette only)",{0xffffdeacu,0xffe6ac7bu,0xff7b5a8bu,0xff002031u}},
    {"SGB 4-F (palette only)",{0xffbdd5d5u,0xffde83deu,0xff8300a4u,0xff390000u}},
    {"SGB 4-G (palette only)",{0xffb4e618u,0xffbd205au,0xff291000u,0xff008362u}},
    {"SGB 4-H (palette only)",{0xffffffcdu,0xffbdc55au,0xff838b41u,0xff415229u}},
};
const auto palette_names=[] {std::array<const char*,std::size(palettes)> a{};for(size_t i=0;i<a.size();++i)a[i]=palettes[i].name;return a;}();
constexpr const char* corrections[]={"Off","GBC","GBA"};
constexpr const char* filters[]={"Nearest","Linear"};
constexpr const char* effects[]={"Off (use filter)","Sharp","Smooth","LCD grid"};
const PresentationOption options[]={
    {"palette","Palette","Native preserves the game's colors. Other choices tint the whole frame to four shades. SGB choices are palettes only; no borders or SGB hardware.",palette_names},
    {"correction","Color correction","Legacy GBC or GBA display color matrix, applied to native colors or the selected tint.",corrections},
    {"filter","Texture filter","Nearest pixels or linear interpolation. An enabled Sharp, Smooth or LCD effect selects its own sampling.",filters},
    {"effect","Screen effect","Native host equivalents of the legacy built-in Sharp, Smooth and LCD shaders.",effects}
};
unsigned channel(uint32_t p,unsigned shift){return (p>>shift)&255;}
uint32_t color(uint32_t p,const PresentationSettings& s) {
    auto alpha=p&0xff000000u;
    if(s.palette>0&&size_t(s.palette)<std::size(palettes)) {
        // Preserve exact four-shade DMG pixels. For native color frames,
        // quantize integer Rec.601 luminance into four equally sized bands.
        constexpr uint32_t dmg[]={0xffe6f6cdu,0xff73bd62u,0xff319c52u,0xff081010u};
        unsigned shade=4;
        for(unsigned i=0;i<4;++i)if((p&0xffffffu)==(dmg[i]&0xffffffu)){shade=i;break;}
        if(shade==4){unsigned l=(77*channel(p,16)+150*channel(p,8)+29*channel(p,0)+128)>>8;shade=3-(l>>6);}
        p=alpha|(palettes[s.palette].colors[shade]&0xffffffu);
    }
    if(s.correction==1||s.correction==2) {
        constexpr int matrices[2][9]={{893,123,8,102,840,82,102,133,789},{860,169,-5,92,676,256,92,261,671}};
        auto& m=matrices[s.correction-1];int r=channel(p,16),g=channel(p,8),b=channel(p,0);
        // C++20 signed right shift matches the legacy integer shader staging.
        auto c=[&](int i){return uint32_t(std::clamp((m[i]*r+m[i+1]*g+m[i+2]*b)>>10,0,255));};
        p=alpha|(c(0)<<16)|(c(3)<<8)|c(6);
    }
    return p;
}
}
std::span<const PresentationPalette> presentation_palettes(){return palettes;}
std::span<const PresentationOption> presentation_options(){return options;}
int presentation_value(const PresentationSettings& s,bool filter,std::string_view key) {
    if(key=="enabled")return s.enabled;if(key=="palette")return s.palette;if(key=="correction")return s.correction;
    if(key=="filter")return filter;if(key=="effect")return s.effect;return -1;
}
bool presentation_set(PresentationSettings& s,bool& filter,std::string_view key,int value) {
    if(key=="enabled"){if(value<0||value>1)return false;s.enabled=value!=0;return true;}
    auto it=std::find_if(std::begin(options),std::end(options),[&](const auto& o){return key==o.key;});
    if(it==std::end(options)||value<0||size_t(value)>=it->choices.size())return false;
    if(key=="palette")s.palette=value;else if(key=="correction")s.correction=value;else if(key=="filter")filter=value!=0;else s.effect=value;
    return true;
}
bool presentation_linear(const PresentationSettings& s,bool filter){return s.enabled&&(s.effect? s.effect!=1:filter);}
ModView RoomFraming::apply(ModView view,uint64_t frame) {
    if(!view.framing_width||view.width<=160||view.pixels.size()!=size_t(view.width)*view.height) {
        width_=height_=target_=from_=0;previous_.clear();edges_.clear();pixels_.clear();return view;
    }
    unsigned target=std::clamp(view.framing_width,160u,view.width);
    int native_left=std::clamp(view.camera_x-view.left,0,int(view.width)-160);
    if(width_!=view.width||height_!=view.height||frame<frame_||previous_.empty()) {
        width_=view.width;height_=view.height;target_=from_=aperture_=target;start_=frame;edges_.clear();shift_=from_shift_=0;
    } else if(target!=target_) {
        from_=aperture_;target_=target;start_=frame;edges_=previous_;from_shift_=shift_+native_left_-native_left;
    }
    frame_=frame;native_left_=native_left;
    unsigned age=unsigned(std::min<uint64_t>(frame-start_,12));
    auto remember=[&]{if(view.framing_background.size()==view.pixels.size())previous_.assign(view.framing_background.begin(),view.framing_background.end());else previous_.assign(view.pixels.size(),0xff000000u);};
    aperture_=target_;
    if(age>=12||(from_==target_&&!from_shift_)){shift_=0;edges_.clear();remember();return view;}
    pixels_.resize(view.pixels.size());previous_.resize(view.pixels.size());
    {
        // Twelve displays (~200ms): ease framing without scaling game pixels.
        // Retained margins contain terrain only; the HUD is always current.
        unsigned ease=age*age*(36-2*age),aperture=unsigned((int(from_)*(1728-int(ease))+int(target_)*int(ease)+864)/1728);
        aperture_=aperture;
        shift_=from_shift_*(1728-int(ease))/1728;
        unsigned mask=(width_-aperture)/2;int room=int(width_-target_)/2+shift_;
        // Clamped room cameras can put the physical viewport at either edge.
        // Never cover that live160px interval while opening the outer world.
        unsigned visible_left=unsigned(std::clamp(native_left+shift_,0,int(width_)-160));
        unsigned mask_left=std::min(mask,visible_left),mask_right=std::max(width_-mask,visible_left+160);
        bool background=view.framing_background.size()==view.pixels.size();
        for(unsigned y=0;y<height_;++y)for(unsigned x=0;x<width_;++x) {
            size_t at=size_t(y)*width_+x;int source_x=int(x)-(y<128?shift_:0);
            bool valid=source_x>=0&&source_x<int(width_);size_t source=size_t(y)*width_+unsigned(std::max(source_x,0));
            pixels_[at]=valid?view.pixels[source]:0xff000000u;previous_[at]=valid&&background?view.framing_background[source]:0xff000000u;
            if(y>=128)continue;
            if(x<mask_left||x>=mask_right)pixels_[at]=previous_[at]=0xff000000u;
            else if(target_<from_&&(int(x)<room||int(x)>=room+int(target_))&&edges_.size()==pixels_.size())pixels_[at]=previous_[at]=edges_[at];
        }
    }
    view.pixels=pixels_;return view;
}
ModView PresentationTransform::cartridge_sgb_colors(const ModView& view,const NativeSgbView& sgb) {
    if(!sgb.colors_ready)return view;
    if(view.width<160||view.width>4096||view.height<144||view.height>4096||
       view.pixels.size()!=size_t(view.width)*view.height||sgb.colors.size()!=160*144||
       sgb.palettes.size()!=16||sgb.attributes.size()!=360||sgb.attributes[0]>3)
        throw std::invalid_argument("Invalid active SGB presentation frame");
    auto native=[&]{auto v=view;v.width=160;v.height=144;v.expanded=false;v.pixels=sgb.colors;return v;};
    if(view.width==160&&view.height==144)return native();
    // The cartridge's attribute plane describes its original160 pixels.
    // A uniform plane also defines the actual field palette for native
    // expanded tiles. Other regions retain their authored native screen.
    if(!sgb.uniform_attributes)return native();
    sgb_.resize(view.pixels.size());
    if(sgb.mask_mode==2||sgb.mask_mode==3)std::fill(sgb_.begin(),sgb_.end(),sgb.mask_mode==2?0xff000000u:sgb.palettes[0]);
    else for(size_t i=0;i<view.pixels.size();++i){auto p=view.pixels[i];unsigned r=(p>>16)&255,g=(p>>8)&255,b=p&255;
        if(r!=g||g!=b||(r!=0&&r!=85&&r!=170&&r!=255))return native();
        sgb_[i]=sgb.palettes[size_t(sgb.attributes[0])*4+3-r/85];
    }
    // Gen1's completed world source is held during MASK1. Its native center
    // can already contain transfer pixels, so preserve the cartridge's exact
    // frozen image there while coloring the retained margins with its latched
    // palette. Both landscape and portrait place Original160 at the center.
    if(sgb.mask_mode==1){unsigned left=(view.width-160)/2,top=(view.height-144)/2;
        for(unsigned y=0;y<144;++y)std::copy_n(sgb.colors.begin()+y*160,160,sgb_.begin()+size_t(y+top)*view.width+left);
    }
    auto v=view;v.pixels=sgb_;return v;
}
PresentationFrame PresentationTransform::apply(const ModView& view,const PresentationSettings& s,unsigned dw,unsigned dh) {
    if(!view.width||!view.height||view.pixels.size()!=size_t(view.width)*view.height)throw std::invalid_argument("Invalid presentation frame");
    auto pixels=view.pixels;
    if(!s.enabled)return {view.width,view.height,pixels};
    if(s.palette||s.correction){colors_.resize(pixels.size());std::transform(pixels.begin(),pixels.end(),colors_.begin(),[&](uint32_t p){return color(p,s);});pixels=colors_;}
    if(s.effect!=3)return {view.width,view.height,pixels};
    // CPU-native equivalent of the finite legacy LCD shader. Evaluate at the
    // actual destination pixel centers, with bilinear sampling and clamp edges.
    // Logical game dimensions remain view.width/view.height in the desktop.
    dw=std::clamp(dw,1u,4096u);dh=std::clamp(dh,1u,4096u);sampled_.resize(size_t(dw)*dh);
    auto axis=[](std::vector<LcdAxis>& values,unsigned source,unsigned dest){
        values.resize(dest);
        for(unsigned i=0;i<dest;++i){double p=(i+0.5)*source/dest;int lo=int(std::floor(p-0.5));double phase=p-std::floor(p);
            values[i]={unsigned(std::clamp(lo,0,int(source)-1)),unsigned(std::clamp(lo+1,0,int(source)-1)),p-0.5-lo,std::min(phase,1-phase)*dest/source};}
    };
    if(lcd_width_!=view.width||lcd_x_.size()!=dw){axis(lcd_x_,view.width,dw);lcd_width_=view.width;}
    if(lcd_height_!=view.height||lcd_y_.size()!=dh){axis(lcd_y_,view.height,dh);lcd_height_=view.height;}
    for(unsigned y=0;y<dh;++y){const auto& yy=lcd_y_[y];double fy=yy.fraction;
        auto row0=pixels.data()+size_t(yy.first)*view.width,row1=pixels.data()+size_t(yy.second)*view.width;
        for(unsigned x=0;x<dw;++x){const auto& xx=lcd_x_[x];double fx=xx.fraction;
            auto a=row0[xx.first],b=row0[xx.second],c=row1[xx.first],d=row1[xx.second];
            double edge=std::clamp(std::min(xx.edge,yy.edge),0.0,1.0);
            double line=edge*edge*(3-2*edge),light=0.78+0.22*line;
            // Every term is nonnegative and bounded by255. For that domain
            // conversion after +0.5 is exactly lround's half-away rounding.
            auto sample=[&](unsigned shift,double tint){double top=channel(a,shift)*(1-fx)+channel(b,shift)*fx,bottom=channel(c,shift)*(1-fx)+channel(d,shift)*fx;return uint32_t((top*(1-fy)+bottom*fy)*light*tint+0.5);};
            sampled_[size_t(y)*dw+x]=0xff000000u|(sample(16,0.9928)<<16)|(sample(8,1.0)<<8)|sample(0,0.9856);
        }
    }
    return {dw,dh,sampled_};
}
}
