#pragma once
#include <gbcllr/machine.hpp>
#include <array>
namespace gbcllr {
struct ModContext;
struct OracleGraphicsData {
 Registers registers;
 std::array<uint8_t,128> hram{};
 std::array<uint8_t,8192> bank{};
 uint16_t destination=0,size=0;
 uint8_t bank_selector=0,rom_bank=0;
 bool video=true,select_banks=true;
};
// Exact Ages/Seasons graphics/layout data callers in private LCD-off evaluation.
// Staged and portable; false leaves live bytes untouched and uses original code.
bool oracle_graphics_data(const ModContext&,Registers,OracleGraphicsData&);
bool oracle_layout_data(const ModContext&,Registers,OracleGraphicsData&);
// Original LCD-off banked VRAM/WRAM bulk fill; preserves all bank latches.
bool oracle_memory_data(const ModContext&,Registers,unsigned vram_bank,OracleGraphicsData&);
}
