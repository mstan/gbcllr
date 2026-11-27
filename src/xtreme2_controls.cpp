#include "xtreme2_controls.hpp"
#include "native_mods.hpp"
#include <gbcllr/input.hpp>
#include <algorithm>
#include <cstring>
#include <stdexcept>
namespace gbcllr {
Xtreme2Controls::Xtreme2Controls(bool enabled):enabled_(enabled){std::memset(&state_,0,sizeof state_);}
void Xtreme2Controls::inputs(uint8_t first,uint8_t second) {
 std::array<uint8_t,2> held{first,second};
 for(unsigned i=0;i<2;++i) {
  if(held[i]&~3u)throw std::runtime_error("Invalid virtual game inputs");
  state_.pending[i]=enabled_&&held[i]!=3?uint8_t(held[i]&~state_.previous[i]):0;
  state_.previous[i]=held[i];
 }
}
void Xtreme2Controls::instruction(const ModContext& c,std::span<uint16_t,6> cpu) {
 if(!state_.active){if(c.wram[0xc2]||c.wram[0xc]||c.wram[0x14]||c.wram[0xac]||!c.wram[0x9b0])discard_pending();return;}
 if(c.rom_bank==0x28&&cpu[5]==0x7b33&&!state_.entered) {
  // The original far wrapper uses A for the destination bank. Supply the
  // chosen weapon only after that wrapper reaches the original entry.
  cpu[0]=uint16_t((unsigned(state_.target)<<8)|(cpu[0]&255));state_.entered=1;
 }
 if(state_.entered&&c.rom_bank==5&&cpu[5]==0x4001&&cpu[4]==state_.caller[4]) {
  std::copy(state_.caller.begin(),state_.caller.end(),cpu.begin());
  state_.active=state_.entered=state_.target=0;state_.caller={};
 }
}
bool Xtreme2Controls::dispatch(const ModContext& c,std::span<uint16_t,6> cpu,std::span<uint8_t> ram,unsigned seat,bool eligible) {
 if(seat>1)throw std::runtime_error("Invalid virtual input player");
 unsigned fresh=state_.pending[seat];state_.pending[seat]=0;
 if(!enabled_||state_.active||!eligible||(fresh!=virtual_l&&fresh!=virtual_r)||ram.size()<0x1000||
    c.rom_bank!=5||cpu[5]!=0x4001||cpu[4]<0xc040||cpu[4]>0xcffc||ram[0xc9]||ram[0x101]!=2||
    !(ram[0x121]&127)||ram[0x102]==0x0c||ram[0x102]==0x22||ram[0x131]>9||
    (ram[0x5f]==6&&ram[0x2d]))return false;
 unsigned current=ram[0x131],next=current;
 do {next=(next+(fresh==virtual_l?9:1))%10;}while(next!=current&&next&&!(ram[0x6d+next]&0x40));
 if(next==current)return false;
 std::copy(cpu.begin(),cpu.end(),state_.caller.begin());state_.active=uint8_t(seat+1);state_.target=uint8_t(next);state_.entered=0;
 // Real CALL frame into the existing far-call wrapper. It switches banks,
 // runs28:7B33 (including native graphics/palette reload), then returns to
 // the original player entry. No cartridge byte is ever written.
 cpu[4]-=2;ram[cpu[4]-0xc000]=0x01;ram[cpu[4]-0xc000+1]=0x40;
 cpu[0]=uint16_t(0x2800|(cpu[0]&255));cpu[3]=0x7b33;cpu[5]=0x0675;return true;
}
std::vector<uint8_t> Xtreme2Controls::snapshot()const {
 std::vector<uint8_t> out(sizeof state_);std::memcpy(out.data(),&state_,sizeof state_);return out;
}
void Xtreme2Controls::restore(std::span<const uint8_t> bytes) {
 State staged{};if(bytes.size()!=sizeof staged)throw std::runtime_error("Invalid virtual controls state size");
 std::memcpy(&staged,bytes.data(),sizeof staged);
 for(unsigned i=0;i<2;++i)if(staged.previous[i]>3||staged.pending[i]>2)throw std::runtime_error("Invalid virtual controls input state");
 if(staged.active>2||staged.target>9||staged.entered>1||(!staged.active&&(staged.target||staged.entered))||
    (staged.active&&(staged.caller[4]<0xc040||staged.caller[4]>0xcffc||staged.caller[5]!=0x4001)))throw std::runtime_error("Invalid native weapon continuation");
 state_=staged;
}
}
