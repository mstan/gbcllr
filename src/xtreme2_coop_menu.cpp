#include "xtreme2_coop.hpp"
#include "native_mods.hpp"
#include <gbcllr/machine.hpp>
#include <algorithm>
#include <stdexcept>

namespace gbcllr {
namespace {
// Original shared campaign inventory. Character workspaces (C083/C084,
// C0D3/C0D4 and C100) stay personal. The native UI performs all transactions.
constexpr unsigned shared_inventory[]{
 0x01,0x02,0x03,
 0x65,0x67,0x68,0x69,0x6a,0x6d,0x6e,0x6f,0x70,0x71,0x72,0x73,0x74,0x75,0x76,
 0xa0,0xd0,0xd1,0xd2,0xd5,0xd6,0xd7,0xd8,0xd9,0xda,0xdb,0xdc,0xdd,0xde,
 0xf0,0xf1,0xf2,0xf3};
void inventory(std::span<uint8_t> to,std::span<const uint8_t> from){for(auto at:shared_inventory)to[at]=(at==0x68||at==0x69)?uint8_t((to[at]&0x80)|(from[at]&0x7f)):from[at];}
}
bool Xtreme2Coop::menu_eligible(unsigned seat)const {
 return !state_.status[seat]&&(state_.body[seat].ram[0x21]&127)&&
        state_.transport[seat].phase!=1&&state_.transport[seat].phase!=2;
}
void Xtreme2Coop::menu_begin(ModContext& c,std::span<uint16_t,6> cpu,std::span<uint8_t> ram,std::span<uint8_t> hram) {
 auto& s=state_;unsigned opener=s.menu.request?s.menu.request-1:s.anchor;
 if(!menu_eligible(opener)||s.phase||s.actor_owner||scene_active(c)||ram[0xac])return;
 capture(s.body[s.anchor],ram,hram);
 common_items(ram);
 auto child=c.owner->presentation_copy();
 child->presentation_menu_frames();
 unsigned primary=menu_eligible(0)?0:1,other=primary^1;
 project(s.body[other],child->mod_wram(),child->mod_hram());child->mod_wram()[0xc9]=uint8_t(s.first^other);
 // A fallen character's menu is read-only, but still shows its real zero HP.
 auto registers=child->debug_registers();registers.pc=0x442a;child->debug_registers(registers);
 auto raw=s.menu.raw;s.menu={};s.menu.active=1;s.menu.primary=uint8_t(primary);s.menu.raw=raw;s.menu.previous=raw;
 menu_copy_=std::move(child);menu_saved_.clear();
 menu_native_.assign(c.owner->pixels().begin(),c.owner->pixels().end());
 project(s.body[primary],ram,hram);ram[0xc9]=uint8_t(s.first^primary);
 cpu[5]=0x442a; // Host admission already verified a living present opener.
}
void Xtreme2Coop::menu_hook(unsigned seat,unsigned bank,std::span<uint16_t,6> cpu,std::span<uint8_t> ram,std::span<uint8_t> hram) {
 auto& m=state_.menu;if(!m.active)return;
 // Native Options saves only the three common preferences. Run that same
 // save service on the owning machine after publishing the shared settings;
 // the isolated menu never supplies cartridge RAM or exports a battery file.
 if(seat!=m.primary&&bank==0x2f&&cpu[5]==0x6897){m.options_save=1;cpu[5]=0x06c0;return;}
 // Native A/B helpers can cross a frame boundary before the root loop
 // next reports idle. Acquire at entry, before any shared resource read.
 // A denied helper remains in its native substate and retries normally.
 if(bank==0x1d&&(cpu[5]==0x4752||cpu[5]==0x49a7||cpu[5]==0x4a63||cpu[5]==0x4a93)) {
  if(m.busy&&m.busy!=seat+1){cpu[5]=0x06c0;return;}
  m.busy=uint8_t(seat+1);
 }
 if(bank==0x1d&&cpu[5]==0x4771)m.selecting|=uint8_t(1u<<seat);
 if(bank==0x1d&&cpu[5]==0x45a4) {
  if(seat==m.primary&&m.options_save) {
   if(m.options_save==2)m.options_save=0;
   else {
    if(cpu[4]<0xc004||cpu[4]>0xcffc)throw std::runtime_error("Invalid native settings save stack");
    cpu[4]-=2;ram[cpu[4]-0xc000]=0xa4;ram[cpu[4]-0xc000+1]=0x45;
    cpu[0]=uint16_t((cpu[0]&255)|0x2f00);cpu[3]=0x6897;cpu[5]=0x0675;m.options_save=2;return;
   }
  }
  m.idle|=uint8_t(1u<<seat);m.ready|=uint8_t(1u<<seat);m.page[seat]=ram[0xaa3];m.cursor[seat]=ram[0xaa1];
  if(m.busy==seat+1)m.busy=0;
  unsigned code=ram[0xaa0];
  if(m.selecting&(1u<<seat)){if(code==0x8f)m.choice[seat]=uint8_t(ram[0xaa1]+1);m.selecting&=uint8_t(~(1u<<seat));ram[0xaa4]=2;}
  else if(menu_eligible(seat)) {
   if(code==2&&seat==0)m.stage_exit=1;
   if((code==1||code==0xff)&&m.last_idle==3&&!m.busy&&!m.options_save)m.resume=1;
  }
  // A confirms personal equipment without dismissing the partner's menu.
  // Co-op's common resume edge alone completes both original menu returns.
  ram[0xaa0]=0;if(code==2||code==0x77)ram[0xac]=0;
  if(m.resume){ram[0xaa0]=m.stage_exit?2:m.choice[seat]?0x8f:0xff;if(m.choice[seat])ram[0xaa1]=m.choice[seat]-1;if(m.stage_exit)ram[0xac]=3;}
 }
 // The original menu's private cursor/heal sounds use this original service.
 // Shared music/fade calls in other banks are not duplicated.
 if(seat!=m.primary&&cpu[5]==0x0a32&&m.sound_count<m.sounds.size())m.sounds[m.sound_count++]=hram[0x77];
 if(seat==m.primary&&((bank==0x1d&&cpu[5]==0x4590)||cpu[5]==0x21d1)&&m.sound_count) {
  if(cpu[4]<0xc004||cpu[4]>0xcffc)throw std::runtime_error("Invalid native menu sound stack");
  unsigned back=cpu[5];cpu[4]-=2;ram[cpu[4]-0xc000]=uint8_t(back);ram[cpu[4]-0xc000+1]=uint8_t(back>>8);
  hram[0x77]=m.sounds[0];std::move(m.sounds.begin()+1,m.sounds.end(),m.sounds.begin());--m.sound_count;cpu[5]=0x0a32;
 }
}
void Xtreme2Coop::restore_menu(Machine& owner) {
 if(menu_saved_.empty())return;
 auto staged=owner.presentation_copy();staged->presentation_refresh(menu_saved_,menu_saved_sealed_);
 staged->presentation_menu_frames(menu_pixels_);
 if(staged->rom_identity()!=owner.rom_identity())throw std::runtime_error("Native menu cartridge mismatch");
 menu_copy_=std::move(staged);menu_saved_.clear();menu_pixels_.clear();
}
void Xtreme2Coop::menu_step(Machine& child,unsigned seat,uint8_t buttons,uint64_t deadline) {
 auto& m=state_.menu;child.set_buttons(buttons&~uint8_t(64|128));
 while(child.stats().cycles<deadline) {
  auto stat=child.stats();
  if(stat.pc==0x0483)static_package(child.presentation_context(),seat);
  if(stat.pc==0x21d1){m.returned|=uint8_t(1u<<seat);break;}
  if((stat.rom_bank==1&&stat.pc==0x656f)||(stat.rom_bank==5&&stat.pc==0x4001))throw std::runtime_error("A private native menu attempted to advance gameplay");
  auto registers=child.debug_registers();std::array<uint16_t,6> cpu{registers.af,registers.bc,registers.de,registers.hl,registers.sp,registers.pc};
  menu_hook(seat,stat.rom_bank,cpu,child.mod_wram(),child.mod_hram());
  if(cpu[4]!=registers.sp||cpu[5]!=registers.pc){registers.sp=cpu[4];registers.pc=cpu[5];child.debug_registers(registers);}
  child.run_until(stat.cycles+1);
 }
 child.take_audio();
}
void Xtreme2Coop::menu_frame(Machine& owner) {
 auto& s=state_;auto& m=s.menu;if(!m.active)return;
 restore_menu(owner);if(!menu_copy_)throw std::runtime_error("Missing native menu continuation");
 unsigned other=m.primary^1;auto ram=owner.mod_wram();auto child_ram=menu_copy_->mod_wram();
 capture(s.body[m.primary],ram,owner.mod_hram());
 inventory(child_ram,ram);
 uint8_t buttons=menu_eligible(other)?m.raw[other]:0;
 if(!(m.idle&(1u<<m.primary))&&(child_ram[0xaa3]||child_ram[0xaa1]>=10))buttons&=~uint8_t(16);
 if(!(m.returned&(1u<<other)))menu_step(*menu_copy_,other,buttons,owner.stats().cycles);
 capture(s.body[other],child_ram,menu_copy_->mod_hram());inventory(ram,child_ram);
 m.page[other]=child_ram[0xaa3];m.cursor[other]=child_ram[0xaa1];m.last_idle=m.idle;++m.frames;
}
bool Xtreme2Coop::draw_menus(std::span<uint32_t> pixels,unsigned width,std::span<const uint32_t> native,int focus)const {
 const auto& m=state_.menu;if(!m.active||m.ready!=3||!menu_copy_||width<160||pixels.size()<size_t(width)*144)return false;
 std::fill(pixels.begin(),pixels.end(),0xff000000u);
 if(m.resume)return true; // Shared black handoff while original menus restore the world.
 auto other=menu_copy_->pixels();
 for(unsigned seat=0;seat<2;++seat) {
  if(focus>=0&&seat!=unsigned(focus))continue;
  auto source=seat==m.primary?native:other;
  unsigned pane=focus<0?width/2:width,x=focus<0?seat*pane:0;
  // Original 160x144 raster; only pane placement changes. Narrow windows
  // scale the two native menus uniformly to fit their respective halves.
  unsigned shown=std::min(160u,pane),height=shown*144/160,y=(144-height)/2;
  x+=(pane-shown)/2;
  for(unsigned row=0;row<height;++row)for(unsigned col=0;col<shown;++col) {
   unsigned sx=col*160/shown,sy=row*144/height;uint32_t color=source[sy*160+sx];
   // Native menus place the life group at the top, unlike the field HUD.
   if(seat==1&&m.page[seat]!=4&&((sx>=80&&sx<96&&sy<16)||(sx>=72&&sx<80&&sy>=8&&sy<16)))color=0xff000000u;
   pixels[size_t(y+row)*width+x+col]=color;
  }
 }
 return true;
}
std::span<const uint32_t> Xtreme2Coop::menu_world_native(std::span<const uint32_t> current)const {
 return state_.menu.active&&state_.menu.ready!=3&&!menu_native_.empty()?std::span<const uint32_t>(menu_native_):current;
}
}
