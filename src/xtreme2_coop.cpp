#include "xtreme2_coop.hpp"
#include "native_mods.hpp"
#include "xtreme2_controls.hpp"
#include <gbcllr/machine.hpp>
#include <algorithm>
#include <cstring>
#include <cstdlib>
#include <functional>
#include <sstream>
#include <stdexcept>
#include <type_traits>
namespace gbcllr {
namespace {constexpr unsigned personal_addresses[]{0x1e,0x21,0x2d,0x97,0xa1,0xa2,0xa3,0xa6,0xa7,0xbb};}
Xtreme2Coop::Xtreme2Coop(unsigned first) {
 if(first>1)throw std::runtime_error("Invalid co-op character");
 std::memset(&state_,0,sizeof state_);state_.schema=16;state_.first=first;state_.shared=1;state_.shared_width=384;state_.current_package={1,1};
}
Xtreme2Coop::~Xtreme2Coop()=default;
Xtreme2Coop::Xtreme2Coop(Xtreme2Coop&&)noexcept=default;
Xtreme2Coop& Xtreme2Coop::operator=(Xtreme2Coop&&)noexcept=default;
uint8_t Xtreme2Coop::native_buttons(uint8_t b) {
 return uint8_t(((b&1)<<4)|((b&2)<<4)|((b&4)<<4)|((b&8)<<4)|
                ((b&16)>>4)|((b&32)>>4)|((b&64)>>4)|((b&128)>>4));
}
void Xtreme2Coop::inputs(uint8_t first,uint8_t second){
 // Native Select exchanges characters. Co-op reserves it for lifecycle
 // commands, so it must never enter the original exchange dispatcher.
 state_.held={uint8_t(native_buttons(first)&~4u),uint8_t(native_buttons(second)&~4u)};
 state_.select={uint8_t(bool(first&64)),uint8_t(bool(second&64))};
 auto& m=state_.menu;m.raw={first,second};
 if(m.active){
  bool resume=false,transaction=false;
  for(unsigned i=0;i<2;++i)if(menu_eligible(i)) {
   uint8_t edge=m.raw[i]&~m.previous[i];resume|=bool(edge&128);
   transaction|=bool(edge&16)&&(m.page[i]||m.cursor[i]>=10);
  }
  if(m.last_idle==3&&!m.busy&&!m.options_save&&!transaction&&(resume||m.stage_exit))m.resume=1;
  m.previous=m.raw;m.idle=0;
 }
}
void Xtreme2Coop::capture(Body& b,std::span<uint8_t> ram,std::span<uint8_t> hram) {
 std::copy_n(ram.data()+0x100,256,b.ram.data());
 std::copy_n(hram.data()+0x30,12,b.graphics.data());
 std::copy_n(ram.data()+0x8d,8,b.palette.data());
 b.equipment={ram[0x83],ram[0x84],ram[0xd3],ram[0xd4]};
 for(unsigned i=0;i<b.personal.size();++i)b.personal[i]=ram[personal_addresses[i]];
 std::copy_n(ram.data()+0xaae,10,b.hitbox.data());
 for(unsigned i=0;i<2;++i)if(&b==&state_.body[i]&&state_.status[i]==1)b.ram[0x21]=0;
}
void Xtreme2Coop::project(const Body& b,std::span<uint8_t> ram,std::span<uint8_t> hram) {
 std::copy(b.ram.begin(),b.ram.end(),ram.data()+0x100);
 std::copy(b.graphics.begin(),b.graphics.end(),hram.data()+0x30);
 std::copy(b.palette.begin(),b.palette.end(),ram.data()+0x8d);
 ram[0x83]=b.equipment[0];ram[0x84]=b.equipment[1];ram[0xd3]=b.equipment[2];ram[0xd4]=b.equipment[3];
 for(unsigned i=0;i<b.personal.size();++i)ram[personal_addresses[i]]=b.personal[i];
 std::copy(b.hitbox.begin(),b.hitbox.end(),ram.data()+0xaae);
}
int Xtreme2Coop::object_key(const ModContext& c,unsigned page) {
 if(page>=0xa0&&page<0xc0&&c.ram_bank<16)return c.ram_bank*32+page-0xa0;
 if(page==0xcb||page==0xcc)return 512+page-0xc0;
 if(page>=0xd6&&page<0xe0&&c.wram_bank<8)return 528+c.wram_bank*16+page-0xd0;
 return -1;
}
uint8_t Xtreme2Coop::peek(const ModContext& c,unsigned address) {
 if(address>=0xa000&&address<0xc000){unsigned offset=c.ram_bank*0x2000+address-0xa000;return c.eram&&offset<c.eram_size?c.eram[offset]:0;}
 if(address>=0xc000&&address<0xd000)return c.wram[address-0xc000];
 if(address>=0xd000&&address<0xe000)return c.wram[c.wram_bank*0x1000+address-0xd000];
 return 0;
}
void Xtreme2Coop::write(const ModContext& c,uint16_t address,uint8_t value) {
 bike_write(c,address,value);
 if(address==0xc0a4)state_.area_owner=value?uint8_t(state_.phase?state_.phase:state_.actor_source):0;
 unsigned scene_owner=state_.reward_owner&&state_.reward_stage==c.wram[0x5f]?state_.reward_owner:state_.actor_owner;
 if(state_.reward_owner&&c.sp!=UINT16_MAX&&c.rom_bank==0xc&&c.instruction_pc>=0x7a9e&&c.instruction_pc<=0x7b18&&
    (((address>=0xc06d&&address<=0xc076)&&((value^peek(c,address))&value&0x40))||
     (address==0xc0a0&&((value^peek(c,address))&value))))++state_.reward_grants;
 if(state_.actor_owner&&address==0xca26&&value) {
  unsigned other=(state_.actor_owner-1)^1;const auto& a=state_.body[state_.actor_owner-1].ram;const auto& b=state_.body[other].ram;
  auto word=[](const auto& body,unsigned at){return int(body[at]|unsigned(body[at+1])<<8);};
  if(std::abs(word(a,8)-word(b,8))>96||std::abs(word(a,5)-word(b,5))>80) {
   state_.pending_anchor=state_.actor_owner;state_.scene_owner=state_.actor_owner;depart(c,other);
  }
 }
 if(state_.actor_owner&&address==0xc066&&value!=peek(c,address)) {
  state_.pending_anchor=state_.actor_owner;state_.pending_checkpoint=1;
  state_.pending_bounds={int(c.wram[0xa0d]|unsigned(c.wram[0xa0e])<<8),int(c.wram[0xa40]|unsigned(c.wram[0xa41])<<8)+160};
 } else if(state_.actor_owner&&(address==0xc0c2||address==0xc00c||address==0xc014)&&value&&!peek(c,address)) {
  state_.pending_anchor=uint8_t(scene_owner);state_.scene_owner=uint8_t(scene_owner);
  depart(c,(scene_owner-1)^1);
 }
 if(state_.initialized&&address==0xc9b0&&!value&&peek(c,address)) {
  unsigned owner=scene_owner?scene_owner:state_.anchor+1;
  state_.scene_owner=uint8_t(owner);depart(c,(owner-1)^1);
 }
 int key=object_key(c,address>>8);if(key<0)return;
 auto& s=state_;unsigned slot=unsigned(key);
 if((address&255)==0) {
  if(!value){s.object_owner[slot]=0;s.object_weapon[slot]=0;s.object_package[slot]=0;s.last_damage[slot]=0;if(s.door_owner&&s.door_key==slot)s.door_owner=0;}
  else if(!peek(c,address)) {
   unsigned owner=s.phase?unsigned(s.phase):s.actor_source;s.object_owner[slot]=uint8_t(owner);
   if(owner) {
    s.object_package[slot]=s.current_package[owner-1];
    if(!s.phase&&s.actor_source&&s.object_owner[s.actor_key]==owner)s.object_package[slot]=s.object_package[s.actor_key];
   }
   if(owner&&!(s.first^(owner-1))) {
    unsigned weapon=peek(c,0xc131);
    if(!s.phase&&s.actor_source&&s.object_owner[s.actor_key]==owner)weapon=s.object_weapon[s.actor_key];
    s.object_weapon[slot]=uint8_t(weapon<10?weapon:0);
   }
  }
 } else if((address&255)==0x21&&!s.object_owner[slot]&&(value&127)<(peek(c,address)&127)) {
  unsigned owner=s.hit_owner&&s.hit_key==slot?s.hit_owner:s.phase?s.phase:s.actor_source;
  if(owner) {
   s.last_damage[slot]=uint8_t(owner);++s.damage_events[owner-1];
   if(!(value&127)){
    s.defeated_owner=uint8_t(owner);s.defeated_key=uint16_t(slot);
    // The native boss HUD names the actual boss actor. Ordinary enemy
    // defeats cannot replace the owner retained through the reward scene.
    if((address>>8)==c.wram[0x9a1]){s.reward_owner=uint8_t(owner);s.reward_stage=c.wram[0x5f];s.reward_grants=0;s.pending_anchor=uint8_t(owner);s.scene_owner=uint8_t(owner);}
   }
  }
  s.hit_owner=0;
 }
}
uint8_t Xtreme2Coop::read(const ModContext& c,uint16_t address,uint8_t value)const {
 if(state_.reward_owner&&address==0xc0c9&&c.sp!=UINT16_MAX&&c.rom_bank==0xc&&
    c.instruction_pc>=0x7867&&c.instruction_pc<=0x7b18&&c.wram[0x5f]==state_.reward_stage)
  return uint8_t(state_.first^(state_.reward_owner-1));
 if(c.sp!=UINT16_MAX&&address==0xc10d&&c.instruction_pc==0x12d5&&state_.phase) {
  unsigned phase=state_.transport[state_.phase-1].phase;if(phase==1||phase==2||state_.status[state_.phase-1])return 0;
 }
 if(!state_.hit_owner||c.sp==UINT16_MAX)return value;
 auto& body=state_.body[state_.hit_owner-1];
 // Native damage formulas consult the attacking character's parts. The
 // enemy may currently target the other player; virtualize these reads only
 // for a confirmed original collision, until its HP write/scope return.
 if(address==0xc0c9)return uint8_t(state_.first^(state_.hit_owner-1));
 if(address==0xc083)return body.equipment[0];
 if(address==0xc0d3)return body.equipment[2];
 if(address==0xc0d4)return body.equipment[3];
 return value;
}
std::vector<uint8_t> Xtreme2Coop::graphics(const ModContext& c,unsigned bank,unsigned root) {
 // Retail player graphics are lists of tile-pair producers: CALL helpers,
 // LD DE,ROM address, JP 113B (copy32), RET. Decode those original assets
 // directly into host storage before publishing a pose, independent of the
 // timing/capacity of the guest's shared graphics staging page.
 unsigned budget=4096;uint16_t source=0,hl=0;uint8_t a=0;std::vector<uint8_t> result;
 auto byte=[&](unsigned bank,unsigned pc) {size_t offset=size_t(bank)*0x4000+pc-0x4000;
  if(pc<0x4000||pc>=0x8000||offset>=c.rom_size)throw std::runtime_error("Invalid original player graphics address");return c.rom[offset];};
 std::function<void(unsigned,unsigned,unsigned)> decode=[&](unsigned bank,unsigned pc,unsigned depth) {
  if(depth>32)throw std::runtime_error("Player graphics recursion exceeded");
  for(;;) {
   if(!budget--)throw std::runtime_error("Player graphics instruction budget exceeded");
   auto op=byte(bank,pc++);if(op==0xc9)return;if(op==0)continue;
   if(op==0x3e){a=byte(bank,pc++);continue;}
   if(op!=0x11&&op!=0x21&&op!=0xcd&&op!=0xc3){std::ostringstream error;error<<"Unsupported original player graphics producer "<<std::hex<<bank<<':'<<pc-1<<" opcode="<<unsigned(op)<<" root="<<root;throw std::runtime_error(error.str());}
   unsigned argument=byte(bank,pc)|unsigned(byte(bank,pc+1))<<8;pc+=2;
   if(op==0x11)source=uint16_t(argument);
   else if(op==0x21)hl=uint16_t(argument);
   else if(op==0xcd){if(argument==0x675)decode(a,hl,depth+1);else decode(bank,argument,depth+1);}
   else if(argument==0x113b) {
    if(result.size()>2048-32)throw std::runtime_error("Player pose exceeds host tile budget");
    for(unsigned i=0;i<32;++i)result.push_back(byte(bank,(source&0xff00)|uint8_t(source+i)));
    source=uint16_t((source&0xff00)|uint8_t(source+32));return;
   } else pc=argument;
  }
 };
 decode(bank,root,0);return result;
}
uint8_t Xtreme2Coop::rom_byte(const ModContext& c,unsigned bank,unsigned address) {
 size_t at=address<0x4000?address:size_t(bank)*0x4000+address-0x4000;
 if(address>=0x8000||at>=c.rom_size||!c.rom)throw std::runtime_error("Invalid original co-op animation address");
 return c.rom[at];
}
bool Xtreme2Coop::scene_active(const ModContext& c)const {
 return state_.door_owner||c.wram[0xc2]||c.wram[0xc]||c.wram[0x14]||!c.wram[0x9b0];
}
bool Xtreme2Coop::encounter_active(const ModContext& c) {
 if(c.wram[0x9a1])return true; // Original shared boss/miniboss health-meter owner.
 // The introductory armored guard has no boss meter. Its original native
 // enemy class28 remains an encounter until its actual HP reaches zero.
 if(c.eram&&c.eram_size>=0x2000)for(unsigned at=0x1800;at<0x2000;at+=256)
  if(c.eram[at]&&c.eram[at+0x2d]==0x28&&(c.eram[at+0x21]&127))return true;
 return false;
}
void Xtreme2Coop::shared_world(bool enabled,unsigned width) {
 if(width<160||width>4096)throw std::runtime_error("Invalid co-op shared viewport");
 state_.shared=uint8_t(enabled);state_.shared_width=width;
}
std::array<int,4> Xtreme2Coop::activation_bounds(const ModContext& c)const {
 auto word=[&](unsigned at){return int(c.wram[at]|unsigned(c.wram[at+1])<<8);};
 int cx=word(0xa08),cy=word(0xa05),margin=int(state_.shared_width-160)/2;
 std::array<int,4> bounds{cx-margin,cx+160+margin,cy,cy+144};
 if(state_.initialized&&state_.stage==c.wram[0x5f]&&!state_.menu.active)for(unsigned seat=0;seat<2;++seat) {
  if(state_.status[seat]||state_.transport[seat].phase||!(state_.body[seat].ram[0x21]&127))continue;
  auto camera=seat==state_.anchor?std::array<int,2>{cx,cy}:std::array<int,2>{state_.cameras[seat][0],state_.cameras[seat][1]};
  bounds[0]=std::min(bounds[0],camera[0]-margin);bounds[1]=std::max(bounds[1],camera[0]+160+margin);
  bounds[2]=std::min(bounds[2],camera[1]);bounds[3]=std::max(bounds[3],camera[1]+144);
 }
 bool riding=state_.bikes.active&&(state_.body[0].ram[0xe5]||state_.body[1].ram[0xe5]);
 int left=riding?state_.bikes.bounds[0]:word(0xa0d),right=riding?state_.bikes.bounds[1]:word(0xa40)+160;
 bounds[0]=std::max(bounds[0],left);bounds[1]=std::min(bounds[1],right);
 return bounds;
}
void Xtreme2Coop::depart(const ModContext& c,unsigned seat) {
 auto& s=state_;auto& t=s.transport[seat];const auto& b=s.body[seat];
 if(t.phase==1||t.phase==2||!(b.ram[0x21]&127))return;
 bike_depart(seat);
 t={};t.phase=1;t.x=b.ram[8]|unsigned(b.ram[9])<<8;t.y=b.ram[5]|unsigned(b.ram[6])<<8;
 t.camera=s.cameras[seat];
 t.bounds={int(c.wram[0xa0d]|unsigned(c.wram[0xa0e])<<8),int(c.wram[0xa40]|unsigned(c.wram[0xa41])<<8)+160};
 // Original animation2 is the teleport beam. Its frame list supplies both
 // the original piece list and its original cross-bank tile producer.
 unsigned table=(s.first^seat)?0x1665:((b.equipment[0]&15)==15?0x1551:0x143d),entry=table+8;
 unsigned bank=rom_byte(c,0,entry),frame=rom_byte(c,0,entry+2)|unsigned(rom_byte(c,0,entry+3))<<8;
 unsigned pose=rom_byte(c,bank,frame)|unsigned(rom_byte(c,bank,frame+1))<<8;
 t.hold=rom_byte(c,bank,frame+2);unsigned count=rom_byte(c,bank,pose);
 if(count>64||!t.hold)throw std::runtime_error("Invalid original teleport pose");
 bool flip=b.ram[0x6b]!=0;
 for(unsigned n=0;n<count;++n){unsigned at=pose+2+n*4;int dy=int8_t(rom_byte(c,bank,at)),dx=int8_t(rom_byte(c,bank,at+1));if(dy==127)break;
  t.pieces[t.count++]={int16_t(flip?8-dx:dx),int16_t(dy-16),uint8_t(rom_byte(c,bank,at+2)+0x40),uint8_t((rom_byte(c,bank,at+3)^(flip?0x20:0))|8)};}
 auto tiles=graphics(c,rom_byte(c,bank,frame+4),rom_byte(c,bank,frame+5)|unsigned(rom_byte(c,bank,frame+6))<<8);
 std::copy(tiles.begin(),tiles.end(),t.texture.begin()+0x400);
 for(unsigned n=0;n<4;++n){unsigned color=b.palette[n*2]|unsigned(b.palette[n*2+1])<<8;auto expand=[](unsigned v){return(v<<3)|(v>>2);};
  t.colors[n]=0xff000000u|(expand(color&31)<<16)|(expand((color>>5)&31)<<8)|expand((color>>10)&31);}
 s.sound_pending=0x36;++s.departures;
}
void Xtreme2Coop::world_tick(const ModContext& c,std::span<uint8_t> ram,std::span<uint8_t> hram) {
 auto& s=state_;if(s.stage!=ram[0x5f])return;
 capture(s.body[s.anchor],ram,hram);s.checkpoint=ram[0x66];
 common_items(ram);
 participation(c,ram,hram);
 if((s.status[s.anchor]||!(s.body[s.anchor].ram[0x21]&127))&&!s.status[s.anchor^1]&&(s.body[s.anchor^1].ram[0x21]&127)) {
  s.anchor^=1;project(s.body[s.anchor],ram,hram);ram[0xc9]=uint8_t(s.first^s.anchor);
 }
 auto word=[](const auto& b,unsigned at){return int(b[at]|unsigned(b[at+1])<<8);};
 unsigned other=s.anchor^1;
 bool riding=s.bikes.active&&(s.body[0].ram[0xe5]||s.body[1].ram[0xe5]);
 if(!riding&&!s.scene_owner&&!scene_active(c)&&!s.transport[other].phase&&(s.body[other].ram[0x21]&127)) {
  int dx=word(s.body[other].ram,8)-word(s.body[s.anchor].ram,8);
  int dy=word(s.body[other].ram,5)-word(s.body[s.anchor].ram,5);
  // The introductory route and its global script gates progress to the
  // right. Other stage gates choose their owner through native contact.
  if((s.stage==0&&dx>32)||(s.shared&&(dx>int(s.shared_width/4)||dy< -96))) {
   s.anchor=other;other^=1;project(s.body[s.anchor],ram,hram);ram[0xc9]=uint8_t(s.first^s.anchor);
  }
  if(s.shared&&(std::abs(dx)>int(s.shared_width)-32||std::abs(dy)>108))depart(c,other);
 }
 if(scene_active(c)&&!s.transport[s.anchor^1].phase){s.scene_owner=s.anchor+1;depart(c,s.anchor^1);}
 other=s.anchor^1;auto& t=s.transport[other];auto& a=s.body[s.anchor].ram;
 if(t.phase==2&&!s.status[other]&&!scene_active(c)&&(a[0x21]&127)&&(a[0x60]&128)) {
  // Overlapping players are legal. The leader's actual grounded position
  // is the known supported landing; do not guess a floor beyond a door.
  auto& b=s.body[other];b.ram[2]=0x22;b.ram[3]=0;b.ram[1]=2;b.ram[0xd]=1;
  for(unsigned at:{0x28u,0x2cu,0x60u,0x66u,0x70u,0x97u,0xa5u,0xa7u,0xb6u,0xb7u,0xb8u,0xbcu,0xbdu,0xbeu})b.ram[at]=0;
  b.ram[4]=b.ram[7]=0;b.ram[8]=a[8];b.ram[9]=a[9];
  unsigned y=unsigned(std::max(0,word(a,5)-(ram[0x66]?80:0)));
  b.ram[5]=uint8_t(y);b.ram[6]=uint8_t(y>>8);b.ram[0x30]=120;b.personal={};
  s.cameras[other]=s.cameras[s.anchor];t.y=word(a,5);t.phase=3;s.scene_owner=0;
 }
}
void Xtreme2Coop::isolate_death(unsigned seat,std::span<uint8_t> ram) {
 auto& s=state_;unsigned other=seat^1;
 s.death_isolated=uint8_t(!(ram[0x121]&127)&&!s.status[other]&&(s.body[other].ram[0x21]&127));
 if(s.death_isolated){std::copy_n(ram.data()+0xd,7,s.death_globals.data());s.death_globals[7]=ram[0xac];}
}
void Xtreme2Coop::participation(const ModContext& c,std::span<uint8_t> ram,std::span<uint8_t> hram) {
 auto& s=state_;
 auto alive=[&](unsigned i){return !s.status[i]&&(s.body[i].ram[0x21]&127);};
 unsigned pressed=0;for(unsigned i=0;i<2;++i)if(s.select[i]&&!s.select_previous[i])pressed|=1u<<i;
 s.select_previous=s.select;
 if(!alive(s.anchor)&&alive(s.anchor^1)) {
  s.anchor^=1;project(s.body[s.anchor],ram,hram);ram[0xc9]=uint8_t(s.first^s.anchor);
 }
 if(scene_active(c)||ram[0xac]){s.select_hold={};return;}
 for(unsigned i=0;i<2;++i) {
  unsigned other=i^1,bit=1u<<i;auto& b=s.body[i];
  if(s.status[i]==1){s.select_hold[i]=0;continue;} // Fallen until the shared team retry.
  if(s.status[i]==2&&(pressed&bit))s.join_pending|=uint8_t(bit);
  const auto& leader=s.body[other].ram;
  bool safe=alive(other)&&!s.transport[other].phase&&(leader[0x60]&128)&&!encounter_active(c);
  if(s.status[i]) {
   s.select_hold[i]=0;
   if(!(s.join_pending&bit)||!safe||s.transport[i].phase==1)continue;
   s.status[i]=0;s.join_pending&=uint8_t(~bit);s.transport[i].phase=2;
  } else {
   if(!s.select[i]||!safe||s.transport[i].phase||!(b.ram[0x21]&127)){s.select_hold[i]=0;continue;}
   int x=b.ram[8]|unsigned(b.ram[9])<<8,y=b.ram[5]|unsigned(b.ram[6])<<8;
   if(std::abs(x-int(leader[8]|unsigned(leader[9])<<8))>=int(s.shared_width)-16||
      std::abs(y-int(leader[5]|unsigned(leader[6])<<8))>=128){s.select_hold[i]=0;continue;}
   if(++s.select_hold[i]<90)continue;
   depart(c,i);s.status[i]=2;s.select_hold={};
   if(s.anchor==i){s.anchor=other;project(s.body[other],ram,hram);ram[0xc9]=uint8_t(s.first^other);}
  }
 }
}
void Xtreme2Coop::instruction(ModContext& c,std::span<uint16_t,6> cpu,std::span<uint8_t> ram,std::span<uint8_t> hram,Xtreme2Controls* controls) {
 if(ram.size()<0x1000||hram.size()<0x7f)return;
 auto& s=state_;
 // An isolated death cannot clear the survivor's global object-freeze flag.
 // Let only its original death-particle actor run through the normal dispatcher.
 if(s.initialized&&c.rom_bank==1&&cpu[5]==0x6d02){
  unsigned page=cpu[3]>>8;int key=object_key(c,page);
  if(page>=0xda&&page<0xe0&&key>=0&&s.object_owner[unsigned(key)]&&peek(c,page<<8)&&peek(c,(page<<8)+0x2d)==14)cpu[5]=0x6d04;
 }
 if(cpu[5]==0x0483) {
  unsigned owner=s.phase?s.phase:s.actor_source?s.actor_source:s.menu.active?unsigned(s.menu.primary)+1:0;
  if(owner)static_package(c,owner-1);
 }
 if(controls&&controls->active())return;
 // An arrival beam uses the leader's known supported floor. Passing through
 // intervening ceilings/walls is presentation, not ordinary walking terrain.
 // Keep the original beam controller and animation, with a host landing target.
 if(s.phase&&s.transport[s.phase-1].phase==3&&ram[0x102]==0x22){
  auto& target=s.transport[s.phase-1];int y=ram[0x105]|unsigned(ram[0x106])<<8;
  if(cpu[5]==0x0c8f&&hram[0x22]==0xc1&&y<target.y){hram[0x20]=hram[0x53]=hram[0x5f]=0;}
  if(c.rom_bank==5&&cpu[5]==0x4af6&&y>=target.y){ram[0x105]=uint8_t(target.y);ram[0x106]=uint8_t(target.y>>8);ram[0x128]|=128;ram[0x160]|=128;}
 }
 if(bike_hook(c,cpu,ram,hram))return;
 if(s.initialized&&c.rom_bank==0x3d&&cpu[5]==0x44d1) {
  auto bounds=activation_bounds(c);std::array<int32_t,2> next{bounds[2]/16,bounds[3]/16};
  s.scan_vertical_changed=uint8_t(next!=s.scan_vertical);s.scan_vertical=next;
 }
 // The retail streamer's previous-footprint test only tracks horizontal
 // movement. A second player's vertical entry also needs one original scan.
 if(s.initialized&&c.rom_bank==0x3d&&cpu[5]==0x4721&&s.scan_vertical_changed) {
  cpu[0]=uint16_t(0x100|(cpu[0]&255));cpu[5]=0x4723;
 }
 if(s.menu.active)menu_hook(s.menu.primary,c.rom_bank,cpu,ram,hram);
 if(s.initialized&&!s.menu.active&&c.rom_bank==0x1d&&cpu[5]==0x440b&&c.owner)menu_begin(c,cpu,ram,hram);
 if(s.menu.active&&cpu[5]==0x21d1) {
  unsigned seat=s.menu.primary;s.menu.returned|=uint8_t(1u<<seat);capture(s.body[seat],ram,hram);
  if(s.menu.returned!=3) {
   if(cpu[4]<0xc004||cpu[4]>0xcffc)throw std::runtime_error("Invalid native menu return stack");
   cpu[4]-=2;ram[cpu[4]-0xc000]=0xd1;ram[cpu[4]-0xc000+1]=0x21;cpu[5]=0x06c1;return;
  }
  project(s.body[s.anchor],ram,hram);ram[0xc9]=uint8_t(s.first^s.anchor);
  s.menu.active=0;menu_copy_.reset();menu_saved_.clear();menu_pixels_.clear();menu_native_.clear();
 }
 if(s.initialized&&s.retry_pending&&cpu[5]==0x2078)++s.team_retries; // Original single life decrement.
 if(s.retry_pending&&c.rom_bank==1&&cpu[5]==0x4829) {
  s.initialized=0;s.anchor=0;s.status={};s.transport={};s.scene_owner=0;s.join_pending=0;
  s.select_hold={};s.retry_pending=0;s.retry_initializing=1;
 }
 if(c.rom_bank==5&&s.phase) {
  if(cpu[5]==0x42e8&&s.death_isolated)cpu[5]=0x42eb; // Keep the survivor's stage music.
  if(cpu[5]==0x4362) {
   unsigned seat=s.phase-1,other=seat^1;
   if(!s.status[other]&&(s.body[other].ram[0x21]&127)) {
    s.status[seat]=1;ram[0x10d]=0;cpu[5]=0x06c0; // Original RET; no shared retry.
   } else s.retry_pending=1;
  }
 }
 if(s.initialized&&!s.phase&&!s.actor_owner&&c.rom_bank==1&&cpu[5]==0x656f&&s.sound_pending&&cpu[4]>=0xc004&&cpu[4]<=0xcffc) {
  // Invoke the original sound service through a real near-call frame at
  // the world boundary. No ROM byte or unrelated register is replaced.
  cpu[4]-=2;ram[cpu[4]-0xc000]=0x6f;ram[cpu[4]-0xc000+1]=0x65;
  hram[0x77]=s.sound_pending;s.sound_pending=0;cpu[5]=0x0a32;return;
 }
 if(s.initialized&&cpu[5]==0x062e) {
  unsigned held=s.held[s.anchor];
  if(s.menu.active) {
   unsigned seat=s.menu.primary;held=s.held[seat]&~8u;
   if(!menu_eligible(seat))held=0;
   if(!(s.menu.last_idle&(1u<<(seat^1)))&&(ram[0xaa3]||ram[0xaa1]>=10))held&=~1u;
  } else if(s.reward_owner&&c.wram[0xac]==1) {
   // The original reward/save screens are common campaign UI. Either
   // seat may advance them; P1 directions win a simultaneous conflict.
   held=(s.held[0]&0xf0)?(s.held[0]&0xf0):(s.held[1]&0xf0);
   held|=(s.held[0]|s.held[1])&0x0b;
  } else if(scene_active(c))held|=(s.held[0]|s.held[1])&3;
  if(!s.menu.active&&!scene_active(c)&&!ram[0xac]) {
   unsigned starts=0;for(unsigned i=0;i<2;++i)if(menu_eligible(i)&&(s.held[i]&8))starts|=1u<<i;
   held=(held&~8u)|(starts?8u:0u);
   if(starts&&!(s.world_previous&8))s.menu.request=uint8_t((starts&1)?1:2);
  }
  hram[0xf]=uint8_t(held);hram[0x10]=uint8_t(held&~s.world_previous);s.world_previous=uint8_t(held);
 }
 if(s.initialized&&!s.phase&&!s.actor_owner&&c.rom_bank==1&&cpu[5]==0x656f)world_tick(c,ram,hram);
 if(s.collision_owner&&cpu[4]==s.collision_sp+2&&cpu[5]==s.collision_return&&c.rom_bank==s.collision_bank) {
  s.hit_owner=(ram[0xacb]&1)?s.collision_owner:0;s.hit_key=s.collision_key;s.collision_owner=0;
 }
 // Original screen-wide attacks use the C0A4 pulse and bypass the ordinary
 // projectile collision helper. Retain their producer through the enemy's
 // native damage service, even when that enemy targets the other player.
 if(s.initialized&&cpu[5]==0x2bef&&ram[0xa4]&&s.area_owner){
  int key=object_key(c,cpu[2]>>8);if(key>=0){s.hit_owner=s.area_owner;s.hit_key=uint16_t(key);}
 }
 // The original door confirms contact before its camera/auto-walk sequence.
 // Keep that participant for the entire sequence; recomputing nearest-player
 // ownership while the door moves can hand its script to the parked partner.
 if(s.actor_owner&&c.rom_bank==0x18&&cpu[5]==0x7807&&!s.door_owner){
  s.door_owner=s.actor_owner;s.door_key=s.actor_key;s.scene_owner=s.actor_owner;s.pending_anchor=s.actor_owner;depart(c,(s.actor_owner-1)^1);
 }
 if(cpu[5]==0x28cb&&s.initialized&&cpu[4]>=0xc002&&cpu[4]<=0xcffc) {
  unsigned d=cpu[2]>>8,e=cpu[2]&255;int dk=object_key(c,d),ek=object_key(c,e);
  unsigned first=dk>=0?s.object_owner[unsigned(dk)]:0,second=ek>=0?s.object_owner[unsigned(ek)]:0;
  unsigned owner=0;int target=-1;s.hit_owner=0;
  if(first&&!second&&ek>=0){owner=first;target=ek;}
  else if(second&&!first&&dk>=0){owner=second;target=dk;}
  else if(e==0xc1&&dk>=0){owner=s.phase?s.phase:s.actor_owner;target=dk;}
  else if(d==0xc1&&ek>=0){owner=s.phase?s.phase:s.actor_owner;target=ek;}
  if(owner&&target>=0) {
   s.collision_owner=uint8_t(owner);s.collision_key=uint16_t(target);s.collision_sp=cpu[4];
   s.collision_return=uint16_t(ram[cpu[4]-0xc000]|unsigned(ram[cpu[4]-0xc000+1])<<8);s.collision_bank=c.rom_bank;
  }
 }
 // Only the shared-view leader advances the retail follow camera. Both
 // independent host views render the same world without calling it again.
 if(c.rom_bank==1&&s.phase&&s.phase!=s.anchor+1&&(cpu[5]==0x4321||cpu[5]==0x41c1)){cpu[5]=0x06c0;return;}
 if(s.actor_owner&&c.rom_bank==1&&cpu[5]==s.actor_return&&cpu[4]==s.actor_sp+2) {
  capture(s.body[s.actor_owner-1],ram,hram);
  common_items(ram);
  if(s.pending_anchor){s.anchor=s.pending_anchor-1;s.pending_anchor=0;}
  if(s.pending_checkpoint) {
   unsigned other=s.anchor^1;const auto& b=s.body[other].ram;
   int x=b[8]|unsigned(b[9])<<8,y=b[5]|unsigned(b[6])<<8;
   auto word=[&](unsigned at){return int(ram[at]|unsigned(ram[at+1])<<8);};
   bool riding=s.bikes.active&&(s.body[0].ram[0xe5]||s.body[1].ram[0xe5]);
   if(!riding&&(x<word(0xa0d)||x>=word(0xa40)+160||y<word(0xa10)||y>=word(0xa12)+144)) {
    s.scene_owner=s.anchor+1;depart(c,other);s.transport[other].bounds=s.pending_bounds;
   }
   s.pending_checkpoint=0;
  }
  project(s.body[s.anchor],ram,hram);
  ram[0xc9]=uint8_t(s.first^s.anchor);s.actor_owner=s.actor_source=s.hit_owner=s.collision_owner=0;
 }
 // The original bank-1 object stubs CALL the far wrapper and then RET.
 // Project a target/shot owner for that single actor update, retaining the
 // common scheduler and native collision/damage/pickup handlers once each.
 if(s.initialized&&!s.menu.active&&s.stage==ram[0x5f]&&!s.phase&&!s.actor_owner&&c.rom_bank==1&&cpu[5]==0x675&&cpu[4]>=0xc002&&cpu[4]<=0xcffc) {
  unsigned caller=ram[cpu[4]-0xc000]|unsigned(ram[cpu[4]-0xc000+1])<<8;
  // AA/CB dispatchers install FFA2 before entering their stub; D is only
  // loaded by the far callee. Enemy dispatchers happen to load D earlier.
  unsigned page=hram[0x22];int key=object_key(c,page);
  size_t stub=0x4000+caller-0x4000;
  bool native_stub=caller>=0x6600&&caller<=0x761f&&stub<c.rom_size&&c.rom[stub]==0xc9&&
   c.rom[stub-3]==0xcd&&c.rom[stub-2]==0x75&&c.rom[stub-1]==6;
  if(native_stub&&key>=0&&peek(c,page<<8)) {
   capture(s.body[s.anchor],ram,hram);
   unsigned owner=s.object_owner[unsigned(key)];s.actor_source=uint8_t(owner);
   if(s.door_owner&&s.door_key==unsigned(key))owner=s.door_owner;
   if(!owner) {
    int x=peek(c,(page<<8)+8)|int(peek(c,(page<<8)+9))<<8,y=peek(c,(page<<8)+5)|int(peek(c,(page<<8)+6))<<8;
    bool capsule=(cpu[0]>>8)==0x36&&(cpu[3]==0x55d0||cpu[3]==0x58e2);
    unsigned capsule_character=(ram[0x5f]<5||ram[0x5f]==11)?0u:1u; // Original hologram/capsule character gates.
    auto distance=[&](unsigned i) {auto& b=s.body[i].ram;if(s.status[i]||!(b[0x21]&127))return int64_t(1)<<60;
     if(s.transport[i].phase)return int64_t(1)<<60;
     if(capsule&&(s.first^i)!=capsule_character)return int64_t(1)<<60;
     int64_t dx=(b[8]|int(b[9])<<8)-x,dy=(b[5]|int(b[6])<<8)-y;return dx*dx+dy*dy;};
    owner=distance(s.anchor^1)<distance(s.anchor)?(s.anchor^1)+1:s.anchor+1;
   }
   s.actor_owner=uint8_t(owner);s.actor_sp=cpu[4];s.actor_return=uint16_t(caller);s.actor_key=uint16_t(key);
   if(owner!=s.anchor+1)project(s.body[owner-1],ram,hram);
   ram[0xc9]=uint8_t(s.first^(owner-1));
  }
 }
 if(c.rom_bank!=5)return;
 // The retail CALL4079 already installed its real return to4078. Draw a
 // parked traveller without running physics, contact or scripted actions.
 if(cpu[5]==0x4079&&s.phase&&(s.status[s.phase-1]||s.transport[s.phase-1].phase==1||s.transport[s.phase-1].phase==2))cpu[5]=0x4af6;
 if(s.phase&&s.phase!=s.anchor+1&&cpu[5]==0x4af6&&(ram[0x121]&127)&&
    s.transport[s.phase-1].phase!=1&&s.transport[s.phase-1].phase!=2) {
  // The retail camera controller also enforces physical room edges and the
  // bottomless-pit threshold. Keep those gameplay rules when its second
  // presentation-camera update is replaced by the host follow service.
  auto word=[&](unsigned at){return int(ram[at]|unsigned(ram[at+1])<<8);};
  int left=word(0xa0d)+8,right=word(0xa40)+152;
  if(s.bikes.active&&ram[0x2d]){int margin=int(s.shared_width-160)/2;left=std::max(s.bikes.bounds[0],word(0xa0d)-margin)+8;right=std::min(s.bikes.bounds[1],word(0xa40)+160+margin)-8;}
  if(!ram[0x1e5]&&left<=right){int x=std::clamp(int(int16_t(word(0x108))),left,right);ram[0x108]=uint8_t(x);ram[0x109]=uint8_t(x>>8);}
  if(word(0x105)>=word(0xa12)+176){ram[0x121]=0;ram[0x1a5]=0x8c;}
 }
 if(cpu[5]==0x4b36&&s.phase) {
  s.graphics_size[s.phase-1]=0;
  unsigned destination=(unsigned(hram[0x49])<<8)|hram[0x4a];
  if(destination>=0xc300&&destination<=0xcf00&&(destination&15)==0)
   hram[0x48]=uint8_t((destination-0xc300)/16);
 }
 if(cpu[5]==0x4b39&&s.phase) {
  unsigned owner=s.phase-1,size=s.graphics_size[owner],base=s.graphics_base[owner];
  unsigned destination=(unsigned(hram[0x49])<<8)|hram[0x4a];
  if(size&&(destination!=0xc300+base+size||base+size>2048||
     !std::equal(s.build_texture[owner].begin()+0x400+base,s.build_texture[owner].begin()+0x400+base+size,ram.data()+0x300+base)))
   throw std::runtime_error("Host player graphics differ from the original producer");
 }
 // Verified original far-call target 05:4001; its sole outer return is
 // 05:4078, following CALL 4079. Neither world dispatcher nor frame/audio
 // scheduler is re-entered. The native far-call wrapper retains its real stack.
 if(cpu[5]==0x4001&&s.phase==0) {
  if(cpu[4]<0xc002||cpu[4]>0xcffc||ram[0x101]!=2)return;
  if(!s.initialized||s.stage!=ram[0x5f]) {
   s.current_package={1,1};
   std::memset(&s.bikes,0,sizeof s.bikes);
   s.scan_vertical={};s.scan_vertical_changed=0;
   if(s.reward_stage!=ram[0x5f]){s.reward_owner=0;s.reward_grants=0;}
   s.transport={};s.scene_owner=0;s.door_owner=0;
   // A new area never revives a fallen player. Only the native team retry
   // above clears fallen status; voluntary withdrawal may end at stage entry.
   for(auto& status:s.status)if(status==2)status=0;
   s.join_pending=0;s.select_hold={};
   if(s.status[s.anchor]==1&&s.status[s.anchor^1]!=1)s.anchor^=1;
   s.initialized=1;s.stage=ram[0x5f];s.checkpoint=ram[0x66];s.previous={};
   s.cameras[0]=s.cameras[1]={int(ram[0xa08]|unsigned(ram[0xa09])<<8),int(ram[0xa05]|unsigned(ram[0xa06])<<8)};
   capture(s.body[s.anchor],ram,hram);s.body[s.anchor^1]=s.body[s.anchor];
   // Native initialization preserves position/HP while rebuilding the
   // counterpart's body and animation controller (05:411D..4196).
   // Later checkpoint tables already supply the original arrival height.
   // Joining a player who is standing in that room needs the same approach;
   // starting the beam at their feet can settle the new body inside a tile.
   auto initialize=[&](unsigned seat){auto& body=s.body[seat].ram;body[1]=0;
    // The authored parking actor owns the section's original mount sequence.
    bool vehicle=body[0xe5]||body[2]==0x4a||body[2]==0x4c||body[2]==0x4e||(ram[0x5f]==6&&ram[0xc00]&&ram[0xc2d]==14);
    if(body[2]!=0x22&&!vehicle){unsigned y=body[5]|unsigned(body[6])<<8;s.transport[seat].phase=3;s.transport[seat].y=int(y);if(ram[0x66]){y=y>80?y-80:0;body[5]=uint8_t(y);body[6]=uint8_t(y>>8);}}
   };
   initialize(s.anchor^1);
   // The original 28:7A5B character loader selects these existing native
   // records. Keep each actor's active workspace separate; no unlock is granted.
   auto equipment=[&](unsigned character) {
    return std::array<uint8_t,4>{ram[character?0xdd:0xd8],ram[character?0xde:0xd9],ram[character?0xdb:0xd6],ram[character?0xdc:0xd7]};
   };
   s.body[s.anchor^1].equipment=equipment(s.first^(s.anchor^1));
   s.body[s.anchor^1].ram[0x21]=s.status[s.anchor^1]==1?0:s.body[s.anchor^1].equipment[1];
   s.body[s.anchor].equipment=equipment(s.first^s.anchor);
   if(s.retry_initializing){s.body[s.anchor].ram[0x21]=s.body[s.anchor].equipment[1];s.retry_initializing=0;project(s.body[s.anchor],ram,hram);}
   if(ram[0xc9]!=(s.first^s.anchor)){initialize(s.anchor);s.body[s.anchor].ram[0x21]=s.body[s.anchor].equipment[1];project(s.body[s.anchor],ram,hram);}
  }
  capture(s.body[s.anchor],ram,hram);
  std::copy(cpu.begin(),cpu.end(),s.entry.begin());
  s.original_character=uint8_t(s.first^s.anchor);s.native_input={hram[0xf],hram[0x10]};s.tile_base=hram[0x48];
  s.graphics_start=uint16_t(unsigned(hram[0x49])<<8|hram[0x4a]);
  ram[0xc9]=uint8_t(s.first^s.anchor);s.phase=s.anchor+1;
  bike_project(s.anchor,ram);
  isolate_death(s.anchor,ram);
  if(!scene_active(c)) {
   hram[0xf]=s.held[s.anchor];hram[0x10]=s.held[s.anchor]&~s.previous[s.anchor];
  }
  s.previous[s.anchor]=s.held[s.anchor];
 } else if(cpu[5]==0x4078&&s.phase&&cpu[4]==s.entry[4]) {
  if(s.death_isolated){std::copy_n(s.death_globals.data(),7,ram.data()+0xd);ram[0xac]=s.death_globals[7];s.death_isolated=0;}
  if(s.phase==s.anchor+1) {
   s.hit_owner=s.collision_owner=0;
   capture(s.body[s.anchor],ram,hram);std::copy(cpu.begin(),cpu.end(),s.exit.begin());
   bike_capture(s.anchor,ram);
   s.cameras[s.anchor]={int(ram[0xa08]|unsigned(ram[0xa09])<<8),int(ram[0xa05]|unsigned(ram[0xa06])<<8)};
   project(s.body[s.anchor^1],ram,hram);
   // Isolate the original single-player graphics allocation between actors.
   // Complete host textures are already decoded at pose emission, so a
   // scanline publication never observes a half-produced graphics buffer.
   std::copy_n(ram.data()+0x300,s.first_graphics.size(),s.first_graphics.data());
   s.graphics_end=uint16_t(unsigned(hram[0x49])<<8|hram[0x4a]);
   hram[0x49]=uint8_t(s.graphics_start>>8);hram[0x4a]=uint8_t(s.graphics_start);
   unsigned other=s.anchor^1;
   bike_project(other,ram);
   unsigned held=s.transport[other].phase?0:s.held[other];
   ram[0xc9]=uint8_t(s.first^other);hram[0xf]=uint8_t(held);
   hram[0x10]=uint8_t(held&~s.previous[other]);s.previous[other]=s.held[other];
   std::copy(s.entry.begin(),s.entry.end(),cpu.begin());s.phase=other+1;
   isolate_death(other,ram);
  } else {
   unsigned other=s.anchor^1;capture(s.body[other],ram,hram);bike_capture(other,ram);bike_idle(ram);
   if(s.transport[other].phase==3&&s.body[other].ram[2]!=0x22){bike_arrive(other);s.transport[other].phase=0;++s.arrivals;}
   // Host port of the retail follow deadzones and speed/room clamps. The
   // second camera has its own history and never installs guest map/PPU state.
   auto word=[&](unsigned at){return int(ram[at]|unsigned(ram[at+1])<<8);};
   auto follow=[&](unsigned axis,int position,unsigned low,unsigned high,unsigned minimum,unsigned maximum) {
    auto& camera=s.cameras[other][axis];int min=word(minimum),max=word(maximum);
    if(axis==0&&s.bikes.active&&(s.body[0].ram[0xe5]||s.body[1].ram[0xe5])){min=s.bikes.bounds[0];max=s.bikes.bounds[1]-160;}
    if(min>max)return;
    int relative=position-camera,delta=relative<word(low)?relative-word(low):relative>word(high)?relative-word(high):0;
    int speed=std::max(1,word(0xa0b));camera=std::clamp(camera+std::clamp(delta,-speed,speed),min,max);
   };
   const auto& b=s.body[other].ram;
   follow(0,b[8]|unsigned(b[9])<<8,0xa5a,0xa5c,0xa0d,0xa40);
   follow(1,b[5]|unsigned(b[6])<<8,0xa56,0xa58,0xa10,0xa12);
   project(s.body[s.anchor],ram,hram);
   ram[0xc9]=s.original_character;
   hram[0xf]=s.native_input[0];hram[0x10]=s.native_input[1];
   hram[0x48]=s.tile_base;
   if(s.graphics_end>=0xc300&&s.graphics_end<=0xcb00)
    std::copy_n(s.first_graphics.data(),s.graphics_end-0xc300,ram.data()+0x300);
   hram[0x49]=uint8_t(s.graphics_end>>8);hram[0x4a]=uint8_t(s.graphics_end);
   std::copy(s.exit.begin(),s.exit.end(),cpu.begin());s.phase=s.hit_owner=s.collision_owner=0;++s.passes;
  }
 }
 if(controls&&cpu[5]==0x4001&&s.phase) {
  unsigned seat=s.phase-1;
  controls->dispatch(c,cpu,ram,seat,!s.menu.active&&!scene_active(c)&&!s.status[seat]&&!s.transport[seat].phase);
 }
}
std::array<Xtreme2Coop::Actor,2> Xtreme2Coop::actors(bool presented)const {
 std::array<Actor,2> result{};
 for(unsigned i=0;i<2;++i) {
  auto& b=(presented?state_.presented_body:state_.body)[i].ram;
  result[i]={int(b[8]|unsigned(b[9])<<8),int(b[5]|unsigned(b[6])<<8),unsigned(b[0x21]&0x7f),state_.first^i,b[2],b[1],unsigned((presented?state_.presented_body:state_.body)[i].equipment[1]&127)};
 }return result;
}
std::array<int,2> Xtreme2Coop::camera(unsigned actor)const {
 if(actor>1)throw std::runtime_error("Invalid camera actor");
 const auto& t=state_.presented_transport[actor];if(t.phase==1)return {t.camera[0],t.camera[1]};
 if(t.phase==2)return {state_.presented_cameras[state_.anchor][0],state_.presented_cameras[state_.anchor][1]};
 return {state_.presented_cameras[actor][0],state_.presented_cameras[actor][1]};
}
std::vector<uint8_t> Xtreme2Coop::snapshot(bool simulation,bool sealed_child)const {
 static_assert(std::is_trivially_copyable_v<State>);
 std::vector<uint8_t> out(sizeof(State));std::memcpy(out.data(),&state_,sizeof(State));
 if(simulation&&!menu_copy_&&!menu_saved_.empty())throw std::runtime_error("Native menu comparison requires its restored isolated room");
 std::vector<uint8_t> child;bool child_sealed=menu_saved_sealed_;
 if(menu_copy_) {
  if(simulation)child=menu_copy_->simulation_snapshot();
  else if(sealed_child){child=menu_copy_->snapshot();child_sealed=true;}
  else {Machine::RewindState local;menu_copy_->checkpoint(local);child=std::move(local.bytes_);child_sealed=false;}
 } else child=menu_saved_;
 if(!simulation&&!child.empty()&&child_sealed!=sealed_child) {
  if(sealed_child){auto checksum=sha256(child);child.insert(child.end(),checksum.begin(),checksum.end());}
  else {if(child.size()<64)throw std::runtime_error("Invalid sealed native menu state");child.resize(child.size()-64);}
 }
 uint32_t size=uint32_t(child.size());
 auto at=out.size();out.resize(at+4+size+(size?2*160*144*4:0));std::memcpy(out.data()+at,&size,4);
 if(size){std::memcpy(out.data()+at+4,child.data(),size);auto pixels=menu_copy_?menu_copy_->pixels():std::span<const uint32_t>(menu_pixels_);
  if(pixels.size()!=160*144||menu_native_.size()!=160*144)throw std::runtime_error("Missing native menu raster state");
  std::memcpy(out.data()+at+4+size,pixels.data(),160*144*4);std::memcpy(out.data()+at+4+size+160*144*4,menu_native_.data(),160*144*4);}
 return out;
}
void Xtreme2Coop::restore(std::span<const uint8_t> bytes,bool sealed_child) {
 auto staged_storage=std::make_unique<State>();auto& staged=*staged_storage;
 if(bytes.size()<sizeof(State)+4)throw std::runtime_error("Invalid co-op state size");
 std::memcpy(&staged,bytes.data(),sizeof(State));uint32_t child_size=0;std::memcpy(&child_size,bytes.data()+sizeof(State),4);
 if(child_size>2*1024*1024||(child_size&&child_size<64)||bytes.size()!=sizeof(State)+4+child_size+(child_size?2*160*144*4:0)||bool(child_size)!=bool(staged.menu.active))throw std::runtime_error("Invalid native menu continuation size");
 if(sealed_child&&child_size&&sha256(bytes.subspan(sizeof(State)+4,child_size-64))!=std::string(reinterpret_cast<const char*>(bytes.data()+sizeof(State)+4+child_size-64),64))throw std::runtime_error("Invalid native menu continuation checksum");
 if(staged.menu.active>1||staged.menu.primary>1||staged.menu.request>2||staged.menu.resume>1||staged.menu.stage_exit>1||staged.menu.returned>3||staged.menu.idle>3||staged.menu.last_idle>3||staged.menu.ready>3||staged.menu.selecting>3||staged.menu.busy>2||staged.menu.options_save>2||staged.menu.sound_count>8)throw std::runtime_error("Invalid native menu state");
 if(staged.schema!=16||staged.first!=state_.first||staged.reward_owner>2||staged.anchor>1||staged.pending_anchor>2||staged.pending_checkpoint>1||staged.scene_owner>2||staged.scan_vertical_changed>1||(staged.sound_pending&&staged.sound_pending!=0x36)||staged.shared>1||staged.shared_width<160||staged.shared_width>4096||staged.phase>2||staged.initialized>1||
    (staged.phase&&(!staged.initialized||staged.entry[4]<0xc002||staged.entry[4]>0xcffc||staged.entry[5]!=0x4001||
     (staged.phase!=staged.anchor+1&&staged.exit[5]!=0x4078))))throw std::runtime_error("Invalid co-op state/profile");
 if(staged.death_isolated>1||(staged.death_isolated&&!staged.phase)||staged.join_pending>3||staged.retry_pending>1||staged.retry_initializing>1||
    (staged.status[0]&&staged.status[1]))throw std::runtime_error("Invalid co-op lifecycle");
 for(unsigned i=0;i<2;++i)if(staged.status[i]>2||staged.presented_status[i]>2||staged.select[i]>1||staged.select_previous[i]>1||staged.select_hold[i]>90||
    ((staged.join_pending&(1u<<i))&&staged.status[i]!=2))throw std::runtime_error("Invalid co-op participation");
 for(auto* list:{&staged.build_palette,&staged.active_palette,&staged.presented_palette})
  for(const auto& palette:*list)if(palette.owned>4||palette.tile_known>1||(palette.tile_known&&!palette.owned))throw std::runtime_error("Invalid co-op sprite owner/tile");
 if(staged.actor_owner>2||staged.actor_source>2||(staged.actor_owner&&(staged.phase||staged.actor_sp<0xc002||staged.actor_sp>0xcffc||staged.actor_return<0x6600||staged.actor_return>0x761f||staged.actor_key>=656)))throw std::runtime_error("Invalid co-op actor projection");
 for(auto owner:staged.object_owner)if(owner>2)throw std::runtime_error("Invalid co-op projectile owner");
 for(auto weapon:staged.object_weapon)if(weapon>9)throw std::runtime_error("Invalid co-op projectile weapon");
 for(auto package:staged.object_package)if(package>9)throw std::runtime_error("Invalid co-op projectile package");
 for(auto package:staged.current_package)if(package<1||package>9)throw std::runtime_error("Invalid co-op character package");
 for(auto cell:staged.scan_vertical)if(cell<0||cell>8192)throw std::runtime_error("Invalid co-op activation footprint");
 if(staged.bikes.active>1||staged.bikes.phase>2||staged.bikes.done>3||
    staged.bikes.returning[0]>1||staged.bikes.returning[1]>1||
    (staged.bikes.phase&&(!staged.bikes.active||staged.actor_owner!=staged.bikes.phase||staged.bikes.entry[5]!=0x675||staged.bikes.entry[4]!=staged.actor_sp))||
    (staged.bikes.active&&(staged.stage!=6||staged.bikes.bounds[0]<0||staged.bikes.bounds[1]>65536||staged.bikes.bounds[0]>=staged.bikes.bounds[1])))throw std::runtime_error("Invalid co-op vehicle state");
 for(const auto* list:{&staged.bikes.attachments,&staged.bikes.presented_attachments})for(const auto& a:*list) {
  if(a.key>=656||a.active>1||a.count>64||a.slot>63||a.age>4||a.x<0||a.x>65535||a.y<0||a.y>65535)throw std::runtime_error("Invalid co-op bike attachment pose");
  for(unsigned n=0;n<a.count;++n)if(a.pieces[n].x< -1024||a.pieces[n].x>1024||a.pieces[n].y< -1024||a.pieces[n].y>1024)throw std::runtime_error("Invalid co-op bike attachment piece");
 }
 for(auto owner:staged.last_damage)if(owner>2)throw std::runtime_error("Invalid co-op damage owner");
 if(staged.hit_owner>2||staged.defeated_owner>2||staged.collision_owner>2||staged.area_owner>2||staged.door_owner>2||staged.door_key>=656||staged.hit_key>=656||staged.defeated_key>=656||staged.collision_key>=656||
  (staged.collision_owner&&(staged.collision_sp<0xc002||staged.collision_sp>0xcffc||staged.collision_bank>=64)))throw std::runtime_error("Invalid co-op combat state");
 for(unsigned i=0;i<2;++i)if(unsigned(staged.graphics_base[i])+staged.graphics_size[i]>2048||staged.graphics_size[i]%32)throw std::runtime_error("Invalid co-op graphics state");
 for(const auto* list:{&staged.cameras,&staged.presented_cameras})for(auto camera:*list)for(auto value:camera)
  if(value< -131072||value>131072)throw std::runtime_error("Invalid co-op camera state");
 for(const auto* list:{&staged.transport,&staged.presented_transport})for(const auto& t:*list)
  {if(t.phase>3||t.count>64||t.x<0||t.x>65535||t.y<0||t.y>65535||t.offset< -65536||t.offset>0||(t.phase==1&&!t.hold))throw std::runtime_error("Invalid co-op transport state");
   for(auto pair:{t.camera,t.bounds})for(auto value:pair)if(value< -131072||value>131072)throw std::runtime_error("Invalid co-op transport view");
   if((t.phase==1||t.phase==2)&&t.bounds[1]<=t.bounds[0])throw std::runtime_error("Invalid co-op departure bounds");}
 std::vector<uint8_t> child(bytes.begin()+sizeof(State)+4,bytes.begin()+sizeof(State)+4+child_size);
 std::vector<uint32_t> pixels(child_size?160*144:0),native(pixels.size());
 if(child_size){std::memcpy(pixels.data(),bytes.data()+sizeof(State)+4+child_size,160*144*4);std::memcpy(native.data(),bytes.data()+sizeof(State)+4+child_size+160*144*4,160*144*4);}
 state_=staged;menu_saved_=std::move(child);menu_saved_sealed_=sealed_child;menu_pixels_=std::move(pixels);menu_native_=std::move(native);menu_copy_.reset();
}
void Xtreme2Coop::palette(ModContext& c,unsigned first,unsigned last) {
 if(state_.menu.active)return;
 if(!state_.initialized||first>=1024)return;
 last=std::min(last,1024u);
 unsigned owner=state_.phase?state_.phase-1:state_.anchor,base=unsigned(c.hram[0x48])*16;
 auto tiles=graphics(c,c.hram[0x31],c.hram[0x32]|unsigned(c.hram[0x33])<<8);
 if(base+tiles.size()>2048)throw std::runtime_error("Invalid original player graphics allocation");
 auto& texture=state_.build_texture[owner];std::fill(texture.begin()+0x400,texture.end(),0);
 std::copy(tiles.begin(),tiles.end(),texture.begin()+0x400+base);
 state_.graphics_base[owner]=uint16_t(base);state_.graphics_size[owner]=uint16_t(tiles.size());
 for(unsigned n=first;n<last;++n) {
  auto& pal=state_.build_palette[n];pal.owned=owner+1;pal.tile_known=0;
  for(unsigned i=0;i<4;++i) {
   unsigned color=c.wram[0x8d + i*2]|unsigned(c.wram[0x8e + i*2])<<8;
   pal.colors[i]=0xff000000u|((color&31)*255/31<<16)|(((color>>5)&31)*255/31<<8)|(((color>>10)&31)*255/31);
  }
 }
}
void Xtreme2Coop::player_piece(unsigned index,uint8_t tile) {
 if(index>=1024)return;auto& p=state_.build_palette[index];
 if(p.owned<1||p.owned>2)return;
 // A later emission can reuse the same character's native graphics slots.
 // Keep the tile pair that belonged to this piece when its pose was emitted.
 p.tile_known=1;std::copy_n(state_.build_texture[p.owned-1].data()+unsigned(tile&0xfe)*16,32,p.tile.data());
}
void Xtreme2Coop::common_items(std::span<uint8_t> ram) {
 // Native pickups already update both original records. Project their
 // common item bits into both live workspaces without granting anything
 // or replacing character-specific armor bits.
 uint8_t common=uint8_t((ram[0xd8]|ram[0xdd])&0xf0);
 for(auto& body:state_.body)body.equipment[0]=uint8_t((body.equipment[0]&15)|common);
 ram[0x83]=uint8_t((ram[0x83]&15)|common);
}
void Xtreme2Coop::static_package(const ModContext& c,unsigned seat) {
 unsigned package=c.wram[0xbc4];
 // These original weapon packages cover8000..83FF. Package0 and later
 // environment/UI uploads target other slots and cannot replace them.
 if(package<1||package>9)return;
 state_.current_package[seat]=uint8_t(package);
 if(state_.actor_source==seat+1)state_.object_package[state_.actor_key]=uint8_t(package);
}
void Xtreme2Coop::projectile_piece(const ModContext& c,unsigned index,uint8_t attributes,uint8_t tile) {
 if(state_.menu.active)return;
 if(index>=1024||!c.hram)return;
 unsigned page=c.hram[0x22];
 if(page>=0xda&&page<0xe0&&peek(c,(page<<8)+0x2d)==14){
  int key=object_key(c,page);unsigned owner=key<0?0:state_.object_owner[unsigned(key)];if(!owner)return;
  const auto& body=state_.body[owner-1];
  unsigned table=(state_.first^(owner-1))?0x1665:((body.equipment[0]&15)==15?0x1551:0x143d),entry=table+0x44*4;
  unsigned bank=rom_byte(c,0,entry),frame=rom_byte(c,0,entry+2)|unsigned(rom_byte(c,0,entry+3))<<8;
  // Native death animation44 uploads these shared scratch tiles. Retain
  // the exact original producer for each particle while the partner draws.
  auto tiles=graphics(c,rom_byte(c,bank,frame+4),rom_byte(c,bank,frame+5)|unsigned(rom_byte(c,bank,frame+6))<<8);
  unsigned offset=unsigned(uint8_t((tile&0xfe)-0x40))*16;
  if(offset+32>tiles.size())throw std::runtime_error("Original death particle exceeds its graphics producer");
  auto& palette=state_.build_palette[index];palette.owned=owner+2;palette.tile_known=1;
  std::copy_n(tiles.data()+offset,32,palette.tile.data());
  for(unsigned n=0;n<4;++n){unsigned color=body.palette[n*2]|unsigned(body.palette[n*2+1])<<8;auto expand=[](unsigned v){return(v<<3)|(v>>2);};palette.colors[n]=0xff000000u|(expand(color&31)<<16)|(expand((color>>5)&31)<<8)|expand((color>>10)&31);}
  return;
 }
 if(page<0xb0||page>=0xb8)return;
 int key=object_key(c,page);if(key<0)return;
 unsigned owner=state_.object_owner[unsigned(key)];
 if(!owner)return;
 // Original X weapon/package table28:7BA2 and loader00:0483. Retain
 // the firing weapon on each object: a later menu choice cannot replace
 // existing shots. Tile pairs remain per emitted piece when old/new
 // packages occupy the same native VRAM slots. The cartridge is read-only.
 unsigned package=(state_.first^(owner-1))?state_.object_package[unsigned(key)]:rom_byte(c,0x28,0x7ba2+state_.object_weapon[unsigned(key)]);
 if(!package)return;
 if(package==255)return; // Giga Crush has no static shot package.
 unsigned entry=0x56f+package*4;
 unsigned descriptor=rom_byte(c,0,entry)|unsigned(rom_byte(c,0,entry+1))<<8;
 unsigned bank=rom_byte(c,0x22,descriptor),at=rom_byte(c,0x22,descriptor+1)|unsigned(rom_byte(c,0x22,descriptor+2))<<8;
 unsigned control=rom_byte(c,bank,at++),group=(control&0x38)/8;
 auto* ppu=static_cast<const ModPpu*>(c.ppu);
 auto& palette=state_.build_palette[index];palette.owned=owner+2;palette.tile_known=0;
 if(ppu)for(unsigned n=0;n<4;++n)palette.colors[n]=ppu->obj_rgb[(attributes&7)*4+n];
 if(control!=255)for(unsigned count=0;;++count,++group) {
  if(count>=8||group>=8)throw std::runtime_error("Invalid original weapon palette package");
  for(unsigned n=0;n<4;++n){unsigned color=rom_byte(c,bank,at)|unsigned(rom_byte(c,bank,at+1))<<8;at+=2;
   if(group==(attributes&7)){auto expand=[](unsigned v){return(v<<3)|(v>>2);};palette.colors[n]=0xff000000u|(expand(color&31)<<16)|(expand((color>>5)&31)<<8)|expand((color>>10)&31);}}
  if(rom_byte(c,bank,at)==255&&rom_byte(c,bank,at+1)==255){at+=2;break;}
 }
 unsigned destination=rom_byte(c,bank,at)|unsigned(rom_byte(c,bank,at+1))<<8,size=unsigned(rom_byte(c,bank,at+2))*16;at+=3;
 if(destination<0x8000||destination>0x8400||size>0x8400-destination)throw std::runtime_error("Invalid original weapon tile package");
 for(unsigned n=0;n<size;++n)state_.build_texture[owner-1][destination-0x8000+n]=rom_byte(c,bank,at+n);
 unsigned height=ppu&&(ppu->lcdc&4)==0?8:16,offset=unsigned(height==16?tile&0xfe:tile)*16;
 if(offset>=destination-0x8000&&offset+height*2<=destination-0x8000+size) {
  palette.tile_known=1;
  for(unsigned n=0;n<height*2;++n)palette.tile[n]=rom_byte(c,bank,at+offset-(destination-0x8000)+n);
 }
}
void Xtreme2Coop::attachment_piece(const ModContext& c,unsigned index,uint8_t tile,uint8_t attributes) {
 if(state_.menu.active||index>=1024||!c.hram)return;
 unsigned page=c.hram[0x22];int key=object_key(c,page);
 bool parked=page==0xcc&&state_.bikes.phase&&(peek(c,0xcc2d)==14||peek(c,0xcc2d)==15);
 if(key<0||(!parked&&(peek(c,(page<<8)+0x2d)!=0x35||peek(c,(page<<8)+0xb)!=0xc1)))return;
 unsigned owner=state_.object_owner[unsigned(key)];if(!owner)owner=state_.phase;
 if(parked)owner=state_.bikes.phase;
 if(!owner)return;
 // Original1E9C draws this actor before1107 executes its graphics producer.
 // Its independent tiles share native scratch slots with both player poses.
 // Decode that exact producer and retain each emitted tile pair independently.
 auto& palette=state_.build_palette[index];palette.owned=owner+2;palette.tile_known=0;
 const auto& colors=state_.body[owner-1].palette;
 for(unsigned n=0;n<4;++n){unsigned color=colors[n*2]|unsigned(colors[n*2+1])<<8;palette.colors[n]=0xff000000u|((color&31)*255/31<<16)|(((color>>5)&31)*255/31<<8)|(((color>>10)&31)*255/31);}
 if(parked&&c.ppu){auto* ppu=static_cast<const ModPpu*>(c.ppu);for(unsigned n=0;n<4;++n)palette.colors[n]=ppu->obj_rgb[(attributes&7)*4+n];}
 auto tiles=graphics(c,c.hram[0x44],c.hram[0x45]|unsigned(c.hram[0x46])<<8);
 unsigned origin=uint8_t(0x40+c.hram[0x30]+c.hram[0x48])&0xfe;
 unsigned offset=unsigned(uint8_t((tile&0xfe)-origin))*16;
 if(offset+32<=tiles.size()){palette.tile_known=1;std::copy_n(tiles.data()+offset,32,palette.tile.data());}
}
void Xtreme2Coop::publish(bool updated) {
 if(state_.menu.active)return;
 if(updated){state_.active_palette=state_.build_palette;state_.active_texture=state_.build_texture;}
 state_.build_palette={};
}
void Xtreme2Coop::complete_frame(const ModContext&) {
 if(state_.menu.active)return;
 for(auto& t:state_.transport)if(t.phase==1) {
  if(t.timer<t.hold)++t.timer;
  else {t.offset-=848;if(t.y+t.offset/256-t.camera[1]< -24)t.phase=2;}
 }
 state_.presented_palette=state_.active_palette;state_.presented_texture=state_.active_texture;
 state_.presented_body=state_.body;state_.presented_cameras=state_.cameras;state_.presented_transport=state_.transport;
 state_.presented_status=state_.status;
 for(unsigned seat=0;seat<2;++seat){auto& attachment=state_.bikes.attachments[seat];attachment.age=uint8_t(std::min(4u,unsigned(attachment.age)+1));if(!attachment.active&&attachment.age>2&&!state_.body[seat].ram[0xe5])attachment.count=0;}
 state_.bikes.presented_attachments=state_.bikes.attachments;
}
bool Xtreme2Coop::sprite_visible(unsigned index)const {
 if(index>=1024)return true;unsigned owner=state_.presented_palette[index].owned;
 if(owner>2)return true; // A departing actor does not erase its existing shots.
 return !owner||(!state_.presented_status[owner-1]&&(state_.presented_transport[owner-1].phase==0||state_.presented_transport[owner-1].phase==3));
}
const Xtreme2Coop::Transport* Xtreme2Coop::transport_pose(unsigned actor)const {
 if(actor>1)return nullptr;const auto& t=state_.presented_transport[actor];return t.phase==1?&t:nullptr;
}
std::array<int,2> Xtreme2Coop::shared_camera()const {
 if(state_.bikes.active&&(state_.presented_body[0].ram[0xe5]||state_.presented_body[1].ram[0xe5]))return camera(state_.anchor);
 unsigned other=state_.anchor^1;
 auto a=actors(true);unsigned phase=state_.presented_transport[other].phase;
 bool visible_departure=phase==1&&std::abs(a[0].x-a[1].x)<int(state_.shared_width)-16&&std::abs(a[0].y-a[1].y)<128;
 if(state_.presented_status[other]||(phase&&!visible_departure)||!(state_.presented_body[other].ram[0x21]&127))return camera(state_.anchor);
 return {(a[0].x+a[1].x)/2-80,(state_.presented_cameras[0][1]+state_.presented_cameras[1][1])/2};
}
const uint32_t* Xtreme2Coop::sprite_palette(unsigned index)const {
 if(index>=1024||!state_.presented_palette[index].owned)return nullptr;
 return state_.presented_palette[index].colors.data();
}
const uint8_t* Xtreme2Coop::sprite_texture(unsigned index)const {
 if(index>=1024)return nullptr;unsigned owner=state_.presented_palette[index].owned;
 if(owner>2)owner-=2;
 return owner>=1&&owner<=2?state_.presented_texture[owner-1].data():nullptr;
}
const uint8_t* Xtreme2Coop::sprite_tile(unsigned index)const {
 return index<1024&&state_.presented_palette[index].tile_known?state_.presented_palette[index].tile.data():nullptr;
}
void Xtreme2Coop::draw_hud(const ModContext& c,std::span<uint32_t> pixels,unsigned width,std::span<const uint32_t> native,int focus,bool split)const {
 if(width<160||pixels.size()<size_t(width)*144||native.size()<160*144||!state_.initialized||!c.rom||c.rom_size<0x40000||!c.wram)return;
 // Port of the original 0F:6EB4 tile builder, using its original 0F:70F3
 // tile art and 00:1FC6/1FCE character palettes. Only placement changes.
 // There are no replacement glyphs, numeric HP readouts or new meter art.
 const bool stacked=!split&&focus<0&&width<192;
 unsigned top=stacked?112:128;
 std::fill(pixels.begin()+size_t(top)*width,pixels.begin()+size_t(144)*width,0xff000000u);
 auto boss=[&](unsigned x){for(unsigned y=128;y<144;++y)std::copy_n(native.data()+y*160+96,64,pixels.data()+y*width+x);};
 if(split){for(unsigned i=0;i<2;++i)boss((i+1)*width/2-64);}
 else if(focus>=0)boss(width-64);
 else if(width>=256)boss((width-64)/2);
 for(unsigned i=0;i<2;++i) {
  if(focus>=0&&i!=unsigned(focus))continue;
  const auto& body=state_.presented_body[i];unsigned hp=body.ram[0x21]&127,max=std::clamp(unsigned(body.equipment[1]&127),16u,32u),character=state_.first^i;
  std::array<uint8_t,24> tiles;tiles.fill(0x5e);
  unsigned base=character?0x66:0x60;
  tiles[0]=uint8_t(base+4);tiles[1]=uint8_t(base+5);
  if(i==0){tiles[10]=uint8_t(base);tiles[11]=uint8_t(base+1);tiles[22]=uint8_t(base+2);tiles[23]=uint8_t(base+3);tiles[21]=uint8_t(0x40+c.wram[0x65]);}
  auto meter=[&](unsigned at,unsigned value,unsigned capacity) {
   for(unsigned n=0;n<capacity/4;++n){tiles[at++]=uint8_t(0x56+std::min(value,4u));value=value>4?value-4:0;}
   if(capacity&2)tiles[at]=uint8_t(0x5b+std::min(value,2u));
  };
  meter(2,hp,max);
  unsigned weapon=body.ram[0x31];
  if(!character&&weapon&&weapon<11) {
   size_t name=0x0f*0x4000+0x6e9e - 0x4000+weapon*2;
   tiles[12]=c.rom[name];tiles[13]=c.rom[name+1];meter(14,c.wram[0x6d+weapon]&31,28);
  } else if(character&&(c.wram[0xa0]||(body.equipment[0]&4)))meter(14,c.wram[0x6d]&31,28);
  unsigned x=split?i*width/2:focus<0&&i&&!stacked?width-96:0,y=stacked?112+i*16:128;
  std::array<uint32_t,4> palette;
  for(unsigned n=0;n<4;++n){size_t at=(character?0x1fce:0x1fc6)+n*2;unsigned color=c.rom[at]|unsigned(c.rom[at+1])<<8;
   auto expand=[](unsigned v){return (v<<3)|(v>>2);};
   palette[n]=0xff000000u|(expand(color&31)<<16)|(expand((color>>5)&31)<<8)|expand((color>>10)&31);}
  for(unsigned ty=0;ty<2;++ty)for(unsigned tx=0;tx<12;++tx)for(unsigned py=0;py<8;++py)for(unsigned px=0;px<8;++px) {
   unsigned tile=tiles[ty*12+tx];if(tile<0x40||tile>=0x6c)continue;
   size_t at=0x0f*0x4000+0x70f3-0x4000+(tile-0x40)*16+py*2;
   unsigned color=((c.rom[at]>>(7-px))&1)|(((c.rom[at+1]>>(7-px))&1)<<1);
   pixels[size_t(y+ty*8+py)*width+x+tx*8+px]=palette[color];
  }
 }
}
}
