#include "../src/native_mods.hpp"
#include "../src/sml2_native_mods.hpp"
#ifdef GBCLLR_SML2_TEST_ASSETS
#include "sml2_color_data.hpp"
#endif
#include <algorithm>
#include <cstring>
#include <iostream>
#include <stdexcept>
static void check(bool v,const char* m){if(!v)throw std::runtime_error(m);}
template<class F>static void rejects(F f,const char* m){bool caught=false;try{f();}catch(const std::exception&){caught=true;}check(caught,m);}
int main(int argc,char**argv){try{
 using namespace gbcllr;check(argc==2||(argc==3&&std::string(argv[2])=="--no-assets"),"SML2 package fixture required");bool assets=argc==2;
 const std::string sha="42045a1642f77b568534ab85b9db93c8a33d2b95c38ae84c987f497e8d52f97f";
 check(detail::sml2_window_address(0x1c00,-1,-1)==0x1fff&&detail::sml2_window_address(0x1800,256,256)==0x1800&&detail::sml2_window_address(0x1c00,4096,399)==0x1e20,"SML2 window did not wrap both32-tile coordinates");
 auto package=read_mod_package(argv[1],sha);check(package.default_enabled&&package.feature.view_width==-1&&package.feature.colorization,"SML2 fresh Fit/default contract");
 std::array feature{package.feature};feature[0].colorization=false;
 rejects([&]{NativeMods m(std::string(64,'0'),feature,false);},"wrong SML2 target accepted");
 rejects([&]{NativeMods m(sha,feature,true);},"unqualified CGB model accepted");
 auto bad=feature;bad[0].abi=2;rejects([&]{NativeMods m(sha,bad,false);},"wrong SML2 ABI accepted");
 NativeMods a(sha,feature,false),b(sha,feature,false),disabled(sha,{});
 check(!a.observes_scanlines()&&!disabled.observes_scanlines(),"SML2 monochrome/disabled profile requested raster observation");
 std::vector<uint8_t> rom(0x80000),wram(0x8000),vram(0x4000),eram(0x2000);
 std::array<uint8_t,160> oam{};std::array<uint8_t,127> hram{};
 std::array<uint8_t,64> colors{};std::array<uint32_t,32> bg{},obj{};bg[1]=0xff123456;obj[1]=0xff77ff88;
 ModPpu ppu{176,184,0xe3,7,136,colors.data(),colors.data(),bg.data(),obj.data()};
 ModContext c{rom.data(),wram.data(),vram.data(),eram.data(),oam.data(),hram.data(),rom.size(),eram.size(),0,0,0,0,0,0,0,1,&ppu};
 hram[0x1b]=4;hram[0x49]=hram[0x4b]=hram[0x3b]=1;
 std::fill_n(eram.data()+0x600,4,1);std::fill_n(vram.data()+0x1800,1024,1);
 for(int y=0;y<8;y++)vram[16+y*2]=vram[0x1010+y*2]=0xff;
 std::array<uint32_t,160*144> native{};native.fill(bg[1]);native[136*160]=0xffaabbcc;native[136*160+159]=0xffddeeff;
 a.viewport(1280,720);a.scanline(c,0);auto before=a.snapshot();auto wide=a.render(c,native);
 // Actual castle shaft right-edge camera4016 publishes aligned SCX96.
 // The visible strip ends at4095; an extra off-screen tile must not drop Fit.
 {NativeMods edge(sha,feature,false);edge.viewport(1280,720);auto old_x=hram[0x4a],old_hi=hram[0x4b],old_scx=ppu.scx;
  hram[0x4a]=0xb0;hram[0x4b]=15;ppu.scx=96;edge.scanline(c,0);
  check(edge.render(c,native).expanded,"SML2 aligned right-edge source lost real margins");
  hram[0x4a]=0xb1;ppu.scx=97;edge.scanline(c,0);
  check(!edge.render(c,native).expanded,"SML2 accepted actual visible pixels outside the world");
  hram[0x4a]=old_x;hram[0x4b]=old_hi;ppu.scx=old_scx;}
 std::array dx_feature{package.feature};std::unique_ptr<NativeMods> dx;
 if(assets){dx=std::make_unique<NativeMods>(sha,dx_feature,false);dx->viewport(1280,720);dx->scanline(c,0);
  check(dx->observes_scanlines(),"SML2 color profile omitted raster observation");
  check(dx->render(c,native).pixels[0]==0xffffde08,"native DX color differs from stage0 palette");
  rejects([&]{dx->restore(before);},"cross-color SML2 snapshot accepted");
  auto state=dx->snapshot(),wrong=state;wrong[16]^=1;rejects([&]{dx->restore(wrong);},"wrong DX asset identity accepted");check(dx->snapshot()==state,"wrong assets changed live state");
  auto forged=state;uint32_t off=0;std::memcpy(forged.data()+12,&off,4);std::memset(forged.data()+16,'0',64);
  rejects([&]{a.restore(forged);},"colored state entered a monochrome profile");check(a.snapshot()==before,"wrong color scene changed live state");
 }else rejects([&]{NativeMods missing(sha,dx_feature,false);},"assetless build accepted native color");
 if(assets){
  // Native enemy updates may omit a culled metasprite for a frame. Retain
  // its complete pose, follow its live record, and retire it with that record.
  auto enemy_rom=rom,enemy_eram=eram;auto enemy_hram=hram;std::array<uint8_t,160> enemy_oam{};
  auto ec=c;ec.rom=enemy_rom.data();ec.eram=enemy_eram.data();ec.hram=enemy_hram.data();ec.oam=enemy_oam.data();
  enemy_eram[0xd00]=1;enemy_eram[0xd01]=44;enemy_eram[0xd03]=0;enemy_eram[0xd04]=220;enemy_eram[0xd05]=9;enemy_eram[0xd06]=0xab;enemy_eram[0xd07]=8;
  enemy_hram[0x50]=1;enemy_hram[0x51]=44;enemy_hram[0x53]=0;enemy_hram[0x54]=220;enemy_hram[0x55]=9;
  enemy_rom[0xc0b1]=0;enemy_rom[0xc0b2]=0x50;
  enemy_rom[0xd000]=enemy_rom[0xd001]=enemy_rom[0xd003]=0;enemy_rom[0xd002]=0xb0;enemy_rom[0xd004]=0x80;
  NativeMods enemy(sha,dx_feature,false);enemy.viewport(1280,720);enemy.scanline(ec,0);
  auto empty=enemy.render(ec,native);std::vector<uint32_t> empty_pixels(empty.pixels.begin(),empty.pixels.end());
  ec.rom_bank=3;ec.instruction_pc=0x4019;ec.pc=0x401b;enemy.read(ec,0xffe2,0);enemy.scanline(ec,0);
  auto complete=enemy.render(ec,native);auto pieces=complete.sprites;check(pieces>0,"SML2 complete native enemy pose was not captured");
  std::vector<uint32_t> first_pose(complete.pixels.begin(),complete.pixels.end());
  check(first_pose!=empty_pixels,"SML2 captured enemy has no visible body");
  enemy_eram[0xd01]=46;enemy.scanline(ec,0);auto moved=enemy.render(ec,native);
  check(moved.sprites==pieces&&!std::equal(first_pose.begin(),first_pose.end(),moved.pixels.begin()),"SML2 omitted producer split or froze the enemy body");
  std::vector<uint32_t> moved_pixels(moved.pixels.begin(),moved.pixels.end());
  enemy_hram[0x1b]=8;enemy.scanline(ec,0);auto paused=enemy.render(ec,native);
  check(paused.expanded&&paused.sprites==pieces&&std::equal(moved_pixels.begin(),moved_pixels.end(),paused.pixels.begin()),"SML2 native pause lost wide color or a complete enemy pose");
  auto held=enemy.snapshot();enemy.restore(held);enemy.render(ec,native);check(enemy.snapshot()==held,"SML2 enemy pose render/roundtrip changed state");
  enemy_eram[0xd05]=0;enemy.scanline(ec,0);auto retired=enemy.render(ec,native);
  check(retired.sprites==0&&std::equal(empty_pixels.begin(),empty_pixels.end(),retired.pixels.begin()),"SML2 retired native enemy left a cached ghost");
 }
 auto wrong_input=feature;wrong_input[0].asset_input=std::string(64,'0');rejects([&]{NativeMods bad_input(sha,wrong_input,false);},"wrong presentation input accepted");
 check(wide.expanded&&wide.width==256,"SML2 world did not expand");
 check(wide.pixels[0]==bg[1]&&wide.pixels[255]==bg[1],"SML2 margins did not use real world pixels");
 check(wide.pixels[136*256]==native[136*160]&&wide.pixels[136*256+255]==native[136*160+159],"SML2 HUD did not follow the view edges");
 check(a.snapshot()==before,"SML2 render changed simulation state");
 check(disabled.render(c,native).pixels.data()==native.data(),"disabled SML2 adapter altered native output");
 c.rom_bank=2;c.instruction_pc=0x3caa;c.pc=0x3cab;
 check(a.read(c,0xaf0b,0x60)==0x90,"SML2 activation rectangle was not widened");
 c.instruction_pc=0;check(a.read(c,0xaf0b,0x60)==0x60,"SML2 data read received an unrelated override");
 // ROM0 remains fixed while the MBC-selected bank changes. Only the
 // horizontal CP operand may extend a live projectile's world boundary.
 rom[0x3281]=0xfe;rom[0x3282]=0xc0;c.rom_bank=3;c.instruction_pc=0x3281;c.pc=0x3283;c.hl=0xa885;eram[0x880]=1;
 auto fire_x=[&](int x){eram[0x883]=uint8_t(x);eram[0x884]=uint8_t(x>>8);eram[0x25f]=uint8_t(x);};
 fire_x(368);check(a.read(c,0x3282,0xc0)==0xd0,"SML2 projectile died inside the wide view");
 check(a.read(c,0x3281,0xfe)==0xfe,"SML2 projectile hook changed an opcode fetch");
 check(a.read(c,0x3293,0xc0)==0xc0,"SML2 projectile hook changed the vertical boundary");
 c.hl=0xa886;check(a.read(c,0x3282,0xc0)==0xc0,"SML2 projectile accepted an unrelated slot");c.hl=0xa885;
 rom[0x3281]=0;check(a.read(c,0x3282,0xc0)==0xc0,"SML2 projectile accepted an unqualified instruction");rom[0x3281]=0xfe;
 fire_x(wide.left+wide.width+32);check(a.read(c,0x3282,0xc0)==uint8_t((eram[0x25f]+80)&0xf0),"SML2 projectile survived its expanded boundary");
 fire_x(700);a.viewport(160,144);check(a.read(c,0x3282,0xc0)==uint8_t((eram[0x25f]+80)&0xf0),"SML2 narrow resize retained an extended projectile");a.viewport(1280,720);
 fire_x(368);eram[0x881]=eram[0x25d]=144;eram[0x882]=1;eram[0x885]=eram[0x212]=1;a.read(c,0x3282,0xc0);
 eram[0x2b0]=184;eram[0x2b1]=176;eram[0xd00]=1;eram[0xd01]=104;eram[0xd03]=1;eram[0xd04]=140;
 eram[0xd13]=243;eram[0xd14]=253;eram[0xd15]=250;eram[0xd16]=6;
 rom[0x300e]=rom[0x3033]=0x7e;rom[0x302e]=0x2a;
 c.instruction_pc=0x302e;c.pc=0x302f;check(a.read(c,0xad09,136)==228,"SML2 projectile collision retained a culled actor Y");
 c.instruction_pc=0x3033;c.pc=0x3034;check(a.read(c,0xad0a,184)==192,"SML2 projectile collision retained a culled actor X");
 auto projectile_state=a.snapshot();a.restore(projectile_state);check(a.read(c,0xad0a,184)==192,"SML2 current state lost its projectile collision cursor");
 c.instruction_pc=0x300e;c.pc=0x300f;check(a.read(c,0xad12,0x4e)==0x4e,"SML2 real world collision changed actor vulnerability");
 eram[0xd00]=2;check(a.read(c,0xad12,0x4e)==0,"SML2 projectile hit an actor a full page away");eram[0xd00]=1;
 c.instruction_pc=0x3032;check(a.read(c,0xad0a,184)==184,"SML2 actor correction changed an unrelated read");
 c.instruction_pc=0x3033;a.viewport(160,144);check(a.read(c,0xad0a,184)==184,"SML2 native width changed original actor collision");a.viewport(1280,720);
 eram[0x880]=0;
 c.instruction_pc=0x408a;c.pc=0x408d;a.read(c,0xaf12,1);
 c.instruction_pc=0x4090;c.pc=0x4093;check(a.read(c,0xaf13,0x70)==0x70,"SML2 initial spawn edge skipped the vanilla scan");
 c.rom_bank=3;c.instruction_pc=0x4019;c.pc=0x401b;
 hram[0x50]=1;hram[0x51]=100;hram[0x53]=0;hram[0x54]=200;
 auto r=rom.data()+3*0x4000; r[0xb1]=0;r[0xb2]=0x50;r[0x1002]=1;r[0x1004]=0x80;
 check(a.read(c,0x4019,0xf0)==0xf0,"SML2 actor hook changed an opcode fetch");
 a.read(c,0xffe2,0);a.scanline(c,0);wide=a.render(c,native);
 check(wide.sprites==1&&wide.pixels[16*256+228]==obj[1],"SML2 margin entity was not drawn in world space");
 c.instruction_pc=0x409b;c.pc=0x409e;check(a.read(c,0xa2b1,0)!=0,"SML2 wide actor can alias into native OAM");
 // The emitter may run in VBlank before the next line0 snapshot. It must
 // anchor newly emitted pieces to the live uploaded scroll, not last frame.
 ppu.scx=184;hram[0x4a]=8;hram[0x4b]=1;hram[0x45]=184;hram[0x44]=64;
 auto er=rom.data()+0x4000;er[0]=0;er[1]=0x50;er[0x1002]=1;er[0x1004]=0x80;
 c.rom_bank=1;c.instruction_pc=0x52b8;c.pc=0x52ba;a.read(c,0xffc5,184);a.scanline(c,0);wide=a.render(c,native);
 check(wide.pixels[48*256+224]==obj[1]&&wide.pixels[48*256+216]==bg[1],"SML2 emitter used a stale camera origin");
 hram[0x3b]=0;a.read(c,0xffc5,184);a.scanline(c,0);wide=a.render(c,native);
 check(wide.pixels[48*256+224]==bg[1],"SML2 pipe priority drew Mario over opaque background");
 hram[0x3b]=1;a.read(c,0xffc5,184);a.scanline(c,0);wide=a.render(c,native);
 check(wide.pixels[48*256+224]==obj[1],"SML2 pipe exit retained behind-background priority");
 eram[0x880]=1;eram[0x883]=30;eram[0x884]=0;eram[0x881]=10;eram[0x882]=1;
 hram[0x46]=0xb0;hram[0x45]=102;hram[0x44]=80;er[0x12b5]=0xf0;er[0x12b6]=0xc4;
 c.instruction_pc=0x52b5;c.pc=0x52b7;check(a.read(c,0xffc4,80)==0xf0,"SML2 wrapped projectile leaked a hardware ghost");
 hram[0x46]=0;check(a.read(c,0xffc4,80)==80,"SML2 ghost guard changed another emitter client");
 hram[0x46]=0xb0;c.instruction_pc=0x52b4;check(a.read(c,0xffc4,80)==80,"SML2 ghost guard changed an unrelated read");
 eram[0x880]=0;
 a.viewport(1280,400);check(a.render(c,native).width==461,"SML2 Fit did not follow the viewport");
 a.restore(before);check(a.snapshot()==before&&b.snapshot()!=before,"SML2 per-instance state roundtrip/isolation failed");
 auto invalid=before;uint32_t width=4097;std::memcpy(invalid.data()+8,&width,4);
 rejects([&]{a.restore(invalid);},"SML2 invalid state accepted");check(a.snapshot()==before,"SML2 rejected state changed live state");
 hram[0x1b]=12;a.scanline(c,0);wide=a.render(c,native);
 check(!wide.expanded&&wide.pixels[48]==native[0],"SML2 scene transition did not retain native frame");
 if(dx){
 // Actual live wave bits must animate the colored view after a VRAM change.
 hram[0x1b]=4;std::fill_n(eram.data()+0x600,4,0x20);std::fill_n(vram.data()+0x1800,1024,0x20);vram[0x1202]=0x80;
 dx->viewport(1280,720);dx->scanline(c,0);auto water=dx->render(c,native);
 check(water.pixels[256]==0xffffffff&&water.pixels[257]==0xff31bde7,"native DX live water colors differ");
 vram[0x1202]=0x40;dx->scanline(c,0);water=dx->render(c,native);
 check(water.pixels[256]==0xff31bde7&&water.pixels[257]==0xffffffff,"native DX water animation froze on an atlas phase");
 }
#ifdef GBCLLR_SML2_TEST_ASSETS
 // Menus must survive stale gameplay fields, including in current snapshots.
 NativeMods fixed(sha,dx_feature,false);fixed.viewport(160,144);
 auto fixed_fixture=[&](unsigned scene){
  std::fill(vram.begin(),vram.end(),0);std::fill(eram.begin(),eram.end(),0);oam.fill(0);hram.fill(0);
  for(unsigned a=0;a<0x1800;a+=16)std::copy_n(sml2_color::fixed_original(scene,a),16,vram.data()+a);
  std::copy_n(sml2_color::fixed_old_map+scene*2048,2048,vram.data()+0x1800);
  auto regs=sml2_color::fixed_old_ppu+scene*16;ppu.lcdc=regs[0];ppu.scy=regs[2];ppu.scx=regs[3];ppu.bgp=regs[7];ppu.obp0=regs[8];ppu.obp1=regs[9];ppu.wy=regs[10];ppu.wx=regs[11];hram[0x1b]=sml2_color::fixed_modes[scene];
  if(sml2_color::fixed_submodes[scene]>=0)eram[0x68b]=uint8_t(sml2_color::fixed_submodes[scene]);
 };
 fixed_fixture(0);fixed.scanline(c,0);auto title=fixed.render(c,native);std::vector<uint32_t> expected(title.pixels.begin(),title.pixels.end());
 eram[0x216]=eram[0x269]=255;fixed.scanline(c,0);auto stale_title=fixed.render(c,native);
 check(std::equal(expected.begin(),expected.end(),stale_title.pixels.begin()),"SML2 fixed rendering depends on stale gameplay fields");
 auto fixed_state=fixed.snapshot();fixed.restore(fixed_state);check(fixed.snapshot()==fixed_state,"SML2 fixed state rejected stale gameplay fields");
 // Cleanup changes mode before its rendered title, then resets LY while the
 // LCD-off framebuffer still holds that title. Only its real white publication
 // may replace the color picture; current snapshots retain the matching source.
 hram[0x1b]=25;fixed.scanline(c,0);check(std::equal(expected.begin(),expected.end(),fixed.render(c,native).pixels.begin()),"SML2 title cleanup dropped its still-rendered colors");
 // GB_lcd_off calls line0 before memory.c stores the disabled LCDC value.
 fixed.scanline(c,0);auto held_title=fixed.snapshot();ppu.lcdc&=0x7f;std::fill(vram.begin(),vram.end(),0);
 check(std::equal(expected.begin(),expected.end(),fixed.render(c,native).pixels.begin()),"SML2 LCD-off transition replaced the published title source");
 auto white=native;white.fill(bg[4]);check(std::equal(white.begin(),white.end(),fixed.render(c,white).pixels.begin()),"SML2 title cache replaced an actual LCD-off blank");
 fixed.restore(held_title);check(std::equal(expected.begin(),expected.end(),fixed.render(c,native).pixels.begin()),"SML2 held-title current-load lost its source");
 hram[0x1b]=26;check(std::equal(native.begin(),native.end(),fixed.render(c,native).pixels.begin()),"SML2 title source leaked into the first file frame");
 // The LCD starts the actual file map before cleanup mode25 returns. Only
 // that qualified source on a real white publication can begin its history.
 fixed_fixture(0);fixed.scanline(c,0);fixed_fixture(1);hram[0x1b]=25;
 check(!fixed.first_line(c,native),"SML2 first-file source replaced a nonwhite native publication");
 check(fixed.first_line(c,white),"SML2 first-file source waited for the later guest mode26");
 auto file_sink=fixed.pixel_sink(0);check(file_sink.output&&file_sink.colors,"SML2 first-file pixels were not observed");
 file_sink.output[0]=0xfff7d6bd;auto partial_file=fixed.snapshot();
 auto partial=fixed.render(c,white);check(partial.pixels[0]==0xfff7d6bd&&partial.pixels[160]==white[160],"SML2 first-file view invented an unpublished row");
 fixed.framebuffer_cleared();check(fixed.render(c,white).pixels[0]==white[0],"SML2 first-file provenance survived an actual native clear");
 fixed.restore(partial_file);check(fixed.snapshot()==partial_file&&fixed.render(c,white).pixels[0]==0xfff7d6bd,"SML2 partial file current-load lost its published source");
 fixed_fixture(0);hram[0x1b]=25;std::fill(vram.begin()+0x1800,vram.begin()+0x2000,0xff);fixed.scanline(c,0);
 check(std::equal(native.begin(),native.end(),fixed.render(c,native).pixels.begin()),"SML2 title cleanup accepted an unrelated map");
 // Actual Big lead OAM uniquely selects original ROM1 frame$08. Its paired
 // DX producer owns the extra bank1 pieces and feet palette; the room's
 // Small-only observed object table cannot describe this player.
 fixed_fixture(28);eram[0x269]=24;eram[0x216]=1;eram[0xfc0]=8;
 const uint8_t big_lead[][4]={{121,152,1,32},{121,160,8,32},{129,152,17,32},{129,160,24,32},{137,144,42,32},{137,152,41,32},{137,160,40,32},{145,152,57,32},{145,160,56,32}};
 std::memcpy(oam.data(),big_lead,sizeof big_lead);std::fill(rom.begin()+0x4000,rom.begin()+0x8000,0);
 rom[0x4010]=0;rom[0x4011]=0x44;unsigned ptr=0x4400;
 for(auto e:big_lead){rom[ptr++]=uint8_t(e[0]-133);rom[ptr++]=uint8_t(e[1]-160);rom[ptr++]=e[2];rom[ptr++]=e[3];}rom[ptr]=0x80;
 NativeMods lead(sha,dx_feature,false);lead.viewport(160,144);lead.scanline(c,0);auto lead_state=lead.snapshot();auto lead_view=lead.render(c,native);
 check(lead_view.pixels[115*160+150]==0xffef9c4a&&lead_view.pixels[134*160+145]==0xffb56b00,"SML2 non-Small lead lost actual DX player color pieces");
 check(lead.snapshot()==lead_state,"SML2 lead producer rendering changed canonical state");
 oam[2]=0xff;lead.scanline(c,0);check(lead.render(c,native).pixels[134*160+145]==0xffe71821,"SML2 lead accepted an unrelated native OAM group");
 NativeMods bonus(sha,dx_feature,false);bonus.viewport(160,144);
 for(unsigned scene=32;scene<38;scene++){
  fixed_fixture(scene);bonus.scanline(c,0);auto source=bonus.snapshot();auto view=bonus.render(c,native);
  check(!std::equal(native.begin(),native.end(),view.pixels.begin()),"SML2 bonus/coin source stayed native gray");
  bonus.restore(source);check(bonus.snapshot()==source,"SML2 bonus current-load lost its source binding");
  hram[0x1b]=0xff;bonus.scanline(c,0);check(std::equal(native.begin(),native.end(),bonus.render(c,native).pixels.begin()),"SML2 bonus source accepted an unrelated mode");
 }
 // Paid prizes scroll complete native6x9 records, including erases. Their
 // original/DX tile IDs agree; a checkpoint's preceding carrot or sign must
 // not survive beneath another record or the native selection highlight.
 for(unsigned scene=32;scene<36;scene++){
  fixed_fixture(scene);unsigned course=12-sml2_color::fixed_submodes[scene];
  NativeMods paid(sha,dx_feature,false);paid.viewport(160,144);
  auto check_panel=[&]{
   paid.scanline(c,0);auto state=paid.snapshot();auto view=paid.render(c,native);
   for(unsigned row=0;row<9;row++)for(unsigned col=0;col<6;col++){
    unsigned cell=0x8a+row*32+col,tile=vram[0x1800+cell],attr=sml2_color::fixed_attributes[scene*2048+cell];
    unsigned address=(ppu.lcdc&0x10)?tile*16:unsigned(0x1000+int8_t(tile)*16);
    auto texture=sml2_color::fixed_texture(scene,(attr&8)!=0,address);
    for(unsigned y=0;y<8;y++)for(unsigned x=0;x<8;x++){
     unsigned bit=(7-x)^((attr&0x20)?7:0),py=y^((attr&0x40)?7:0);
     unsigned color=((texture[py*2]>>bit)&1)|(((texture[py*2+1]>>bit)&1)<<1);
     auto p=sml2_color::fixed_bg_palettes+scene*64+((attr&7)*4+color)*2;unsigned v=p[0]|(p[1]<<8);
     auto ch=[](unsigned z){z&=31;return (z<<3)|(z>>2);};unsigned rgb=0xff000000u|(ch(v)<<16)|(ch(v>>5)<<8)|ch(v>>10);
     unsigned sx=(((cell&31)*8+x)-ppu.scx)&255,sy=(((cell>>5)*8+y)-ppu.scy)&255;
     check(sx<160&&sy<144&&view.pixels[sy*160+sx]==rgb,"SML2 paid panel retained stale checkpoint prize pixels");
    }
   }
   std::vector<uint32_t> expected_panel(view.pixels.begin(),view.pixels.end());
   check(paid.snapshot()==state,"SML2 paid panel render changed canonical state");paid.restore(state);
   check(std::equal(expected_panel.begin(),expected_panel.end(),paid.render(c,native).pixels.begin()),"SML2 paid current-load lost its published record");
  };
  for(unsigned frame=0;frame<30;frame++){
   auto record=sml2_color::paid_bonus_tiles+course*228+frame*6;
   for(unsigned row=0;row<9;row++)std::copy_n(record+row*6,6,vram.data()+0x188a+row*32);
   check_panel();
  }
  vram[0x190c]=0xd4;vram[0x190d]=0xd5;vram[0x192c]=0xd8;vram[0x192d]=0xd9;check_panel();
  vram[0x188a]^=0x80;paid.scanline(c,0);
  check(std::equal(native.begin(),native.end(),paid.render(c,native).pixels.begin()),"SML2 paid panel accepted an incomplete native producer record");
 }
#if GBCLLR_OBSERVED_DISPLAY
 // The native bag producer selects frame1 at its live B/C anchor. DX
 // adds two eye pieces; an XY match against a later DX frame loses them.
 fixed_fixture(32);c.b=64;c.c=57;c.rom_bank=24;c.instruction_pc=0x50a2;c.pc=0x50a3;
 rom[0x6108b]=0x21;rom[0x6108c]=0xb1;rom[0x6108d]=0x51;
 rom[0x610a2]=0x2a;rom[0x610a3]=0x66;rom[0x610a4]=0x6f;
 rom[0x611b3]=0x65;rom[0x611b4]=0x52;
 const uint8_t bag[]={0,0,176,0,0,8,176,32,8,0,177,0,8,8,177,32,255,255,255,255};
 std::copy_n(bag,sizeof bag,rom.data()+0x61265);eram[0x782]=10;
 NativeMods props(sha,dx_feature,false);props.viewport(160,144);props.scanline(c,0);
 auto no_props=props.snapshot();c.instruction_pc=0x50a1;props.read(c,0x51b3,0x65);
 check(props.snapshot()==no_props,"SML2 bonus producer accepted a foreign instruction");c.instruction_pc=0x50a2;
 check(props.read(c,0x51b3,0x65)==0x65,"SML2 bonus producer changed its native pointer read");
 auto queued_props=props.snapshot();props.restore(queued_props);props.scanline(c,0);
 auto props_state=props.snapshot();auto props_view=props.render(c,native);
 auto eye=sml2_color::fixed_texture(32,1,0xf00);bool saw_eye=false;
 for(unsigned y=0;y<8;y++)for(unsigned x=0;x<8;x++){
  unsigned color=((eye[y*2]>>(7-x))&1)|(((eye[y*2+1]>>(7-x))&1)<<1);if(!color)continue;
  bool covered=false;auto sequence=sml2_color::bonus_presentation_index[1];
  for(unsigned i=0;i<4;i++){
   auto piece=sml2_color::pieces[sml2_color::presentation_offset[sequence]+i];int px=int(x)+2-piece.x,py=int(y)+7-piece.y;
   if(px<0||px>=8||py<0||py>=8)continue;if(piece.attr&0x20)px=7-px;
   auto pattern=sml2_color::fixed_texture(32,(piece.attr&8)!=0,piece.tile*16);
   if(((pattern[py*2]|pattern[py*2+1])>>(7-px))&1)covered=true;
  }
  if(covered)continue;
  auto p=sml2_color::fixed_obj_palettes+32*64+(8+color)*2;unsigned v=p[0]|(p[1]<<8);
  auto ch=[](unsigned z){z&=31;return (z<<3)|(z>>2);};
  uint32_t expected=0xff000000u|(ch(v)<<16)|(ch(v>>5)<<8)|ch(v>>10);
  check(props_view.pixels[(48+y)*160+58+x]==expected,"SML2 bonus bag lost native-anchor DX eye pieces");saw_eye=true;
 }
 check(saw_eye&&props.snapshot()==props_state,"SML2 bonus render changed queued/current state");
#endif
 fixed_fixture(2);std::copy_n(sml2_color::world_old_map,4096,eram.data()+0x1000);eram[0x680]=ppu.scx;eram[0x682]=ppu.scy;eram[0x683]=1;
 ppu.wy=0;eram[0x216]=eram[0x269]=255;fixed.scanline(c,0);auto negative_window=fixed.render(c,native);check(negative_window.pixels.size()==160*144,"SML2 shifted window failed its fixed-scene render");
 fixed_state=fixed.snapshot();fixed.restore(fixed_state);check(fixed.snapshot()==fixed_state,"SML2 shifted-window fixed state failed roundtrip");
 // The main-map animation follows actual row writes, not a counter guess.
 fixed_fixture(2);std::copy_n(sml2_color::world_old_map,4096,eram.data()+0x1000);eram[0x680]=ppu.scx;eram[0x682]=ppu.scy;eram[0x683]=1;
 constexpr unsigned animation[]={0x8e0,0x8f0,0x9e0,0x9f0,0xce0,0xcf0,0xde0,0xdf0};
 for(unsigned i=0;i<8;i++){std::copy_n(sml2_color::world_animation_original+i*16,16,vram.data()+animation[i]);std::copy_n(sml2_color::world_animation_original+i*16,16,rom.data()+0x34000+animation[i]);}
 NativeMods animated(sha,dx_feature,false);animated.viewport(160,144);c.rom_bank=15;c.instruction_pc=0x67eb;c.pc=0x67ec;rom[0x3e7eb]=0x2a;
 auto uninitialized=animated.snapshot();check(animated.read(c,0x67eb,0x2a)==0x2a&&animated.snapshot()==uninitialized,"SML2 animation hook changed an opcode fetch");
 check(animated.read(c,0x88e0,vram[0x8e0])==vram[0x8e0],"SML2 animation initializer changed the bus value");animated.scanline(c,0);auto base_view=animated.render(c,native);std::vector<uint32_t> base_pixels(base_view.pixels.begin(),base_view.pixels.end());
 unsigned row=0;for(;row<16;row++){unsigned x=sml2_color::world_animation_color[row],y=sml2_color::world_animation_color[16+row];if(uint8_t((x>>1)|((y&1)<<7))!=x||uint8_t((y>>1)|((x&1)<<7))!=y)break;}check(row<16,"SML2 animation fixture has no moving row");
 c.instruction_pc=0x6876;c.pc=0x6877;c.hl=uint16_t(0x88f0+row);rom[0x3e876]=0x0a;rom[0x3e877]=0xcb;rom[0x3e878]=0x3f;
 auto rotate_row=[&](bool right){unsigned x=vram[0x8e0+row],y=vram[0x8f0+row];vram[0x8e0+row]=uint8_t(right?(x>>1)|((y&1)<<7):(x<<1)|(y>>7));vram[0x8f0+row]=uint8_t(right?(y>>1)|((x&1)<<7):(y<<1)|(x>>7));};
 uint8_t bus=vram[0x8f0+row];check(animated.read(c,uint16_t(0xa710+row),bus)==bus,"SML2 animated row changed the guest data");rotate_row(true);animated.scanline(c,0);auto shifted_view=animated.render(c,native);std::vector<uint32_t> shifted_pixels(shifted_view.pixels.begin(),shifted_view.pixels.end());check(shifted_pixels!=base_pixels,"SML2 colored map animation stayed on a frozen phase");
 auto animation_state=animated.snapshot();animated.restore(animation_state);check(animated.snapshot()==animation_state&&std::equal(shifted_pixels.begin(),shifted_pixels.end(),animated.render(c,native).pixels.begin()),"SML2 animation state failed current-load replay");
 c.instruction_pc=0x6939;c.pc=0x693a;rom[0x3e939]=0x0a;rom[0x3e93a]=0xcb;rom[0x3e93b]=0x27;animated.read(c,uint16_t(0xa710+row),vram[0x8f0+row]);rotate_row(false);animated.scanline(c,0);check(std::equal(base_pixels.begin(),base_pixels.end(),animated.render(c,native).pixels.begin()),"SML2 opposite row rotation did not restore the actual picture");
 // Map props and HUD share tiles but have different DX palettes. The
 // fifth Mario piece follows the actual pre-update shadow OAM pose.
 check(sml2_color::world_object_attributes[0x7c]==0&&sml2_color::fixed_object_attributes[2*256+0x7c]==7,"SML2 map prop palette contaminated its HUD icon");
 fixed_fixture(2);std::copy_n(sml2_color::world_old_map,4096,eram.data()+0x1000);eram[0x680]=ppu.scx;eram[0x682]=ppu.scy;eram[0x683]=1;
 for(unsigned i=0;i<4;i++){std::copy_n(oam.data()+i*4,4,eram.data()+0x100+i*4);rom[0x3cb07+i*2]=oam[i*4+2];rom[0x3cb08+i*2]=oam[i*4+3];}
 NativeMods marker(sha,dx_feature,false);marker.viewport(160,144);c.rom_bank=15;c.instruction_pc=0x49b8;c.pc=0x49bb;rom[0x3c9b8]=0xfa;rom[0x3c9b9]=0x44;rom[0x3c9ba]=0xa8;
 auto untouched=marker.snapshot();check(marker.read(c,0x49b8,0xfa)==0xfa&&marker.snapshot()==untouched,"SML2 map marker hook changed an opcode fetch");
 check(marker.read(c,0xa844,0x35)==0x35,"SML2 map marker hook changed the guest value");marker.scanline(c,0);check(marker.render(c,native).captures==1,"SML2 live map Mario pose did not emit its DX overlay");auto marker_state=marker.snapshot();marker.restore(marker_state);check(marker.snapshot()==marker_state,"SML2 live marker current-load state differed");
 fixed_fixture(3);eram[0x216]=eram[0x269]=255;NativeMods tree(sha,dx_feature,false);tree.viewport(160,144);tree.scanline(c,0);auto tree_view=tree.render(c,native);std::vector<uint32_t> tree_pixels(tree_view.pixels.begin(),tree_view.pixels.end());check(!std::equal(tree_pixels.begin(),tree_pixels.end(),native.begin()),"SML2 Tree submap was not colored");auto tree_state=tree.snapshot();tree.restore(tree_state);check(tree.snapshot()==tree_state&&std::equal(tree_pixels.begin(),tree_pixels.end(),tree.render(c,native).pixels.begin()),"SML2 Tree current-load failed with stale gameplay fields");eram[0x68b]=15;tree.scanline(c,0);check(std::equal(native.begin(),native.end(),tree.render(c,native).pixels.begin()),"SML2 Tree assets leaked into an unsupported submap");
 for(unsigned scene=4;scene<sml2_color::fixed_count;scene++){
  fixed_fixture(scene);eram[0x216]=eram[0x269]=255;if(sml2_color::fixed_phases[scene]>=0)eram[0x2dd]=uint8_t(sml2_color::fixed_phases[scene]);auto boss=sml2_color::fixed_bosses[scene];
  auto source_power=sml2_color::fixed_phases[scene]==0?0:3;
  if(boss){eram[0x216]=source_power;eram[0x269]=sml2_color::fixed_stages[scene];eram[0x28b]=240;eram[0xfc0]=uint8_t(boss-1);}
  NativeMods map_scene(sha,dx_feature,false);map_scene.viewport(160,144);map_scene.scanline(c,0);auto view=map_scene.render(c,native);check(!std::equal(view.pixels.begin(),view.pixels.end(),native.begin()),"SML2 qualified scene fixture fell back to monochrome");auto state=map_scene.snapshot();map_scene.restore(state);check(map_scene.snapshot()==state,"SML2 current scene state failed roundtrip");
  if(boss){eram[0x216]=4;map_scene.scanline(c,0);check(std::equal(native.begin(),native.end(),map_scene.render(c,native).pixels.begin()),"SML2 unsupported boss player form was colored");eram[0x216]=source_power;eram[0x269]=0;map_scene.scanline(c,0);check(std::equal(native.begin(),native.end(),map_scene.render(c,native).pixels.begin()),"SML2 boss assets leaked into a different stage");}
 }
 // Native Game Over can be preparing a later plaque pose before its OAM
 // upload. Paired DX records change presentation only; keep actual OAM.
 fixed_fixture(38);oam[0]=16;oam[1]=68;oam[2]=0x50;
 NativeMods game_over(sha,dx_feature,false);game_over.viewport(160,144);game_over.scanline(c,0);auto plaque=game_over.render(c,native);std::vector<uint32_t> plaque_pixels(plaque.pixels.begin(),plaque.pixels.end());
 rom[0x41c6]=0;rom[0x41c7]=0x50;rom[0x5000]=rom[0x5001]=rom[0x5003]=0;rom[0x5002]=0x50;rom[0x5004]=0x80;
 hram[0x44]=1;hram[0x45]=68;hram[0x46]=0xe3;c.rom_bank=1;c.instruction_pc=0x52b8;c.pc=0x52ba;
 check(game_over.read(c,0xffc5,68)==68,"SML2 Game Over changed a guest emitter read");game_over.scanline(c,0);
 check(std::equal(plaque_pixels.begin(),plaque_pixels.end(),game_over.render(c,native).pixels.begin()),"SML2 future Game Over emitter moved the actual OAM plaque");
 fixed_fixture(26);eram[0x216]=3;eram[0x269]=19;eram[0x28b]=240;eram[0xfc0]=4;NativeMods boss_hud(sha,dx_feature,false);boss_hud.viewport(160,144);boss_hud.scanline(c,0);auto opening_hud=boss_hud.render(c,native);std::vector<uint32_t> opening_pixels(opening_hud.pixels.begin(),opening_hud.pixels.end());ppu.wx=7;boss_hud.scanline(c,0);check(std::equal(opening_pixels.begin(),opening_pixels.end(),boss_hud.render(c,native).pixels.begin()),"SML2 settled boss HUD inherited the original entrance wipe offset");
 for(auto binding:{std::array<unsigned,2>{15,5},{17,11},{18,13},{18,14},{19,16},{28,1},{28,2},{28,3},{28,4}}){
  auto scene=binding[0];fixed_fixture(scene);eram[0x2dd]=uint8_t(sml2_color::fixed_phases[scene]);if(sml2_color::fixed_bosses[scene]){eram[0x216]=sml2_color::fixed_phases[scene]==0?0:3;eram[0x269]=24;eram[0xfc0]=8;}
  NativeMods alias(sha,dx_feature,false);alias.viewport(160,144);alias.scanline(c,0);auto source=alias.render(c,native);std::vector<uint32_t> source_pixels(source.pixels.begin(),source.pixels.end());eram[0x2dd]=binding[1];alias.scanline(c,0);check(std::equal(source_pixels.begin(),source_pixels.end(),alias.render(c,native).pixels.begin()),"SML2 proven shared ending phase lost its source presentation");eram[0x2dd]=20;alias.scanline(c,0);check(std::equal(native.begin(),native.end(),alias.render(c,native).pixels.begin()),"SML2 ending assets leaked into an unsupported phase");
 }
 // Protected real final defeats retain Big, Bunny and Fire. Stage24 atlases
 // and the observed DX lead VRAM/palettes share this existing source record.
 for(unsigned power:{1u,2u,3u}){
  fixed_fixture(28);eram[0x216]=power;eram[0x269]=24;eram[0xfc0]=8;eram[0x2dd]=0;
  NativeMods lead(sha,dx_feature,false);lead.viewport(160,144);lead.scanline(c,0);auto view=lead.render(c,native);check(!std::equal(native.begin(),native.end(),view.pixels.begin()),"SML2 reachable non-Small ending lead lost its DX presentation");auto state=lead.snapshot();lead.restore(state);check(lead.snapshot()==state,"SML2 reachable non-Small ending lead failed current-load");
 }
 fixed_fixture(28);eram[0x216]=0;eram[0x269]=24;eram[0xfc0]=8;eram[0x2dd]=0;
 auto off_color=bg[4];bg[4]=0xffffffff;std::array<uint32_t,160*144> loader_native;loader_native.fill(bg[4]);
 NativeMods loader(sha,dx_feature,false);loader.viewport(160,144);loader.scanline(c,0);auto loader_state=loader.snapshot();
 check(!std::equal(loader_native.begin(),loader_native.end(),loader.render(c,loader_native).pixels.begin()),"SML2 ending lead lost its intentional DX color during the DMG white fade");
 eram[0x2dd]=5;ppu.lcdc&=uint8_t(~32);check(std::equal(loader_native.begin(),loader_native.end(),loader.render(c,loader_native).pixels.begin()),"SML2 ending loader recolored the stale door room over its actual blank");check(loader.snapshot()==loader_state,"SML2 loader blank composition mutated state");bg[4]=off_color;
 fixed_fixture(18);eram[0x2dd]=12;NativeMods finale(sha,dx_feature,false);finale.viewport(160,144);finale.scanline(c,0);auto finale_view=finale.render(c,native);std::vector<uint32_t> finale_pixels(finale_view.pixels.begin(),finale_view.pixels.end());
 ppu.scx=13;finale.scanline(c,40);auto wave=finale.render(c,native);std::vector<uint32_t> wave_pixels(wave.pixels.begin(),wave.pixels.end());check(!std::equal(finale_pixels.begin()+40*160,finale_pixels.begin()+41*160,wave_pixels.begin()+40*160),"SML2 live finale scroll did not move the observed row");check(std::equal(finale_pixels.begin(),finale_pixels.begin()+40*160,wave_pixels.begin())&&std::equal(finale_pixels.begin()+41*160,finale_pixels.end(),wave_pixels.begin()+41*160),"SML2 finale row affected unobserved scanlines");
 ppu.scx=0;ppu.lcdc^=0x10;finale.scanline(c,80);auto split=finale.render(c,native);check(!std::equal(wave_pixels.begin()+80*160,wave_pixels.begin()+81*160,split.pixels.begin()+80*160),"SML2 finale ignored the live signed/unsigned tile split");auto finale_state=finale.snapshot();finale.restore(finale_state);check(finale.snapshot()==finale_state,"SML2 finale raster state failed current-load");
 fixed_fixture(20);eram[0x2dd]=18;ppu.lcdc^=0x10;for(unsigned y=12;y<18;y++)for(unsigned x=0;x<20;x++){unsigned cell=((ppu.lcdc&8)?0x1c00:0x1800)+((ppu.scy+y*8)&255)/8*32+((ppu.scx+x*8)&255)/8;vram[cell]^=0x3f;}NativeMods credits(sha,dx_feature,false);credits.viewport(160,144);credits.scanline(c,0);check(!std::equal(native.begin(),native.end(),credits.render(c,native).pixels.begin()),"SML2 live credit letters or signed/unsigned split prevented castle recognition");
 fixed_fixture(20);eram[0x2dd]=18;ppu.lcdc|=0x10;
 for(unsigned y=14;y<18;y++)for(unsigned x=0;x<32;x++)vram[((ppu.lcdc&8)?0x1c00:0x1800)+((ppu.scy+y*8)&255)/8*32+x]=17;
 auto original_bg=bg;for(unsigned shade=0;shade<4;shade++)bg[shade]=0xff000000u|((255-shade*85)*0x010101u);
 NativeMods font(sha,dx_feature,false);font.viewport(160,144);font.scanline(c,0);auto font_state=font.snapshot();
 std::array<uint32_t,160*144> glyph_native,unknown_native;glyph_native.fill(0xffffffff);glyph_native[120*160+80]=0xff000000;unknown_native.fill(0xff123456);
 auto glyph=font.render(c,glyph_native);check(glyph.pixels[120*160+79]==0xff000000&&glyph.pixels[120*160+80]==0xffffffff,"SML2 verified font layer lost authoritative LLE glyph geometry");check(font.snapshot()==font_state,"SML2 font composition mutated state");
 auto unknown=font.render(c,unknown_native);std::vector<uint32_t> composed(unknown.pixels.begin(),unknown.pixels.end());oam[0]=136;oam[1]=88;
 check(std::equal(composed.begin(),composed.end(),font.render(c,glyph_native).pixels.begin()),"SML2 font projection covered a live native object");oam[0]=oam[1]=0;
 vram[17*16]^=1;font.scanline(c,0);unknown=font.render(c,unknown_native);composed.assign(unknown.pixels.begin(),unknown.pixels.end());check(std::equal(composed.begin(),composed.end(),font.render(c,glyph_native).pixels.begin()),"SML2 font projection froze a changed live texture");bg=original_bg;
 // Actual FIFO provenance disambiguates portrait/font colors without moving
 // native glyphs. Pending texels and emitted history must survive a mid-row load.
 fixed_fixture(20);eram[0x2dd]=18;ppu.lcdc|=0x10;NativeMods provenance(sha,dx_feature,false);provenance.viewport(160,144);provenance.scanline(c,0);
 check(provenance.pixel_sink(120).output&&!a.pixel_sink(120).output&&!disabled.pixel_sink(120).output,"SML2 pixel observation escaped its qualified staff scene");
 unsigned map=0x1c00+15*32,t=17;vram[map]=uint8_t(t);auto attr=sml2_color::fixed_attributes[20*2048+map-0x1800];auto texture=sml2_color::fixed_texture(20,(attr&8)!=0,t*16);unsigned py=(attr&0x40)?7:0,bit=(attr&0x20)?0:7;
 unsigned color=((texture[py*2]>>bit)&1)|(((texture[py*2+1]>>bit)&1)<<1);auto palette=sml2_color::fixed_bg_palettes+20*64+(attr&7)*8+color*2;unsigned rgb15=palette[0]|(palette[1]<<8);auto rgbchannel=[](unsigned x){x&=31;return (x<<3)|(x>>2);};uint32_t provenance_expected=0xff000000u|(rgbchannel(rgb15)<<16)|(rgbchannel(rgb15>>5)<<8)|rgbchannel(rgb15>>10);
 provenance.background_row(c,{uint16_t(map),uint16_t(t*16),uint8_t(t),vram[t*16],vram[t*16+1],0});auto pending=provenance.snapshot();provenance.restore(pending);provenance.pixel_sink(120).output[80]=provenance.pixel_sink(120).colors[0];auto emitted=provenance.snapshot();check(provenance.render(c,unknown_native).pixels[120*160+80]==provenance_expected,"SML2 source texel lost its actual emitted position/color");provenance.restore(emitted);check(provenance.snapshot()==emitted,"SML2 emitted history failed current-load");
 provenance.scanline(c,0);check(provenance.pixel_sink(120).output[80]==provenance_expected,"SML2 line0 discarded still-published staff provenance");
 provenance.framebuffer_cleared();check(provenance.pixel_sink(120).output[80]==0,"SML2 native framebuffer clear kept stale source colors");provenance.restore(emitted);
 provenance.pixel_sink(120).output[80]=0;auto uncovered=provenance.render(c,unknown_native).pixels[120*160+80];check(!provenance.pixel_sink(111).output&&!provenance.pixel_sink(144).output&&!disabled.pixel_sink(120).output,"SML2 sink escaped its supported rows/profile");
 provenance.background_row(c,{uint16_t(map),uint16_t(t*16),uint8_t(t),uint8_t(vram[t*16]^1),vram[t*16+1],0});provenance.pixel_sink(120).output[80]=provenance.pixel_sink(120).colors[0];check(provenance.render(c,unknown_native).pixels[120*160+80]==uncovered,"SML2 changed fetched data reused a frozen texel");
 auto invalid_color=emitted;uint32_t invalid_rgb=0x01123456;std::memcpy(invalid_color.data()+invalid_color.size()-4,&invalid_rgb,4);rejects([&]{provenance.restore(invalid_color);},"SML2 malformed emitted color accepted");
 fixed_fixture(9);NativeMods raster(sha,dx_feature,false);raster.viewport(160,144);raster.scanline(c,0);auto castle=raster.render(c,native);std::vector<uint32_t> castle_pixels(castle.pixels.begin(),castle.pixels.end());
 check(sml2_color::castle_object_attributes[0x40]==3&&sml2_color::castle_object_attributes[512+0x40]==0x85,"SML2 castle bolt/foreground shared tile lost its original priority binding");
 ppu.scx=183;raster.scanline(c,0);check(std::equal(castle_pixels.begin(),castle_pixels.end(),raster.render(c,native).pixels.begin()),"SML2 castle body inherited the line0 cloud scroll");
 eram[0x78a]=13;eram[0x78b]=7;eram[0x78c]=19;raster.scanline(c,0);auto clouds=raster.render(c,native);std::vector<uint32_t> cloud_pixels(clouds.pixels.begin(),clouds.pixels.end());check(!std::equal(castle_pixels.begin(),castle_pixels.begin()+15*160,cloud_pixels.begin()),"SML2 live cloud band did not scroll");check(std::equal(castle_pixels.begin()+31*160,castle_pixels.end(),cloud_pixels.begin()+31*160),"SML2 cloud bands shifted the static castle body");auto cloud_state=raster.snapshot();raster.restore(cloud_state);check(raster.snapshot()==cloud_state&&std::equal(cloud_pixels.begin(),cloud_pixels.end(),raster.render(c,native).pixels.begin()),"SML2 cloud scroll state failed current-load replay");
 eram[0x797]=1;ppu.bgp=0x1e;raster.scanline(c,0);auto flash=raster.render(c,native);std::vector<uint32_t> flash_pixels(flash.pixels.begin(),flash.pixels.end());check(flash_pixels!=cloud_pixels&&!std::equal(flash_pixels.begin(),flash_pixels.end(),native.begin()),"SML2 actual castle lightning fell back or kept its ordinary palette");
 auto flash_state=raster.snapshot();raster.restore(flash_state);check(raster.snapshot()==flash_state&&std::equal(flash_pixels.begin(),flash_pixels.end(),raster.render(c,native).pixels.begin()),"SML2 lightning palette failed current-load");
 eram[0x797]=2;ppu.bgp=0xe1;raster.scanline(c,0);check(std::equal(flash_pixels.begin(),flash_pixels.end(),raster.render(c,native).pixels.begin()),"SML2 lightning followed BGP rather than the original effect state");
 eram[0x797]=0;ppu.bgp=0x1e;raster.scanline(c,0);check(std::equal(cloud_pixels.begin(),cloud_pixels.end(),raster.render(c,native).pixels.begin()),"SML2 cleared lightning retained its flash palette");
 fixed_fixture(3);ppu.bgp=0x1e;raster.scanline(c,0);check(std::equal(native.begin(),native.end(),raster.render(c,native).pixels.begin()),"SML2 castle lightning guard leaked into another submap");
#endif
 std::cout<<"SML2 target/model/package, real margins/entities/HUD, operand guards, spawn ramp, Fit and state passed\n";return 0;
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
