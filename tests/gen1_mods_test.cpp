#include "../src/native_mods.hpp"
#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <utility>
static void check(bool v,const char* m){if(!v)throw std::runtime_error(m);}
template<class F>static void rejects(F f){bool caught=false;try{f();}catch(const std::exception&){caught=true;}check(caught,"unqualified Gen1 profile/state accepted");}
int main(int argc,char** argv){try{
 using namespace gbcllr;check(argc==4,"three Gen1 manifests required");
 const char* hashes[]={"5ca7ba01642a3b27b0cc0b5349b52792795b62d3ed977e98a09390659af96b7b","2a951313c2640e8c2cb21f25d1db019ae6245d9c7121f754fa61afd7bee6452d","8cbaa499397e4f1a679c992ea9382a2dd7942ab398b48c19829c2d9529de47bf"};
 for(int i=0;i<3;++i){auto package=read_mod_package(argv[i+1],hashes[i]);
  check(package.default_enabled&&package.feature.view_width==-1,"Gen1 fresh Fit default differs");
  std::array feature{package.feature};
  rejects([&]{read_mod_package(argv[i+1],hashes[(i+1)%3]);});
  rejects([&]{NativeMods wrong(std::string(64,'0'),feature,false);});
  if(i!=2)rejects([&]{NativeMods wrong(hashes[i],feature,true);});
  auto bad=feature;bad[0].abi=2;rejects([&]{NativeMods wrong(hashes[i],bad,false);});
  bad=feature;bad[0].plugin="gbcllr.gen1.extended";
  rejects([&]{NativeMods wrong(hashes[i],bad,false);}); // Missing pinned owned data never enables Extended.
  NativeMods a(hashes[i],feature,false),b(hashes[i],feature,false),other(hashes[(i+1)%3],feature,false);
  check(!a.observes_scanlines(),"Gen1 field unnecessarily observes every scanline");a.viewport(1280,720);
  const auto before=a.snapshot();b.restore(before);check(b.snapshot()==before,"Gen1 current state differs");
  rejects([&]{other.restore(before);});
  auto corrupt=before;corrupt.pop_back();rejects([&]{a.restore(corrupt);});
  corrupt=before;corrupt[12]=1;rejects([&]{a.restore(corrupt);});
  corrupt=before;corrupt[16]=2;rejects([&]{a.restore(corrupt);});
  check(a.snapshot()==before,"invalid Gen1 load mutated live state");
  // The original cache has three32px blocks of border on each side and a
  // source-defined row stride. Distinct off-camera terrain checks narrow,
  // wide and tall maps rather than repeating the Pallet16-byte stride.
  for(auto dimensions:{std::pair{9u,9u},std::pair{52u,12u},std::pair{10u,72u}}){
   std::vector<uint8_t> rom(0x100000),ram(0x8000),vram(0x4000);
   std::array<uint32_t,32> bg{0xff000000,0xff001122,0xffaabbcc,0xffffffff};
   std::array<uint32_t,32> obj{};ModPpu p{0,0,0x91,167,144,nullptr,nullptr,bg.data(),obj.data()};
   ModContext c{};c.rom=rom.data();c.rom_size=rom.size();c.wram=ram.data();c.vram=vram.data();c.ppu=&p;
   unsigned offset=i==2?1:0,stride=dimensions.first+6,anchor_row=dimensions.second>=36?20:dimensions.first>20?10:3,anchor=0xc6e8+anchor_row*stride+3;
   auto set=[&](unsigned address,unsigned value){ram[address-0xc000]=uint8_t(value);};
   auto word=[&](unsigned address,unsigned value){set(address,value);set(address+1,value>>8);};
   set(0xd35e - offset,7);set(0xd367 - offset,1);set(0xd368 - offset,dimensions.second);set(0xd369 - offset,dimensions.first);
   word(0xd35f - offset,anchor);word(0xd526 - offset,0x9800);set(0xd52b - offset,2);word(0xd52c - offset,0x4000);
   std::fill_n(rom.begin()+0x8000,16,1);std::fill_n(rom.begin()+0x8010,16,2);
   std::fill_n(vram.begin()+0x1800,0x400,1);
   for(unsigned y=0;y<8;++y){vram[16+y*2]=255;vram[32+y*2+1]=255;}
   set(0xc6e8+(anchor_row+1)*stride+8,1);
   NativeMods field(hashes[i],feature,false);field.viewport(1280,720);field.scanline(c,0);
   std::vector<uint32_t> native(160*144,0xff123456);auto view=field.render(c,native);
   check(view.expanded&&view.bound_right==int(dimensions.first*32),"source-defined field dimensions did not expand");
   check(view.pixels[40*256+224]==bg[2],"non-Pallet cache stride lost actual off-camera terrain");
   for(unsigned y=0;y<144;++y)check(std::equal(native.begin()+y*160,native.begin()+(y+1)*160,view.pixels.begin()+y*256+48),"variable-map view changed the LLE center");
   auto current=field.snapshot();b.restore(current);check(b.snapshot()==current,"variable map lost current-build state");
   // Fit exposes actual cached terrain above the native center in portrait.
   // Geometry is a host choice; neither resize nor current-load may change it
   // through the canonical simulation snapshot.
   field.viewport(720,1280);check(field.snapshot()==current,"portrait resize changed canonical Gen1 state");
   set(0xc6e8+(anchor_row-2)*stride+5,1);field.scanline(c,0);view=field.render(c,native);
   check(view.width==160&&view.height==284&&view.expanded,"Gen1 portrait Fit did not expand vertically");
   check(view.pixels[30*160+64]==bg[2],"portrait margin lost original cached terrain");
   for(unsigned y=0;y<144;++y)check(std::equal(native.begin()+y*160,native.begin()+(y+1)*160,view.pixels.begin()+(y+70)*160),"portrait changed the native center");
   // A native-active NPC clipped above the DMG view must remain visible in
   // the portrait margin without being copied over the authoritative center.
   obj[1]=0xffff00ff;set(0xc110,1);set(0xc21e,1);set(0xc112,255);set(0xc114,220);set(0xc116,64);
   set(0xc214,(anchor_row-3)*2+2);set(0xc215,8);set(0xd361-offset,4);
   rom[0x4000]=0;rom[0x4001]=0x41;rom[0x4002]=0;rom[0x4003]=0x42;
   if(i==2){rom[0x4100]=1;rom[0x4101]=0;rom[0x4102]=0;rom[0x4103]=3;rom[0x4104]=0;}
   else std::fill_n(rom.begin()+0x4100,4,3);
   for(unsigned y=0;y<8;++y)vram[48+y*2]=255;
   field.scanline(c,0);view=field.render(c,native);check(view.pixels[34*160+64]==obj[1],"portrait dropped native-active vertically clipped NPC");
   if(i==2){
    auto saved=p;p.bgp=p.obp0=p.obp1=0x1b;obj[17]=0xff2468ac;
    // Real CGB palette conversion has already applied the DMG registers.
    // Source OAM_PAL1 maps to CGB palette4, whose color1 is index17.
    rom[0x4104]=16;
    NativeMods colored(hashes[i],feature,true);colored.viewport(720,1280);colored.scanline(c,0);
    auto color_view=colored.render(c,native);
    check(color_view.expanded&&color_view.pixels[30*160+64]==bg[2],"CGB terrain palette was remapped twice");
    check(color_view.pixels[34*160+64]==obj[17],"CGB OBP1 sprite did not use palette4");
    for(unsigned y=0;y<144;++y)check(std::equal(native.begin()+y*160,native.begin()+(y+1)*160,color_view.pixels.begin()+(y+70)*160),"CGB changed authoritative native center");
    auto color_state=colored.snapshot();rejects([&]{field.restore(color_state);});
    colored.restore(color_state);check(colored.snapshot()==color_state,"CGB current-load changed palette state");
    check(colored.observes_scanlines(),"CGB palette transfers lost native row timing");
    colored.complete_frame(c,false);word(0xd35f-offset,0);
    colored.scanline(c,0);colored.complete_frame(c,false);
    check(colored.render(c,native).expanded,"staged map metadata discarded a proven visible source");
    vram[0x1800]=2;colored.scanline(c,0);colored.complete_frame(c,false);
    check(!colored.render(c,native).expanded,"previous world survived a real visible tile mismatch");
    vram[0x1800]=1;word(0xd35f-offset,anchor);colored.restore(color_state);
    colored.viewport(1008,432);colored.scanline(c,0);auto old_color=bg[1];bg[1]=0xff468ace;
    colored.scanline(c,80);colored.complete_frame(c,false);color_view=colored.render(c,native);
    check(color_view.pixels[80*336+300]==bg[1]&&color_view.pixels[79*336+300]==old_color,"CGB margin palette did not follow the native scanline");
    bg[1]=old_color;colored.restore(color_state);
    vram[0x3800]=1;colored.scanline(c,0);check(!colored.render(c,native).expanded,"non-world CGB attributes extended with guessed colors");
    vram[0x3800]=0;rom[0x4104]=0;p=saved;
   }
   // A culled NPC can retain uninitialized $F0 screen bytes. Its source map
   // coordinates must still project to the same complete body across the
   // left native-view seam, rather than disappear or split at x=0.
   p.scx=4;set(0xc114,240);set(0xc116,240);set(0xc214,(anchor_row-3)*2+7);set(0xc215,4);
   field.viewport(1008,432);field.scanline(c,0);view=field.render(c,native);
   check(view.pixels[44*336+86]==obj[1]&&view.pixels[44*336+90]==obj[1],"culled NPC was split at the native seam");
   // RAM can already hold the next pose while the original PPU still draws
   // the uploaded packet. Margin pieces must use that actual packet too.
   std::array<uint8_t,160> oam{};for(unsigned n=0;n<4;++n){oam[n*4]=60;oam[n*4+1]=4;oam[n*4+2]=3;}
   // Keep the padded map coordinates consistent with y=44 on every tested
   // cache row; otherwise this fixture describes a different wrapped row.
   c.oam=oam.data();set(0xc112,0);set(0xc114,44);set(0xc116,254);set(0xd362-offset,4);set(0xd361-offset,(anchor_row-3)*2+4);
   field.scanline(c,0);view=field.render(c,native);check(view.pixels[44*336+84]==obj[1],"margin actor used future RAM instead of published OAM");
   oam.fill(0);set(0xc116,0);field.scanline(c,0);view=field.render(c,native);
   check(view.pixels[44*336+92]==obj[1],"reactivated boundary NPC disappeared before its first OAM upload");
   c.oam=nullptr;set(0xd362-offset,0);
   field.viewport(160,144);view=field.render(c,native);check(view.pixels[44*160+2]==native[44*160+2],"Original160 changed clipped NPC presentation");
   // New rooms initialize FF images before their first uploaded packet too.
   // An interior actor is not a boundary continuation and keeps the PPU image.
   set(0xc112,255);set(0xc215,8);field.viewport(1008,432);field.scanline(c,0);view=field.render(c,native);
   check(view.pixels[44*336+148]==native[44*160+60],"unpublished interior NPC appeared before the native PPU");
   p.scx=0;field.viewport(720,1280);
   set(0xc110,0);set(0xd361-offset,0);
   // Publish one completed source, then change the next drawing source. The
   // displayed frame must not combine its native center with future terrain.
   field.complete_frame(c,false);auto published=field.render(c,native);auto published_pixels=std::vector<uint32_t>(published.pixels.begin(),published.pixels.end());
   set(0xc6e8+(anchor_row-2)*stride+5,0);field.scanline(c,0);view=field.render(c,native);
   check(std::equal(published_pixels.begin(),published_pixels.end(),view.pixels.begin()),"in-progress terrain leaked into a completed frame");
   auto staged=field.snapshot();b.restore(staged);b.viewport(720,1280);view=b.render(c,native);
   check(std::equal(published_pixels.begin(),published_pixels.end(),view.pixels.begin()),"current load lost the completed source frame");
   if(i==2&&dimensions.first==9){
    field.complete_frame(c,true,true);view=field.render(c,native);
    check(view.expanded&&std::equal(published_pixels.begin(),published_pixels.end(),view.pixels.begin()),"SGB MASK1 replaced the published world during a native clear");
    auto frozen=field.snapshot();field.viewport(1280,720);auto held=field.render(c,native);auto held_pixels=std::vector<uint32_t>(held.pixels.begin(),held.pixels.end());
    b.restore(frozen);b.viewport(1280,720);view=b.render(c,native);
    check(view.width==256&&view.expanded&&std::equal(held_pixels.begin(),held_pixels.end(),view.pixels.begin())&&field.snapshot()==frozen,"SGB held world was viewport-dependent or lost on current load");
    field.viewport(720,1280);
   }
   field.complete_frame(c,false);view=field.render(c,native);check(view.pixels[30*160+64]==bg[1],"next completed source did not publish");
   // Restore the existing synthetic fixture's immediate source path.
   NativeMods unlatched(hashes[i],feature,false);field.restore(unlatched.snapshot());field.viewport(720,1280);field.scanline(c,0);
   auto portrait=field.snapshot();field.viewport(1280,720);check(field.snapshot()==portrait,"landscape resize changed canonical Gen1 state");
   field.restore(portrait);view=field.render(c,native);check(view.width==256&&view.height==144,"restore replaced the current host viewport");
   set(0xc6e8+(anchor_row-2)*stride+5,0);field.scanline(c,0);
   word(0xd52c - offset,0x7fe0);std::fill_n(rom.begin()+0xbfe0,16,1);std::fill_n(rom.begin()+0xbff0,16,2);
   field.scanline(c,0);view=field.render(c,native);
   check(view.expanded&&view.pixels[40*256+224]==bg[2],"valid bank-end block table was rejected");
   set(0xc6e8,2);field.scanline(c,0);check(!field.render(c,native).expanded,"out-of-bank map block was accepted");set(0xc6e8,0);
   word(0xd52c - offset,0x4000);
   // The player's unused C2 fields must not create a second player in the
   // margin of a32:9-or-wider view at a large current map coordinate.
   if(i!=2){
    obj[1]=0xffff00ff;set(0xc100,1);set(0xc20e,1);set(0xc104,60);set(0xc106,64);set(0xd362,32);
    rom[0x4000]=0;rom[0x4001]=0x41;rom[0x4002]=0;rom[0x4003]=0x42;
    std::fill_n(rom.begin()+0x4100,4,3);for(unsigned y=0;y<8;++y)vram[48+y*2]=255;
    field.viewport(4096,144);field.scanline(c,0);view=field.render(c,native);
    check(view.pixels[60*4096+1520]!=obj[1],"unused player map fields produced a phantom margin player");
   }
   set(0xd369 - offset,250);field.scanline(c,0);check(!field.render(c,native).expanded,"wrapping eight-bit map stride accepted");
   field.viewport(1008,432);view=field.render(c,native);check(!view.expanded&&std::all_of(view.pixels.begin(),view.pixels.end(),[&](uint32_t p){return p==native.front();}),"uniform native fade exposed an original-aspect rectangle");
  }
 }
 std::cout<<"Gen1 revision/model/ABI and atomic current-state guards passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
