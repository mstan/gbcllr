#include "../src/native_mods.hpp"
#include "../src/gen2_native_mods.hpp"
#include <iostream>
#include <algorithm>
#include <stdexcept>
static void check(bool v,const char* m){if(!v)throw std::runtime_error(m);}
template<class F>static void rejects(F f){bool caught=false;try{f();}catch(const std::exception&){caught=true;}check(caught,"unqualified Gen2 profile/state accepted");}
static void actor_presentation(std::string_view identity,const gbcllr::NativeFeature& feature,bool crystal) {
 using namespace gbcllr;
 // A loaded original-format map with an ordinary NPC deliberately outside
 // the guest's160px activation range. Its mask and resident graphics remain
 // the authority; presenting it must not allocate or modify any guest object.
 std::vector<uint8_t> rom(0x200000),wram(0x8000),vram(0x4000),hram(0x80);
 std::array<uint32_t,32> bg{},obj{};bg.fill(0xff114422);obj.fill(0xff000000);obj[13]=0xffff2200;
 ModPpu ppu{};ppu.lcdc=0x91;ppu.wx=167;ppu.wy=144;ppu.bg_rgb=bg.data();ppu.obj_rgb=obj.data();
 ModContext c{};c.rom=rom.data();c.rom_size=rom.size();c.wram=wram.data();c.vram=vram.data();c.hram=hram.data();c.ppu=&ppu;
 auto put=[&](unsigned p,unsigned v){wram[p-0xc000]=uint8_t(v);};
 auto word=[&](unsigned p,unsigned v){put(p,v);put(p+1,v>>8);};
 auto rput=[&](unsigned bank,unsigned p,unsigned v){rom[bank*0x4000+p-0x4000]=uint8_t(v);};
 auto rword=[&](unsigned bank,unsigned p,unsigned v){rput(bank,p,v);rput(bank,p+1,v>>8);};
 const unsigned map=crystal?0xdcb5:0xda00,env=crystal?0xd19a:0xd083,blocks=crystal?0xc800:0xc700;
 const unsigned anchor=crystal?0xd194:0xd07d,bg_anchor=crystal?0xd152:0xd05b;
 const unsigned objects=crystal?0xd4d6:0xd1fd,map_objects=crystal?0xd71e:0xd445,masks=crystal?0xd81e:0xd545;
 const unsigned used=crystal?0xd154:0xd05d,facings=crystal?0x4049:0x404a,movement=crystal?0x4273:0x4274;
 put(map,26);put(map+1,3);put(env-2,3);put(env-1,1);put(env,1);word(env+1,0x6000);put(env+3,1);put(env+4,9);put(env+5,20);
 word(anchor,blocks+3*26+3);word(bg_anchor,0x9800);put(crystal?0xd1dc:0xd0c5,1);word(crystal?0xd1dd:0xd0c6,0x6000);word(crystal?0xd1e6:0xd0cf,0x7000);
 hram[crystal?0x5e:0x60]=1;
 rword(0x25,(crystal?0x4000:0x40ed)+50,0x5000);unsigned header=0x5000+18;
 rput(0x25,header,3);rput(0x25,header+1,1);rput(0x25,header+2,1);rword(0x25,header+3,0x6000);
 rput(3,0x6000,1);rput(3,0x6001,9);rput(3,0x6002,20);rput(3,0x6003,4);rword(3,0x6004,0x6000);
 rword(1,facings,0x5000);rput(1,0x5000,4);
 for(unsigned n=0;n<4;++n){unsigned p=0x5001+n*4;rput(1,p,n/2*8);rput(1,p+1,n%2*8);rput(1,p+3,n);}
 // Original standing-down movement; same sprite bank selection as the guest.
 rput(1,movement+6*6+1,0);put(map_objects+16,255);put(map_objects+17,47);put(map_objects+18,8);put(map_objects+19,14);put(map_objects+20,6);put(map_objects+24,0x30);
 put(used+2,47);put(used+3,crystal?160:32);
 for(unsigned tile=32;tile<36;++tile)for(unsigned y=0;y<8;++y)vram[tile*16+y*2]=255;
 Gen2NativeMods a(identity,feature,true);std::vector<uint32_t> native(160*144,0xff778899);
 // Connection setup may still be incomplete at line zero. The map is
 // ready by the completed native picture, which must publish a wide field.
 put(env+5,0);auto guest=wram;
 a.scanline(c,0);check(wram==guest,"dormant presentation mutated guest RAM");
 check(!a.render(c,native).expanded,"uncompleted field was published");
 put(env+5,20);
 a.complete_frame(c,false);auto view=a.render(c,native);
 check(view.expanded&&view.sprites==4&&view.pixels[60*256+48+160]==obj[13],"culled original-format NPC was not presented with its resident graphics/palette");
 for(unsigned y=0;y<144;++y)for(unsigned x=0;x<160;++x)check(view.pixels[y*256+x+48]==native[y*160+x],"dormant actor modified native center");
 auto published=a.snapshot();put(masks+1,255);a.scanline(c,0);
 check(a.render(c,native).sprites==4,"future mask changed already published frame");
 auto pending=a.snapshot();a.complete_frame(c,false);check(a.render(c,native).sprites==0,"original event/time/script mask resurrected NPC");
 a.restore(pending);a.complete_frame(c,false);check(a.render(c,native).sprites==0,"pending frame was lost during restore");
 a.restore(published);check(a.render(c,native).sprites==4,"completed actor frame was lost during restore");
 put(masks+1,0);put(map_objects+24,0);rput(5,(crystal?0x4736:0x47de)+46*6+5,3);
 a.scanline(c,0);a.complete_frame(c,false);check(a.render(c,native).pixels[60*256+48+160]==obj[13],"original sprite-header default palette differs");
 put(masks+1,0);put(used+2,0);a.scanline(c,0);a.complete_frame(c,false);check(a.render(c,native).sprites==0,"missing resident graphics borrowed a different actor");
 put(used+2,47);put(map_objects+16,1);put(objects+40,47);put(objects+44,1);a.scanline(c,0);a.complete_frame(c,false);
 check(a.render(c,native).sprites==0,"invisible live object was resurrected as dormant NPC");
 put(objects+44,0);put(objects+45,64);put(objects+46,3);put(objects+42,crystal?160:32);put(objects+53,255);put(objects+56,14);put(objects+57,8);put(objects+63,160);put(objects+64,64);
 a.scanline(c,0);a.complete_frame(c,false);check(a.render(c,native).sprites==4,"native offscreen-facing suppression removed retained actor");
 // Native wander/script displacement must not snap back to the spawn tile
 // when native DeleteMapObject drops the live slot at the160px boundary.
 put(objects+41,1);put(objects+45,0);put(objects+53,0);put(objects+63,176);
 a.scanline(c,0);a.complete_frame(c,false);check(a.render(c,native).pixels[60*256+48+176]==obj[13],"displaced native pose missing");
 // Guest RAM already describes the next walk phase; physical OAM still
 // publishes the preceding standing packet two pixels earlier.
 std::array<uint8_t,160> oam{};c.oam=oam.data();
 for(unsigned n=0;n<4;++n){oam[n*4]=uint8_t(76+n/2*8);oam[n*4+1]=uint8_t(184+n%2*8);oam[n*4+2]=uint8_t(32+n);oam[n*4+3]=3;}
 rword(1,facings+2,0x5011);rput(1,0x5011,4);
 for(unsigned n=0;n<4;++n){unsigned p=0x5012+n*4;rput(1,p,n/2*8);rput(1,p+1,n%2*8);rput(1,p+3,128+n);}
 put(objects+53,1);put(objects+63,178);a.scanline(c,0);a.complete_frame(c,false);
 view=a.render(c,native);check(view.pixels[60*256+48+176]==obj[13]&&view.pixels[60*256+48+192]==bg[0],"actor used future shadow position/facing instead of published OAM");
 put(objects+40,0);put(map_objects+16,255);a.scanline(c,0);a.complete_frame(c,false);
 view=a.render(c,native);check(view.pixels[60*256+48+176]==obj[13]&&view.pixels[60*256+48+160]==bg[0],"native culling snapped wandering actor to spawn");
 // The native scroll byte wraps through128 while an actor crosses the
 // left edge. Its map cell disambiguates the same eight-bit OAM position.
 {
  const auto original_wram=wram;const auto original_oam=oam;const auto original_ppu=ppu;
  put(objects+40,47);put(objects+41,1);put(objects+53,0);put(objects+45,0);
  put(objects+56,14);put(objects+57,8);put(objects+63,176);put(objects+64,64);
  put(map_objects+16,1);put(map_objects+19,14);put(masks+1,0);
  word(anchor,blocks+3*26+7);word(bg_anchor,0x9810);put(crystal?0xd14c:0xce81,72);ppu.scx=184;
  for(unsigned n=0;n<4;++n){oam[n*4]=uint8_t(76+n/2*8);oam[n*4+1]=uint8_t(n%2*8);oam[n*4+2]=uint8_t(32+n);oam[n*4+3]=3;}
  Gen2NativeMods wrapped(identity,feature,true);wrapped.scanline(c,0);wrapped.complete_frame(c,false);
  auto crossing=wrapped.render(c,native);
  check(crossing.expanded&&crossing.pixels[60*256+40]==obj[13],"native coordinate wrap moved margin actor to another world tile");
  auto current=wrapped.snapshot();wrapped.restore(current);
  check(wrapped.render(c,native).pixels[60*256+40]==obj[13]&&wrapped.snapshot()==current,"wrapped margin actor did not retain current-state pose");
  wram=original_wram;oam=original_oam;ppu=original_ppu;
 }
 put(objects+40,47);put(objects+53,255);put(map_objects+16,1);oam.fill(0);
 a.scanline(c,0);a.complete_frame(c,false);check(a.render(c,native).pixels[60*256+48+176]==obj[13],"native reactivation dropped retained body before first published packet");
 put(objects+40,0);put(map_objects+16,255);
 put(map_objects+19,16);a.scanline(c,0);a.complete_frame(c,false);check(a.render(c,native).pixels[60*256+48+192]==obj[13],"changed original object identity retained stale pose");
 put(masks+1,255);a.scanline(c,0);a.complete_frame(c,false);check(a.render(c,native).sprites==0,"mask failed to invalidate retained pose");
 // A current outdoor map is a cartridge capability, not a map-ID whitelist.
 // Its authored header and live attributes still have to agree exactly.
 rword(0x25,(crystal?0x4000:0x40ed)+48,0x5200);
 for(unsigned n=0;n<9;++n)rput(0x25,0x5200+27+n,rom[0x25*0x4000+header+n-0x4000]);
 put(map,25);put(map+1,4);a.scanline(c,0);a.complete_frame(c,false);
 check(a.render(c,native).expanded,"authored outdoor map rejected by identity whitelist");
 auto generic_map=a.snapshot();a.restore(generic_map);
 check(a.render(c,native).expanded&&a.snapshot()==generic_map,"authored outdoor map restore differs");
 put(env-1,2);a.scanline(c,0);a.complete_frame(c,false);
 check(!a.render(c,native).expanded,"outdoor map with mismatched resident tileset admitted");
 put(env-1,1);put(map,26);put(map+1,3);a.scanline(c,0);a.complete_frame(c,false);
 if(crystal) {
  const auto field_vram=vram;
  auto box=[&](unsigned top,unsigned bottom) {
   for(unsigned y=top;y<=bottom;++y)for(unsigned x=0;x<20;++x)
    vram[0x1800+y*32+x]=uint8_t(y==top?(x==0?0x79:x==19?0x7b:0x7a):
     y==bottom?(x==0?0x7d:x==19?0x7e:x==18?0xee:0x7a):(x==0||x==19?0x7c:0x7f));
  };
  box(0,3);box(12,17);
  for(unsigned x=1;x<19;++x)vram[0x1800+32+x]=0x80; // A full line has no blank tile.
  hram[0x5e]=0; // The native call pauses map animations.
  for(unsigned y=0;y<144;++y)for(unsigned x=0;x<160;++x)native[y*160+x]=0xff000000u|(y<<12)|x;
  const auto guest_before=wram;
  a.scanline(c,0);a.complete_frame(c,false);const auto dialog=a.snapshot();
  for(unsigned width:{192u,256u,320u}) {
   auto phone=a.render_host(c,native,width,144);
   check(phone.expanded&&phone.width==width,"native phone collapsed adaptive field");
   check(phone.pixels[8*width]==native[8*160]&&phone.pixels[8*width+width-1]==native[8*160+159],"phone borders did not move to canvas edges");
   check(phone.pixels[8*width+8]==native[8*160+8],"phone content moved independently of its left border");
   check(phone.pixels[8*width+152]==native[16*160+8],"full phone line repeated text into the extra canvas");
   check(phone.pixels[136*width+width-16]==native[136*160+144],"phone continuation glyph did not follow right border");
   check(phone.pixels[64*width+(width-160)/2+80]==native[64*160+80],"phone changed authentic field/actor center");
   check(a.snapshot()==dialog&&wram==guest_before,"phone resize/render mutated canonical state or guest RAM");
   const std::vector<uint32_t> expected(phone.pixels.begin(),phone.pixels.end());
   a.restore(dialog);auto restored=a.render_host(c,native,width,144);
   check(std::equal(restored.pixels.begin(),restored.pixels.end(),expected.begin(),expected.end()),"phone current-state restore changed presentation");
  }
  vram[0x1800+6*32]=1;a.scanline(c,0);a.complete_frame(c,false);
  check(!a.render(c,native).expanded,"phone admitted an unqualified uncovered field tile");
  vram[0x1800+6*32]=0;vram[0x1800+12*32]=0;a.scanline(c,0);a.complete_frame(c,false);
  check(!a.render(c,native).expanded,"partial phone frame bypassed field qualification");
  vram=field_vram;hram[0x5e]=1;a.scanline(c,0);a.complete_frame(c,false);
  check(a.render(c,native).expanded,"ending phone did not return to the adaptive field");
  auto rectangle=[&](unsigned left,unsigned top,unsigned right,unsigned bottom) {
   for(unsigned y=top;y<=bottom;++y)for(unsigned x=left;x<=right;++x)
    vram[0x1800+y*32+x]=uint8_t(y==top?(x==left?0x79:x==right?0x7b:0x7a):
     y==bottom?(x==left?0x7d:x==right?0x7e:0x7a):(x==left||x==right?0x7c:0x7f));
  };
  rectangle(14,7,19,11);box(12,17);
  a.scanline(c,0);a.complete_frame(c,false);
  auto choice=a.snapshot();
  for(unsigned width:{192u,256u,320u}) {
   auto view=a.render_host(c,native,width,144);unsigned center=(width-160)/2;
   check(view.expanded,"field choice collapsed adaptive canvas");
   for(unsigned y=56;y<96;++y)for(unsigned x=112;x<160;++x)
    check(view.pixels[y*width+center+x]==native[y*160+x],"choice pixels changed in native center");
   check(a.snapshot()==choice&&wram==guest_before,"choice render mutated canonical state");
  }
  vram=field_vram;rectangle(10,0,19,15);
  for(unsigned y=13;y<18;++y)for(unsigned x=0;x<10;++x)vram[0x1800+y*32+x]=0x80;
  a.scanline(c,0);a.complete_frame(c,false);auto menu=a.snapshot();
  for(unsigned width:{192u,256u,320u}) {
   auto view=a.render_host(c,native,width,144);unsigned center=(width-160)/2;
   check(view.expanded,"field menu or its description collapsed adaptive canvas");
   for(unsigned y=0;y<144;++y)for(unsigned x=0;x<160;++x)
    check(view.pixels[y*width+center+x]==native[y*160+x],"field menu changed native UI pixels");
   check(a.snapshot()==menu&&wram==guest_before,"menu resize/render mutated canonical state");
  }
  vram[0x1800+19]=0;a.scanline(c,0);a.complete_frame(c,false);
  check(!a.render(c,native).expanded,"partial field menu admitted arbitrary BG changes");
  vram=field_vram;a.scanline(c,0);a.complete_frame(c,false);
  const auto field_ppu=ppu;
  for(unsigned y=0;y<18;++y)for(unsigned x=0;x<20;++x) {
   vram[0x1c00+y*32+x]=field_vram[0x1800+y*32+x];vram[0x1800+y*32+x]=0x60;
  }
  ppu.lcdc=0xe3;ppu.wx=7;ppu.wy=0;ppu.scx=48;ppu.scy=160;
  a.scanline(c,0);a.complete_frame(c,false);auto window=a.snapshot();
  auto window_view=a.render(c,native);
  check(window_view.expanded&&window_view.camera_x==0&&window_view.camera_y==0,"native Window field used the unrelated BG scroll");
  for(unsigned y=0;y<144;++y)for(unsigned x=0;x<160;++x)
   check(window_view.pixels[y*256+48+x]==native[y*160+x],"Window field changed authoritative native center");
  vram[0x1c00]=1;a.scanline(c,0);a.complete_frame(c,false);
  check(!a.render(c,native).expanded,"unrelated full-screen Window admitted as field");
  a.restore(window);check(a.render(c,native).expanded&&a.snapshot()==window,"native Window field restore differs");
  ppu=field_ppu;
  vram=field_vram;a.scanline(c,0);a.complete_frame(c,false);
  // The original route-entry sign is a distinct four-row Window, not a
  // menu. Its authored border/blank tiles qualify only that native overlay.
  auto banner_vram=vram;auto banner_ppu=ppu;
  constexpr unsigned left[]{0x61,0x65,0x66,0x67},right[]{0x64,0x6b,0x6c,0x6a};
  for(unsigned y=0;y<4;++y)for(unsigned x=0;x<20;++x) {
   vram[0x1c00+y*32+x]=uint8_t(!x?left[y]:x==19?right[y]:y==0?0x62+((x-1)%4<2):y==3?0x68+((x-1)%4<2):0x6d);
   vram[0x3c00+y*32+x]=0x87;
  }
  put(0xc2da,60);ppu.lcdc=0xe3;ppu.wx=7;ppu.wy=112;
  Gen2NativeMods sign(identity,feature,true);sign.scanline(c,0);sign.complete_frame(c,false);
  const auto sign_state=sign.snapshot();
  for(unsigned width:{192u,256u,320u}) {
   auto view=sign.render_host(c,native,width,144);unsigned center=(width-160)/2;
   check(view.expanded&&view.pixels[112*width]==native[112*160]&&view.pixels[112*width+width-1]==native[112*160+159],"route sign collapsed the field or retained native side rails");
   check(view.pixels[128*width+center+16]==native[128*160+16],"route sign moved native landmark text");
   check(view.pixels[64*width+center+80]==native[64*160+80]&&sign.snapshot()==sign_state,"route sign modified field center or canonical state");
   sign.restore(sign_state);
  }
  put(0xc2da,0);sign.scanline(c,0);sign.complete_frame(c,false);
  check(sign.render(c,native).expanded,"expired timer hid a still-published native route sign");
  put(0xc2da,60);vram[0x1c00]=0;sign.scanline(c,0);sign.complete_frame(c,false);
  check(!sign.render(c,native).expanded,"partial unrelated Window admitted as native landmark sign");
  put(0xc2da,0);vram=banner_vram;ppu=banner_ppu;
  // A real connection can publish its new map identity before replacing
  // the resident block buffer. Only its already-qualified neighbor and an
  // exact unchanged physical tilemap may bridge that completed picture.
  put(map,26);put(map+1,3);word(env+1,0x6000);put(env+4,9);put(env+5,20);
  word(anchor,blocks+3*26+3);put(0xd196,0);put(0xd197,0);ppu.scx=ppu.scy=0;
  std::fill_n(wram.begin()+blocks-0xc000,1300,0);
  rput(3,0x600b,8);put(env+14,8);
  for(unsigned i=0;i<12;++i){rput(3,0x600c+i,0);put(env+15+i,0);}
  rput(3,0x600c,26);rput(3,0x600d,1);rput(3,0x6013,10);
  put(env+15,26);put(env+16,1);put(env+22,10);
  rput(0x25,0x5000,3);rput(0x25,0x5001,1);rput(0x25,0x5002,1);rword(0x25,0x5003,0x6100);
  rput(3,0x6100,1);rput(3,0x6101,27);rput(3,0x6102,10);rput(3,0x6103,4);rword(3,0x6104,0x6100);
  Gen2NativeMods crossing(identity,feature,true);crossing.scanline(c,0);crossing.complete_frame(c,false);
  check(crossing.render(c,native).expanded,"connection source was not qualified");
  put(map+1,1);word(env+1,0x6100);put(env+4,27);put(env+5,10);word(anchor,blocks+30*16+3);
  std::fill_n(wram.begin()+blocks-0xc000,1300,2);
  for(unsigned i=0;i<16;++i)rput(1,0x6020+i,1);
  crossing.scanline(c,0);crossing.complete_frame(c,false);
  check(crossing.render(c,native).expanded,"unchanged connected picture lost its qualified source");
  const auto handoff=crossing.snapshot();crossing.restore(handoff);
  check(crossing.render(c,native).expanded&&crossing.snapshot()==handoff,"connection handoff was not current-state exact");
  vram[0x1800]=1;crossing.scanline(c,0);crossing.complete_frame(c,false);
  check(!crossing.render(c,native).expanded,"connection handoff admitted changed physical graphics");
 }
 a.complete_frame(c,true);check(!a.render(c,native).expanded,"blank native frame retained widened field");
}
int main(int argc,char** argv){try{
 using namespace gbcllr;check(argc==4,"three Gen2 manifests required");
 const char* hashes[]={"fb0016d27b1e5374e1ec9fcad60e6628d8646103b5313ca683417f52b97e7e4e","72b190859a59623cbef6c49d601f8de52c1d2331b4f08a8d2acc17274fc19a8c","fdcc3c8c43813cf8731fc037d2a6d191bac75439c34b24ba1c27526e6acdc8a2"};
 check(Gen2NativeMods::linked_pair(hashes[0],hashes[1])&&Gen2NativeMods::linked_pair(hashes[1],hashes[0]),"qualified linked cartridge order rejected");
 check(!Gen2NativeMods::linked_pair(hashes[0],hashes[0])&&!Gen2NativeMods::linked_pair(hashes[0],hashes[2]),"unevaluated cartridge pair admitted");
 for(int i=0;i<3;++i){auto package=read_mod_package(argv[i+1],hashes[i]);
  actor_presentation(hashes[i],package.feature,i==2);
  check(package.default_enabled&&package.feature.view_width==-1,"Gen2 fresh Fit default differs");
  std::array feature{package.feature};
  check(Gen2NativeMods::supports(hashes[i])&&Gen2NativeMods::linked_profile(feature,true),"qualified Gen2 linked feature rejected");
  check(!Gen2NativeMods::linked_profile(feature,false),"DMG admitted to CGB linked profile");
  auto fixed=feature;fixed[0].view_width=336;check(!Gen2NativeMods::linked_profile(fixed,true),"host aspect entered fixed guest profile");
  auto gifted=feature;gifted[0].gift=35;
  check(!Gen2NativeMods::linked_profile(gifted,true),"unqualified gift entered linked profile");
  NativeMods gift_a(hashes[i],gifted,true),gift_b(hashes[i],gifted,true);
  const auto gift_state=gift_a.snapshot();gift_b.restore(gift_state);check(gift_b.snapshot()==gift_state,"pending gift current state differs");
  auto invalid_gift=gift_state;invalid_gift.back()=2;rejects([&]{gift_a.restore(invalid_gift);});check(gift_a.snapshot()==gift_state,"invalid gift restore mutated pending input");
  auto another_gift=gifted;another_gift[0].gift=38;NativeMods different_gift(hashes[i],another_gift,true);rejects([&]{different_gift.restore(gift_state);});
  auto excessive=gifted;excessive[0].gift=75;rejects([&]{NativeMods wrong(hashes[i],excessive,true);});
  rejects([&]{read_mod_package(argv[i+1],hashes[(i+1)%3]);});
  rejects([&]{NativeMods wrong(std::string(64,'0'),feature,true);});
  rejects([&]{NativeMods wrong(hashes[i],feature,false);});
  auto bad=feature;bad[0].abi=2;rejects([&]{NativeMods wrong(hashes[i],bad,true);});
  NativeMods a(hashes[i],feature,true),b(hashes[i],feature,true),other(hashes[(i+1)%3],feature,true);
  check(!a.observes_scanlines(),"Gen2 field unnecessarily observes every scanline");a.viewport(1280,720);
  const auto before=a.snapshot();b.restore(before);check(b.snapshot()==before,"Gen2 current state differs");
  ModContext context{};std::vector<uint32_t> native(160*144);
  for(size_t p=0;p<native.size();++p)native[p]=0xff000000u|uint32_t(p);
  for(unsigned width:{160u,256u,336u,512u,4096u}) {
   auto view=a.render_host_gen2(context,native,width,144);
   check(view.width==width&&view.height==144,"host Gen2 output width differs");
   for(unsigned y=0;y<144;++y)for(unsigned x=0;x<160;++x)
    check(view.pixels[size_t(y)*width+(width-160)/2+x]==native[y*160+x],"host Gen2 view changed original center");
   check(a.snapshot()==before,"host Gen2 output dimensions entered canonical module state");
  }
  for(auto size:{std::array<unsigned,2>{720,1280},std::array<unsigned,2>{400,1200}}) {
   auto view=a.render_host_gen2(context,native,size[0],size[1]);
   auto expected=(uint64_t(size[1])*160+size[0]/2)/size[0];
   check(view.width==160&&view.height==expected,"portrait Gen2 logical shape differs");
   for(unsigned y=0;y<144;++y)for(unsigned x=0;x<160;++x)
    check(view.pixels[size_t(y+(view.height-144)/2)*160+x]==native[y*160+x],"portrait changed authentic center");
   check(a.snapshot()==before,"portrait geometry entered canonical module state");
   a.viewport(size[0],size[1]);check(a.snapshot()==before,"standalone viewport entered canonical module state");
   NativeMods restored(hashes[i],feature,true);restored.restore(before);restored.preserve_host_presentation(a);
   auto after=restored.render(context,native);
   check(after.width==view.width&&after.height==view.height,"Gen2 restore reset current portrait host geometry");
   check(restored.snapshot()==before,"preserving portrait host geometry changed canonical state");
  }
  a.viewport(1008,432);NativeMods landscape(hashes[i],feature,true);landscape.restore(before);landscape.preserve_host_presentation(a);
  check(landscape.render(context,native).width==336&&landscape.snapshot()==before,"Gen2 restore reset current landscape geometry or changed canonical state");
  rejects([&]{other.restore(before);});
  auto corrupt=before;corrupt.pop_back();rejects([&]{a.restore(corrupt);});
  corrupt=before;corrupt[12]=0;corrupt[13]=0;rejects([&]{a.restore(corrupt);});
  corrupt=before;corrupt[16]=2;rejects([&]{a.restore(corrupt);});
  check(a.snapshot()==before,"invalid Gen2 load mutated live state");
 }
 std::cout<<"Gen2 revision/model/ABI and atomic current-state guards passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
