#include "../src/native_mods.hpp"
#include <gbcllr/machine.hpp>
#include <algorithm>
#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>

static void check(bool pass,const char* message){if(!pass)throw std::runtime_error(message);}
template<class F> void rejects(F f,const char* message){bool rejected=false;try{f();}catch(const std::exception&){rejected=true;}check(rejected,message);}
int main(int argc,char**argv){try{
 using namespace gbcllr;
 const std::string sha="e37a14a8e61bd64f2226dacc26a7ee75dd90abfcf88c5016eb97b76ecda0ddca";
 check(argc==2,"package fixture required");auto package=read_mod_package(argv[1],sha);
 check(package.default_enabled&&package.feature.view_width==-1,"fresh package Fit/default contract");
 rejects([&]{read_mod_package(argv[1],std::string(64,'0'));},"wrong ROM package accepted");
 std::array<NativeFeature,1> features{package.feature};
 rejects([&]{NativeMods bad(std::string(64,'0'),features);},"wrong native adapter target accepted");
 auto unknown=features;unknown[0].plugin="unknown.native";
 rejects([&]{NativeMods bad(sha,unknown);},"unknown native implementation accepted");
 NativeMods a(sha,features),b(sha,features);
 check(!a.observes_scanlines()&&!NativeMods(sha,{}).observes_scanlines(),"unrequested native raster observation enabled");
 std::vector<uint8_t> rom(1<<20),wram(0x8000),vram(0x4000),eram(0x2000);
 std::array<uint8_t,160> oam{};std::array<uint8_t,127> hram{};
 std::array<uint8_t,64> colors{};std::array<uint32_t,32> rgb{};rgb[1]=0xff123456;
 ModPpu ppu{0,0,0x91,7,144,colors.data(),colors.data(),rgb.data(),rgb.data()};
 ModContext c{rom.data(),wram.data(),vram.data(),eram.data(),oam.data(),hram.data(),rom.size(),eram.size(),0,0,1,0,0,0,0,1,&ppu};
 c.sp=0xfffe;
 // Actual early-boot captures can observe CABA before the game clears RAM.
 // Such a frame is invalid for rendering, but must still restore exactly.
 wram[0xaba]=255;a.scanline(c,0);auto boot=a.snapshot();a.restore(boot);
 check(a.snapshot()==boot,"early-boot invalid map bank did not round-trip");
 auto invalid_boot=boot;int valid_map=1;std::memcpy(invalid_boot.data()+16,&valid_map,sizeof valid_map);
 rejects([&]{a.restore(invalid_boot);},"out-of-range bank admitted as a valid map");
 check(a.snapshot()==boot,"rejected map state changed the live view");
 // A synthetic MMX2 map page proves the compositor uses world data beyond
 // the hardware's 160-pixel screen and uses the hardware's encoded palettes.
 wram[0xaba]=1;wram[0xa41]=4;
 std::fill_n(wram.data()+0x5a00,256,0x50);
 for(unsigned i=0;i<4;++i)rom[0x4000+i*0x100]=1;
 std::fill_n(vram.data()+0x1800,1024,1);
 for(unsigned y=0;y<8;++y)vram[16+y*2]=0xff;
 std::array<uint32_t,160*144> native{};native.fill(rgb[1]);
 a.viewport(1280,720);a.scanline(c,0);auto before=a.snapshot();auto wide=a.render(c,native);
 check(wide.expanded&&wide.width==256&&wide.pixels.size()==256*144,"world compositor did not expand");
 check(std::all_of(wide.pixels.begin(),wide.pixels.end(),[&](auto pixel){return pixel==rgb[1];}),"world tiles/palette differ outside native frame");
 check(a.snapshot()==before,"presentation render changed simulation state");
 // VBlank publishes one coherent camera/map/graphics image. Line zero of
 // the next frame and live map writes must not move only the margins.
 NativeMods published(sha,features);published.viewport(1280,720);
 check(published.completes_frames(),"wide view lacks completed-frame publication");
 published.scanline(c,0);published.complete_frame(c,false,false);
 auto completed=published.snapshot();
 wram[0x5a00]=0;
 check(published.render(c,native).pixels.front()==rgb[1],"presented terrain used live map pages");
 wram[0xa08]=16;ppu.scx=16;published.scanline(c,0);
 auto held=published.render(c,native);
 check(held.expanded&&held.camera_x==0&&held.pixels.front()==rgb[1],"next line zero replaced the completed frame");
 published.complete_frame(c,false,false);
 check(!published.render(c,native).expanded,"invalid newly completed scene retained an old world");
 published.restore(completed);
 check(published.render(c,native).expanded&&published.snapshot()==completed,"completed frame did not survive current-state restore");
 wram[0x5a00]=0x50;wram[0xa08]=0;ppu.scx=0;
 // Keep the meters, lives count and character icon in all twelve left HUD
 // tiles together; only the independent boss group belongs on the right.
 NativeMods hud(sha,features);hud.viewport(512,144);ppu.lcdc|=32;ppu.wy=128;
 auto hud_native=native;
 for(unsigned y=128;y<144;++y)for(unsigned x=0;x<160;++x)
  hud_native[y*160+x]=x>=16&&x<80?0xffff8800u:x>=80&&x<96?0xff4488ffu:x>=112&&x<144?0xff44dd88u:0xff000000u;
 for(unsigned y=128;y<144;++y)hud_native[y*160+159]=0xff0088ffu;
 hud.scanline(c,0);auto hud_view=hud.render(c,hud_native);
 for(unsigned y=128;y<144;++y)for(unsigned x=0;x<160;++x)
  check(hud_view.pixels[y*512+(x<96?x:512-160+x)]==hud_native[y*160+x],"wide HUD split a native meter, lives icon or boss group");
 check(hud_view.pixels[128*512+128]==0xff000000u,"wide HUD extended the boss meter into its blank separator");
 ppu.lcdc&=uint8_t(~32);ppu.wy=144;
 // A mounted Ride Chaser locks the camera, while the loaded room still
 // contains terrain on both sides of that camera.
 auto set_word=[&](unsigned address,unsigned value){wram[address-0xc000]=uint8_t(value);wram[address-0xbfff]=uint8_t(value>>8);};
 // A boss-room camera can lock to exactly160 pixels while authored terrain
 // continues on either side. Preserve the original PPU band and extend only
 // valid map pages, including margin sprites beyond those camera locks.
 NativeMods locked(sha,features);locked.viewport(256,144);
 set_word(0xca08,256);set_word(0xca0d,256);set_word(0xca40,256);ppu.scx=0;
 rgb[2]=0xffaabbcc;for(unsigned y=0;y<8;++y)vram[33+y*2]=0xff;
 oam[0]=32;oam[1]=0;oam[2]=2;
 locked.scanline(c,0);auto locked_state=locked.snapshot();
 auto locked_native=native;locked_native[0]=0xffabcdefu;
 auto locked_view=locked.render(c,locked_native);
 check(locked_view.expanded&&locked_view.bound_right-locked_view.bound_left==160,"locked-room fixture lost its native camera rails");
 check(locked_view.pixels.front()==rgb[1]&&locked_view.pixels[255]==rgb[1],"native camera lock clipped authored margin terrain");
 check(locked_view.pixels[16*256+40]==rgb[2],"native camera lock clipped an authored margin sprite");
 check(locked_view.pixels[48]==locked_native[0]&&locked.snapshot()==locked_state,"locked-room margins changed native pixels or simulation state");
 // An unmapped page must remain black rather than sampling stale map data.
 wram[0x5a00]=0;locked.scanline(c,0);
 auto unmapped=locked.render(c,locked_native);
 check(unmapped.expanded&&unmapped.pixels.front()==0xff000000u&&unmapped.pixels[16*256+40]==0xff000000u,"unmapped margin rendered stale terrain or sprites");
 oam.fill(0);
 // The intro's adjoining map contains a reciprocal entrance. Suppress only
 // that entrance; terrain still follows the actual world map on either side
 // of the native viewport, including changes in ground/ceiling height.
 auto intro_rom=16*0x4000;
 for(unsigned q=0;q<4;++q){rom[intro_rom+q*256]=1;rom[intro_rom+q*256+1]=2;}
 std::fill_n(rom.data()+intro_rom+0x1100,256,1);
 for(unsigned y=0;y<16;++y)for(unsigned x=10;x<16;++x)rom[intro_rom+0x1000+y*16+x]=1;
 rgb[3]=0xffcc44ff;for(unsigned y=0;y<8;++y){vram[48+y*2]=0xff;vram[49+y*2]=0xff;}
 for(auto id:{0xa7,0xc5,0xb7,0xd5})for(unsigned q=0;q<4;++q)rom[intro_rom+q*256+id]=3;
 rom[intro_rom+0x1000+2*16+9]=0xa7;rom[intro_rom+0x1000+2*16+10]=0xc5;
 rom[intro_rom+0x1000+3*16+9]=0xb7;rom[intro_rom+0x1000+3*16+10]=0xd5;
 wram[0xaba]=16;wram[0x66]=3;wram[0x5a00]=0x51;
 ppu.wy=128;ppu.lcdc|=32;
 oam[0]=32;oam[1]=0;oam[2]=2;
 NativeMods closed_room(sha,features);closed_room.viewport(320,144);closed_room.scanline(c,0);
 auto closed_native=locked_native;
 for(unsigned y=32;y<64;++y)for(unsigned x=152;x<160;++x)closed_native[y*160+x]=rgb[3];
 auto closed_state=closed_room.snapshot();auto closed_view=closed_room.render(c,closed_native);
 check(closed_view.pixels.front()==rgb[2]&&closed_view.pixels[319]==rgb[2],"closed room replaced authored margin terrain with a repeated interior column");
 check(closed_view.pixels[16*320+72]==rgb[2],"closed room projected an actor through its boundary");
 check(std::count(closed_view.pixels.begin()+32*320,closed_view.pixels.begin()+33*320,rgb[3])==8,"closed room lost its native complete door or projected its reciprocal entrance");
 check(closed_view.pixels[80]==locked_native[0]&&closed_room.snapshot()==closed_state,"closed room projection changed the native picture or simulation");
 // The door scroll moves one rail before the other; keep excluding its
 // reciprocal entrance throughout that temporary wider interval.
 set_word(0xca40,288);closed_room.scanline(c,0);
 auto entering=closed_room.render(c,closed_native);
 check(entering.pixels[16*320+72]==rgb[1],"door scroll exposed a margin actor when camera rails temporarily separated");
 check(entering.pixels[32*320+303]==closed_view.pixels[32*320+319],"door scroll changed stationary decorative terrain with the moving camera rail");
 set_word(0xca40,256);
 // A reciprocal entrance remains in the arriving room's first map column.
 // It belongs only to the authoritative native band, never repeated left.
 for(unsigned y=2;y<4;++y)rom[intro_rom+0x1000+y*16]=0xa7;
 closed_room.scanline(c,0);auto reciprocal=closed_room.render(c,closed_native);
 check(std::count(reciprocal.pixels.begin()+32*320,reciprocal.pixels.begin()+33*320,rgb[3])==8,"incoming door was repeated across the decorative left margin");
 wram[0xaba]=1;wram[0x66]=0;oam.fill(0);ppu.wy=144;ppu.lcdc&=uint8_t(~32);
 // A locked boss shifts its native background/camera registers without
 // moving the authored arena. Neighboring pages belong to another room.
 wram[0x5a00]=0x51;std::fill_n(rom.data()+0x5100,256,1);
 for(unsigned i=0;i<4;++i)rom[0x4001+i*0x100]=2;
 for(unsigned y=0;y<32;++y)for(unsigned x=0;x<32;++x)vram[0x1800+y*32+x]=uint8_t(x<4||x>=30?2:1);
 std::fill_n(vram.data()+0x1c00,1024,1);
 wram[0x9a1]=0xb8;set_word(0xca08,261);ppu.scx=5;ppu.wy=112;ppu.lcdc|=32|64;
 NativeMods arena(sha,features);arena.viewport(256,144);arena.scanline(c,0);
 auto arena_native=hud_native;arena_native[0]=0xffabcdefu;
 auto arena_state=arena.snapshot();auto arena_view=arena.render(c,arena_native);
 check(arena_view.expanded&&arena_view.pixels[48]==arena_native[0],"boss background camera displaced the native arena");
 check(arena_view.pixels.front()==rgb[1]&&arena_view.pixels[255]==rgb[1]&&arena_view.pixels[112*256]==rgb[1],"boss margins lost the authored BG/window planes");
 for(unsigned y=128;y<144;++y)for(unsigned x=0;x<160;++x)
  check(arena_view.pixels[y*256+(x<96?x:256-160+x)]==arena_native[y*160+x],"HBlank HUD was treated as scrolling terrain");
 check(arena.snapshot()==arena_state,"boss arena presentation changed simulation state");
 // An OAM particle can cross the native edge while the boss uses a separate
 // BG camera. It still belongs to the expanded arena's presentation.
 oam[0]=32;oam[1]=0;oam[2]=2;arena.scanline(c,0);
 auto particle=arena.render(c,arena_native);
 check(particle.pixels[16*256+40]==rgb[2],"arena clipped a native particle at the original viewport edge");
 oam.fill(0);arena.scanline(c,0);
 // The arriving room retains its resident BG while a native dialogue Window
 // wipes from WX39 to WX7. It must not fall back to the preceding map or
 // stretch dialogue pixels over the BG/native pixels outside that Window.
 {
  auto saved_wram=wram,saved_vram=vram;auto saved_ppu=ppu;auto saved_color=rgb[3];
  wram[0xaba]=16;wram[0x66]=3;wram[0x94d]=1;wram[0x94f]=0;wram[0x9b0]=0;wram[0xc2]=1;
  set_word(0xca08,256);ppu.scx=0;ppu.wx=39;rgb[3]=0xff778899u;
  std::fill_n(vram.data()+0x1800,1024,1);vram[0x1800+4*32+21]=3;
  for(unsigned y=0;y<8;++y){vram[48+y*2]=0xff;vram[49+y*2]=0xff;}
  auto dialogue_native=arena_native;dialogue_native[120*160+159]=0xffaa00ffu;
  NativeMods arrival(sha,features);arrival.viewport(256,144);arrival.scanline(c,0);
  auto arrival_state=arrival.snapshot();auto incoming=arrival.render(c,dialogue_native);
  check(incoming.expanded&&incoming.pixels[32*256+216]==rgb[3],"dialogue Window changed incoming room BG ownership to the preceding map");
  check(incoming.pixels[120*256]==dialogue_native[120*160+159]&&incoming.pixels[120*256+48]==dialogue_native[120*160],"dialogue Window lost its screen-space background or native portrait pixels");
  check(arrival.snapshot()==arrival_state,"incoming room presentation changed state");
  std::copy(saved_wram.begin(),saved_wram.end(),wram.begin());std::copy(saved_vram.begin(),saved_vram.end(),vram.begin());ppu=saved_ppu;rgb[3]=saved_color;
 }
 arena.viewport(512,144);auto large_arena=arena.render(c,arena_native);
 check(large_arena.pixels.front()==rgb[1]&&large_arena.pixels[511]==rgb[1]&&large_arena.pixels[176]==arena_native[0],"wide arena wrapped/repeated its finite boss artwork or displaced native pixels");
 set_word(0xca08,289);ppu.scx=33;arena.viewport(256,144);arena.scanline(c,0);
 check(arena.render(c,arena_native).pixels[255]==rgb[1],"arena exposed the unpainted final BG columns as a colored bar");
 set_word(0xca08,335);ppu.scx=79;arena.scanline(c,0);
 auto left_edge=arena.render(c,arena_native);
 check(left_edge.pixels.front()==rgb[1]&&left_edge.pixels[48]==arena_native[0],"arena exposed unpainted leading BG columns or changed native pixels");
 // During boss spawn the new clear row reaches the edges before the lower
 // rows. A complete top row must not admit the previous room's lower tiles.
 std::fill_n(vram.data()+0x1800+((ppu.scy>>3)&31)*32,32,1);
 set_word(0xca08,304);ppu.scx=48;arena.scanline(c,0);
 auto spawning=arena.render(c,arena_native);
 check(spawning.pixels[80*256]==rgb[1]&&spawning.pixels[80*256+255]==rgb[1],"arena spawn exposed stale lower-row tiles after the top row was cleared");
 check(spawning.pixels[80*256+48]==arena_native[80*160],"arena spawn repair changed native pixels");
 // With an unaligned vertical scroll the final visible pixel uses another
 // BG row. That row also owns the safe margin extent.
 std::fill_n(vram.data()+0x1800,1024,1);
 std::fill_n(vram.data()+0x1800+14*32,4,2);
 set_word(0xca05,1);ppu.scy=1;arena.scanline(c,0);
 auto unaligned=arena.render(c,arena_native);
 if(unaligned.pixels[111*256]!=rgb[1])std::cerr<<"unaligned fixture expanded="<<unaligned.expanded<<" camera="<<unaligned.camera_x<<','<<unaligned.camera_y<<" pixel="<<std::hex<<unaligned.pixels[111*256]<<" expected="<<rgb[1]<<std::dec<<'\n';
 check(unaligned.pixels[111*256]==rgb[1],"unaligned boss scroll admitted the uncleared final BG row");
 // An entirely uncleared row cannot prove either margin belongs to this
 // frame. Use its established clear color while retaining native pixels.
 std::fill_n(vram.data()+0x1800+12*32,32,2);arena.scanline(c,0);
 auto uncleared=arena.render(c,arena_native);
 check(uncleared.expanded&&uncleared.pixels[96*256]==rgb[1]&&uncleared.pixels[96*256+48]==arena_native[96*160],"uncleared boss row admitted stale margins or changed native pixels");
 set_word(0xca05,0);ppu.scy=0;
 std::fill_n(vram.data()+0x1800,1024,1);ppu.lcdc&=uint8_t(~64);
 wram[0x9a1]=0;ppu.wy=144;ppu.lcdc&=uint8_t(~32);
 wram[0x5a00]=0x50;set_word(0xca08,0);ppu.scx=0;
 NativeMods ride(sha,features);ride.viewport(512,144);
 wram[0x5f]=6;wram[0x66]=2;set_word(0xca0d,96);set_word(0xca40,3336);
 set_word(0xca08,234);ppu.scx=234;ride.scanline(c,0);
 // The native locks approach each other over several updates before they
 // become equal. Preserve the domain from the first mounted update.
 set_word(0xca0d,350);set_word(0xca40,352);set_word(0xca08,352);ppu.scx=96;
 wram[0xc00]=1;wram[0xc2d]=14;wram[0x2d]=1;ride.scanline(c,0);
 auto riding=ride.render(c,native);
 check(riding.expanded&&riding.bound_left==96&&riding.bound_right==3496,"Ride Chaser camera lock clipped the loaded room");
 wram[0xc00]=wram[0xc2d]=0;wram[0x2d]=1;
 set_word(0xca0d,544);set_word(0xca40,544);set_word(0xca08,542);ppu.scx=30;ride.scanline(c,0);
 check(ride.render(c,native).bound_right==3496,"removing the parked vehicle clipped mounted autoscroll");
 auto mounted=ride.snapshot();ride.restore(mounted);
 check(ride.render(c,native).bound_right==3496,"mounted map bounds lost on current-state restore");
 wram[0x5f]=7;ride.scanline(c,0);
 check(ride.render(c,native).bound_left==544&&ride.render(c,native).bound_right==704,"vehicle map bounds leaked into another stage");
 wram[0x5f]=wram[0x66]=wram[0xc00]=wram[0xc2d]=wram[0x2d]=0;
 set_word(0xca0d,0);set_word(0xca40,1024);set_word(0xca08,0);ppu.scx=0;
 // Co-op still activates both views at Native160. Its one-view control
 // must retain the retail asymmetric right/bottom culling padding.
 auto cooperative=features;cooperative[0].plugin="gbcllr.xtreme2.coop";
 NativeMods narrow_coop(sha,cooperative);narrow_coop.viewport(160,144);narrow_coop.scanline(c,0);
 c.d=0xa4;c.instruction_pc=0x34dd;c.pc=0x34e0;
 eram[0x408]=230;eram[0x409]=0;
 check(narrow_coop.read(c,0xca08,0)==0,"Native160 co-op shortened the original right cull guard");
 c.instruction_pc=0x356d;c.pc=0x3570;eram[0x408]=64;eram[0x409]=1;
 check(narrow_coop.read(c,0xca08,0)==0,"Native160 co-op shortened the original large right cull guard");
 c.instruction_pc=0x34ff;c.pc=0x3502;eram[0x405]=230;
 check(narrow_coop.read(c,0xca05,0)==0,"Native160 co-op shortened the original bottom cull guard");
 c.d=0;
 wram[0xa09]=2;c.instruction_pc=0x35e9;c.pc=0x35eb;
 check(a.read(c,0x35e9,0xc6)==0xc6,"native view changed opcode fetch");
 check(a.read(c,0x35ea,0x20)==0x20,"wide view changed the hardware OAM draw bias");
 c.instruction_pc=0x35f1;c.pc=0x35f3;
 check(a.read(c,0x35f2,0xf8)==0xf8,"wide view changed the hardware OAM draw extent");
 c.instruction_pc=0;
 check(a.read(c,0x35ea,0x20)==0x20,"data read received an instruction override");
 c.rom_bank=0x3d;c.instruction_pc=0x44d4;c.pc=0x44d5;
 a.read(c,0xca08,0); // Snapshot the widened current activation rectangle.
 c.instruction_pc=0x4554;c.pc=0x4556;
 check(a.read(c,0x4555,0x2a)==0x2a,"enemy coordinate hook changed opcode fetch");
 rom[0x3d*0x4000+0x2000]=0;rom[0x3d*0x4000+0x2001]=1;
 c.instruction_pc=0x4555;
 check(a.read(c,0x6000,0)==0xd0,"enemy low coordinate did not use expanded bound");
 c.instruction_pc=0x4557;c.pc=0x4558;
 check(a.read(c,0x6001,1)==0,"enemy high coordinate lost expanded-bound borrow");
 // Scene object-cache reset must discard the preceding stage's expanded
 // history, including when the reset occurs at native width. Otherwise
 // first-scan objects in the new margins are mistaken for old occupants.
 c.instruction_pc=0x454e;c.pc=0x4551;a.read(c,0xca05,0);
 a.viewport(160,144);c.instruction_pc=0x44c3;c.pc=0x44c7;
 check(a.read(c,0x44c6,0x21)==0x21,"spawn-cache observation changed a native opcode");
 a.viewport(1280,720);c.instruction_pc=0x44d4;c.pc=0x44d5;a.read(c,0xca08,0);
 c.instruction_pc=0x469b;c.pc=0x469c;
 check(a.read(c,0x6000,0)==0,"first scan inherited expanded previous-stage bounds");
 c.instruction_pc=0x454e;c.pc=0x4551;a.read(c,0xca05,0);
 c.instruction_pc=0x469b;c.pc=0x469c;
 check(a.read(c,0x6000,0)==0xd0,"completed scan did not publish its expanded history");
 a.viewport(160,144);c.instruction_pc=0x44d4;c.pc=0x44d5;a.read(c,0xca08,0);
 c.instruction_pc=0x454e;c.pc=0x4551;a.read(c,0xca05,0);
 a.viewport(1280,720);c.instruction_pc=0x44d4;c.pc=0x44d5;a.read(c,0xca08,0);
 c.instruction_pc=0x469b;c.pc=0x469c;
 check(a.read(c,0x6000,0)==0,"native scan retained obsolete wide margins across a resize");
 // Only the mounted autoscroll death-plane consumer uses the expanded
 // left edge. Preserve actual camera reads for movement and native width.
 c.rom_bank=0x2c;wram[0x5f]=6;wram[0x2d]=1;
 c.instruction_pc=0x75f6;c.pc=0x75f7;
 check(a.read(c,0xca08,0)==0xd0,"mounted death plane did not follow the expanded left edge");
 c.sp=UINT16_MAX;
 check(a.read(c,0xca08,0)==0,"mounted death-plane hook changed a DMA read");
 c.sp=0xfffe;
 c.instruction_pc=0x75f7;c.pc=0x75f8;
 check(a.read(c,0xca09,2)==1,"mounted death-plane high byte lost expanded-left borrow");
 c.instruction_pc=0x75f8;c.pc=0x75f9;
 check(a.read(c,0xca08,0)==0,"expanded death plane changed an unrelated camera read");
 a.viewport(160,144);c.instruction_pc=0x75f6;c.pc=0x75f7;
 check(a.read(c,0xca08,0)==0,"native-width autoscroll death plane changed");
 a.viewport(1280,720);c.rom_bank=1;
 set_word(0xca0d,512);set_word(0xca40,512);set_word(0xc108,512);
 struct ClampRead{uint16_t address,instruction;uint8_t expected;};
 for(auto test:{ClampRead{0xc108,0x44ce,0x30},{0xc109,0x44cf,2},
     {0xca0d,0x44ee,0xd0},{0xca0e,0x44f0,1},
     {0xc108,0x448b,0xd0},{0xc109,0x448c,1},
     {0xca40,0x44a5,0x30},{0xca41,0x44a7,2}}) {
  c.instruction_pc=test.instruction;c.pc=test.instruction+1;
  check(a.read(c,test.address,0)==test.expected,"mounted player guard/clamp did not use the complete visible range");
 }
 check(wram[0x108]==0&&wram[0x109]==2,"visible clamp changed physical player coordinates while comparing");
 c.instruction_pc=0x446d;c.pc=0x446e;
 check(a.read(c,0xca40,0)==0,"visible player clamp changed actual camera control");
 set_word(0xca0d,0);set_word(0xca40,1024);set_word(0xc108,0);
 wram[0x5f]=wram[0x2d]=0;c.rom_bank=0x2c;c.instruction_pc=0x75f6;c.pc=0x75f7;
 check(a.read(c,0xca08,0)==0,"mounted death-plane hook leaked into an unrelated state");
 a.viewport(1280,400);check(a.render(c,native).width==461,"Fit did not follow actual viewport");
 // CAD6 can reject original OAM emission while the native actor still has
 // a complete world-space pose beyond the hardware draw rectangle.
 a.viewport(512,144);wram[0xa08]=wram[0xa09]=0;hram[0x22]=0xc1;
 wram[0x108]=94;wram[0x109]=1;wram[0x105]=32;wram[0x106]=0;
 wram[0x112]=0;wram[0x113]=0x50;wram[0x114]=2;wram[0xad6]=1;
 rom[0x9000]=0;rom[0x9001]=0x51;rom[0x9100]=1;rom[0x9102]=rom[0x9103]=0;rom[0x9104]=2;rom[0x9105]=0;rom[0x9106]=127;
 rgb[2]=0xffaabbcc;for(unsigned y=0;y<8;++y){vram[32+y*2]=0;vram[33+y*2]=0xff;}
 a.scanline(c,0);c.pc=0x1c39;a.read(c,0xcad6,1);a.scanline(c,0);auto culled=a.render(c,native);
 check(culled.expanded&&culled.sprites==1&&culled.pixels[16*512+342]==rgb[2],"native OAM culling removed a real margin actor");
 // The outer emitter can reject a far body before CAD6 is read at all.
 // An early capture must also avoid duplicating a later admitted body.
 c.pc=0x35df;c.instruction_pc=0x35de;c.hl=0xca08;c.d=0xc1;
 a.read(c,0xca08,0);c.pc=0x1c39;a.read(c,0xcad6,1);a.scanline(c,0);
 auto early=a.render(c,native);
 check(early.sprites==1&&early.pixels[16*512+342]==rgb[2],"outer draw cull lost or duplicated a native margin pose");
 check(early.pixels[16*512+182]!=rgb[2],"ordinary actor was repeated as screen weather");
 wram[0x12d]=0x43;c.pc=0x35df;a.read(c,0xca08,0);a.scanline(c,0);
 auto dust=a.render(c,native);
 check(dust.pixels[16*512+182]==rgb[2]&&dust.pixels[16*512+502]==rgb[2],"native dust density did not extend through the wide view");
 check(dust.pixels[16*512+22]==rgb[1],"wide dust replaced authoritative native pixels");
 // Player emission runs with an unrelated FFA2 page. Its unsigned screen
 // X must unwrap against C108 when the mounted player is in the left margin.
 NativeMods player(sha,features);player.viewport(512,144);
 wram[0x5f]=6;wram[0x2d]=1;
 set_word(0xca08,500);ppu.scx=244;set_word(0xc108,400);hram[0x22]=0xca;
 wram[0xb80]=164;wram[0xb82]=48;c.rom_bank=2;c.hl=0x6000;c.c=0;c.pc=0x1326;
 rom[0xa000]=1;rom[0xa001]=rom[0xa002]=rom[0xa003]=0;rom[0xa004]=0xc2;rom[0xa005]=0;
 for(unsigned y=0;y<8;++y){vram[0x2020+y*2]=0;vram[0x2021+y*2]=0xff;}
 player.scanline(c,0);player.read(c,0xcb80,164);player.scanline(c,0);
 auto left_player=player.render(c,native);
 check(left_player.expanded&&left_player.sprites==1&&left_player.pixels[32*512+76]==rgb[2],"player body wrapped256pixels when FFA2 held camera RAM");
 c.instruction_pc=0x1327;c.pc=0x132a;
 check(player.read(c,0xcb82,48)==160&&wram[0xb82]==48,"off-band native player OAM was not hidden without changing backing coordinates");
 c.sp=UINT16_MAX;check(player.read(c,0xcb82,48)==48,"player OAM masking changed a DMA read");c.sp=0xfffe;
 set_word(0xc108,500);wram[0xb80]=8;
 check(player.read(c,0xcb82,48)==48,"OAM masking hid a player visible in the native band");
 set_word(0xc108,800);wram[0xb80]=52;
 check(player.read(c,0xcb82,48)==160,"right-margin player OAM wrapped into the native band");
 player.viewport(160,144);check(player.read(c,0xcb82,48)==48,"native-width player OAM changed");
 NativeMods trail(sha,features);trail.viewport(512,144);
 set_word(0xc108,400);hram[0x22]=0xb0;wram[0xb80]=132;
 eram[0x1008]=144;eram[0x1009]=1;eram[0x102d]=0x35;eram[0x100b]=0xc1;
 c.instruction_pc=0x1396;c.pc=0x1399;trail.scanline(c,0);trail.read(c,0xcb80,132);trail.scanline(c,0);
 auto left_trail=trail.render(c,native);
 check(left_trail.sprites==1&&left_trail.pixels[32*512+44]==rgb[2],"player attachment lost its original world pose");
 c.instruction_pc=0x139a;c.pc=0x139d;
 check(trail.read(c,0xcb82,48)==160,"off-band player attachment OAM remained in the native band");
 eram[0x100b]=0xc2;check(trail.read(c,0xcb82,48)==48,"player attachment masking changed an unrelated alternate emitter");
 wram[0x5f]=wram[0x2d]=0;
 check(b.snapshot()!=a.snapshot(),"native module instances share state");
 a.restore(before);check(a.snapshot()==before,"native module state roundtrip");
 auto invalid=before;uint32_t width=4097;std::memcpy(invalid.data()+8,&width,4);
 rejects([&]{a.restore(invalid);},"invalid module width accepted");
 check(a.snapshot()==before,"failed native state load changed live state");
 wram[0xaba]=0;a.scanline(c,0);auto fallback=a.render(c,native);
 check(!fallback.expanded&&fallback.pixels.front()==0xff000000&&fallback.pixels[48]==rgb[1],"unsupported scene did not center native output");
 std::cout<<"native package targets, real margin tiles, Fit, operand guards, isolation and state contracts passed\n";
 return 0;
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
