#pragma once
#include <array>
#include <cstdint>
#include <span>
#include <vector>
namespace gbcllr {
struct ModContext;
// Optional host inputs invoke the cartridge's original weapon-selection
// service. Nothing is installed in cartridge memory or the eight-bit joypad.
class Xtreme2Controls {
public:
 explicit Xtreme2Controls(bool enabled=false);
 bool enabled()const{return enabled_;}
 bool active()const{return state_.active!=0;}
 void inputs(uint8_t first,uint8_t second);
 void discard_pending(){state_.pending={};}
 // Called before the co-op projection hook. Preserves the original caller's
 // registers while its real bank wrapper and reload routines execute.
 void instruction(const ModContext&,std::span<uint16_t,6>);
 // Call after projecting a logical player at05:4001. Ineligible requests are
 // discarded, including menu/scene/death/transport and Zero's techniques.
 bool dispatch(const ModContext&,std::span<uint16_t,6>,std::span<uint8_t>,unsigned seat,bool eligible);
 std::vector<uint8_t> snapshot()const;
 void restore(std::span<const uint8_t>);
private:
 struct State {
  std::array<uint8_t,2> previous{},pending{};
  uint8_t active=0,target=0,entered=0;
  std::array<uint16_t,6> caller{};
 } state_;
 bool enabled_=false;
};
}
