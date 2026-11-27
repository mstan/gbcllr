#include "../src/mm1_native_mods.hpp"
#include "../src/native_mods.hpp"
#include <array>
#include <iostream>
#include <stdexcept>
#include <vector>
using namespace gbcllr;
static void require(bool b,const char* m){if(!b)throw std::runtime_error(m);}
int main(){try{
 std::vector<uint8_t> rom(0x8000),ram(0x8000),vram(0x4000),hram(127),oam(160);
 std::array<uint32_t,32> colors{};colors[0]=0xffffffff;colors[1]=0xffaaaaaa;colors[2]=0xff555555;colors[3]=0xff000000;
 ModPpu ppu{};ppu.lcdc=0x83;ppu.obp0=0xe4;ppu.bg_rgb=colors.data();ppu.obj_rgb=colors.data();
 ModContext c{};c.rom=rom.data();c.rom_size=rom.size();c.wram=ram.data();c.vram=vram.data();c.hram=hram.data();c.oam=oam.data();c.ppu=&ppu;c.rom_bank=1;c.wram_bank=1;
 auto word=[&](unsigned a,unsigned v){ram[a-0xc000]=uint8_t(v);ram[a-0xc000+1]=uint8_t(v>>8);};
 ram[0x18c0]=64;ram[0x18c1]=8;ram[0x1f9b]=1;ram[0x1802]=1;word(0xd8c4,0x6000);word(0xd80f,80);word(0xd861,80);word(0xd863,944);
 for(unsigned n=0;n<8;n++)word(0xd877+n*2,0x4100+n*64);
 NativeFeature f;f.plugin="gbcllr.mm1.adaptive-view";f.view_width=256;f.no_flicker=true;f.weapon_cycle=true;f.smooth_gameplay=true;
 Mm1NativeMods mod(std::span<const NativeFeature>(&f,1),false);std::array<uint16_t,6> cpu{};auto physical=[&](){mod.write(c,0xff46,0xc0);for(unsigned line=0;line<144;line++)mod.scanline(c,uint8_t(line));mod.complete_frame(c,false);};cpu[5]=0x0a17;cpu[4]=0xfffc;mod.instruction(c,cpu,ram,hram);
 // 81 authored pieces exceed both hardware capacities. No OAM allocation is
 // needed for their complete native producer to remain visible.
 unsigned a=0x4000;rom[a++]=0;rom[a++]=0;rom[a++]=0x11;rom[a++]=2;
 for(unsigned n=0;n<80;n++){rom[a++]=0;rom[a++]=0;rom[a++]=2;}rom[a]=0x80;
 hram[0x22]=32;hram[0x23]=32;cpu[5]=0x0a05;mod.instruction(c,cpu,ram,hram);cpu[5]=0x2cf0;cpu[3]=0x4000;cpu[0]=0;mod.instruction(c,cpu,ram,hram);cpu[5]=0x0a05;mod.instruction(c,cpu,ram,hram);
 std::vector<uint32_t> native(160*144,0xffffffff);require(mod.render(c,native).width==160,"unlatched original pose became visible before line0");mod.write(c,0xff46,0xc0);mod.scanline(c,0);require(mod.render(c,native).width==160,"in-progress raster replaced the completed public frame");for(unsigned line=1;line<144;line++)mod.scanline(c,uint8_t(line));mod.complete_frame(c,false);auto view=mod.render(c,native);require(view.width==256&&view.sprites==81,"complete mapping capture failed");
 require(mod.cpu_budget(c,false)>0&&mod.cpu_budget(c,true)==0,"gameplay CPU budget boundary");
 auto gameplay=mod.snapshot();auto gameplay_cpu=cpu;c.rom_bank=14;cpu[5]=0x455b;auto ending_cpu=cpu;mod.instruction(c,cpu,ram,hram);require(mod.cpu_budget(c,false)==0&&cpu==ending_cpu,"original ending retained gameplay CPU headroom or changed CPU");c.rom_bank=1;mod.restore(gameplay);cpu[5]=0x62d1;ending_cpu=cpu;mod.instruction(c,cpu,ram,hram);require(mod.cpu_budget(c,false)==0&&cpu==ending_cpu,"weapon-get retained gameplay CPU headroom or changed CPU");mod.restore(gameplay);cpu=gameplay_cpu;
 auto saved=mod.snapshot();mod.write(c,0xff46,0xc0);cpu[5]=0x0a17;mod.instruction(c,cpu,ram,hram);cpu[5]=0x0a05;mod.instruction(c,cpu,ram,hram);for(unsigned line=0;line<144;line++)mod.scanline(c,uint8_t(line));mod.complete_frame(c,false);require(mod.render(c,native).sprites==81,"work completed after DMA replaced its selected pose");physical();require(mod.render(c,native).sprites==0,"intentional omitted pose was retained");mod.restore(saved);require(mod.render(c,native).sprites==81,"pose replay lost");
 auto before=mod.snapshot();bool rejected=false;try{mod.restore(std::span<const uint8_t>(before).first(before.size()-1));}catch(const std::runtime_error&){rejected=true;}require(rejected&&before==mod.snapshot(),"invalid state was not atomic");
 // Actual LCDC83 selects8x8 objects. Adjacent grid rows are8 pixels apart,
 // and odd tile3 must keep its own art rather than tile2's blank texture.
 const uint8_t grid[]{0,0,0x12,3,3,0x80};std::copy(std::begin(grid),std::end(grid),rom.begin()+0x4300);vram[3*16]=128;
 cpu[5]=0x0a17;mod.instruction(c,cpu,ram,hram);cpu[5]=0x2cf0;cpu[0]=0;cpu[1]=0;cpu[3]=0x4300;mod.instruction(c,cpu,ram,hram);cpu[5]=0x0a05;mod.instruction(c,cpu,ram,hram);
 vram[3*16]=64;physical();auto late=mod.render(c,native);require(late.pixels[32*256+33]==colors[1]&&late.pixels[32*256+32]==colors[0],"line0 did not latch newly uploaded original texture");vram[3*16]=128;
 physical();auto geometry=mod.render(c,native);require(geometry.pixels[32*256+32]==colors[1]&&geometry.pixels[40*256+32]==colors[1]&&geometry.pixels[48*256+32]==colors[0],"8x8 odd-tile/grid-row geometry differs");
 for(unsigned line=0;line<144;line++){ppu.lcdc=uint8_t(line==40?0x81:0x83);mod.scanline(c,uint8_t(line));}ppu.lcdc=0x83;mod.complete_frame(c,false);
 auto hidden=mod.render(c,native);require(hidden.pixels[32*256+32]==colors[1]&&hidden.pixels[40*256+32]==colors[0],"original per-line OBJ hiding was not preserved");mod.restore(saved);
 // The separate original1:6F4B hero writer precedes0A17. Nine hero tiles
 // must survive that boundary together with subsequent ordinary actor art.
 const uint8_t body[]{0,0,0x33,3,3,3,3,3,3,3,3,3,0x80};std::copy(std::begin(body),std::end(body),rom.begin()+0x4400);word(0xd80d,100);ram[0x1824]=80;
 cpu[5]=0x6f5d;cpu[3]=0x4400;mod.instruction(c,cpu,ram,hram);cpu[5]=0x0a17;mod.instruction(c,cpu,ram,hram);cpu[5]=0x2cf0;cpu[3]=0x4300;mod.instruction(c,cpu,ram,hram);cpu[5]=0x0a05;mod.instruction(c,cpu,ram,hram);
 physical();auto full=mod.render(c,native);require(full.sprites==11&&full.pixels[82*256+100]==colors[1],"reserved hero producer disappeared at actor boundary");mod.restore(saved);
 // Buster pellets have a dedicated tile65 writer. Its C2 slot coordinates
 // are hardware-relative, and the native copy must retain priority bit7.
 ram[0x244]=64;ram[0x248]=44;ram[0x180b]=128;vram[0x65*16]=128;cpu[5]=0x4388;cpu[1]=0x5a40;
 auto pellet_cpu=cpu;mod.instruction(c,cpu,ram,hram);require(cpu==pellet_cpu,"Buster capture changed CPU registers");cpu[5]=0x0a05;mod.instruction(c,cpu,ram,hram);physical();auto pellet=mod.render(c,native);
 require(pellet.sprites==1&&pellet.pixels[40*256+60]==colors[1],"dedicated Buster pellet was omitted or mispositioned");mod.restore(saved);
 // DMG priority follows the smaller X, independently of producer/OAM order.
 // An opaque higher-priority OBJ behind BG also hides lower-priority OBJs.
 const uint8_t one3[]{0,0,0x11,3,0x80},one4[]{0,0,0x11,4,0x80};std::copy(std::begin(one3),std::end(one3),rom.begin()+0x4500);std::copy(std::begin(one4),std::end(one4),rom.begin()+0x4510);
 vram[3*16]=255;vram[4*16+1]=255;cpu[0]=0;cpu[1]=0;cpu[5]=0x2cf0;cpu[3]=0x4500;hram[0x22]=44;hram[0x23]=32;ram[0x13ea]=0;mod.instruction(c,cpu,ram,hram);
 cpu[3]=0x4510;hram[0x22]=40;mod.instruction(c,cpu,ram,hram);cpu[5]=0x0a05;mod.instruction(c,cpu,ram,hram);physical();require(mod.render(c,native).pixels[32*256+44]==colors[2],"DMG smaller-X priority followed capture order");mod.restore(saved);
 vram[0x1000]=255;cpu[5]=0x2cf0;cpu[3]=0x4500;hram[0x22]=44;ram[0x13ea]=0;mod.instruction(c,cpu,ram,hram);cpu[3]=0x4510;hram[0x22]=40;ram[0x13ea]=128;mod.instruction(c,cpu,ram,hram);cpu[5]=0x0a05;mod.instruction(c,cpu,ram,hram);physical();require(mod.render(c,native).pixels[32*256+44]==colors[1],"behind-BG winning OBJ exposed a lower-priority OBJ");vram[0x1000]=0;ram[0x13ea]=0;mod.restore(saved);
 // Distinct real and cached columns expose a far-margin collision-ring wrap.
 c.sp=0xfffc;word(0xcc40,0xc600);word(0xc60a,216);ram[0x60d]=216;ram[0x61b]=1;rom[0x42cd]=0x22;
 cpu[5]=0x102a;cpu[2]=uint16_t(112<<8|216);mod.instruction(c,cpu,ram,hram);c.instruction_pc=0x1041;
 require(mod.read(c,0xde7d,0x11)==0x22,"far sensor read wrapped collision-ring terrain");
 cpu[5]=0x102a;word(0xc60a,100);ram[0x60d]=100;cpu[2]=uint16_t(112<<8|100);mod.instruction(c,cpu,ram,hram);
 require(mod.read(c,0xde76,0x35)==0x35,"central mutable terrain was replaced");c.instruction_pc=0x1042;
 require(mod.read(c,0xde76,0x36)==0x36,"unrelated collision-ring read changed");
 // Original player contact must reject the same modulo256 projection when
 // the actor is a full world page away; its native weapon path stays intact.
 word(0xd80d,128);ram[0x1821]=128;ram[0x1841]=120;ram[0x1842]=136;word(0xce53,0x6200);rom[0x6200]=248;rom[0x6201]=8;
 word(0xc60a,384);cpu[4]=0xfffc;hram[0x7c]=0;hram[0x7d]=0xc6;cpu[1]=0x1234;cpu[5]=0x2b0e;mod.instruction(c,cpu,ram,hram);require(cpu[5]==0x2b2a,"far contact page alias reached native damage");
 word(0xc60a,128);cpu[5]=0x2b0e;mod.instruction(c,cpu,ram,hram);require(cpu[5]==0x2b0e&&cpu[1]==0x1234,"nearby native contact was bypassed");
 // A genuine projectile must not hit an actor a full world page away. Keep
 // the original Y/damage handler once the actual world X overlap succeeds.
 mod.restore(saved);word(0xd80d,81);word(0xd80f,80);word(0xc60a,385);word(0xce53,0x6200);ram[0x239]=7;ram[0x23d]=129;cpu[1]=0xc239;cpu[5]=0x42cb;mod.instruction(c,cpu,ram,hram);
 hram[0x78]=0x3a;hram[0x79]=0x2b;hram[0x7a]=0;hram[0x7b]=0xc6;cpu[4]=0xfff8;cpu[5]=0x1093;mod.instruction(c,cpu,ram,hram);ram[0x205]=2;ram[0x206]=253;cpu[3]=0xc204;cpu[5]=0x10ae;mod.instruction(c,cpu,ram,hram);ram[0x22a]=0x44;
 cpu[3]=0xc20a;cpu[5]=0x10d0;auto shot_cpu=cpu;mod.instruction(c,cpu,ram,hram);require(cpu[5]==0x10e0&&cpu[3]==0xc20b&&cpu[0]==shot_cpu[0]&&cpu[1]==shot_cpu[1]&&cpu[2]==shot_cpu[2],"far shot page alias reached native damage or changed unrelated registers");
 word(0xc60a,129);cpu[3]=0xc20a;cpu[5]=0x10d0;mod.instruction(c,cpu,ram,hram);require(cpu[5]==0x10d7&&cpu[3]==0xc20b,"real near shot did not retain the original Y/damage service");mod.restore(saved);
 ram[0x1fa0]=0;ram[0x239]=7;ram[0x23d]=200;ram[0x241]=44;cpu[1]=0xc239;cpu[5]=0x42cb;mod.instruction(c,cpu,ram,hram);cpu[5]=0x4345;mod.instruction(c,cpu,ram,hram);require(cpu[5]==0x434e,"native Buster viewport cull removed a visible margin projectile");
 cpu[5]=0x4388;mod.instruction(c,cpu,ram,hram);cpu[5]=0x0a05;mod.instruction(c,cpu,ram,hram);physical();require(mod.render(c,native).pixels[40*256+196]==colors[1],"extended Buster art did not use its true world coordinate");mod.restore(saved);
 // HRAM stack continuation uses the cartridge's real selection/reset service.
 // A requested wide window must retain the native cull in a160-pixel domain.
 word(0xd861,80);word(0xd863,80);ram[0x1fa0]=0;ram[0x239]=7;ram[0x23d]=200;cpu[1]=0xc239;cpu[5]=0x4345;auto narrow_cpu=cpu;mod.instruction(c,cpu,ram,hram);require(cpu==narrow_cpu,"single-screen native projectile cull was replaced");
 ram[0x1fa0]=0;ram[0x1fa1]=3;cpu={0,0,0,0,0xfffe,0x0a17};mod.inputs(2,0);mod.inputs(2,0);mod.instruction(c,cpu,ram,hram);require(cpu[5]==0x40b7&&cpu[4]==0xfffc&&ram[0x1fa0]==1,"native weapon entry/lagged input edge");
 cpu[4]=0xfffe;cpu[5]=0x0a17;mod.instruction(c,cpu,ram,hram);require(cpu[5]==0x0a17&&cpu[4]==0xfffe,"native weapon return");
 f.view_width=160;f.no_flicker=false;Mm1NativeMods original(std::span<const NativeFeature>(&f,1),false);
 cpu[5]=0x0a05;original.instruction(c,cpu,ram,hram);original.write(c,0xff46,0xc0);for(unsigned line=0;line<144;line++)original.scanline(c,uint8_t(line));original.complete_frame(c,false);
 require(original.render(c,native).pixels.data()==native.data(),"native160 no-flicker opt-out replaced physical pixels");
 std::cout<<"Mega Man I producer, replay and continuation contracts passed\n";
 return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
