// Actual owned-game qualification of presentation-only neighbor simulation.
#include <gbcllr/machine.hpp>
#include "../src/oracle_native_mods.hpp"
#include "../src/oracle_graphics_data.hpp"
#include "../src/native_mods.hpp"
#define GB_INTERNAL
#define GB_DISABLE_DEBUGGER
#define GB_DISABLE_CHEATS
#define GB_DISABLE_CHEAT_SEARCH
#define GB_DISABLE_REWIND
#define GB_DISABLE_TIMEKEEPING
#include "../third_party/sameboy/Core/gb.h"
#include <algorithm>
#include <cstring>
#include <iostream>
#include <stdexcept>
namespace gbcllr {void execute_commands(Machine&,const std::filesystem::path&,bool*);}
static void check(bool v,const char* m){if(!v)throw std::runtime_error(m);}
static void graphics_data_check(char** argv){
 using namespace gbcllr;auto rom=read_file(argv[1]);Options options;options.model=Model::cgb;options.boot_rom=read_file(argv[2]);
 Machine original(rom,options);std::string error;auto input=read_file(argv[5]);check(original.restore(input,error),error.c_str());
 if(std::filesystem::path(argv[7]).filename()=="supplied-raw-control"){auto r=original.debug_registers();r.bc&=0xff3f;original.debug_registers(r);input=original.snapshot();}
 constexpr size_t core_at=4+64*3+8+8+1+4;uint32_t core_size=0;std::memcpy(&core_size,input.data()+core_at-4,4);
 check(core_size==offsetof(GB_gameboy_t,llr_reset_seed),"graphics data fixture ABI differs");
 auto core=std::make_unique<GB_gameboy_t>();std::memcpy(core.get(),input.data()+core_at,core_size);
 size_t ram_at=core_at+core_size+4,video_at=ram_at+32768+4;ModPpu ppu{};ppu.lcdc=core->io_registers[GB_IO_LCDC];auto before=original.debug_registers();bool layout=before.pc==0x7da||before.pc==0x7fe,memory=before.pc==0x451||before.pc==0x452||before.pc==0x475||before.pc==0x476;auto initial_svbk=core->io_registers[GB_IO_SVBK];auto initial_vbk=core->cgb_vram_bank;
 ModContext context{};context.rom=rom.data();context.rom_size=rom.size();context.wram=input.data()+ram_at;context.vram=input.data()+video_at;
 context.hram=core->hram;context.ppu=&ppu;context.pc=before.pc;context.rom_bank=core->mbc_rom_bank;context.wram_bank=core->cgb_ram_bank;
 auto decode=[&](const ModContext& c,Registers r,OracleGraphicsData& out){return memory?oracle_memory_data(c,r,initial_vbk,out):layout?oracle_layout_data(c,r,out):oracle_graphics_data(c,r,out);};OracleGraphicsData data;check(decode(context,before,data),"data service refused actual original caller");
 auto bad=before;if(memory){bad.hl=0x9fff;bad.bc=2;}else {bad.de=0x9ff1;bad.bc=0x7f00;}OracleGraphicsData guard;
 check(!decode(context,bad,guard),"data service accepted foreign destination extent");
 auto wrong=context;++wrong.pc;check(!decode(wrong,before,guard),"data service accepted foreign caller");
 unsigned caller=context.wram[before.sp-0xc000]|(unsigned(context.wram[before.sp+1-0xc000])<<8);
 bool done=false;for(unsigned n=0;n<1000000;++n){auto r=original.debug_registers();if(r.pc==caller&&r.sp==before.sp+2){done=true;break;}original.run_until(original.stats().cycles+1);}
 check(done,"original graphics caller exceeded instruction bound");auto after=original.snapshot();auto result=original.debug_registers();auto expected=data.registers;expected.pc=uint16_t(caller);expected.sp=uint16_t(before.sp+2);
 check(result.af==expected.af&&result.bc==expected.bc&&result.de==expected.de&&result.hl==expected.hl&&result.sp==expected.sp&&result.pc==expected.pc&&result.ime==expected.ime,"graphics data decoder return registers differ");
 auto output=std::vector<uint8_t>(input.begin()+video_at,input.begin()+video_at+16384);
 if(data.video)std::copy_n(data.bank.begin()+(data.destination-0x8000),data.size,output.begin()+(data.bank_selector*8192+data.destination-0x8000));
 check(std::equal(output.begin(),output.end(),after.begin()+video_at),"graphics data VRAM or protected neighboring bytes differ");
 auto ram=std::vector<uint8_t>(input.begin()+ram_at,input.begin()+ram_at+32768);
 if(!data.video)std::copy_n(data.bank.begin()+(data.destination-0xd000),data.size,ram.begin()+((data.bank_selector?data.bank_selector:1)*4096+data.destination-0xd000));
 for(unsigned at=0;at<32768;++at)if(at<0x180||at>=0x220)check(ram[at]==after[ram_at+at],"graphics data WRAM or protected neighboring bytes differ");
 std::memcpy(core.get(),after.data()+core_at,core_size);
 check(std::equal(data.hram.begin(),data.hram.end(),core->hram),"graphics data original HRAM scratch differs");
 if(core->mbc_rom_bank!=data.rom_bank||(memory?(core->io_registers[GB_IO_SVBK]!=initial_svbk||core->cgb_vram_bank!=initial_vbk):((core->io_registers[GB_IO_SVBK]&7)!=data.bank_selector||core->cgb_vram_bank!=bool(data.bank_selector&1))))
  throw std::runtime_error("graphics/memory bank latch output differs");
 write_file_atomic(std::string(argv[7])+".original-return.state",after);
 std::cout<<"{\"original_return_registers_hram_vram_banks\":\"exact\",\"protected_wram\":\"exact_except_inactive_stack\",\"extent_and_caller_guards\":\"pass\",\"entry\":"<<before.pc<<",\"mode\":"<<(layout?(core->hram[0xf]&128):(before.bc&0xc0))<<",\"bytes\":"<<data.size<<"}\n";
}
static void cold_data_check(char** argv){
 using namespace gbcllr;auto rom=read_file(argv[1]);Options options;options.model=Model::cgb;options.boot_rom=read_file(argv[2]);
 bool variant=sha256(rom)=="862a51368fb30539279d336b3fe193b43876d2cb15c87a36f5da517804ab3971";
 unsigned group_at=variant?0xc49:0xc2d,room_at=variant?0xc4c:0xc30,queue=variant?0xffa3:0xffa5;
 Machine reference(rom,options);std::string error;auto input=read_file(argv[5]),actual=read_file(argv[4]);check(reference.restore(input,error),error.c_str());
 constexpr size_t core_at=4+64*3+8+8+1+4;uint32_t core_size=0;std::memcpy(&core_size,input.data()+core_at-4,4);check(core_size==offsetof(GB_gameboy_t,llr_reset_seed),"cold fixture ABI differs");
 auto core=std::make_unique<GB_gameboy_t>();std::memcpy(core.get(),input.data()+core_at,core_size);auto in=reference.debug_registers();
 check(in.pc==(variant?0x5892:0x5a4f)&&in.sp==0xc21e&&core->hram[variant?0x1c:0x1e]==0xe8&&core->mbc_rom_bank==1,"cold fixture lacks original foreground caller");
 auto write=[&](unsigned at,uint8_t value){std::array<uint8_t,1> data{value};reference.debug_write(uint16_t(at),data);};
 write(0xff40,uint8_t(reference.debug_read(0xff40,1)[0]&127));unsigned calls=0;
 auto invoke=[&](unsigned pc,unsigned bank){
  auto regs=reference.debug_registers();write(in.sp,7);write(in.sp+1,0x40);regs.sp=in.sp;regs.ime=0;
  // Loading starts in its real foreground context. The later original
  // bank-call wrapper establishes the callee register/stack/bank contract;
  // the reference executes every decoder instruction, without host services.
  if(!calls)regs.pc=uint16_t(pc);else {regs.pc=0x8a;regs.hl=uint16_t(pc);regs.de=uint16_t((regs.de&0xff00)|bank);}
  reference.debug_registers(regs);reference.debug_wake();
  unsigned previous=pc;for(unsigned n=0;n<1000000;++n){auto r=reference.debug_registers();if(r.pc==0x4007&&r.sp==in.sp+2){++calls;return;}if(r.pc==0x8c7||r.pc==0x8da||r.pc==0x8dc){auto stack=reference.debug_read(r.sp,2);throw std::runtime_error("cold native yield phase"+std::to_string(pc)+" caller"+std::to_string(stack[0]|unsigned(stack[1])<<8)+" bank"+std::to_string(reference.stats().rom_bank));}if(r.sp<0xc180)throw std::runtime_error("cold original phase "+std::to_string(pc)+" left foreground stack after PC"+std::to_string(previous)+" now PC"+std::to_string(r.pc));previous=r.pc;reference.run_until(reference.stats().cycles+1);}auto r=reference.debug_registers();throw std::runtime_error("cold original phase "+std::to_string(pc)+" exceeded instruction bound at PC"+std::to_string(r.pc)+" SP"+std::to_string(r.sp)+" bank"+std::to_string(reference.stats().rom_bank));
 };
 auto drain=[&]{write(0xff40,uint8_t(reference.debug_read(0xff40,1)[0]&127));if(!reference.debug_read(uint16_t(queue),1)[0])return;unsigned svbk=reference.debug_read(0xff70,1)[0]&7,vbk=reference.debug_read(0xff4f,1)[0]&1;invoke(variant?0xa4d:0xa71,1);write(0xff70,uint8_t(svbk));write(0xff4f,uint8_t(vbk));check(!reference.debug_read(uint16_t(queue),1)[0],"cold original data queue did not drain");};
 invoke(variant?0x5892:0x5a4f,1);drain();auto loaded=reference.snapshot();write_file_atomic(std::string(argv[7])+".loaded.state",loaded);
 auto service_loaded=read_file(std::filesystem::path(argv[4]).parent_path()/"loaded.state");
 auto lc=std::make_unique<GB_gameboy_t>(),sc=std::make_unique<GB_gameboy_t>();std::memcpy(lc.get(),loaded.data()+core_at,core_size);std::memcpy(sc.get(),service_loaded.data()+core_at,core_size);
 check(lc->cgb_ram_bank==sc->cgb_ram_bank&&lc->cgb_vram_bank==sc->cgb_vram_bank,"cold loaded physical bank latches differ");
 check(std::equal(std::begin(lc->hram),std::end(lc->hram),sc->hram),"cold loaded native HRAM differs");
 invoke(variant?0x2e86:0x2ea5,1);drain();invoke(variant?0x3b0a:0x3b36,1);drain();unsigned transitions=0;
 while(reference.debug_read(0xcd00,1)[0]!=1||reference.debug_read(0xcd04,1)[0]!=2){check(++transitions<=128,"cold original transition readiness bound");invoke(0x4000,1);drain();}
 invoke(variant?0x5959:0x5b2c,1);drain();unsigned svbk=reference.debug_read(0xff70,1)[0]&7,vbk=reference.debug_read(0xff4f,1)[0]&1;invoke(0x4016,0x3f);invoke(variant?0xae3:0xb07,1);write(0xff70,uint8_t(svbk));write(0xff4f,uint8_t(vbk));
 auto expected=reference.snapshot();size_t ram_at=core_at+core_size+4,video_at=ram_at+32768+4;
 for(unsigned at=0;at<32768;++at)if(at<0x180||at>=0x220)if(expected[ram_at+at]!=actual[ram_at+at])throw std::runtime_error("cold original DATA WRAM differs at "+std::to_string(0xc000+at)+" expected "+std::to_string(expected[ram_at+at])+" actual "+std::to_string(actual[ram_at+at]));
 check(std::equal(expected.begin()+video_at,expected.begin()+video_at+16384,actual.begin()+video_at),"cold original DATA VRAM differs");
 auto a=std::make_unique<GB_gameboy_t>(),z=std::make_unique<GB_gameboy_t>();std::memcpy(a.get(),expected.data()+core_at,core_size);std::memcpy(z.get(),actual.data()+core_at,core_size);
 check(std::equal(std::begin(a->background_palettes_data),std::end(a->background_palettes_data),z->background_palettes_data)&&std::equal(std::begin(a->object_palettes_data),std::end(a->object_palettes_data),z->object_palettes_data),"cold original palettes differ");
 check(std::equal(std::begin(a->hram),std::end(a->hram),z->hram),"cold original complete DATA HRAM differs");
 check(a->mbc_rom_bank==z->mbc_rom_bank&&a->cgb_ram_bank==z->cgb_ram_bank&&a->cgb_vram_bank==z->cgb_vram_bank,"cold original complete DATA bank latches differ");
 Machine data_output(rom,options);check(data_output.restore(actual,error),error.c_str());auto result=data_output.debug_registers(),expected_regs=reference.debug_registers();
 check(result.af==expected_regs.af&&result.bc==expected_regs.bc&&result.de==expected_regs.de&&result.hl==expected_regs.hl&&result.sp==expected_regs.sp&&result.pc==expected_regs.pc&&result.ime==expected_regs.ime,"cold original complete DATA return registers differ");
 check(reference.battery()==std::vector<uint8_t>(actual.begin()+video_at+16384+4,actual.begin()+video_at+16384+4+reference.battery().size()),"cold original battery differs");
 write_file_atomic(std::string(argv[7])+".original-data.state",expected);
 auto ready=read_file(std::filesystem::path(argv[4]).parent_path()/"ready.state");std::memcpy(z.get(),ready.data()+core_at,core_size);if(z->cgb_ram_bank!=core->cgb_ram_bank)throw std::runtime_error("cold native return WRAM bank differs: input"+std::to_string(core->cgb_ram_bank)+" ready"+std::to_string(z->cgb_ram_bank));Machine picture(rom,options);check(picture.restore(ready,error),error.c_str());
 auto view_options=options;view_options.mods.push_back(read_mod_package(argv[6],sha256(rom)).feature);Machine wide(rom,view_options);auto public_state=read_file(argv[3]);check(wide.restore(public_state,error),error.c_str());
 auto world=std::make_unique<OracleWorldState>();std::memcpy(world.get(),public_state.data()+public_state.size()-69-sizeof(OracleWorldState),sizeof *world);
 unsigned target=actual[ram_at+room_at],group=actual[ram_at+group_at];const OracleRoomState* room=nullptr;for(auto& r:world->rooms)if(r.valid==1&&r.ram[group_at]==group&&r.ram[room_at]==target)room=&r;check(room,"cold current result lacks target room");
 unsigned steps=0;bool published=false;
 auto visible=[](const uint8_t* o){return o[0]>0&&o[0]<160&&o[1]>0&&o[1]<168;};
 auto same_visible_oam=[&]{
  // The disabled private Link may keep moving its hidden OAM entries. Those
  // cannot affect a native pixel. Every sprite touching the native raster
  // must still use the exact retained pose, tile and priority in both sources.
  for(unsigned slot=0;slot<40;++slot){auto* expected=room->ram.data()+0xb00+slot*4;auto* actual=z->oam+slot*4;
   if((visible(expected)||visible(actual))&&!std::equal(expected,expected+4,actual))return false;
  }return true;
 };
 while(steps++<4000){auto s=picture.snapshot();std::memcpy(z.get(),s.data()+core_at,core_size);
  if(z->io_registers[GB_IO_LY]==20&&same_visible_oam()){published=true;break;}
  picture.run_until(picture.stats().cycles+100);
 }check(published,("cold original PPU did not publish retained source pose; last LY="+std::to_string(z->io_registers[GB_IO_LY])).c_str());
 while(steps++<6000){auto s=picture.snapshot();std::memcpy(z.get(),s.data()+core_at,core_size);if(z->io_registers[GB_IO_LY]==144)break;picture.run_until(picture.stats().cycles+100);}
 check(steps<6000,"cold original PPU did not finish body");auto completed=picture.snapshot();std::memcpy(z.get(),completed.data()+core_at,core_size);
 check(std::equal(room->bg_palette.begin(),room->bg_palette.end(),z->background_palettes_data)&&std::equal(room->obj_palette.begin(),room->obj_palette.end(),z->object_palettes_data),"cold native body palettes changed");
 for(unsigned slot=0;slot<40;++slot){auto* o=z->oam+slot*4;if(!o[0]||!o[1]||o[0]>=160||o[1]>=168)continue;unsigned tile=(o[2]&254)*16+((o[3]&8)?0x2000:0);check(std::equal(room->vram.begin()+tile,room->vram.begin()+tile+32,completed.begin()+video_at+tile),"cold PPU selected different cached glyph source");}
 // The screen-overlay interaction E0 belongs to the live owner and is
 // relocated to the viewport edge. Exclude only its source-proven original
 // OAM footprint from the room-body comparison; terrain/glyph DATA is exact
 // above, and every remaining body pixel is compared against native PPU.
 std::array<bool,160*144> hud{};unsigned hud_sprites=0;
 for(unsigned page=0;page<16;++page){const auto* o=room->ram.data()+0x1040+page*256;
  if(!o[0]||o[1]!=0xe0||!(o[0x1a]&128))continue;unsigned pointer=o[0x1e]|(unsigned(o[0x1f])<<8);
  size_t at=((variant?0x12:0x13)+(pointer>>14))*0x4000u+(pointer&0x3fff);check(at<rom.size(),"cold native HUD source extent");unsigned count=rom[at++];check(count<=40&&at+count*4<=rom.size(),"cold native HUD metasprite extent");
  for(unsigned n=0;n<count;++n){const auto* m=rom.data()+at+n*4;std::array<uint8_t,4> expected{uint8_t(o[0xb]+int8_t(o[0xf])+int8_t(m[0])+16),uint8_t(o[0xd]+int8_t(m[1])),uint8_t(o[0x1d]+m[2]),uint8_t(o[0x1c]^m[3])};
   bool present=false;for(unsigned slot=0;slot<40;++slot)if(std::equal(expected.begin(),expected.end(),z->oam+slot*4))present=true;if(!present){if(expected[0]<160&&expected[1]<168)throw std::runtime_error("cold native HUD differs from original metasprite y"+std::to_string(expected[0])+" x"+std::to_string(expected[1])+" tile"+std::to_string(expected[2])+" attr"+std::to_string(expected[3]));continue;}++hud_sprites;
   for(int y=int(expected[0])-16;y<int(expected[0]);++y)for(int x=int(expected[1])-8;x<int(expected[1]);++x)if(x>=0&&x<160&&y>=16&&y<144)hud[size_t(y)*160+unsigned(x)]=true;
  }
 }
 if(variant)check(hud_sprites>0,"cold original screen overlay absent");unsigned compared=0;auto pixels=picture.pixels();
 for(unsigned width:{512u,1000u}){wide.mod_viewport(width,144);auto view=wide.mod_view();int dx=(int(target&15)-int(world->active.ram[room_at]&15))*160;
  for(unsigned y=16;y<144;++y)for(unsigned x=0;x<160;++x){int vx=dx+int(x)-view.left;if(vx<0||vx>=int(view.width)||hud[size_t(y)*160+x])continue;
   if(view.pixels[size_t(y)*view.width+unsigned(vx)]!=pixels[size_t(y)*160+x])throw std::runtime_error("cold native body differs width"+std::to_string(width)+" x"+std::to_string(x)+" y"+std::to_string(y));++compared;
  }
 }
 check(compared>0,"cold original body comparison was empty");
 write_file_atomic(std::string(argv[7])+".completed-ppu.state",completed);
 std::cout<<"{\"original_foreground_pc\":"<<in.pc<<",\"original_sp\":"<<in.sp<<",\"original_phase_calls\":"<<calls<<",\"transition_calls\":"<<transitions<<",\"wram_vram_palettes\":\"exact_except_inactive_stack\",\"native_body_pixels\":"<<compared<<",\"scope\":\"Original cold DATA plus first native actor/draw readiness; no legacy transition-frame loop or clone hardware import\"}\n";
}
static void entry_caller_check(int argc,char** argv){
 using namespace gbcllr;
 auto rom=read_file(argv[1]);bool variant=sha256(rom)=="862a51368fb30539279d336b3fe193b43876d2cb15c87a36f5da517804ab3971";
 Options options;options.model=Model::cgb;options.boot_rom=read_file(argv[2]);Machine native(rom,options),reference(rom,options);
 options.mods.push_back(read_mod_package(argv[6],sha256(rom)).feature);Machine service(rom,options);std::string error;
 auto input=read_file(argv[5]);check(service.restore(input,error),error.c_str());
 constexpr size_t core_at=4+64*3+8+8+1+4,module_size=16+sizeof(OracleWorldState);
 size_t module_at=input.size()-69-module_size;uint32_t size=0;std::memcpy(&size,input.data()+module_at-4,4);check(size==module_size,"entry input module differs");
 auto world=std::make_unique<OracleWorldState>();std::memcpy(world.get(),input.data()+module_at+16,sizeof *world);
 auto bare=input;bare.resize(module_at);std::fill(bare.end()-4,bare.end(),0);bare.insert(bare.end(),5,0);auto hash=sha256(bare);bare.insert(bare.end(),hash.begin(),hash.end());
 check(native.restore(bare,error),error.c_str());check(reference.restore(bare,error),error.c_str());
 auto in=native.debug_registers();check(in.pc==(variant?0x41af:0x41e4),"entry fixture is not successful original border RET");
 unsigned group=variant?0xc49:0xc2d,room=variant?0xc4c:0xc30;auto before=native.debug_read(0xc000,8192);
 unsigned following_pointer=variant?0xcfe:0xce8,following=0,following_base=0;
 if(before[following_pointer]>=0xd0&&before[following_pointer]<=0xdf&&before[following_pointer-1]==0x40)
  following=(before[following_pointer]-0xc0)*256+0x40;
 auto follower_slot=[&](const uint8_t* r,unsigned page){return r[following_pointer-1]==0x40&&r[following_pointer]==0xd0+page&&(r[0x1040+page*256]&3)==3;};
 auto original_path=native.debug_read(0xff70,1);auto path_bank=original_path[0];
 std::array<uint8_t,1> bank2{2};native.debug_write(0xff70,bank2);original_path=native.debug_read(0xda90,48);bank2[0]=path_bank;native.debug_write(0xff70,bank2);
 constexpr std::array<int,4> delta{-16,1,16,-1};
 unsigned direction=before[0xd02]&3,target=unsigned(int(before[room])+delta[direction]);
 unsigned dungeon=variant?0xc55:0xc39;
 if(before[dungeon]!=255) {
  constexpr std::array<int,4> offsets{-8,1,8,-1};
  int position=int(before[dungeon+1])+offsets[direction];check(position>=0&&position<64,"entry left native dungeon floor");
  target=world->active.ram[0x2c00+unsigned(before[dungeon+2])*64+unsigned(position)];
 }
 const OracleRoomState* cached=nullptr;for(auto& candidate:world->rooms)if(candidate.valid==1&&candidate.ram[room]==target&&candidate.ram[group]==before[group])cached=&candidate;
 check(cached,"entry destination absent from cache");
 // Independent source-native DATA boundary: invoke the original load tail,
 // loading-mode enemy/interaction initialization, transition data bodies,
 // cutscene00 completion, then palette preparation/publication. This removes
 // legacy scroll/main/audio scheduler frames rather than importing their
 // evolved RNG/cooldowns. No native-mod reader runs on this reference.
 // Unlike the service's bank-call trampoline, this test selects each ROM bank
 // directly and calls its original body with a synthetic test return address.
 auto write=[&](unsigned at,uint8_t value){std::array<uint8_t,1> bytes{value};reference.debug_write(uint16_t(at),bytes);};
 write(0xff40,uint8_t(reference.debug_read(0xff40,1)[0]&127));
 std::vector<uint8_t> music;unsigned sound=variant?0xc74:0xc98,phase_calls=0;
 auto invoke=[&](unsigned address,unsigned bank){
  auto regs=reference.debug_registers();write(0xff97,uint8_t(bank));write(0x2000,uint8_t(bank));
  write(in.sp,7);write(in.sp+1,0x40);regs.sp=in.sp;regs.pc=uint16_t(address);regs.ime=0;
  reference.debug_registers(regs);reference.debug_wake();
  for(unsigned n=0;n<200000;++n){auto s=reference.stats();auto r=reference.debug_registers();
   if(r.pc==0x4007&&r.sp==in.sp+2){++phase_calls;return;}
   if(s.pc==sound&&r.af>>8&&!(reference.debug_read(uint16_t(variant?0xffb5:0xffb7),1)[0]&8))music.push_back(uint8_t(r.af>>8));
   reference.run_until(s.cycles+1);
  }throw std::runtime_error("original phase body exceeded instruction bound");
 };
 auto drain=[&]{if(!reference.debug_read(uint16_t(variant?0xffa3:0xffa5),1)[0])return;
  auto svbk=reference.debug_read(0xff70,1)[0],vbk=reference.debug_read(0xff4f,1)[0];
  invoke(variant?0xa4d:0xa71,1);write(0xff70,svbk);write(0xff4f,vbk);
  check(!reference.debug_read(uint16_t(variant?0xffa3:0xffa5),1)[0],"original VBlank data queue did not drain");
 };
 invoke(variant?0x59a8:0x5b97,1);drain();
 invoke(variant?0x2e86:0x2ea5,1);drain();
 invoke(variant?0x3b0a:0x3b36,1);drain();
 unsigned transitions=0;while(reference.debug_read(0xcd00,1)[0]!=1||reference.debug_read(0xcd04,1)[0]!=2){
  check(transitions++<128,"original transition bodies never became ready");invoke(0x4000,1);drain();
 }
 invoke(variant?0x5959:0x5b2c,1);drain();
 auto door_cell=[](const uint8_t* o){return o[o[4]?0x3e:11];};
 if(cached->authority&1) {
  std::array<bool,256> incoming{};
  auto fresh=reference.debug_read(0xd000,4096);
  bool solved_entry=false;
  for(unsigned page=0;page<16;++page){auto* o=fresh.data()+0x40+page*256;if(!o[0]||o[1]!=0x1e||o[2]<0x10||o[2]>0x13)continue;
   unsigned cell=door_cell(o);bool solved=false;
   if(!cached->ram[0xe00+cell]) {
    for(unsigned p=0;p<16;++p)if(cached->interaction_origin_mask&(1u<<p)) {
     unsigned origin=cached->interaction_origins[p],sub=(origin>>8)&255,pos=((origin>>16)&255)/16+((origin>>24)&0xf0);
     if((origin&255)==0x1e&&pos==cell&&sub>=8&&sub<=11&&!cached->ram[variant?0xc30:0xdd1])solved=true;
    }
    for(unsigned p=0;p<16;++p){auto* d=cached->ram.data()+0x1040+p*256;if(!d[0]||d[1]!=0x1e||door_cell(d)!=cell)continue;
     if(d[4]==2||(d[2]>=4&&d[2]<=7&&(cached->ram[variant?0xcba:0xca0]&d[0x3d])))solved=true;
    }
   }
   if(solved){for(unsigned n=0;n<64;++n)write(0xd040+page*256+n,0);solved_entry=true;}else incoming[cell]=true;
  }
  if(solved_entry)write(variant?0xccad:0xcc93,cached->ram[variant?0xcad:0xc93]&0x7f);
  for(unsigned row=0;row<unsigned(cached->room_height/16);++row)for(unsigned col=0;col<unsigned(cached->room_width/16);++col) {
   unsigned cell=row*16+col;if(incoming[cell])continue;
   auto saved=reference.debug_read(0xff70,1)[0];write(0xff70,3);write(0xdf00+cell,cached->ram[0x3f00+cell]);write(0xff70,saved);
   if(reference.debug_read(uint16_t(0xcf00+cell),1)[0]==cached->ram[0xf00+cell])continue;
   auto regs=reference.debug_registers();regs.af=uint16_t((cached->ram[0xf00+cell]<<8)|(regs.af&255));regs.bc=uint16_t(cell);reference.debug_registers(regs);
   invoke(variant?0x3a52:0x3a9c,0);invoke(variant?0x6b25:0x6c32,4);drain();
  }
  write(variant?0xccba:0xcca0,cached->ram[variant?0xcba:0xca0]);write(variant?0xcc30:0xcdd1,cached->ram[variant?0xc30:0xdd1]);
 }
 unsigned carried=0,descriptors=variant?0xc05:0xc06,descriptor_end=variant?0xc19:0xc1a;
 std::array<uint8_t,16> carried_base{};
 for(unsigned page=7;page<16;++page)if(before[0x1000+page*256]&&!world->items[page].room)carried|=1u<<page;
 if(carried||following) {
  // Independently invoke original graphics allocation with the cached room's
  // actor/header inputs and real carried objects. No item/AI pass is run.
  auto fresh_actors=reference.debug_read(0xd000,4096);
  for(unsigned page=0;page<16;++page)for(unsigned type=1;type<4;++type) {
   unsigned at=0x1000+page*256+type*64;
   if(type==1&&cached->ram[at+1]==0xe0)continue; // Fresh native screen overlay.
   reference.debug_write(uint16_t(0xc000+at),std::span(cached->ram).subspan(at,64));
  }
  if(following)reference.debug_write(uint16_t(0xc000+following),std::span(before).subspan(following,64));
  reference.debug_write(uint16_t(0xc000+descriptors),std::span(cached->ram).subspan(descriptors,descriptor_end-descriptors));
  auto bank=reference.debug_read(0xff4f,1)[0];write(0xff4f,0);reference.debug_write(0x8000,std::span(cached->vram).first(0x1400));write(0xff4f,bank);
  for(unsigned page=7;page<16;++page) {
   if(carried&(1u<<page))reference.debug_write(uint16_t(0xd000+page*256),std::span(before).subspan(0x1000+page*256,64));
   else if(world->items[page].room==target+1)reference.debug_write(uint16_t(0xd000+page*256),std::span(cached->ram).subspan(0x1000+page*256,64));
   else write(0xd000+page*256,0);
  }
  invoke(0x4154,0x3f);drain();
  if(following) {
   auto regs=reference.debug_registers();regs.de=uint16_t((0xc000+following)&0xff00);reference.debug_registers(regs);invoke(0x4404,0x3f);drain();
   following_base=reference.debug_read(uint16_t(0xc000+following+0x1d),1)[0];
  }
  for(unsigned page=7;page<16;++page)if(carried&(1u<<page)) {
   auto regs=reference.debug_registers();regs.de=uint16_t((0xd0+page)<<8);reference.debug_registers(regs);invoke(0x4422,0x3f);drain();carried_base[page]=reference.debug_read(uint16_t(0xd01d+page*256),1)[0];
  }
  for(unsigned page=0;page<16;++page)for(unsigned type=1;type<4;++type)reference.debug_write(uint16_t(0xd000+page*256+type*64),std::span(fresh_actors).subspan(page*256+type*64,64));
 }
 auto svbk=reference.debug_read(0xff70,1)[0],vbk=reference.debug_read(0xff4f,1)[0];
 invoke(0x4016,0x3f);invoke(variant?0xae3:0xb07,1);write(0xff70,svbk);write(0xff4f,vbk);
 check(reference.debug_read(uint16_t(variant?0xcc85:0xcc6b),1)[0]==8,"entry phase boundary lost original eight-step instrument cooldown");
 auto caller_bytes=native.debug_read(in.sp,2);unsigned caller=caller_bytes[0]|unsigned(caller_bytes[1])<<8;
 auto finish_ret=[&](Machine& m){for(unsigned n=0;n<100000;++n){auto regs=m.debug_registers();if(regs.pc==caller&&regs.sp==in.sp+2)return;m.run_until(m.stats().cycles+1);}throw std::runtime_error("native border RET/IRQ did not finish");};
 finish_ret(native);finish_ret(service);
 auto a=native.snapshot(),b=service.snapshot(),ref=reference.snapshot();uint32_t core_size=0;std::memcpy(&core_size,a.data()+core_at-4,4);
 write_file_atomic(std::string(argv[7])+".input.state",input);write_file_atomic(std::string(argv[7])+".native-ret.state",a);
 write_file_atomic(std::string(argv[7])+".original-entry.state",ref);write_file_atomic(std::string(argv[7])+".service.state",b);
 check(core_size==offsetof(GB_gameboy_t,llr_reset_seed),"entry hardware ABI differs");
 auto allow_palette=[&](size_t at,size_t count){check(std::equal(b.begin()+core_at+at,b.begin()+core_at+at+count,ref.begin()+core_at+at),"entry palette differs from complete original caller");std::copy_n(b.begin()+core_at+at,count,a.begin()+core_at+at);};
 allow_palette(offsetof(GB_gameboy_t,background_palettes_data),64);allow_palette(offsetof(GB_gameboy_t,object_palettes_data),64);
 for(unsigned at:{0x14u,0x15u,variant?0x33u:0x35u})a[core_at+offsetof(GB_gameboy_t,hram)+at]=b[core_at+offsetof(GB_gameboy_t,hram)+at];
 auto mismatch=std::mismatch(a.begin(),a.begin()+core_at+core_size,b.begin());
 if(mismatch.first!=a.begin()+core_at+core_size)throw std::runtime_error("entry changed owner CPU/IRQ/clock/FIFO/hardware at snapshot byte"+std::to_string(mismatch.first-a.begin()));
 size_t ram_at=core_at+core_size+4,video_at=ram_at+32768+4;
 auto ram=[&](const auto& data,unsigned at){return data[ram_at+at];};
 unsigned flagtable=variant?0x9a8:0x9cc,flag=(unsigned(rom[flagtable+ram(ref,group)])<<8|target)-0xc000,gasha=variant?0x65c:0x65f;
 auto queue=native.debug_read(uint16_t(variant?0xffb2:0xffb4),2);unsigned tail=queue[1];
 auto expected_ram=std::vector<uint8_t>(a.begin()+ram_at,a.begin()+ram_at+32768);
 auto copy=[&](unsigned lo,unsigned hi){std::copy(ref.begin()+ram_at+lo,ref.begin()+ram_at+hi,expected_ram.begin()+lo);};
 copy(0x300,0x400);copy(0x48b,0x497);copy(0xbca,0xbcb);copy(0xc01,0x1000);copy(0x2000,0x4000);copy(0x4000,0x4100);copy(0x4c00,0x5000);
 // Untouched predictions retain fresh entry controllers. A room already
 // changed by the real player/projectile retains its complete actor lifecycle,
 // including deleted puzzle controllers; only fresh UI/ingress helpers remain.
 unsigned registry=variant?0xcca:0xcb3;std::array<int,16> npc_map;npc_map.fill(-1);
 auto registered=[&](unsigned page){for(unsigned n=0;n<32;n+=2)if(cached->ram[registry+n]==0xd0+page&&cached->ram[registry+n+1]==0x71)return true;return false;};
 auto adopted=[&](unsigned page){unsigned at=0x1040+page*256;return cached->ram[at]&&cached->ram[at+1]!=0xe0&&!follower_slot(cached->ram.data(),page)&&((cached->authority&1)||(cached->ram[at+0x1a]&128)||registered(page));};
 std::array<bool,16> reserved{};
 if(cached->authority&1) {
  for(unsigned p=0;p<16;++p){unsigned at=0x1040+p*256;reserved[p]=(following==at)||(ram(ref,at)&&!(ram(ref,at)&2)&&(ram(ref,at+1)==0xe0||(ram(ref,at+1)==0x1e&&ram(ref,at+2)>=0x10&&ram(ref,at+2)<=0x13)));}
  auto used=reserved;
  for(unsigned p=0;p<16;++p)if(adopted(p)) {
   auto* o=cached->ram.data()+0x1040+p*256;bool ingress=false;
   if(o[1]==0x1e&&o[2]>=0x10&&o[2]<=0x13)for(unsigned f=0;f<16;++f){auto* n=ref.data()+ram_at+0x1040+f*256;if(reserved[f]&&n[1]==0x1e&&door_cell(n)==door_cell(o))ingress=true;}
   if(ingress)continue;unsigned slot=p;if(used[slot]){slot=0;while(slot<16&&used[slot])++slot;}check(slot<16,"reference interaction slots exhausted");used[slot]=true;npc_map[p]=int(slot);
  }
 }else {
 for(unsigned page=0;page<16;++page){unsigned at=0x1040+page*256;if(!cached->ram[at]||cached->ram[at+1]==0xe0||follower_slot(cached->ram.data(),page))continue;
  unsigned ordinal=0;for(unsigned p=0;p<page;++p){unsigned q=0x1040+p*256;if(cached->ram[q]&&!follower_slot(cached->ram.data(),p)&&cached->ram[q+1]==cached->ram[at+1]&&cached->ram[q+2]==cached->ram[at+2])++ordinal;}
  for(unsigned p=0;p<16;++p){unsigned q=0x1040+p*256;if(ram(ref,q)&&!(ram(ref,q)&2)&&ram(ref,q+1)==cached->ram[at+1]&&ram(ref,q+2)==cached->ram[at+2])if(!ordinal--){npc_map[page]=int(p);break;}}
 }
 }
 auto actor=[&](unsigned from,unsigned to){std::copy_n(cached->ram.begin()+from,64,expected_ram.begin()+to);
  if(!expected_ram[to])return;for(unsigned at:{0x16u,0x18u}){auto lo=expected_ram[to+at],hi=expected_ram[to+at+1];
   if(lo>=0x40&&lo<0x80&&hi>=0xd0&&hi<=0xdf){check(npc_map[hi-0xd0]>=0,"reference has an unmapped interaction pointer");expected_ram[to+at+1]=uint8_t(0xd0+npc_map[hi-0xd0]);}
  }
 };
 for(unsigned page=0;page<16;++page){unsigned npc=0x1040+page*256;copy(npc,npc+64);if((cached->authority&1)&&!reserved[page])std::fill_n(expected_ram.begin()+npc,64,0);}
 for(unsigned page=0;page<16;++page){
  if(adopted(page)&&npc_map[page]>=0)actor(0x1040+page*256,0x1040+unsigned(npc_map[page])*256);
  for(unsigned type=2;type<4;++type){unsigned at=0x1000+page*256+type*64;actor(at,at);}
 }
 std::array<int,16> enemy_map;enemy_map.fill(-1);
 for(unsigned f=0;f<16;++f){auto* o=ref.data()+ram_at+0x1080+f*256;if(!o[0]||(o[0]&2))continue;unsigned key=o[1]|(unsigned(o[2])<<8),fresh_count=0,live_count=0;
  for(unsigned p=0;p<16;++p){auto* n=ref.data()+ram_at+0x1080+p*256;auto* s=cached->ram.data()+0x1080+p*256;if(n[0]&&!(n[0]&2)&&(n[1]|(unsigned(n[2])<<8))==key)++fresh_count;if(s[0]&&(s[1]|(unsigned(s[2])<<8))==key)++live_count;}
  int chosen=-1,distance=100000;for(unsigned p=0;p<16;++p)if((cached->enemy_origin_mask&(1u<<p))&&enemy_map[p]<0&&(cached->enemy_origins[p]&65535)==key) {
   if(!cached->ram[0x1080+p*256]&&fresh_count<=live_count)continue;unsigned origin=cached->enemy_origins[p];int d=std::abs(int((origin>>16)&255)-int(o[13]))+std::abs(int(origin>>24)-int(o[11]));if(d<distance){chosen=int(p);distance=d;}
  }check(chosen>=0,"reference enemy origin absent");enemy_map[unsigned(chosen)]=int(f);
 }
 for(unsigned f=0;f<16;++f){bool imported=false;for(unsigned p=0;p<16;++p)if(npc_map[p]==int(f)&&adopted(p))imported=true;if(imported||following==0x1040+f*256)continue;auto* o=expected_ram.data()+0x1040+f*256;
  for(unsigned at:{0x16u,0x18u})if(o[0]&&o[at]>=0x80&&o[at]<0xc0&&o[at+1]>=0xd0&&o[at+1]<=0xdf){auto found=std::find(enemy_map.begin(),enemy_map.end(),int(o[at+1]-0xd0));if(found!=enemy_map.end())o[at+1]=uint8_t(0xd0+(found-enemy_map.begin()));}
 }
 if(cached->authority&1)std::fill_n(expected_ram.begin()+registry,32,0);
 for(unsigned n=0;n<32;n+=2){unsigned page=cached->ram[registry+n]-0xd0;if(page>=16||cached->ram[registry+n+1]!=0x71||!adopted(page))continue;
  if(npc_map[page]<0)continue;
  unsigned high=0xd0+unsigned(npc_map[page]);bool present=false;for(unsigned k=0;k<32;k+=2)if(expected_ram[registry+k]==high&&expected_ram[registry+k+1]==0x71)present=true;
  if(!present){unsigned k=0;while(k<32&&expected_ram[registry+k])k+=2;check(k<32,"reference interaction registry is full");expected_ram[registry+k]=uint8_t(high);expected_ram[registry+k+1]=0x71;}
 }
 copy(gasha,gasha+2);if(before[group-1]==0xd1&&before[0x1001]==9)copy(0x638,0x63a);copy(0x63a,0x63e);copy(flag,flag+1);
 if(!carried&&!following)std::copy(cached->ram.begin()+descriptors,cached->ram.begin()+descriptor_end,expected_ram.begin()+descriptors);
 if(following) {
  std::copy_n(before.begin()+following,64,expected_ram.begin()+following);expected_ram[following+0x1d]=uint8_t(following_base);
  std::copy_n(before.begin()+following_pointer-2,3,expected_ram.begin()+following_pointer-2);
  std::copy(original_path.begin(),original_path.end(),expected_ram.begin()+0x2a90);
 }
 for(unsigned page=7;page<16;++page)if(carried&(1u<<page))expected_ram[0x101d+page*256]=carried_base[page];
 bool horizontal=direction&1,positive=direction==1||direction==2;
 unsigned respawn=horizontal?(variant?0xc3e:0xc22):(variant?0xc3d:0xc21);
 unsigned primary_actor=before[group-1]==0xd1?0x1100:0x1000,component=horizontal?13:11;
 unsigned coordinate=primary_actor+component,extent=unsigned(horizontal?cached->room_width:cached->room_height);
 expected_ram[respawn]=positive?0:extent;
 int shift=int(positive?0:extent)-expected_ram[coordinate];
 for(unsigned page=0;page<16;++page)if(expected_ram[0x1000+page*256]&&!world->items[page].room)
  expected_ram[0x1000+component+page*256]=uint8_t(int(expected_ram[0x1000+component+page*256])+shift);
 if(following) {
  expected_ram[following+component]=uint8_t(int(expected_ram[following+component])+shift);
  for(unsigned n=0;n<16;++n)expected_ram[0x2a90+n*3+(horizontal?2:1)]=uint8_t(int(expected_ram[0x2a90+n*3+(horizontal?2:1)])+shift);
 }
 // A projectile already owned by this destination has native local coordinates
 // and graphics. Its reserved source slot becomes that same running object.
 for(unsigned page=7;page<16;++page)if(world->items[page].room==target+1)
  std::copy_n(cached->ram.begin()+0x1000+page*256,64,expected_ram.begin()+0x1000+page*256);
 for(unsigned at:{0x48cu,0x492u})expected_ram[at]=uint8_t(expected_ram[0xd08]-16);
 for(unsigned at:{0x48du,0x493u})expected_ram[at]=expected_ram[0xd09];
 for(auto id:music){expected_ram[tail]=id;tail=0xa0|((tail+1)&15);}
 auto ram_difference=std::mismatch(expected_ram.begin(),expected_ram.end(),b.begin()+ram_at);
 if(ram_difference.first!=expected_ram.end())throw std::runtime_error("whole entry declared data output or protected owner RAM differs at "+std::to_string(0xc000+ram_difference.first-expected_ram.begin())+" expected "+std::to_string(*ram_difference.first)+" actual "+std::to_string(*ram_difference.second));
 for(unsigned at=0;at<16384;++at){uint8_t expected=ref[video_at+at];
  if(at<0x1400&&!carried&&!following)expected=cached->vram[at];
  if((at>=0x1c00&&at<0x2000)||(at>=0x2600&&at<0x2800)||at>=0x3c00)expected=a[video_at+at];
  check(b[video_at+at]==expected,"whole entry native terrain/cached actor/live HUD glyph source differs");
 }
 auto ha=native.debug_read(0xff80,128),hb=service.debug_read(0xff80,128),hr=reference.debug_read(0xff80,128);
 ha[0x14]=hr[0x14];ha[0x15]=hr[0x15];ha[variant?0x33:0x35]=uint8_t(tail);check(ha==hb,"entry imported undeclared HRAM/control or RNG/music output differs");
 check(native.battery()==service.battery()&&native.battery()==reference.battery(),"whole entry changed battery");
 check(native.take_audio()==service.take_audio(),"whole entry changed owner elapsed PCM");
 auto after=service.snapshot();auto state=std::make_unique<OracleWorldState>();std::memcpy(state.get(),after.data()+module_at+16,sizeof *state);
 check(state->handoffs==world->handoffs+1&&!state->handoff_stage,"whole entry was refused or duplicated");
 write_file_atomic(std::string(argv[7])+".input.state",input);write_file_atomic(std::string(argv[7])+".original-entry.state",ref);write_file_atomic(std::string(argv[7])+".service.state",after);
 uint64_t publication_ticks=0;bool picture_checked=false;
 if(std::filesystem::path(argv[7]).filename()=="plain") {
  // First current hardware frame after the service: independent original
  // PPU completion, using exactly the retained source inputs. Raw mixed-row
  // native history remains untouched and is retained beside this reference.
  auto service_line=[&]{return service.debug_read(0xff44,1)[0];};
  auto publication_start=service.stats().cycles;unsigned publication_steps=0;
  // Host frame deadlines are not PPU scanlines. Select the first actual
  // line20 after line0 has captured the newly owned room; do not advance a
  // reference to a later actor pose. Retain the exact publication interval.
  while(publication_steps++<2000){
   if(service_line()==20){auto bytes=service.snapshot();std::memcpy(state.get(),bytes.data()+module_at+16,sizeof *state);if(state->active.ram[room]==target)break;}
   service.run_until(service.stats().cycles+100);
  }
  publication_ticks=service.stats().cycles-publication_start;
  check(publication_steps<2000&&publication_ticks<160000,"entry missed first native room publication");
  auto current=service.snapshot();auto view=service.mod_view();check(service.snapshot()==current,"entry view mutated owner");
  check(view.expanded,"whole entry lost native world presentation");
  auto source=std::make_unique<OracleWorldState>();std::memcpy(source.get(),current.data()+module_at+16,sizeof *source);
  auto original=current;original.resize(module_at);std::fill(original.end()-4,original.end(),0);original.insert(original.end(),5,0);hash=sha256(original);original.insert(original.end(),hash.begin(),hash.end());
  Options po=options;po.mods.clear();po.native=false;Machine picture(rom,po);check(picture.restore(original,error),error.c_str());
  auto line=[&]{auto bytes=picture.snapshot();return bytes[core_at+offsetof(GB_gameboy_t,io_registers)+0x44];};
  unsigned steps=0;while(line()<143&&steps++<1000)picture.run_until(picture.stats().cycles+500);
  while(line()!=144&&steps++<2000)picture.run_until(picture.stats().cycles+1);check(steps<2000,"entry original PPU did not complete");
  auto completed=picture.snapshot();auto core=std::make_unique<GB_gameboy_t>();std::memcpy(core.get(),completed.data()+core_at,core_size);
  auto& active=source->active;
  check(std::equal(active.bg_palette.begin(),active.bg_palette.end(),core->background_palettes_data)&&std::equal(active.obj_palette.begin(),active.obj_palette.end(),core->object_palettes_data),"entry PPU changed retained native palettes");
  const unsigned regs[]={0x40,0x42,0x43,0x4a,0x4b};for(unsigned i=0;i<5;++i)check(active.ram[0x491+i]==core->io_registers[regs[i]],"entry native body register publication differs");
  for(unsigned slot=0;slot<40;++slot){auto* o=core->oam+slot*4;if(o[0]>=160||o[1]>=168||!o[0]||!o[1])continue;
   auto& from=(o[3]&8)&&o[2]>=0x70&&o[2]<0x80?source->owner:active;
   check(std::equal(o,o+4,from.ram.data()+0xb00+slot*4),"entry reference selected another native pose/priority");
   unsigned tile=(o[2]&254)*16+((o[3]&8)?0x2000:0);check(std::equal(from.vram.begin()+tile,from.vram.begin()+tile+32,completed.begin()+video_at+tile),"entry reference selected another native actor glyph");
  }
  auto picture_pixels=picture.pixels();
  write_file_atomic(std::string(argv[7])+".current.state",current);write_file_atomic(std::string(argv[7])+".completed-ppu.state",completed);
  // The adaptive camera may expose the room above a mounted actor. Register
  // the original PPU window in world coordinates before comparing its visible
  // pixels; its native camera origin need not equal the adaptive origin.
  int top=active.camera_y,above=0,below=active.room_height;
  for(const auto& candidate:source->rooms)if(candidate.valid==1&&candidate.ram[group]==active.ram[group]) {
   int dy=(int(candidate.ram[room]>>4)-int(active.ram[room]>>4))*active.room_height;
   above=std::min(above,dy);below=std::max(below,dy+candidate.room_height);
  }
  if(before[dungeon]==255)top=std::clamp(source->focus_y-80,above,below-128);
  unsigned compared=0;
  for(unsigned y=16;y<144;++y)for(unsigned x=0;x<160;++x){int vx=int(x)+active.camera_x-view.left,vy=int(y)+active.camera_y-top;
   if(vx<0||vx>=int(view.width)||vy<16||vy>=144)continue;
   if(view.pixels[size_t(vy)*view.width+unsigned(vx)]!=picture_pixels[size_t(y)*160+x])throw std::runtime_error("entry complete native body differs at x"+std::to_string(x)+" y"+std::to_string(y));
   ++compared;
  }
  check(compared>0,"entry native window comparison was empty");
  write_file_atomic(std::string(argv[7])+".current.state",current);write_file_atomic(std::string(argv[7])+".completed-ppu.state",completed);
  picture_checked=true;
 }
 std::cout<<"{\"original_entry_phase_data_rng_music\":\"exact\",\"original_phase_calls\":"<<phase_calls<<",\"transition_data_calls\":"<<transitions<<",\"entry_instrument_cooldown\":8,\"owner_cpu_irq_clocks_fifo_pcm_battery\":\"exact\",\"actor_cache_records\":\"exact\",\"native_terrain_owner_hud_player\":\"exact\",\"adoptions\":1,\"native_body_source_pixels\":\""<<(picture_checked?"exact":"not_run")<<"\",\"publication_ticks\":"<<publication_ticks<<",\"boundary_ly\":"<<unsigned(b[core_at+offsetof(GB_gameboy_t,io_registers)+0x44])<<"}\n";
}
int main(int argc,char** argv){try{
 using namespace gbcllr;check(argc==8||argc==9,"usage: oracle-world-qa ROM BOOT BATTERY BASELINE_COMMANDS CURRENT_FIELD_STATE PACKAGE OUT_BASE [--joined|--crossing]");
 if(argc==9&&std::string(argv[8])=="--gfx-data"){graphics_data_check(argv);return 0;}
 if(argc==9&&std::string(argv[8])=="--cold-data"){cold_data_check(argv);return 0;}
 if(argc==9&&std::string(argv[8])=="--entry"){entry_caller_check(argc,argv);return 0;}
 bool crossing=argc==9&&std::string(argv[8])=="--crossing";
 bool joined=crossing||(argc==9&&std::string(argv[8])=="--joined");check(argc==8||joined,"unknown world check mode");
 auto rom=read_file(argv[1]);Options options;options.model=Model::cgb;options.boot_rom=read_file(argv[2]);
 Machine stock(rom,options);options.mods.push_back(read_mod_package(argv[6],sha256(rom)).feature);Machine wide(rom,options);
 std::string error;check(stock.load_battery(read_file(argv[3]),error),error.c_str());bool rewound=false;execute_commands(stock,argv[4],&rewound);
 Options picture_options=options;picture_options.mods.clear();picture_options.native=false;Machine picture(rom,picture_options);
 check(wide.restore(read_file(argv[5]),error),error.c_str());
 auto same_owner=[&]{auto a=stock.snapshot(),b=wide.snapshot();
  // The current ABI-specific state ends with native-size/native-bytes/SHA64.
  // A stock machine has zero native bytes. Compare every preceding CPU,
  // device, RAM, RTC, serial and framebuffer byte; only module data differs.
  check(a.size()>=68&&b.size()>=a.size(),"invalid owner snapshot envelope");
  check(std::equal(a.begin(),a.end()-73,b.begin()),"neighbor execution changed owner CPU/hardware state");
  check(stock.battery()==wide.battery(),"neighbor execution changed native battery");
  check(stock.pixels().size()==wide.pixels().size()&&std::equal(stock.pixels().begin(),stock.pixels().end(),wide.pixels().begin()),"neighbor execution changed original PPU frame");
 };
 same_owner();unsigned first=wide.mod_view().neighbor_ticks;check(wide.mod_view().neighbor_rooms>=1,"no actual neighbor initialized");
 auto initial_view=wide.mod_view();std::vector<uint32_t> scenery(initial_view.pixels.begin(),initial_view.pixels.end()),frozen_actor;
 unsigned frames=crossing?320:96,crossed=0;
 for(unsigned frame=0;frame<frames;++frame){uint8_t pad=crossing?(frame<160?1:2):(frame<8?1:frame<16?2:0);stock.run_frame(pad);wide.run_frame(pad);
  check(stock.take_audio()==wide.take_audio(),"neighbor execution changed owner PCM");same_owner();
  auto before=wide.snapshot();auto view=wide.mod_view();check(wide.snapshot()==before,"render mutated neighbor state");
  auto native=wide.pixels();unsigned center=(view.width-160)/2;
  for(unsigned y=0;y<(joined?16u:144u);++y)check(std::equal(native.begin()+y*160,native.begin()+(y+1)*160,view.pixels.begin()+y*view.width+center),"neighbor view changed LLE center/HUD");
  if(joined) {
   if(!view.expanded)throw std::runtime_error("joined view fell back during native crossing at frame "+std::to_string(frame));
   check(view.bound_right-view.bound_left==320,"actual joined house component differs");
   for(unsigned y=16;y<144;++y)for(unsigned x=0;x<view.width;++x)
    if(view.left+int(x)<view.bound_left||view.left+int(x)>=view.bound_right)check(view.pixels[size_t(y)*view.width+x]==0xff000000u,"joined view escaped native room bounds");
   if(crossing){check(view.left-view.bound_left==-96,"whole-house camera moved during native crossing");
    for(unsigned y=32;y<144;++y)if(y<80||y>=128)for(unsigned x=96;x<416;++x)
     check(view.pixels[size_t(y)*view.width+x]==scenery[size_t(y)*view.width+x],"native crossing displaced actual house scenery");
    if(frame==3||frame==24||frame==32||frame==40||frame==56){
     // The actual owner ends at LY20: lower rows still belong to its prior
     // raster. Complete only an independent original CPU/PPU reference copy.
     // Before comparing RGB, prove it used the retained source OAM, glyph
     // bytes and palettes, rather than selecting a later convenient pose.
     auto source_bytes=wide.snapshot();constexpr size_t module_size=16+sizeof(OracleWorldState);
     check(source_bytes.size()>module_size+68,"invalid actor source envelope");size_t at=source_bytes.size()-69-module_size;uint32_t native_size;
     std::memcpy(&native_size,source_bytes.data()+at-4,4);check(native_size==module_size,"actor source format differs");
     auto source=std::make_unique<OracleWorldState>();std::memcpy(source.get(),source_bytes.data()+at+16,sizeof *source);
     check(picture.restore(stock.snapshot(),error),error.c_str());unsigned steps=0;
     constexpr size_t core_at=4+64*3+8+8+1+4;
     auto line=[&]{auto bytes=picture.snapshot();return bytes[core_at+offsetof(GB_gameboy_t,io_registers)+0x44];};
     while(line()<143&&steps++<1000)picture.run_until(picture.stats().cycles+500);
     while(line()!=144&&steps++<2000)picture.run_until(picture.stats().cycles+1);
     check(steps<2000,"original native picture did not finish");auto picture_bytes=picture.snapshot();uint32_t core_size;
     std::memcpy(&core_size,picture_bytes.data()+core_at-4,4);check(core_size==offsetof(GB_gameboy_t,llr_reset_seed),"native picture ABI differs");
     auto core=std::make_unique<GB_gameboy_t>();std::memcpy(core.get(),picture_bytes.data()+core_at,core_size);
     size_t vram_at=core_at+core_size+4+0x8000+4;
     for(auto* room:{&source->active,&source->owner}){
      check(std::equal(room->obj_palette.begin(),room->obj_palette.end(),core->object_palettes_data),"reference selected different object palettes");
      check(std::equal(room->bg_palette.begin(),room->bg_palette.end(),core->background_palettes_data),"reference selected different background palettes");
     }
     for(unsigned slot=0;slot<40;++slot){auto* o=core->oam+slot*4;int x=int(o[1])-8,y=int(o[0])-16;
      if(x>=124||x+8<=100||y>=112||y+16<=72)continue;
      auto& room=(o[3]&8)&&o[2]>=0x70&&o[2]<0x80?source->owner:source->active;
      check(std::equal(o,o+4,room.ram.data()+0xb00+slot*4),"completed PPU selected different native actor pose/priority");
      unsigned tile=(o[2]&254)*16+((o[3]&8)?0x2000:0);
      check(std::equal(room.vram.begin()+tile,room.vram.begin()+tile+32,picture_bytes.begin()+vram_at+tile),"completed PPU selected different actor glyph bytes");
     }
     write_file_atomic(std::string(argv[7])+".priority-source-"+std::to_string(frame)+".state",source_bytes);
     write_file_atomic(std::string(argv[7])+".priority-native-"+std::to_string(frame)+".state",picture_bytes);
     auto original=picture.pixels();
     if(frame==3){
      // Prove the ready native VRAM-ring input, including its direct clock
      // writer, before comparing all non-object pixels to the original PPU.
      auto& room=source->active;unsigned lcdc=room.ram[0x491],map=(lcdc&8)?0x1c00:0x1800;
      const unsigned regs[]={0x40,0x42,0x43,0x4a,0x4b};for(unsigned i=0;i<5;++i)
       check(room.ram[0x491+i]==core->io_registers[regs[i]],"completed PPU selected different published body registers");
      for(unsigned y=0;y<128;y+=8)for(unsigned x=0;x<160;x+=8){unsigned bx=(x+room.ram[0xd09])&255,by=(y+room.ram[0xd08])&255,cell=map+by/8*32+bx/8;
       auto tile=room.vram[cell],attr=room.vram[cell+0x2000];check(tile==picture_bytes[vram_at+cell]&&attr==picture_bytes[vram_at+cell+0x2000],"completed PPU selected different native background descriptors");
       unsigned pattern=((lcdc&16)?unsigned(tile)*16:unsigned(0x1000+int8_t(tile)*16))+((attr&8)?0x2000:0);
       check(std::equal(room.vram.begin()+pattern,room.vram.begin()+pattern+16,picture_bytes.begin()+vram_at+pattern),"completed PPU selected different native background glyph bytes");
      }
      for(unsigned y=16;y<144;++y)for(unsigned x=0;x<160;++x){bool object=false;for(unsigned slot=0;slot<40;++slot){auto* o=core->oam+slot*4;int ox=int(o[1])-8,oy=int(o[0])-16;if(int(x)>=ox&&int(x)<ox+8&&int(y)>=oy&&int(y)<oy+16)object=true;}
       if(!object)check(view.pixels[size_t(y)*view.width+96+x]==original[y*160+x],"complete native F8background differs including direct clock writer");
      }
     }
     for(unsigned y=72;y<112;++y)for(unsigned x=100;x<124;++x)
      if(view.pixels[size_t(y)*view.width+96+x]!=original[y*160+x])throw std::runtime_error("completed original OAM priority differs at frame "+std::to_string(frame)+" x"+std::to_string(x)+" y"+std::to_string(y));
    }
    unsigned scroll=stock.debug_read(0xcd00,1)[0];
    if(scroll==4||scroll==8){std::vector<uint32_t> actor;for(unsigned y=72;y<112;++y)for(unsigned x=192;x<224;++x)actor.push_back(view.pixels[size_t(y)*view.width+x]);
     if(frozen_actor.empty())frozen_actor=actor;else check(actor==frozen_actor,"cached whole Blossom actor clipped or changed during native scroll");
    }else frozen_actor.clear();
    if(stock.debug_read(0xcc30,1)[0]==0xf9)++crossed;
    if(frame==84){check(stock.debug_read(0xcd00,1)[0]==8&&stock.debug_read(0xcd04,1)[0]==5,"actual native transition checkpoint missing");
     auto checkpoint=wide.snapshot();write_file_atomic(std::string(argv[7])+".crossing.state",checkpoint);
     for(unsigned i=0;i<17;++i)wide.run_frame(pad);auto expected=wide.snapshot();auto pcm=wide.take_audio();auto v=wide.mod_view();std::vector<uint32_t> image(v.pixels.begin(),v.pixels.end());
     check(wide.restore(checkpoint,error),error.c_str());for(unsigned i=0;i<17;++i)wide.run_frame(pad);v=wide.mod_view();
     check(wide.snapshot()==expected&&wide.take_audio()==pcm&&std::equal(image.begin(),image.end(),v.pixels.begin(),v.pixels.end()),"native crossing state/view/PCM replay differs");
     check(wide.restore(checkpoint,error),error.c_str());same_owner();
    }
   }
  }
 }
 if(crossing)check(crossed>0&&stock.debug_read(0xcc30,1)[0]==0xf8,"actual native house round trip missing");
 auto view=wide.mod_view();check(view.neighbor_ticks>first&&view.neighbor_rooms>=1,"neighbors did not continue native AI");
 if(joined)write_file_atomic(std::string(argv[7])+".stock.state",stock.snapshot());
 auto checkpoint=wide.snapshot();for(unsigned i=0;i<16;++i)wide.run_frame(0);auto expected=wide.snapshot();auto pcm=wide.take_audio();
 check(wide.restore(checkpoint,error),error.c_str());for(unsigned i=0;i<16;++i)wide.run_frame(0);
 check(wide.snapshot()==expected&&wide.take_audio()==pcm,"neighbor current-state/PCM replay differs");
 std::filesystem::path base=argv[7];write_file_atomic(base.string()+".state",checkpoint);
 std::cout<<"{\"owner_frames\":"<<frames<<",\"owner_cpu_hardware_native_save_pixels_pcm\":\"exact\",\"native_round_trip\":"<<(crossing?"true":"false")<<",\"neighbor_rooms\":"<<view.neighbor_rooms<<",\"neighbor_ticks\":"<<view.neighbor_ticks<<",\"neighbor_failures\":"<<view.neighbor_failures<<",\"current_state_pcm_replay\":\"pass\"}\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
