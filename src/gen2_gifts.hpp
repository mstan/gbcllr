#pragma once
#include <array>
#include <cstdint>
#include <string_view>

namespace gbcllr {
// Original cart catalogues: pret/pokegold f2cde36f and
// pret/pokecrystal 3c813b0b, data/items/mystery_gift_items.asm and
// data/decorations/mystery_gift_decos.asm. Values are pending input,
// never inventory quantities or claimed/owned event flags.
struct Gen2Gift {std::string_view name;uint8_t item,decoration;};
inline constexpr std::array<Gen2Gift,74> gen2_gifts{{
 {"Berry",0xad,0},{"PRZCureBerry",0x4e,0},{"Mint Berry",0x54,0},
 {"Ice Berry",0x50,0},{"Burnt Berry",0x4f,0},{"PSNCureBerry",0x4a,0},
 {"Guard Spec.",0x29,0},{"X Defend",0x33,0},{"X Attack",0x31,0},
 {"Bitter Berry",0x53,0},{"Dire Hit",0x2c,0},{"X Special",0x35,0},
 {"X Accuracy",0x21,0},{"Eon Mail",0xb9,0},{"Morph Mail",0xba,0},
 {"Music Mail",0xbc,0},{"MiracleBerry",0x6d,0},{"Gold Berry",0xae,0},
 {"Revive",0x27,0},{"Great Ball",0x04,0},{"Super Repel",0x2a,0},
 {"Max Repel",0x2b,0},{"Elixer",0x41,0},{"Ether",0x3f,0},
 {"Water Stone",0x18,0},{"Fire Stone",0x16,0},{"Leaf Stone",0x22,0},
 {"Thunderstone",0x17,0},{"Max Ether",0x40,0},{"Max Elixer",0x15,0},
 {"Max Revive",0x28,0},{"Scope Lens",0x8c,0},{"HP Up",0x1a,0},
 {"PP Up",0x3e,0},{"Rare Candy",0x20,0},{"Bluesky Mail",0xbb,0},
 {"Mirage Mail",0xbd,0},
 {"Jigglypuff Doll",0,22},{"Poliwag Doll",0,26},{"Diglett Doll",0,27},
 {"Staryu Doll",0,28},{"Magikarp Doll",0,29},{"Oddish Doll",0,30},
 {"Gengar Doll",0,31},{"Shellder Doll",0,32},{"Grimer Doll",0,33},
 {"Voltorb Doll",0,34},{"Clefairy Poster",0,13},{"Jigglypuff Poster",0,14},
 {"Super NES",0,16},{"Weedle Doll",0,35},{"Geodude Doll",0,37},
 {"Machop Doll",0,38},{"Magnaplant",0,8},{"Tropicplant",0,9},
 {"Famicom",0,15},{"Nintendo 64",0,17},{"Bulbasaur Doll",0,23},
 {"Squirtle Doll",0,25},{"Pink Bed",0,1},{"Polkadot Bed",0,2},
 {"Red Carpet",0,4},{"Blue Carpet",0,5},{"Yellow Carpet",0,6},
 {"Green Carpet",0,7},{"Jumboplant",0,10},{"Virtual Boy",0,18},
 {"Big Onix Doll",0,41},{"Pikachu Poster",0,12},{"Big Lapras Doll",0,42},
 {"Surf Pikachu Doll",0,20},{"Pikachu Bed",0,3},{"Unown Doll",0,36},
 {"Tentacool Doll",0,39}
}};
}
