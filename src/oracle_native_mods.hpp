#pragma once
#include <gbcllr/mods.hpp>
#include <array>
#include <memory>
#include <span>
namespace gbcllr {
struct ModContext;
class Machine;
// Current-build native source history. The actual original PPU/actor inputs
// are retained here; expanded pixels and host sampling buffers are separate.
struct OracleRoomState {
 uint32_t valid=0,captures=0;
 int camera_x=0,camera_y=0,room_width=0,room_height=0;
 uint8_t lcdc=0;
 std::array<uint8_t,0x8000> ram{};
 std::array<uint8_t,0x4000> vram{};
 std::array<uint32_t,32> bg{},obj{};
 std::array<uint8_t,64> bg_palette{},obj_palette{};
 uint32_t authority=0; // bit0: real player/projectile; bit1: uncommitted foreign effects.
 std::array<uint32_t,16> enemy_origins{}; // Original ID/subid/X/Y, including later deletions.
 uint32_t enemy_origin_mask=0;
 std::array<uint32_t,16> interaction_origins{};
 uint32_t interaction_origin_mask=0;
};
struct OracleWorldState {
 OracleRoomState active;
 std::array<OracleRoomState,16> rooms;
 std::array<uint32_t,16> retry;
 OracleRoomState owner;
 uint32_t frame=0,failures=0,ticks=0,progress=0,mode=0;
 int native_x=0,focus_x=0;
 // Pending ordinary native entry; no speculative progress or script memory.
 uint32_t handoff_stage=0; // Retired phase word; must remain zero.
 uint32_t handoff_room=0,handoff_direction=0,handoffs=0;
 int native_y=0,focus_y=0;
 struct ItemOwner {uint32_t room=0,arriving=0;}; // room ID+1, zero is the live owner.
 std::array<ItemOwner,16> items{};
 uint32_t item_transfers=0;
};
class OracleNativeMods {
public:
 OracleNativeMods(std::string_view,const NativeFeature&,bool);
 ~OracleNativeMods();
 uint8_t read(ModContext&,uint16_t,uint8_t);
 static constexpr bool watches_cpu_read(uint16_t a) {
  switch(a) {
   case 0xcd00:case 0xcd06:case 0xcd0c:case 0xcd0d:case 0x4126:case 0x4133:case 0xc49d:case 0xd00d:case 0xd00b:case 0xd10d:case 0xd10b:
   case 0x1585:case 0x1573:case 0x6bfc:case 0x6bfe:case 0x6aef:case 0x6af1:return true;
   default:return (a>=0xc200&&a<0xc300)||(a>=0xce00&&a<0xd000)||(a>=0xd700&&a<=0xdf24&&((a&255)==0||(a&255)==0x24))||a==0xd024||a==0xcc86||a==0xcc87||a==0xcca0||a==0xcca1; // qualified native dispatcher stack reads
  }
 }
 uint8_t cpu_read(ModContext&,uint16_t,uint8_t,std::span<uint16_t,6>);
 void scanline(ModContext&,uint8_t);
 void viewport(unsigned,unsigned);
 void visibility(bool hide_closed_rooms);
 void preserve_host_presentation(const OracleNativeMods&)noexcept;
 void frame(Machine&);
 ModView render(ModContext&,std::span<const uint32_t>);
 std::vector<uint8_t> snapshot()const;
 void restore(std::span<const uint8_t>);
private:
 struct Impl;std::unique_ptr<Impl> impl_;
};
}
