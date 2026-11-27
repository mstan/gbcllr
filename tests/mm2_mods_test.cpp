#include "../src/mm2_native_mods.hpp"
#include "../src/native_mods.hpp"
#include <array>
#include <iostream>
#include <stdexcept>
#include <vector>
using namespace gbcllr;
static void require(bool b,const char* m){if(!b)throw std::runtime_error(m);}
int main(){try{
 std::vector<uint8_t> rom(0x10000),ram(0x8000),vram(0x4000),hram(127),oam(160);
 std::array<uint32_t,32> colors{};colors[0]=0xffffffff;colors[1]=0xffaaaaaa;colors[2]=0xff555555;colors[3]=0xff000000;
 ModPpu ppu{};ppu.lcdc=0xe3;ppu.obp0=0xe4;ppu.bg_rgb=colors.data();ppu.obj_rgb=colors.data();
 ModContext c{};c.rom=rom.data();c.rom_size=rom.size();c.wram=ram.data();c.vram=vram.data();c.hram=hram.data();c.oam=oam.data();c.ppu=&ppu;c.rom_bank=3;c.wram_bank=1;c.sp=0xdff0;
 hram[0x16]=128;ram[0xfd0]=0x98;for(unsigned n=0;n<256;n++)ram[0xb00+n]=0x80;
 NativeFeature f;f.plugin="gbcllr.mm2.adaptive-view";f.view_width=256;f.no_flicker=true;f.weapon_cycle=true;f.smooth_gameplay=true;
 Mm2NativeMods mod(std::span<const NativeFeature>(&f,1),false);std::array<uint16_t,6> cpu{};auto physical=[&](){mod.write(c,0xff46,0xdf);for(unsigned line=0;line<144;line++)mod.scanline(c,uint8_t(line));mod.complete_frame(c,false);};cpu[5]=0x27f8;cpu[4]=0xdff0;mod.instruction(c,cpu,ram,hram);
 unsigned a=0xc000;for(unsigned n=0;n<48;n++){rom[a++]=0;rom[a++]=0;rom[a++]=2;rom[a++]=0;}
 cpu[5]=0x1ae9;mod.instruction(c,cpu,ram,hram);cpu[5]=0x1b4a;cpu[1]=48<<8;cpu[2]=0x4000;ram[0xf15]=48;ram[0xf16]=64;mod.instruction(c,cpu,ram,hram);cpu[5]=0x2825;mod.instruction(c,cpu,ram,hram);
 std::vector<uint32_t> native(160*144,0xffffffff);require(mod.render(c,native).width==160,"unlatched original pose became visible before line0");mod.write(c,0xff46,0xdf);mod.scanline(c,0);require(mod.render(c,native).width==160,"in-progress raster replaced the completed public frame");for(unsigned line=1;line<144;line++)mod.scanline(c,uint8_t(line));mod.complete_frame(c,false);auto view=mod.render(c,native);require(view.width==256&&view.sprites==48,"complete mapping capture failed");
 c.rom_bank=2;c.instruction_pc=0x4031;require(mod.read(c,0xff97,160)==0,"full renderer capacity not expanded");c.instruction_pc=0x4000;require(mod.read(c,0xff97,160)==160,"unrelated guest read changed");
 auto saved=mod.snapshot();mod.write(c,0xff46,0xdf);cpu[5]=0x27f8;mod.instruction(c,cpu,ram,hram);cpu[5]=0x2825;mod.instruction(c,cpu,ram,hram);for(unsigned line=0;line<144;line++)mod.scanline(c,uint8_t(line));mod.complete_frame(c,false);require(mod.render(c,native).sprites==48,"work completed after DMA replaced its selected pose");physical();require(mod.render(c,native).sprites==0,"omitted damage pose was retained");mod.restore(saved);require(mod.render(c,native).sprites==48,"pose replay failed");
 bool rejected=false;auto before=mod.snapshot();try{mod.restore(std::span<const uint8_t>(before).first(before.size()-1));}catch(const std::runtime_error&){rejected=true;}require(rejected&&before==mod.snapshot(),"invalid state not atomic");
 // Original LCDCE3 uses8x8 objects, including odd source tile numbers.
 rom[0xc100]=0;rom[0xc101]=0;rom[0xc102]=3;rom[0xc103]=0;vram[3*16]=128;ram[0xf0c]=10;c.rom_bank=3;
 cpu[5]=0x27f8;mod.instruction(c,cpu,ram,hram);cpu[5]=0x1ae9;mod.instruction(c,cpu,ram,hram);cpu[5]=0x1b4a;cpu[1]=1<<8;cpu[2]=0x4100;ram[0xf15]=48;ram[0xf16]=64;mod.instruction(c,cpu,ram,hram);cpu[5]=0x2825;mod.instruction(c,cpu,ram,hram);
 physical();auto geometry=mod.render(c,native);int x=geometry.camera_x+40-geometry.left;
 require(x>=0&&x<256&&geometry.pixels[48*256+x]==colors[1]&&geometry.pixels[56*256+x]==colors[0],"8x8 odd-tile object geometry differs");
 cpu[5]=0x27f8;mod.instruction(c,cpu,ram,hram);cpu[5]=0x1ae9;mod.instruction(c,cpu,ram,hram);cpu[5]=0x1b6d;cpu[1]=1<<8;cpu[2]=0x4100;mod.instruction(c,cpu,ram,hram);cpu[5]=0x2825;mod.instruction(c,cpu,ram,hram);
 physical();auto mirrored=mod.render(c,native);require(mirrored.pixels[48*256+x]==colors[1]&&mirrored.pixels[48*256+x-1]==colors[0],"original player mirror minus7 differs");mod.restore(saved);c.rom_bank=2;
 // The original D0..DF sensor interval is false-solid; larger eight-bit
 // values wrap columns. Preserve the original Y/solidity service at334F.
 ram[0xf0c]=10;ram[0xf2f]=0;hram[0x20]=0x94;hram[0x23]=0;hram[0x25]=216;ram[0xf0d]=220;ram[0xc01]=1;
 cpu[5]=0x3334;cpu[1]=0x5a00;mod.instruction(c,cpu,ram,hram);
 require(cpu[5]==0x334f&&(cpu[1]&255)==23,"far sensor did not retain its real terrain column");
 c.rom_bank=1;cpu[5]=0x3334;cpu[1]=0x5a77;mod.instruction(c,cpu,ram,hram);
 require(cpu[5]==0x3334&&cpu[1]==0x5a77,"player sensor path changed");c.rom_bank=2;
 ram[0xdf0]=0x94;ram[0xdf3]=28;ram[0xdf5]=48;ram[0xef0]=7;ram[0xf15]=48;
 cpu[2]=0xcdf0;cpu[5]=0x357f;mod.instruction(c,cpu,ram,hram);require(cpu[5]==0x35b3,"far actor contact page alias reached native damage");
 ram[0xdf3]=12;cpu[5]=0x357f;mod.instruction(c,cpu,ram,hram);require(cpu[5]==0x357f,"nearby native actor contact was bypassed");
 // Shot collision uses the directly visited CD slot and the actual weapon's
 // CFECh radius, before any actor workspace is copied to HRAM.
 ram[0xdf3]=28;ram[0xfec]=3;hram[0x25]=48;cpu[2]=0xcdf0;cpu[5]=0x3394;auto shot_cpu=cpu;mod.instruction(c,cpu,ram,hram);require(cpu[5]==0x33cf&&cpu[2]==shot_cpu[2],"far actor shot page alias reached native damage");
 ram[0xdf3]=12;cpu[5]=0x3394;mod.instruction(c,cpu,ram,hram);require(cpu[5]==0x3394,"nearby original weapon overlap was bypassed");
 // A native Buster crossing the modulo boundary remains in the expanded
 // view. Its true near hit must survive the original eight-bit X alias.
 mod.restore(saved);c.rom_bank=1;ram[0xfdf]=0;ram[0xf0c]=10;ram[0xf15]=48;ram[0xc01]=1;ram[0xc60]=128;ram[0xc65]=48;
 cpu[3]=0xcc60;cpu[5]=0x43e4;mod.instruction(c,cpu,ram,hram);hram[0x20]=128;
 for(unsigned x:{112u,176u,240u,248u}){hram[0x25]=uint8_t(x);cpu[5]=0x47bb;mod.instruction(c,cpu,ram,hram);require(cpu[5]==0x47c3,"original Buster cull deleted a visible margin shot");}
 ram[0xdf0]=0x94;ram[0xdf3]=25;ram[0xdf5]=0;ram[0xef0]=7;ram[0xfec]=3;cpu[2]=0xcdf0;cpu[5]=0x3394;mod.instruction(c,cpu,ram,hram);require(cpu[5]==0x3394,"true wrapped near hit was rejected");
 cpu[0]=0xf65a;cpu[5]=0x33ad;mod.instruction(c,cpu,ram,hram);require(cpu[0]==0x085a,"native X distance did not use the true eight-pixel overlap");
 ram[0xdf3]=41;cpu[2]=0xcdf0;cpu[5]=0x3394;mod.instruction(c,cpu,ram,hram);require(cpu[5]==0x33cf,"tracked shot hit a separate world page");mod.restore(saved);
 // Preserve native page/staging semantics when the source room is one screen.
 ram[0xc01]=0;ram[0xdf0]=0x94;ram[0xdf3]=28;ram[0xdf5]=48;ram[0xef0]=7;ram[0xfec]=3;hram[0x25]=48;cpu[2]=0xcdf0;cpu[5]=0x3394;auto narrow_cpu=cpu;mod.instruction(c,cpu,ram,hram);require(cpu==narrow_cpu,"single-screen native shot overlap was replaced");ram[0xc01]=1;
 ram[0xfdd]=1;cpu={0,0,0,0,0xdffe,0x27f8};mod.inputs(2,0);mod.inputs(2,0);mod.instruction(c,cpu,ram,hram);require(cpu[5]==0x39ef&&cpu[4]==0xdffc,"save-ammo service/lagged input edge not invoked");
 cpu[4]=0xdffe;cpu[5]=0x27f8;mod.instruction(c,cpu,ram,hram);require(cpu[5]==0x38e0&&ram[0xfdf]==1&&cpu[4]==0xdffa,"native selection service not invoked");
 f.view_width=160;f.no_flicker=false;Mm2NativeMods original(std::span<const NativeFeature>(&f,1),false);cpu[5]=0x27f8;original.instruction(c,cpu,ram,hram);cpu[5]=0x2825;original.instruction(c,cpu,ram,hram);original.write(c,0xff46,0xdf);for(unsigned line=0;line<144;line++)original.scanline(c,uint8_t(line));original.complete_frame(c,false);
 require(original.render(c,native).pixels.data()==native.data(),"native160 no-flicker opt-out replaced physical pixels");
 std::cout<<"Mega Man II producer, read-site, replay and continuation contracts passed\n";
 return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
