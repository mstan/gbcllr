#include <gbcllr/presentation.hpp>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <stdexcept>
using namespace gbcllr;
static void check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
int main(){try{
 std::array<uint32_t,4> grayscale={0xffffffff,0xffaaaaaa,0xff555555,0xff000000};
 ModView v;v.width=4;v.height=1;v.pixels=grayscale;PresentationTransform t;PresentationSettings s;
 auto native=t.apply(v,s,12,3);check(native.pixels.data()==grayscale.data()&&native.width==4,"native identity copied or altered output");
 check(presentation_palettes().size()==50&&presentation_options().size()==4,"finite legacy/SGB catalog incomplete");
 for(size_t i=1;i<presentation_palettes().size();++i){s.palette=int(i);auto f=t.apply(v,s,12,3);check(std::equal(f.pixels.begin(),f.pixels.end(),presentation_palettes()[i].colors.begin()),"four shades differ from selected palette data");}
 s={};s.correction=1;std::array<uint32_t,4> rgb={0xff0000ff,0xff00ff00,0xffff0000,0x1255aaff};v.pixels=rgb;
 auto gbc=t.apply(v,s,12,3);check(gbc.pixels[0]==0xff0114c4&&gbc.pixels[1]==0xff1ed121&&gbc.pixels[2]==0xffde1919,"legacy GBC integer matrix differs");check((gbc.pixels[3]>>24)==0x12,"matrix lost alpha");
 s.correction=2;auto gba=t.apply(v,s,12,3);check(gba.pixels[0]==0xff003fa7,"legacy GBA negative coefficient/clamp differs");
 s.palette=4;s.correction=0;auto bw=t.apply(v,s,12,3);for(auto pixel:bw.pixels)check((pixel&255)==((pixel>>8)&255)&&((pixel>>8)&255)==((pixel>>16)&255),"BW did not remove actual color");
 s={};s.effect=3;std::array<uint32_t,4> white={0xffffffff,0xffffffff,0xffffffff,0xffffffff};v.pixels=white;
 auto lcd=t.apply(v,s,12,3);check(lcd.width==12&&lcd.height==3,"LCD changed logical view or ignored destination sampling");check(lcd.pixels[13]==0xfffdfffb&&lcd.pixels[0]==0xffe1e3e0,"LCD grid center/edge formula differs from legacy GLSL");
 // Original double sampler is the oracle for cached coordinate and rounding
 // changes. Reuse the transform through source/destination resize reversals.
 struct Shape{unsigned sw,sh,dw,dh;};
 for(auto shape:{Shape{4,1,12,3},Shape{160,144,641,577},Shape{256,144,1280,720},Shape{160,284,360,641},Shape{32,29,17,13},Shape{1,1,37,29},Shape{7,9,1,1},Shape{160,144,641,577}}){
  std::vector<uint32_t> source(size_t(shape.sw)*shape.sh);uint32_t random=0x617075;
  for(auto& p:source){random=random*1664525u+1013904223u;p=random|0xff000000u;}
  ModView input;input.width=shape.sw;input.height=shape.sh;input.pixels=source;
  auto actual=t.apply(input,s,shape.dw,shape.dh);
  for(unsigned y=0;y<shape.dh;++y)for(unsigned x=0;x<shape.dw;++x){
   double sx=(x+0.5)*shape.sw/shape.dw,sy=(y+0.5)*shape.sh/shape.dh;
   int x0=int(std::floor(sx-0.5)),y0=int(std::floor(sy-0.5));double fx=sx-0.5-x0,fy=sy-0.5-y0;
   auto at=[&](int xx,int yy){return source[size_t(std::clamp(yy,0,int(shape.sh)-1))*shape.sw+std::clamp(xx,0,int(shape.sw)-1)];};
   auto a=at(x0,y0),b=at(x0+1,y0),c=at(x0,y0+1),d=at(x0+1,y0+1);
   double cx=sx-std::floor(sx),cy=sy-std::floor(sy);
   double edge=std::clamp(std::min(std::min(cx,1-cx)*shape.dw/shape.sw,std::min(cy,1-cy)*shape.dh/shape.sh),0.0,1.0);
   double line=edge*edge*(3-2*edge),light=0.78+0.22*line;
   auto sample=[&](unsigned shift,double tint){auto ch=[&](uint32_t p){return(p>>shift)&255;};double top=ch(a)*(1-fx)+ch(b)*fx,bottom=ch(c)*(1-fx)+ch(d)*fx;return uint32_t(std::clamp(std::lround((top*(1-fy)+bottom*fy)*light*tint),0l,255l));};
   check(actual.pixels[size_t(y)*shape.dw+x]==(0xff000000u|(sample(16,0.9928)<<16)|(sample(8,1)<<8)|sample(0,0.9856)),"cached LCD sampler differs from original double reference");
  }
 }
 s.enabled=false;auto off=t.apply(v,s,12,3);check(off.pixels.data()==white.data()&&off.width==4,"disabled mod altered frame");check(!presentation_linear(s,true),"disabled mod kept a screen filter");
 bool filter=false;s={};check(!presentation_set(s,filter,"palette",50)&&!presentation_set(s,filter,"correction",3)&&!presentation_set(s,filter,"effect",4)&&!presentation_set(s,filter,"enabled",2),"invalid settings accepted");
 check(presentation_set(s,filter,"filter",1)&&presentation_linear(s,filter),"linear filter ignored");s.effect=1;check(!presentation_linear(s,filter),"Sharp ignored nearest sampling");s.effect=2;filter=false;check(presentation_linear(s,filter),"Smooth ignored linear sampling");
 // World framing changes only side margins; native center/HUD stay exact.
 RoomFraming framing;std::vector<uint32_t> wide(256*144,0xffff0000u),narrow(256*144,0xff000000u);
 for(unsigned y=0;y<144;++y)std::fill_n(narrow.begin()+y*256+48,160,0xff0000ffu);
 auto terrain=wide;wide[80*256+230]=0xffffff00u;
 ModView room;room.width=256;room.height=144;room.left=-48;room.pixels=wide;room.framing_width=256;room.framing_background=terrain;framing.apply(room,0);
 room.pixels=narrow;room.framing_width=160;room.framing_background=narrow;room.left=-48;unsigned previous_margin=48;
 for(unsigned n=0;n<=12;++n){auto f=framing.apply(room,n+1);unsigned visible=0;
  for(unsigned y=0;y<144;++y)for(unsigned x=48;x<208;++x)check(f.pixels[y*256+x]==0xff0000ffu,"room framing altered/scaled native center or HUD");
  for(unsigned x=0;x<48;++x)visible+=f.pixels[x]!=0xff000000u;
  check(f.pixels[80*256+230]!=0xffffff00u,"retained margins duplicated an old actor");
  check(visible<=previous_margin,"closing side masks reversed direction");previous_margin=visible;
  std::vector<uint32_t> paused(f.pixels.begin(),f.pixels.end());auto again=framing.apply(room,n+1);check(std::equal(paused.begin(),paused.end(),again.pixels.begin()),"paused host frame advanced room mask animation");
 }
 check(previous_margin==0,"room margins did not settle to black after12 displays");
 room.pixels=wide;room.framing_width=256;room.framing_background=terrain;room.left=0;auto opening=framing.apply(room,14);check(opening.pixels[48]==0xffff0000u&&opening.pixels[255]==0xff000000u,"opening mask hid the projected native viewport or exposed the far edge");
 auto opened=framing.apply(room,26);check(std::equal(opened.pixels.begin(),opened.pixels.end(),wide.begin()),"wide room did not finish opening");
 room.framing_width=0;check(framing.apply(room,27).pixels.data()==wide.data(),"unmarked presentation changed");
 // Representative full-screen opt-in cost, not an optimization claim.
 std::vector<uint32_t> frame(256*144,0xff7f9fbf);v.width=256;v.height=144;v.pixels=frame;s.effect=3;
 auto begin=std::chrono::steady_clock::now();for(int i=0;i<12;++i)t.apply(v,s,768,432);
 auto ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count()/12;
 std::cout<<"PASS native identity,50 palettes,legacy correction,LCD grid,settings bounds; LCD 768x432 ms/frame="<<ms<<'\n';return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
