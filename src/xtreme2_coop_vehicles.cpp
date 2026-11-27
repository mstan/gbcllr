#include "xtreme2_coop.hpp"
#include "native_mods.hpp"
#include <algorithm>
#include <cstring>
#include <stdexcept>
namespace gbcllr {
void Xtreme2Coop::bike_project(unsigned seat,std::span<uint8_t> ram) {
 if(state_.bikes.active)std::copy(state_.bikes.body[seat].begin(),state_.bikes.body[seat].end(),ram.data()+0xc00);
}
void Xtreme2Coop::bike_capture(unsigned seat,std::span<uint8_t> ram) {
 if(state_.bikes.active)std::copy_n(ram.data()+0xc00,256,state_.bikes.body[seat].begin());
}
void Xtreme2Coop::bike_idle(std::span<uint8_t> ram) {
 if(!state_.bikes.active)return;
 unsigned seat=state_.anchor;
 if(!state_.bikes.body[seat][0]&&state_.bikes.body[seat^1][0])seat^=1;
 bike_project(seat,ram);
}
void Xtreme2Coop::bike_depart(unsigned seat) {
 auto& s=state_;auto& b=s.bikes;
 if(!b.active||!s.body[seat].ram[0xe5]||s.body[seat].ram[2]!=0x4c||b.returning[seat])return;
 b.riders[seat]=s.body[seat];b.returning[seat]=1;
}
void Xtreme2Coop::bike_arrive(unsigned seat) {
 auto& s=state_;auto& b=s.bikes;if(!b.returning[seat])return;
 b.returning[seat]=0;auto& body=s.body[seat];
 if(b.ends||!s.body[seat^1].ram[0xe5]) {
  body.ram[0xe5]=0;body.personal[2]=0;b.body[seat][0]=0;b.attachments[seat].active=0;return;
 }
 // Finish the original teleport before resuming this rider's native controller.
 // The native teleport lands with the on-foot collision origin. Resume at
 // the live rider's vehicle origin so the returning bike cannot sink into it.
 auto arrived=body;body=b.riders[seat];
 for(unsigned at:{4u,5u,6u,7u,8u,9u})body.ram[at]=s.body[seat^1].ram[at];
 body.ram[0x21]=arrived.ram[0x21];
 body.ram[0x30]=120;body.equipment=arrived.equipment;body.hitbox=arrived.hitbox;
 body.personal=arrived.personal;body.personal[2]=b.riders[seat].personal[2];
}
void Xtreme2Coop::bike_write(const ModContext& c,uint16_t address,uint8_t value) {
 auto& s=state_;
 if(!s.bikes.active)return;
 if(!(address&255)&&!value){int key=object_key(c,address>>8);for(auto& a:s.bikes.attachments)if(a.active&&key>=0&&a.key==unsigned(key))a.active=0;}
 // An original later authored spawn replaces the parking contexts. Host
 // projection uses physical copies, so it never masquerades as this event.
 if(address==0xcc00&&value==1&&!s.phase&&!s.actor_owner)std::memset(&s.bikes,0,sizeof s.bikes);
 if(address==0xc02d&&value&&c.rom_bank==0xb&&c.instruction_pc==0x5f82&&s.actor_owner) {
  ++s.bikes.mounts[s.actor_owner-1];
  // The first rider owns the section's one autoscroll. A second mount must
  // retain that world owner and must not repeat the camera advance.
  if(!s.body[s.anchor].personal[2])s.pending_anchor=s.actor_owner;
 }
 if(address==0xca42&&c.rom_bank==0x2c&&c.instruction_pc==0x7664&&s.phase==s.anchor+1)++s.bikes.scrolls;
 if(address==0xc1f4&&value==255&&c.rom_bank==0x2c&&c.instruction_pc==0x7692&&s.phase==s.anchor+1) {
  ++s.bikes.ends;
  unsigned other=s.anchor^1;
  if(s.body[other].personal[2]&&!s.status[other])s.body[other].ram[0xf4]=255;
 }
}
bool Xtreme2Coop::bike_hook(ModContext& c,std::span<uint16_t,6> cpu,std::span<uint8_t> ram,std::span<uint8_t> hram) {
 auto& s=state_;auto& b=s.bikes;
 if(!s.initialized||s.stage!=6||ram[0x5f]!=6)return false;
 if(b.active&&s.phase&&c.rom_bank==0x2c&&cpu[5]==0x75e4&&ram[0x1e5]&&ram[0x1cc]&&!ram[0x1f4]&&(ram[0x121]&127)){
  auto word=[&](unsigned at){return int(ram[at]|unsigned(ram[at+1])<<8);};
  int margin=s.shared?int(s.shared_width-160)/2:0;
  int left=std::max(b.bounds[0],word(0xa08)-margin)+16,right=std::min(b.bounds[1],word(0xa08)+160+margin)-16;
  int x=word(0x108);uint16_t continuation=s.phase==s.anchor+1?0x7655:0x7695;
  // Advance through original movement/collision, never through a teleport or
  // a position clamp. The original crush service owns destruction and effects.
  if(x<left){
   if((ram[0x160]&16)&&!ram[0x1f6]){
    if(cpu[4]<0xc002||cpu[4]>0xcffc)throw std::runtime_error("Invalid bike crush return stack");
    cpu[4]-=2;ram[cpu[4]-0xc000]=uint8_t(continuation);ram[cpu[4]-0xc000+1]=uint8_t(continuation>>8);cpu[5]=0x7cc2;return true;
   }
   ram[0x1b6]=0;ram[0x1b7]=3;hram[0xf]=uint8_t((hram[0xf]&~0x20u)|0x10u);
  }else if(x>right){ram[0x1b6]=ram[0x1b7]=0;hram[0xf]&=uint8_t(~0x30u);}
  cpu[5]=continuation;return true;
 }
 if(b.active&&s.phase&&s.phase!=s.anchor+1&&c.rom_bank==0x2c&&cpu[5]==0x7655) {
  cpu[5]=0x7695;return true;
 }
 // The native fixed CC actor gets one original update per retained parking
 // context. The original far wrapper/return stack and world schedule stay
 // single-pass; each mounted player's native4A/4C physics remain in its body.
 const bool entry=c.rom_bank==1&&cpu[5]==0x675&&cpu[0]>>8==0xb&&(cpu[3]==0x5f0c||cpu[3]==0x5fd6)&&
  cpu[4]>=0xc002&&cpu[4]<=0xcffc&&
  ((ram[cpu[4]-0xc000]==0xff&&ram[cpu[4]-0xc000+1]==0x75)||(ram[cpu[4]-0xc000]==8&&ram[cpu[4]-0xc000+1]==0x76));
 if(entry&&!s.phase&&!s.actor_owner) {
  if(!b.active) {
   if(!ram[0xc00]||ram[0xc2d]!=14)return false;
   std::copy_n(ram.data()+0xc00,256,b.body[0].begin());b.body[1]=b.body[0];
   unsigned x=ram[0xc08]|unsigned(ram[0xc09])<<8;
   for(unsigned i=0;i<2;++i){unsigned px=x+i*32;b.body[i][8]=uint8_t(px);b.body[i][9]=uint8_t(px>>8);}
   b.bounds={int(ram[0xa0d]|unsigned(ram[0xa0e])<<8),int(ram[0xa40]|unsigned(ram[0xa41])<<8)+160};
   b.active=1;
  }
  unsigned seat=0;
  if(!b.body[0][0]||s.status[0]||s.transport[0].phase)seat=1;
  if(!b.body[seat][0]||s.status[seat]||s.transport[seat].phase){cpu[5]=0x06c0;return true;}
  capture(s.body[s.anchor],ram,hram);b.done=0;std::copy(cpu.begin(),cpu.end(),b.entry.begin());
  s.actor_sp=cpu[4];s.actor_return=uint16_t(ram[cpu[4]-0xc000]|unsigned(ram[cpu[4]-0xc000+1])<<8);s.actor_key=524;
  s.actor_owner=s.actor_source=uint8_t(seat+1);b.phase=uint8_t(seat+1);
  bike_project(seat,ram);project(s.body[seat],ram,hram);ram[0xc9]=uint8_t(s.first^seat);
  cpu[3]=b.body[seat][0x2d]==15?0x5fd6:0x5f0c;
  return true;
 }
 if(b.phase&&c.rom_bank==1&&cpu[5]==s.actor_return&&cpu[4]==s.actor_sp+2) {
  unsigned seat=b.phase-1,other=seat^1;
  bool exiting=b.body[seat][0x2d]==15;
  capture(s.body[seat],ram,hram);bike_capture(seat,ram);b.done|=uint8_t(1u<<seat);++b.passes[seat];if(exiting)++b.exits[seat];
  if(!(b.done&(1u<<other))&&b.body[other][0]&&!s.status[other]&&!s.transport[other].phase) {
   bike_project(other,ram);project(s.body[other],ram,hram);ram[0xc9]=uint8_t(s.first^other);
   s.actor_owner=s.actor_source=b.phase=uint8_t(other+1);std::copy(b.entry.begin(),b.entry.end(),cpu.begin());cpu[3]=b.body[other][0x2d]==15?0x5fd6:0x5f0c;return true;
  }
  if(s.pending_anchor){s.anchor=s.pending_anchor-1;s.pending_anchor=0;}
  project(s.body[s.anchor],ram,hram);ram[0xc9]=uint8_t(s.first^s.anchor);bike_idle(ram);
  s.actor_owner=s.actor_source=s.hit_owner=s.collision_owner=0;b.phase=b.done=0;return true;
 }
 return false;
}
bool Xtreme2Coop::capture_attachment(const ModContext& c,std::span<const Piece> pieces,unsigned slot) {
 if(!state_.bikes.active||!c.hram||pieces.empty()||pieces.size()>64)return false;
 unsigned page=c.hram[0x22];int key=object_key(c,page);
 if(key<0||peek(c,(page<<8)+0x2d)!=0x35||peek(c,(page<<8)+0xb)!=0xc1)return false;
 unsigned owner=state_.object_owner[unsigned(key)];if(!owner)return false;
 auto tiles=graphics(c,c.hram[0x44],c.hram[0x45]|unsigned(c.hram[0x46])<<8);
 Attachment a;std::memset(&a,0,sizeof a);a.key=uint16_t(key);a.active=1;a.count=uint8_t(pieces.size());a.slot=uint8_t(slot&63);
 auto word=[&](unsigned at){return int(c.wram[at]|unsigned(c.wram[at+1])<<8);};
 a.x=word(0x108);a.y=word(0x105);a.camera={word(0xa08),word(0xa05)};
 std::copy(pieces.begin(),pieces.end(),a.pieces.begin());
 // The alternate emitter adds these two original tile biases. Host poses
 // address the producer's own bytes directly, independent of shared C300
 // scratch placement and the other player's physical graphics allocation.
 unsigned origin=uint8_t(0x40+c.hram[0x30]+c.hram[0x48])&0xfe;
 for(const auto& p:pieces){unsigned offset=unsigned(uint8_t((p.tile&0xfe)-origin))*16;if(offset+32>tiles.size())throw std::runtime_error("Original bike attachment tile is outside its graphics producer");std::copy_n(tiles.data()+offset,32,a.texture.data()+unsigned(p.tile&0xfe)*16);}
 const auto& colors=state_.body[owner-1].palette;
 for(unsigned n=0;n<4;++n){unsigned color=colors[n*2]|unsigned(colors[n*2+1])<<8;a.colors[n]=0xff000000u|((color&31)*255/31<<16)|(((color>>5)&31)*255/31<<8)|(((color>>10)&31)*255/31);}
 state_.bikes.attachments[owner-1]=a;return true;
}
const Xtreme2Coop::Attachment* Xtreme2Coop::bike_attachment(unsigned seat)const {
 if(seat>1)return nullptr;const auto& a=state_.bikes.presented_attachments[seat];const auto& b=state_.presented_body[seat].ram;
 return a.count&&b[0xe5]&&(b[0x21]&127)&&!state_.presented_status[seat]&&!state_.presented_transport[seat].phase?&a:nullptr;
}
bool Xtreme2Coop::attachment_oam(unsigned slot,std::span<const uint8_t> raw)const {
 if(raw.size()!=4||slot>=40)return false;
 for(const auto& a:state_.bikes.presented_attachments) {
  unsigned n=(slot+64-a.slot)&63;if(n>=a.count)continue;const auto& p=a.pieces[n];
  if(raw[0]==uint8_t(a.y+p.y-a.camera[1]+16)&&raw[1]==uint8_t(a.x+p.x-a.camera[0]+8)&&
     (raw[2]&0xfe)==(p.tile&0xfe)&&raw[3]==p.attributes)return true;
 }
 return false;
}
}
