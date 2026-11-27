#include "../src/native_mods.hpp"
#include "../src/oracle_native_mods.hpp"
#include <cstddef>
#include <cstring>
#include <algorithm>
#include <iostream>
#include <stdexcept>
static void check(bool v,const char* m){if(!v)throw std::runtime_error(m);}
template<class F>static void rejects(F f){bool caught=false;try{f();}catch(const std::exception&){caught=true;}check(caught,"invalid Oracle profile/state accepted");}
int main(int argc,char** argv){try{
 using namespace gbcllr;check(argc==3,"two Oracle manifests required");
 const char* hashes[]={"0b56b78a9e45452e98c33edd111234931f1e034dc097f6f23082eb8db6055474","862a51368fb30539279d336b3fe193b43876d2cb15c87a36f5da517804ab3971"};
 for(unsigned variant=0;variant<2;++variant){
  auto package=read_mod_package(argv[variant+1],hashes[variant]);std::array f{package.feature};
  check(package.default_enabled&&f[0].view_width==-1,"fresh Oracle Fit default differs");
  rejects([&]{NativeMods bad(hashes[variant],f,false);});rejects([&]{NativeMods bad(std::string(64,'0'),f,true);});
  NativeMods a(hashes[variant],f),copy(hashes[variant],f),other(hashes[variant^1],f);a.viewport(1280,720);
  std::vector<uint8_t> rom(0x80000),ram(0x8000),vram(0x4000),hram(0x80),oam(160);
  std::array<uint32_t,32> bg{},obj{};bg[1]=0xff001122;obj[2]=0xffaabbcc;
  ModPpu p{0,0,0x97,0,0,nullptr,nullptr,bg.data(),obj.data()};ModContext c{};
  c.rom=rom.data();c.rom_size=rom.size();c.wram=ram.data();c.vram=vram.data();c.hram=hram.data();c.oam=oam.data();c.ppu=&p;
  ram[0x2ee]=2;ram[0x2ef]=1;ram[0xd00]=1;ram[0xd04]=2;ram[0xd0a]=30;ram[0xd0b]=22;
  std::fill_n(ram.data()+0xf00,256,1);for(unsigned sub=0;sub<4;++sub)ram[0x3008+sub]=1;
  for(unsigned y=0;y<8;++y){vram[16+y*2]=255;vram[32+y*2+1]=255;}
  // An actual metasprite outside the hardware camera must be visible once,
  // while every pixel of the original160x144 presentation stays authoritative.
  ram[0x1080]=1;ram[0x109a]=128;ram[0x108d]=210;ram[0x108b]=50;ram[0x109f]=0x40;
  size_t at=(variant?0x13:0x14)*0x4000;rom[at]=1;rom[at+3]=3;
  std::vector<uint32_t> native(160*144,0xff123456);a.scanline(c,0);auto view=a.render(c,native);
  check(view.width==256&&view.expanded,"Oracle room did not expand");
  check(view.pixels[40*256+240]==bg[1],"margin terrain differs from native room layout");
  check(view.pixels[50*256+250]==obj[2],"off-camera original metasprite missing");
  for(unsigned y=0;y<144;++y)check(std::equal(native.begin()+y*160,native.begin()+(y+1)*160,view.pixels.begin()+y*256+48),"Oracle LLE center/HUD changed");
  // Camera motion sets bit7 of the native scroll mode during ordinary large
  // room gameplay. It must not collapse the adaptive view into a transition.
  ram[0xd00]=0x81;a.scanline(c,0);check(a.render(c,native).expanded,"ordinary camera motion lost expanded view");ram[0xd00]=1;
  auto before=a.snapshot();copy.restore(before);check(copy.snapshot()==before,"Oracle state roundtrip differs");
  rejects([&]{other.restore(before);});auto corrupt=before;corrupt[16]=2;rejects([&]{a.restore(corrupt);});check(a.snapshot()==before,"bad Oracle state mutated live state");
  // Current active room: a160px room cannot own an80px camera.
  corrupt=before;corrupt[24]=80;corrupt[32]=160;corrupt[36]=128;
  rejects([&]{a.restore(corrupt);});check(a.snapshot()==before,"inconsistent small-room load mutated live state");
  corrupt=before;corrupt[16+offsetof(OracleWorldState,mode)]=5;rejects([&]{a.restore(corrupt);});
  corrupt=before;corrupt[16+offsetof(OracleWorldState,native_x)]=161;rejects([&]{a.restore(corrupt);});check(a.snapshot()==before,"bad crossing mode/camera mutated live state");
  ram[0xba0]=1;a.scanline(c,0);check(a.render(c,native).expanded,"dialogue collapsed the expanded field");
  ram[0xbcb]=1;ram[0xbcc]=1;a.scanline(c,0);check(!a.render(c,native).expanded,"menu lost its native presentation");ram[0xbcb]=0;ram[0xba0]=0;
  ram[0x2ee]=1;ram[0x2ef]=0;a.scanline(c,0);
  check(a.render(c,native).expanded,"area loader collapsed the expanded field");ram[0x2ee]=2;ram[0x2ef]=1;
  for(unsigned address=0;address<65536;++address)check(a.read(c,uint16_t(address),uint8_t(address))==uint8_t(address),"Oracle adapter changed a bus read");
  // Supplied room data checks native boundary authorization. Actual entry
  // adoption is qualified against original callers in oracle-world-qa.
  auto world=std::make_unique<OracleWorldState>();world->mode=1;
  unsigned group=variant?0xc49:0xc2d,room=variant?0xc4c:0xc30;
  auto& source=world->active;source.valid=1;source.room_width=160;source.room_height=128;
  source.ram[variant?0xc55:0xc39]=255;source.ram[group]=3;source.ram[room]=0xf8;world->rooms[0]=source;world->rooms[0].ram[room]=0xf9;
  // Native room reloads may retain an A-button registration for a deleted
  // NPC. It must not prevent an otherwise ordinary empty-room crossing.
  unsigned registry=variant?0xcca:0xcb3;world->rooms[0].ram[registry]=0xd2;world->rooms[0].ram[registry+1]=0x71;
  auto* shot=world->rooms[0].ram.data()+0x10c0;shot[0]=1;shot[1]=0x18;shot[4]=1;shot[6]=0x31;
  auto payload=a.snapshot();std::memcpy(payload.data()+16,world.get(),sizeof *world);a.restore(payload);
  // A native menu entered from a connected world must copy its complete
  // original raster, even when the retained field used a free camera.
  ram[0xbcb]=1;ram[0xbcc]=0;ram[0x4ab]=1;a.scanline(c,0);
  check(a.render(c,native).expanded,"opening-menu field fade collapsed");
  ram[0xbcc]=1;ram[0x4ab]=0;a.scanline(c,0);auto menu=a.render(c,native);
  check(!menu.expanded,"native inventory retained world mode");
  for(unsigned y=0;y<144;++y)check(std::equal(native.begin()+y*160,native.begin()+(y+1)*160,menu.pixels.begin()+y*256+48),"native inventory body disappeared after world camera");
  ram[0xbcc]=2;ram[0x4ab]=1;a.scanline(c,0);
  check(!a.render(c,native).expanded,"closing inventory lost native layout");
  ram[0xbcc]=3;a.scanline(c,0);
  check(a.render(c,native).expanded,"returning-field menu fade collapsed");
  a.restore(payload);ram[0xbcb]=0;ram[0xbcc]=0;ram[0x4ab]=0;

  std::fill(ram.begin(),ram.end(),0);ram[0x2ee]=2;ram[0x2ef]=1;ram[0xd00]=1;ram[0xd04]=2;
  ram[0xd0a]=20;ram[0xd0b]=16;ram[variant?0xc55:0xc39]=255;ram[group]=3;ram[room]=0xf8;ram[group-1]=0xd0;
  ram[0x1000]=1;ram[0x1004]=1;ram[0x100d]=159;ram[0x1200]=1;ram[0x1201]=7;ram[0x120d]=174;
  unsigned table=variant?0x5db5:0x5f6f;for(unsigned n=0;n<8;++n){rom[table+n*2]=0;rom[table+n*2+1]=0x60;}
  c.rom_bank=1;c.instruction_pc=0x4137;c.pc=0x413a;
  check(a.read(c,0xcd0c,154)==159,"walking with an active item lost the ordinary boundary path");ram[0x1200]=0;
  check(a.read(c,0xcd0c,154)==159,"ordinary right seam did not use native boundary path");
  ram[0x1004]=0x0e;check(a.read(c,0xcd0c,154)==154,"native portal Link state was overridden");ram[0x1004]=1;
  ram[variant?0xc77:0xc5c]=1;check(a.read(c,0xcd0c,154)==154,"native airborne route was overridden");ram[variant?0xc77:0xc5c]=0;
  rom[0x6000]=0xf8;rom[0x6001]=0;c.instruction_pc=0x4132;c.pc=0x4134;
  check(a.read(c,0x4133,5)==5,"source table-selected maze route was overridden");rom[0x6000]=0;
  c.instruction_pc=variant?0x5d8b:0x5f45;ram[0xd00]=4;ram[0xd04]=3;ram[0xd02]=1;
  check(a.read(c,0xcd00,4)==4,"original entry loader was replaced");
  ram[room]=0xf9;ram[0xd00]=8;ram[0xd04]=5;ram[0xd05]=2;
  ram[0x11c0]=1;ram[0x11c1]=0x1d;ram[0x1140]=1;ram[0x1141]=0xcc;
  auto original_ram=ram;std::array<uint16_t,6> dispatch{0,0,0x7700,0x1234,0xc201,0};
  ram[0x200]=7;ram[0x201]=0x40;original_ram=ram;
  for(unsigned pc:std::array<unsigned,6>{0x400a,variant?0x42cau:0x430cu,variant?0x437du:0x43acu,variant?0x1a9cu:0x1ad6u,0x9f,variant?0x5d08u:0x5ea3u}) {
   c.instruction_pc=uint16_t(pc);c.pc=uint16_t(pc+1);auto original_cpu=dispatch;
   check(a.oracle_cpu_read(c,0xc200,7,dispatch)==7&&dispatch==original_cpu&&ram==original_ram,"retired phase bridge changed original CPU/RAM continuation");
  }
  c.instruction_pc=variant?0x44cb:0x44fa;
  check(a.read(c,0xcd06,0)==0&&ram==original_ram,"retired finalizer changed original continuation");
  c.rom_bank=0;c.instruction_pc=(variant?0x960:0x984)+1;c.pc=c.instruction_pc+2;
  check(a.read(c,0xc49d,0)==0&&ram==original_ram,"retired VBlank adoption changed owner state");
  auto untouched=a.snapshot();std::memcpy(world.get(),untouched.data()+16,sizeof *world);
  check(world->handoffs==0&&!world->handoff_stage,"synthetic reads manufactured an adoption");
  // This supplied post-adoption fixture tests ingress only, not entry effects.
  world->handoffs=1;world->handoff_room=0xf9;world->handoff_direction=1;
  auto adopted=untouched;check(adopted.size()>=16+sizeof *world,"Oracle fixture payload is truncated");
  std::memcpy(adopted.data()+16,world.get(),sizeof *world);a.restore(adopted);
  copy.restore(adopted);check(copy.snapshot()==adopted,"current handoff state roundtrip differs");
  ram[0x11c0]=0;ram[0x1140]=0;ram[0x1200]=0;ram[0xd00]=1;ram[0xd04]=2;
  // The original screen inset must not push Link several pixels inward after
  // a successful open-edge handoff. Native collision remains authoritative.
  ram[0x1009]=8;ram[0x100b]=72;ram[0x100d]=1;c.rom_bank=1;
  c.instruction_pc=0x4132;c.pc=0x4134;
  check(a.read(c,0x4133,5)==0,"adopted open-edge ingress retained native screen inset");
  ram[0xe40]=0x0f;
  check(a.read(c,0x4133,5)==5,"native destination border collision was bypassed");ram[0xe40]=0;
  ram[0x100d]=0;c.instruction_pc=0x4134;c.pc=0x4135;c.hl=0xd00d;
  std::array<uint16_t,6> ingress_cpu{};
  check(a.oracle_cpu_read(c,0xd00d,0,ingress_cpu)==1&&ram[0x100d]==0,"zero-edge compare changed native position");
  ram[0x1009]=24;c.instruction_pc=0x4132;c.pc=0x4134;
  check(a.read(c,0x4133,5)==5,"outward border route lost native handling");
  world->active.ram[room]=0xf9;world->handoff_room=0xf8;world->handoff_direction=3;
  auto reverse=adopted;check(reverse.size()>=16+sizeof *world,"Oracle fixture payload is truncated");
  std::memcpy(reverse.data()+16,world.get(),sizeof *world);a.restore(reverse);
  ram[room]=0xf8;ram[0x100d]=160;c.instruction_pc=0x4137;c.pc=0x413a;
  check(a.read(c,0xcd0c,154)==160,"adopted leftward ingress retained native screen inset");
  ram[0xe49]=0x0f;
  check(a.read(c,0xcd0c,154)==154,"native right border collision was bypassed");ram[0xe49]=0;
  // Vertical ingress must remove only the legacy inset, keeping native
  // collision and outward routes intact at both ends of an adopted edge.
  std::memset(world.get(),0,sizeof *world);world->mode=1;world->handoffs=1;
  world->active.valid=1;world->active.room_width=160;world->active.room_height=128;
  world->active.ram[variant?0xc55:0xc39]=255;world->active.ram[group]=3;world->active.ram[room]=0x88;
  world->handoff_room=0x98;world->handoff_direction=2;
  auto vertical=adopted;std::memcpy(vertical.data()+16,world.get(),sizeof *world);a.restore(vertical);
  ram[room]=0x98;ram[0x100d]=72;ram[0x100b]=1;ram[0x1009]=16;
  c.instruction_pc=0x4125;c.pc=0x4127;
  check(a.read(c,0x4126,5)==0,"downward ingress kept original top inset");
  ram[0xe04]=15;check(a.read(c,0x4126,5)==5,"top edge collision was bypassed");ram[0xe04]=0;
  ram[0x100b]=0;c.instruction_pc=0x4127;c.pc=0x4128;c.hl=0xd00b;
  check(a.oracle_cpu_read(c,0xd00b,0,ingress_cpu)==1&&ram[0x100b]==0,"vertical zero compare changed native position");
  world->active.ram[room]=0x98;world->handoff_room=0x88;world->handoff_direction=0;
  std::memcpy(vertical.data()+16,world.get(),sizeof *world);a.restore(vertical);
  ram[room]=0x88;ram[0x100b]=128;ram[0x1009]=0;c.instruction_pc=0x412a;c.pc=0x412d;
  check(a.read(c,0xcd0d,122)==128,"upward ingress kept original bottom inset");
  ram[0xe74]=15;check(a.read(c,0xcd0d,122)==122,"bottom edge collision was bypassed");ram[0xe74]=0;
  // D1's actual 8x8 map puts room1A above room1C, rather than room0C.
  // Supply that native layout and require the larger doorway boundary path.
  std::memset(world.get(),0,sizeof *world);world->mode=1;
  world->active.valid=1;world->active.room_width=240;world->active.room_height=176;
  unsigned dungeon=variant?0xc55:0xc39;
  world->active.ram[group]=4;world->active.ram[room]=0x1c;
  world->active.ram[dungeon]=1;world->active.ram[dungeon+1]=51;
  world->active.ram[0x2c00+43]=0x1a;world->rooms[0]=world->active;
  world->rooms[0].ram[room]=0x1a;world->rooms[0].ram[dungeon+1]=43;
  auto mapped=adopted;std::memcpy(mapped.data()+16,world.get(),sizeof *world);a.restore(mapped);
  ram[group]=4;ram[room]=0x1c;ram[dungeon]=1;ram[dungeon+1]=51;
  ram[0xd0a]=30;ram[0xd0b]=22;ram[0x100b]=1;ram[0x100d]=120;
  c.instruction_pc=0x4125;c.pc=0x4127;
  check(a.read(c,0x4126,5)==0,"large room did not follow the native dungeon map");
  // A previewed incoming native shutter is open only while its adjoining
  // outgoing passage is passable. Rendering cannot edit either room's state.
  std::memset(world.get(),0,sizeof *world);world->mode=1;world->focus_x=152;world->focus_y=64;
  auto& near=world->active;near.valid=1;near.room_width=160;near.room_height=128;near.lcdc=0x97;
  near.ram[dungeon]=255;near.ram[group]=3;near.ram[room]=0xf8;near.ram[variant?0xc50:0xc34]=4;
  near.vram[16]=255;near.vram[32+1]=255;near.bg[1]=0xff001122;near.bg[2]=0xff445566;
  for(unsigned line=1;line<8;++line){near.vram[16+line*2]=255;near.vram[33+line*2]=255;}
  for(unsigned sub=0;sub<4;++sub)near.ram[0x3000+0xa0*8+sub]=1;
  auto& far=world->rooms[0];far=near;far.ram[room]=0xf9;far.ram[0xf40]=0x7b;far.ram[0xe40]=15;
  for(unsigned yy=8;yy<10;++yy)for(unsigned xx=0;xx<2;++xx)far.vram[0x1800+yy*32+xx]=2;
  unsigned doors=8*0x4000+(variant?0x474e:0x4818)-0x4000;
  for(unsigned n=0;n<16;++n){rom[doors+n*2]=0xa0;rom[doors+n*2+1]=uint8_t(0x70+n);}
  auto doorway=adopted;std::memcpy(doorway.data()+16,world.get(),sizeof *world);a.restore(doorway);
  auto drawn=a.render(c,native);auto door_pixel=size_t(80)*drawn.width+136;
  check(drawn.expanded&&drawn.pixels[door_pixel]==near.bg[1],"passable far-side shutter still appears closed");
  check(a.snapshot()==doorway,"doorway preview changed native room state");
  near.ram[0xe49]=15;std::memcpy(doorway.data()+16,world.get(),sizeof *world);a.restore(doorway);
  check(a.render(c,native).pixels[door_pixel]==near.bg[2],"blocked outgoing doorway was shown open");
  auto unowned=a.snapshot();auto invalid_item=unowned;
  auto bad_items=std::make_unique<OracleWorldState>();std::memcpy(bad_items.get(),invalid_item.data()+16,sizeof *bad_items);
  bad_items->items[7].room=257;std::memcpy(invalid_item.data()+16,bad_items.get(),sizeof *bad_items);
  rejects([&]{a.restore(invalid_item);});check(a.snapshot()==unowned,"invalid projectile ownership changed current state");
  auto bad_authority=unowned;std::memcpy(bad_items.get(),bad_authority.data()+16,sizeof *bad_items);bad_items->active.authority=4;
  std::memcpy(bad_authority.data()+16,bad_items.get(),sizeof *bad_items);rejects([&]{a.restore(bad_authority);});check(a.snapshot()==unowned,"invalid room authority changed current state");
  // No snapshot may resume the removed host scheduler/phase machinery.
  auto valid=a.snapshot();auto retired=valid;check(retired.size()>=16+sizeof *world,"Oracle fixture payload is truncated");
  std::memcpy(world.get(),retired.data()+16,sizeof *world);world->handoff_stage=1;
  std::memcpy(retired.data()+16,world.get(),sizeof *world);
  rejects([&]{a.restore(retired);});check(a.snapshot()==valid,"retired phase marker changed current state");

  // Closed doors keep native collision, while hiding the adjoining room is
  // a live host preference and cannot change deterministic game state.
  std::memset(world.get(),0,sizeof *world);world->mode=1;world->focus_x=232;world->focus_y=80;
  auto& closed=world->active;closed.valid=1;closed.room_width=240;closed.room_height=176;closed.lcdc=0x97;
  closed.ram[group]=4;closed.ram[room]=0x40;closed.ram[dungeon]=255;
  std::fill_n(closed.ram.data()+0xe00,256,15);std::fill_n(closed.ram.data()+0xf00,256,1);
  for(unsigned sub=0;sub<4;++sub)closed.ram[0x3008+sub]=1;
  for(unsigned y=0;y<8;++y)closed.vram[16+y*2]=255;
  std::fill_n(closed.vram.data()+0x1800,1024,1);
  closed.bg[1]=0xff001122;auto& hidden=world->rooms[0];hidden=closed;
  hidden.ram[room]=0x41;hidden.bg[1]=0xff445566;
  auto closed_state=valid;check(closed_state.size()>=16+sizeof *world,"Oracle closed-room fixture is truncated");
  std::memcpy(closed_state.data()+16,world.get(),sizeof *world);a.restore(closed_state);
  auto shown=a.render(c,native);
  check(shown.bound_right==480&&shown.pixels[80*256+255]==hidden.bg[1],"closed neighboring room is hidden by default");
  a.oracle_visibility(true);auto concealed=a.render(c,native);
  check(concealed.bound_right==240&&concealed.pixels[80*256+255]!=hidden.bg[1],"Hide closed rooms did not apply live");
  check(a.snapshot()==closed_state,"room visibility changed serialized game state");
  copy.preserve_host_presentation(a);copy.restore(closed_state);
  check(copy.render(c,native).bound_right==240,"room visibility was lost across state restore");
  a.oracle_visibility(false);check(a.render(c,native).bound_right==480,"room visibility could not be restored live");

 }
 std::cout<<"Oracle native layout/metasprite margins, unchanged reads/LLE center, exact profile/state, wide transitions and reciprocal doorway preview passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
