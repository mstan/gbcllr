#pragma once
#include <cstdint>
#include <string_view>
#ifndef GBCLLR_SML2_COLOR_ASSETS
#define GBCLLR_SML2_COLOR_ASSETS 0
#endif
#ifndef GBCLLR_SML2_ASSET_ID
#define GBCLLR_SML2_ASSET_ID "0000000000000000000000000000000000000000000000000000000000000000"
#endif
#if GBCLLR_SML2_COLOR_ASSETS
#include "sml2_color_data.hpp"
#else
namespace gbcllr::sml2_color {
inline constexpr uint8_t bg_attributes[8192]{},bg_palettes[2048]{},obj_palettes[2048]{};
inline constexpr uint8_t block_tiles[512]{};
inline constexpr uint8_t hud_attributes[32]{};
inline int block_tile(unsigned,unsigned,unsigned,unsigned,unsigned){return -1;}
inline constexpr bool fire_available=false;
inline const uint8_t* fire_texture(unsigned,unsigned){return nullptr;}
inline constexpr unsigned fixed_count=0;
inline constexpr uint8_t fixed_modes[1]{},fixed_old_map[1]{},fixed_color_map[1]{},fixed_attributes[1]{},fixed_old_ppu[1]{},fixed_color_ppu[1]{},fixed_bg_palettes[1]{},fixed_obj_palettes[1]{};
inline constexpr uint8_t fixed_object_attributes[1]{255};
inline constexpr unsigned bonus_frame_count=0;
inline constexpr uint16_t bonus_presentation_index[1]{0};
inline constexpr bool paid_bonus_available=false;
inline constexpr uint8_t paid_bonus_tiles[1]{};
inline constexpr int8_t fixed_submodes[1]{-1};
inline constexpr uint8_t fixed_bosses[1]{};
inline constexpr uint8_t fixed_stages[1]{255};
inline constexpr int8_t fixed_phases[1]{-1};
inline constexpr uint8_t world_old_map[1]{},world_color_map[1]{},world_attributes[1]{};
inline constexpr uint8_t world_animation_original[128]{},world_animation_color[128]{};
inline constexpr uint8_t world_object_attributes[256]{};
inline constexpr bool world_animation_available=false;
inline constexpr bool castle_flash_available=false;
inline constexpr uint8_t castle_flash_palette[64]{};
inline constexpr bool castle_objects_available=false;
inline constexpr uint8_t castle_object_attributes[1024]{};
inline constexpr bool ending_extra_available=false;
inline const uint8_t* fixed_original(unsigned,unsigned){return nullptr;}
inline const uint8_t* fixed_texture(unsigned,unsigned,unsigned){return nullptr;}
struct Piece{int8_t y,x;uint8_t tile,attr;};
inline constexpr Piece pieces[1]{};
inline constexpr uint16_t presentation_offset[1]{};
inline constexpr uint8_t presentation_length[1]{};
inline unsigned presentation(unsigned,unsigned){return 0;}
inline constexpr unsigned stage_count=0;
inline const uint8_t* texture(unsigned,unsigned,unsigned,unsigned){return nullptr;}
inline const uint8_t* original(unsigned,unsigned,unsigned){return nullptr;}
}
#endif
namespace gbcllr::sml2_color {
inline constexpr bool available=GBCLLR_SML2_COLOR_ASSETS!=0;
inline constexpr std::string_view source_identity="1dd604a91b9ee99ef381cc795da3ae28c72d04ba9de78b355df829e56ce49c46";
inline constexpr std::string_view data_identity=GBCLLR_SML2_ASSET_ID;
}
