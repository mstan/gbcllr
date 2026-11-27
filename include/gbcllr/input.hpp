#pragma once
#include <cstdint>
namespace gbcllr {
inline constexpr uint8_t virtual_l=1,virtual_r=2;
// Host-native actions have their own channel. Only buttons enters the actual
// Game Boy joypad: Right Left Up Down A B Select Start.
struct LogicalInput {
 uint8_t buttons=0;
 uint8_t virtual_buttons=0;
};
}
