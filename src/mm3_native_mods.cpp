#include "mm3_native_mods.hpp"

#include "native_mods.hpp"
#include "megaman_pause_canvas.hpp"

#include <gbcllr/input.hpp>

#include <algorithm>

#include <array>

#include <cstring>

#include <stdexcept>

#include <type_traits>

#ifndef GBCLLR_MM34_DX_ASSETS

#define GBCLLR_MM34_DX_ASSETS 0

#endif

#ifndef GBCLLR_MM34_DX_ASSET_ID

#define GBCLLR_MM34_DX_ASSET_ID ""

#endif

#if GBCLLR_MM34_DX_ASSETS

#include "mm34_dx_data.hpp"

#endif

namespace gbcllr {

struct Mm3NativeMods::Impl {

 struct Data {

  std::span<const uint8_t> fades,attributes,banks,object_banks,object_attributes;

  std::span<const uint16_t> layouts,resources,pointers,colors;

  std::span<const uint16_t> body_addresses,body_offsets;

  std::span<const uint8_t> body_records,body_original_art,body_color_art;

  std::span<const uint32_t> art_offsets,pose_keys,pose_offsets;

  std::span<const uint16_t> pose_addresses;

  std::span<const uint8_t> art_counts,resource_native_counts,art,pose_records;

  std::span<const uint8_t> menu_boot_vram,menu_font,menu_labels,menu_lower,menu_energy;

  std::span<const uint16_t> menu_bottom,menu_top;

  std::span<const uint8_t> hud_original_art,hud_color_art,hud_attributes;

  std::span<const uint8_t> title_original,title_art,title_vram1,title_extra_oam;

  std::span<const uint16_t> title_palettes;

  std::span<const uint8_t> ending_original,ending_art,ending_attrs;

  std::span<const uint16_t> ending_palettes;

  std::span<const uint8_t> ending_actor_native,ending_actor_map;

  std::span<const uint8_t> select_original_art,select_color_art,select_vram1,select_completed_attributes,select_oam,select_oam_destinations;

  std::span<const uint16_t> select_palettes,select_alternate_palettes,select_oam_offsets;

  std::span<const uint8_t> intro_original_art,intro_color_art,intro_attributes,intro_overlays;

  std::span<const uint16_t> intro_pose_addresses;

  std::span<const uint8_t> password_original_art,password_color_art,password_vram1,password_original_map,password_color_map,password_map_valid;

  std::span<const uint16_t> password_palettes;

  std::span<const uint32_t> background_records,metatile_records;

  std::span<const uint8_t> background_original,background_art,metatile_original,metatile_art,background_extra_art;

  std::span<const uint16_t> background_extra_keys;

  std::span<const uint16_t> credits_palettes,credits_map_sources;

  std::span<const uint8_t> credits_map_attributes,credits_map_original,credits_map_art;

  std::span<const uint16_t> iv_cast_palettes;

  std::span<const uint8_t> iv_portrait_attributes;

  std::span<const uint16_t> iv_style_addresses,iv_style_palettes,story_map_styles,story_award_palettes,iv_award_object_palettes;
  std::span<const uint8_t> iv_award_map_original,iv_award_map_art,story_overlay_oam,story_map_original,story_map_art,iv_charge_palette_lows;
  std::span<const uint16_t> story_map_records;

  std::span<const uint8_t> iv_style_attributes,iv_style_textures;

  std::span<const uint32_t> iv_style_texture_records;

 } data;

 // Piece flags use bits0/1; bits2..7 retain native OAM slot0..39 (40 if uncapped).

 struct Piece {int16_t x=0,y=0;uint8_t attr=0,color=0,reserved=0,tile_id=0;std::array<uint8_t,16> tile{};};

 struct Raster {uint8_t scx=0,scy=0,bgp=0xe4,lcdc=0,obp0=0xe4,obp1=0xe4,wx=7,wy=144;};

 struct Weapon {int16_t world=0;uint8_t valid=0,padding=0;};

 struct Credits {

  std::array<uint16_t,16> palettes{};

  std::array<uint8_t,2048> attrs{},map_source{},map_art{},map_valid{};

  uint8_t initialized=0,padding=0,shade_valid=0,shade=0;

 };

 struct IvStyle {

  std::array<uint16_t,16> palettes{};

  std::array<uint8_t,2048> attrs{};

  std::array<uint8_t,6144> vram1{};

  uint8_t initialized=0,kind=0,shade_valid=0,shade=0;

  uint8_t fade_override=0,fade_valid=0;

  uint16_t pending_style=0;
  uint8_t overlay=0,map_variant=0,object_fade_override=0,object_fade_valid=0;

 };

 static_assert(std::has_unique_object_representations_v<IvStyle>);

 struct State {

  uint8_t previous=0,pending=0,continuation=0,target=0;

  std::array<uint16_t,6> caller{};

  uint16_t count=0,build_count=0,display_count=0,active_count=0;

  uint16_t overflow=0,build_overflow=0,display_overflow=0,active_overflow=0;

  uint8_t valid=0,building=0,menu=0,recoil_disabled=0,display_valid=0,previous_room_low=0,active_valid=0,previous_room_high=0;

  uint8_t hurt_timer=0,native_wait_flags=0;

  uint16_t hurt_actor=0;

  std::array<Piece,1024> pieces{},build{},display_pieces{},active_pieces{};

  std::array<uint8_t,40> owned{},build_owned{},display_owned{},active_owned{};

  std::array<uint8_t,8192> wram{},display_wram{},vram{},active_wram{},active_vram{};

  uint32_t captures=0;

  uint32_t width=160;

  uint8_t resize_scans=0;

  // Original generic resource copy in flight: ID and destination low/high.

  std::array<uint8_t,3> resource_copy{};

  uint32_t spawn_scans=0,spawn_reads=0,cull_extensions=0;

  std::array<Raster,144> raster{},display_raster{};

  // Actual hardware-visible tile-row planes. HBlank texture updates may

  // change later scanlines; a whole-frame VRAM copy cannot reproduce them.

  std::array<std::array<uint8_t,512>,144> bg_rows{},display_bg_rows{};

  std::array<std::array<uint8_t,32>,144> bg_maps{},display_bg_maps{};

  uint8_t fixed_active=0,fixed_display=0,fixed_active_recoil=0,fixed_display_recoil=0;

  std::array<uint8_t,4> fixed_active_arrow{},fixed_display_arrow{};

  uint16_t animation_stack=0,animation_padding=0;

  std::array<uint8_t,8192> dx_source{},dx_art{},dx_valid{};

  std::array<Weapon,4> weapons{};

  Credits credits{},active_credits{},display_credits{};

  IvStyle iv_credits{},iv_active_credits{},iv_display_credits{};

 } state{};

 static_assert(std::has_unique_object_representations_v<Piece>);

 static_assert(std::has_unique_object_representations_v<State>);

 unsigned title;

 bool shoulders=false,smooth=false,deflicker=false,color=false;

 int requested=160;

 unsigned width=160;

 std::vector<uint32_t> pixels;
 std::vector<uint32_t> pause_canvas;
 std::vector<uint32_t> framing_background;

 std::vector<uint8_t> opaque;

 explicit Impl(unsigned n):title(n){

#if GBCLLR_MM34_DX_ASSETS

  using namespace mm34_dx;

  if(n==3)data={mm3_fade_banks,mm3_stage_attributes,mm3_palette_banks,mm3_object_banks,mm3_object_attributes,mm3_stage_layouts,mm3_resource_layouts,mm3_palette_pointers,mm3_palette_words};

  else data={mm4_fade_banks,mm4_stage_attributes,mm4_palette_banks,mm4_object_banks,mm4_object_attributes,mm4_stage_layouts,mm4_resource_layouts,mm4_palette_pointers,mm4_palette_words};

  if(n==3){data.body_addresses=mm3_body_addresses;data.body_offsets=mm3_body_offsets;data.body_records=mm3_body_records;data.body_original_art=mm3_body_original_art;data.body_color_art=mm3_body_color_art;}

  else{data.body_addresses=mm4_body_addresses;data.body_offsets=mm4_body_offsets;data.body_records=mm4_body_records;data.body_original_art=mm4_body_original_art;data.body_color_art=mm4_body_color_art;}

  if(n==3){data.art_offsets=mm3_actor_art_offsets;data.art_counts=mm3_actor_art_counts;data.resource_native_counts=mm3_resource_native_counts;data.art=mm3_actor_art;data.pose_keys=mm3_actor_pose_keys;data.pose_offsets=mm3_actor_pose_offsets;data.pose_addresses=mm3_actor_pose_addresses;data.pose_records=mm3_actor_pose_records;}

  else{data.art_offsets=mm4_actor_art_offsets;data.art_counts=mm4_actor_art_counts;data.resource_native_counts=mm4_resource_native_counts;data.art=mm4_actor_art;data.pose_keys=mm4_actor_pose_keys;data.pose_offsets=mm4_actor_pose_offsets;data.pose_addresses=mm4_actor_pose_addresses;data.pose_records=mm4_actor_pose_records;}

  if(n==3){data.menu_boot_vram=mm3_menu_boot_vram;data.menu_font=mm3_menu_font;data.menu_labels=mm3_menu_labels;data.menu_lower=mm3_menu_lower;data.menu_energy=mm3_menu_energy;data.menu_bottom=mm3_menu_bottom;data.menu_top=mm3_menu_top;}

  else{data.menu_boot_vram=mm4_menu_boot_vram;data.menu_font=mm4_menu_font;data.menu_labels=mm4_menu_labels;data.menu_lower=mm4_menu_lower;data.menu_energy=mm4_menu_energy;data.menu_bottom=mm4_menu_bottom;data.menu_top=mm4_menu_top;}

  if(n==3){data.hud_original_art=mm3_hud_original_art;data.hud_color_art=mm3_hud_color_art;data.hud_attributes=mm3_hud_attributes;}else{data.hud_original_art=mm4_hud_original_art;data.hud_color_art=mm4_hud_color_art;data.hud_attributes=mm4_hud_attributes;}

  if(n==3){data.title_original=mm3_title_original_vram;data.title_art=mm3_title_color_vram;data.title_vram1=mm3_title_vram1;data.title_extra_oam=mm3_title_extra_oam;data.title_palettes=mm3_title_palettes;}

  else{data.title_original=mm4_title_original_vram;data.title_art=mm4_title_color_vram;data.title_vram1=mm4_title_vram1;data.title_extra_oam=mm4_title_extra_oam;data.title_palettes=mm4_title_palettes;}

  if(n==3){data.ending_original=mm3_ending_original;data.ending_art=mm3_ending_art;data.ending_attrs=mm3_ending_attrs;data.ending_palettes=mm3_ending_palettes;}

  else{data.ending_original=mm4_ending_original;data.ending_art=mm4_ending_art;data.ending_attrs=mm4_ending_attrs;data.ending_palettes=mm4_ending_palettes;data.ending_actor_native=mm4_ending_actor_native;data.ending_actor_map=mm4_ending_actor_map;}

  if(n==3){data.select_original_art=mm3_select_original_art;data.select_color_art=mm3_select_color_art;data.select_vram1=mm3_select_vram1;data.select_palettes=mm3_select_palettes;data.select_alternate_palettes=mm3_select_alternate_palettes;data.select_completed_attributes=mm3_select_completed_attributes;data.select_oam=mm3_select_oam;data.select_oam_offsets=mm3_select_oam_offsets;data.select_oam_destinations=mm3_select_oam_destinations;data.intro_original_art=mm3_intro_original_art;data.intro_color_art=mm3_intro_color_art;data.intro_attributes=mm3_intro_attributes;data.intro_overlays=mm3_intro_overlays;data.intro_pose_addresses=mm3_intro_pose_addresses;data.password_original_art=mm3_password_original_art;data.password_color_art=mm3_password_color_art;data.password_vram1=mm3_password_vram1;data.password_original_map=mm3_password_original_map;data.password_color_map=mm3_password_color_map;data.password_map_valid=mm3_password_map_valid;data.password_palettes=mm3_password_palettes;}

  if(n==3){data.background_records=mm3_background_records;data.background_original=mm3_background_original;data.background_art=mm3_background_art;data.metatile_records=mm3_metatile_records;data.metatile_original=mm3_metatile_original;data.metatile_art=mm3_metatile_art;data.background_extra_keys=mm3_background_extra_keys;data.background_extra_art=mm3_background_extra_art;}

  else{data.background_records=mm4_background_records;data.background_original=mm4_background_original;data.background_art=mm4_background_art;data.metatile_records=mm4_metatile_records;data.metatile_original=mm4_metatile_original;data.metatile_art=mm4_metatile_art;data.background_extra_keys=mm4_background_extra_keys;data.background_extra_art=mm4_background_extra_art;}

  if(n==3){data.iv_style_addresses=mm3_story_style_addresses;data.iv_style_palettes=mm3_story_style_palettes;data.iv_style_attributes=mm3_story_style_attributes;data.iv_style_texture_records=mm3_story_style_texture_records;data.iv_style_textures=mm3_story_style_textures;data.story_map_styles=mm3_story_map_styles;data.story_award_palettes=mm3_story_award_palettes;data.story_overlay_oam=mm3_story_overlay_oam;data.story_map_records=mm3_story_map_records;data.story_map_original=mm3_story_map_original;data.story_map_art=mm3_story_map_art;}

  if(n==4){data.iv_cast_palettes=mm4_iv_cast_palettes;data.iv_portrait_attributes=mm4_iv_portrait_attributes;

   data.iv_style_addresses=mm4_iv_style_addresses;data.iv_style_palettes=mm4_iv_style_palettes;data.iv_style_attributes=mm4_iv_style_attributes;data.iv_style_texture_records=mm4_iv_style_texture_records;data.iv_style_textures=mm4_iv_style_textures;data.iv_award_map_original=mm4_iv_award_map_original;data.iv_award_map_art=mm4_iv_award_map_art;data.iv_award_object_palettes=mm4_iv_award_object_palettes;data.iv_charge_palette_lows=mm4_iv_charge_palette_lows;}

  if(n==3){data.credits_palettes=mm3_credits_palettes;data.credits_map_sources=mm3_credits_map_sources;data.credits_map_attributes=mm3_credits_map_attributes;data.credits_map_original=mm3_credits_map_original;data.credits_map_art=mm3_credits_map_art;}

#endif

 }

 uint8_t piece_color(unsigned bank,unsigned address)const {

  if(!color||address<0x4000||address>=0x8000)return 0;

  auto found=std::find(data.object_banks.begin(),data.object_banks.end(),bank);

  return found==data.object_banks.end()?0:data.object_attributes[size_t(found-data.object_banks.begin())*0x4000+address-0x4000];

 }

 std::array<uint16_t,16> palettes(const ModContext& c)const {

  std::array<uint16_t,16> out{};if(!color)return out;

  unsigned stage=peek(c,title==3?0xdf08:0xdf0a);

  if(stage>=data.layouts.size()/16)return out;

  std::copy_n(data.layouts.data()+stage*16,16,out.begin());

  unsigned weapon=peek(c,title==3?0xde99:0xdeab);if(weapon<13){out[1]=uint16_t(0x4100+weapon*8);out[9]=uint16_t((title==3?0x4700:0x4a00)+weapon*8);}

  for(unsigned i=0;i<12;++i){unsigned resource=peek(c,(title==3?0xc3c5:0xc3c6)+i*2);if(!resource)break;if(resource>=data.resources.size()/16)continue;

   for(unsigned slot=0;slot<16;++slot){auto value=data.resources[resource*16+slot];if(value)out[slot]=value;}}

  return out;

 }

 uint32_t colored(uint16_t pointer,uint8_t shade,unsigned ink,bool object,uint32_t fallback)const {

  if(!color||!pointer||ink>3||(object&&!ink))return fallback;

  auto bank=std::find(data.banks.begin(),data.banks.end(),data.fades[shade]);auto at=std::lower_bound(data.pointers.begin(),data.pointers.end(),pointer);

  if(bank==data.banks.end()||at==data.pointers.end()||*at!=pointer)return fallback;

  size_t offset=(size_t(bank-data.banks.begin())*data.pointers.size()+size_t(at-data.pointers.begin()))*4+(object?ink-1:ink);

  unsigned rgb=data.colors[offset];return 0xff000000u|((rgb&31)*255/31<<16)|(((rgb>>5)&31)*255/31<<8)|(((rgb>>10)&31)*255/31);

 }

 uint16_t weapon()const{return title==3?0xde99:0xdeab;}

 uint16_t tick()const{return title==3?0x0956:0x0ab3;}

 uint16_t health()const{return title==3?0xde9c:0xdeae;}

 bool title_matches(const ModContext& c)const {

  return color&&data.title_original.size()==8192&&c.ppu&&(c.ppu->lcdc&0x80)&&

   std::equal(c.vram+0x1000,c.vram+(title==3?0x1670:0x1630),data.title_original.data()+0x1000)&&

   std::equal(c.vram+0x0800,c.vram+0x0c00,data.title_original.data()+0x0800)&&

   std::equal(c.vram+0x1800,c.vram+0x1c00,data.title_original.data()+0x1800);

 }

 unsigned select_matches(const ModContext& c)const {

  if(!color||title!=3||data.select_original_art.size()!=0x3000||!c.ppu||!(c.ppu->lcdc&128)||peek(c,0xcc96)!=15||peek(c,0xccb2)<1||peek(c,0xccb2)>8)return 0;

  unsigned group=peek(c,0xdf07)?1:0;

  if(peek(c,0xccb2)==1)return std::equal(c.vram,c.vram+0x1800,data.select_original_art.data()+group*0x1800)?2+group:0;

  unsigned boss=peek(c,0xdf08);return boss>=1&&boss<=8&&std::equal(c.vram+0x1000,c.vram+0x17e0,data.select_original_art.data()+group*0x1800+0x1000)?5+group:0;

 }

 bool password_matches(const ModContext& c)const {

  return color&&title==3&&data.password_original_art.size()==0x1800&&c.ppu&&(c.ppu->lcdc&128)&&peek(c,0xcc96)==15&&peek(c,0xcca5)==11&&peek(c,0xccb2)>=1&&peek(c,0xccb2)<=3&&std::equal(c.vram,c.vram+0x800,data.password_original_art.begin())&&std::equal(c.vram+0x1000,c.vram+0x1800,data.password_original_art.begin()+0x1000);

 }

 // The original final controller remains in this state while the meteors

 // and native actor continue. Earlier ending scenes keep their own work.

 bool credits_scene(const ModContext& c)const {return color&&title==3&&c.wram&&peek(c,0xdf0a)==6&&peek(c,0xcc96)==6&&peek(c,0xccb2)<0x38;}

 bool thanks_scene(const ModContext& c)const {return color&&title==3&&c.wram&&peek(c,0xdf0a)==6&&peek(c,0xcc96)==6&&(peek(c,0xccb2)==0x38||peek(c,0xccb2)==0x3a);}

 bool thanks_matches(const ModContext& c)const {

  if(title==4){

   if(!color||data.ending_original.size()!=8192||data.ending_actor_native.size()!=53||!c.ppu||!(c.ppu->lcdc&128)||peek(c,0xc90b)!=1||peek(c,0xde44)!=0||!std::equal(c.vram,c.vram+0xd0,data.ending_original.begin())||!std::equal(c.vram+0x1000,c.vram+0x13a0,data.ending_original.begin()+0x1000))return false;

   // Original14:67FA/67FE fixes this actor origin. Bind its complete real

   // thirteen-piece OAM producer before replacing it with the authored list.

   int x=128,y=88;for(unsigned i=0;i<13;++i){auto source=data.ending_actor_native.data()+i*4;auto actual=c.oam+i*4;y+=int8_t(source[0]);x+=int8_t(source[1]);if(actual[0]!=y+16||actual[1]!=x+8||actual[2]!=source[2]||actual[3]!=source[3])return false;}return true;

  }

  return thanks_scene(c)&&data.ending_original.size()==8192&&c.ppu&&(c.ppu->lcdc&128)&&

   std::equal(c.vram,c.vram+0x420,data.ending_original.data())&&

   std::equal(c.vram+0x1200,c.vram+0x1600,data.ending_original.data()+0x1200);

 }

 ModView title_view(const ModContext& c,std::span<const uint32_t> native) {

  bool ending=state.fixed_display==4,intro=state.fixed_display==5||state.fixed_display==6,password=state.fixed_display==7,board=state.fixed_display==2||state.fixed_display==3||intro,paired=ending||board||password;unsigned group=intro?state.fixed_display-5:(board?state.fixed_display-2:0);

  auto frame=c;if(paired){frame.wram=state.display_wram.data();frame.vram=state.vram.data();}

  std::array<uint16_t,16> layout{};auto source_layout=password?data.password_palettes:(ending?data.ending_palettes:(board?data.select_palettes:data.title_palettes));std::copy_n(source_layout.begin(),16,layout.begin());

  if(board&&(peek(frame,0xde9a)&0x3c)==0x3c)std::copy_n(data.select_alternate_palettes.begin(),16,layout.begin());

  if(intro)layout[9]=0x485c;

  auto art0=password?data.password_color_art:(ending?data.ending_art:(board?data.select_color_art.subspan(group*0x1800,0x1800):data.title_art));

  auto art1=password?data.password_vram1:(ending?data.ending_attrs:(board?data.select_vram1:data.title_vram1));

  auto tilemap=ending?(title==4?std::span<const uint8_t>(frame.vram,8192):data.ending_art):((board||password)?std::span<const uint8_t>(frame.vram,8192):data.title_art);

  unsigned completed=board&&!intro?(peek(frame,0xdf09)>>(group*4))&15:0;

  auto texture=[&](unsigned at,unsigned attr)->const uint8_t*{

   if(attr&8)return art1.data()+at;

   if(password)return std::equal(frame.vram+(at&~15u),frame.vram+(at&~15u)+16,data.password_original_art.data()+(at&~15u))?art0.data()+at:frame.vram+at;

   if(!intro)return art0.data()+at;

   unsigned boss=peek(frame,0xdf08)-1;

   if(at<0x800){unsigned source=boss*0x800+(at&~15u);if(std::equal(frame.vram+(at&~15u),frame.vram+(at&~15u)+16,data.intro_original_art.data()+source))return data.intro_color_art.data()+source+(at&15);}

   if(at<0x1800&&std::equal(frame.vram+(at&~15u),frame.vram+(at&~15u)+16,data.select_original_art.data()+group*0x1800+(at&~15u)))return art0.data()+at;

   return frame.vram+at;

  };

  std::array<std::array<std::array<uint32_t,4>,16>,256> rgba{};std::array<bool,256> ready{};

  auto colors=[&](uint8_t shade)->const std::array<std::array<uint32_t,4>,16>&{if(!ready[shade]){ready[shade]=true;for(unsigned slot=0;slot<16;++slot)for(unsigned ink=0;ink<4;++ink)rgba[shade][slot][ink]=colored(layout[slot],shade,ink,slot>=8,0xff000000u);}return rgba[shade];};

  pixels.assign(160*144,0xff000000u);opaque.assign(160*144,0);

  for(unsigned y=0;y<144;++y){const auto& line=state.display_raster[y];if(!(line.lcdc&128))continue;

   for(unsigned x=0;x<160;++x){

    bool window=paired&&(line.lcdc&32)&&y>=line.wy&&int(x)>=int(line.wx)-7;

    unsigned sx=window?unsigned(int(x)-int(line.wx)+7):(x+line.scx)&255,sy=window?y-line.wy:(y+line.scy)&255;

    unsigned map=(window?((line.lcdc&64)?0x1c00:0x1800):((line.lcdc&8)?0x1c00:0x1800))+(sy/8)*32+sx/8;

    unsigned tile=tilemap[map],attr=art1[map];

    if(password&&data.password_map_valid[map-0x1800]&&tile==data.password_original_map[map-0x1800])tile=data.password_color_map[map-0x1800];

    if(board)for(unsigned bit=0;bit<4;++bit)if(completed&(1u<<bit)){unsigned value=data.select_completed_attributes[bit*0x800+map-0x1800];if(value!=255)attr=value;}

    if(!ending&&!board&&!password&&title==4&&(map==0x198c||map==0x198d))tile=state.fixed_display_recoil?0x62:(map==0x198c?0x60:0x61);

    unsigned at=(line.lcdc&16)?tile*16:unsigned(0x1000+int(int8_t(tile))*16);

    unsigned row=((attr&64)?7-(sy&7):(sy&7))*2,bit=(attr&32)?sx&7:7-(sx&7),ink=((texture(at+row,attr)[0]>>bit)&1)|(((texture(at+row,attr)[1]>>bit)&1)<<1);

    if(ending&&title==4&&!window&&!(attr&0x68)){const auto& planes=state.display_bg_rows[y];ink=((planes[tile*2]>>bit)&1)|(((planes[tile*2+1]>>bit)&1)<<1);}

    pixels[y*160+x]=colors(line.bgp)[attr&7][ink];opaque[y*160+x]=uint8_t(ink?((attr&128)?2:1):0);

   }

  }

  auto draw=[&](int ox,int oy,unsigned tile,unsigned attr){const auto* art=texture(tile*16,attr);

   for(int y=0;y<8;++y)for(int x=0;x<8;++x){int px=ox+x,py=oy+y;if(px<0||px>=160||py<0||py>=144||!(state.display_raster[unsigned(py)].lcdc&2))continue;

    unsigned row=unsigned((attr&64)?7-y:y)*2,bit=unsigned((attr&32)?x:7-x),ink=((art[row]>>bit)&1)|(((art[row+1]>>bit)&1)<<1);if(!ink)continue;

    unsigned at=unsigned(py)*160+unsigned(px);if(opaque[at]==2||((attr&128)&&opaque[at]))continue;pixels[at]=colors(state.display_raster[unsigned(py)].bgp)[8+(attr&7)][ink];

   }

  };

  auto sprite=[&](const uint8_t* oam){draw(int(oam[1])-8,int(oam[0])-16,oam[2],oam[3]);};

  unsigned sprites=0;

  if(paired){

   std::array<uint8_t,160> oam{};std::copy_n(state.display_wram.begin(),160,oam.begin());

   auto extra=[&](unsigned index){unsigned begin=data.select_oam_offsets[index],end=data.select_oam_offsets[index+1];std::copy_n(data.select_oam.data()+begin,end-begin,oam.data()+data.select_oam_destinations[index]);};

   if(board&&!intro){unsigned owned=peek(frame,group?0xde9b:0xde9a)>>(group?0:2);

    for(unsigned index=0;index<4;++index)if(!(owned&(1u<<index)))extra(group*4+index);

    if(group&&(owned&15)==15)extra(8);

   }else for(unsigned index=0;index<40;++index){auto& attr=oam[index*4+3];attr=intro?uint8_t(attr|data.intro_attributes[(peek(frame,0xdf08)-1)*256+oam[index*4+2]]):uint8_t((attr&0xe0)|((attr&16)?1:0));}

   // Original1DD1 identifies the actual custom-sprite pose, before drawing.

   // Its authored record retains every native piece and appends one overlay.

   if(intro&&state.fixed_display_arrow[0]&&state.fixed_display_arrow[0]<=20){unsigned index=state.fixed_display_arrow[0]-1;auto extra=data.intro_overlays.data()+index*5;unsigned tile=extra[4]&127,attr=(state.fixed_display_arrow[3]&0xe0)|((extra[4]&128)?32:0)|data.intro_attributes[(peek(frame,0xdf08)-1)*256+tile];draw(int(uint8_t(state.fixed_display_arrow[1]+extra[2]+8))-8,int(uint8_t(state.fixed_display_arrow[2]+extra[3]+16))-16,tile,attr);++sprites;}

   for(int index=39;index>=0;--index){if(ending&&(title==4?index<13:(state.display_valid&&state.display_owned[unsigned(index)])))continue;auto record=oam.data()+index*4;sprites+=record[0]>0&&record[0]<160&&record[1]>0&&record[1]<168;sprite(record);}

   if(ending&&title==4){std::array<std::array<int,4>,14> pieces{};int x=128,y=88;for(unsigned i=0;i<14;++i){auto source=data.ending_actor_map.data()+i*4;y+=int8_t(source[0]);x+=int8_t(source[1]);pieces[i]={x,y,source[2],source[3]};}for(auto it=pieces.rbegin();it!=pieces.rend();++it)draw((*it)[0],(*it)[1],unsigned((*it)[2]),unsigned((*it)[3]));sprites+=14;}

   else if(ending&&state.display_valid)for(int index=int(state.display_count)-1;index>=0;--index){const auto& p=state.display_pieces[unsigned(index)];draw(int(p.x)-int(word(frame,0xd711)),p.y,p.tile_id,uint8_t((p.attr&0xe0)|(p.color&15)));++sprites;}

  }else{

   for(int i=int(data.title_extra_oam.size()/4)-1;i>=0;--i)sprite(data.title_extra_oam.data()+i*4);

   sprite(state.fixed_display_arrow.data());sprites=unsigned(data.title_extra_oam.size()/4)+1;

  }

  ModView view;view.width=160;view.pixels=pixels;view.sprites=sprites;return view;

 }

 uint32_t window_color(const ModContext& c,const std::array<uint16_t,16>& layout,unsigned x,unsigned y,uint8_t lcdc,uint8_t shade,uint32_t fallback)const {

  unsigned at=((lcdc&64)?0x1c00:0x1800)+(y/8)*32+x/8,tile=c.vram[at];

  unsigned address=(lcdc&16)?tile*16:unsigned(0x1000+int(int8_t(tile))*16);const uint8_t* art=c.vram+address;

  unsigned palette=y<16&&x<160?data.hud_attributes[(y/8)*20+x/8]&7:0,pointer=layout[palette];
  // The authored status-bar producer clears with tile0. Original tile3 is
  // its monochrome blank; sampling its white texels made empty HUD cells
  // inherit skin/weapon colors and left white blocks beside the portrait.
  if(y<16&&tile==3)return 0xff000000u;

  if(tile>=0x72&&tile<=0x7f&&std::equal(art,art+16,data.hud_original_art.data()+(tile-0x72)*16))art=data.hud_color_art.data()+(tile-0x72)*16;

  // The DX boot keeps all authored 16x16 equipment icons in VRAM1. The

  // original status strip's two label cells become the selected icon.

  if(y<16&&x>=40&&x<56){unsigned icon=1+unsigned(peek(c,weapon()))*4+(y/8)*2+(x-40)/8;if(icon*16+0x1000+16<=data.menu_boot_vram.size()){art=data.menu_boot_vram.data()+0x1000+icon*16;pointer=layout[1];}}

  else if(tile>=0xe0&&tile<=0xfe)art=data.menu_font.data()+(tile-0xe0)*16;

  unsigned row=(y&7)*2,bit=7-(x&7),ink=((art[row]>>bit)&1)|(((art[row+1]>>bit)&1)<<1);

  // The original counter service copies inverse glyphs into96E0/96F0.
  // Authored III1:66FB and IV1:55C3 invert both planes of those same glyphs;
  // keep the native counter selection while binding its white-on-black art.
  if(y<16&&(tile==0x6e||tile==0x6f))ink^=3;

  return colored(uint16_t(pointer),shade,ink,false,fallback);

 }

 ModView menu_view(const ModContext& c,std::span<const uint32_t> native) {

  // Original ownership, energy and cursor state remain authoritative. The

  // native surface interprets only the authored menu's data tables and boot

  // texture copies; neither the cartridge nor guest VRAM receives DX bytes.

  unsigned count=title==3?13:14,selected=peek(c,title==3?0xd980:0xd1ed);

  if(!color||!state.menu||data.menu_bottom.size()!=count||peek(c,title==3?0xde95:0xdea6)!=0x1b)return {160,false,native};

  std::array<uint8_t,32*18> map{},kind{},selection{};

  std::copy_n(c.vram+0x1c00,64,map.begin());

  unsigned ownership=word(c,weapon()+1);

  auto put=[&](unsigned address,unsigned index,uint8_t tile,uint8_t source){if(address<0x9c00||address>=0x9e40)return;unsigned at=address-0x9c00;map[at]=tile;kind[at]=source;selection[at]=index==selected;};

  for(unsigned i=0;i<count;++i){bool owns=i==0||(i<count-1?(ownership&(1u<<i)):(title==4||peek(c,0xdf05)));

   if(!owns)continue;

   for(unsigned col=0;col<4;++col){unsigned tile=data.menu_labels[i*4+col];put(data.menu_top[i]+col,i,uint8_t(tile),uint8_t(tile<0xe0?1:2));}

   for(unsigned col=0;col<2;++col){unsigned tile=data.menu_lower[i*2+col];put(data.menu_bottom[i]+col,i,uint8_t(tile),uint8_t(tile<0xe0?1:2));}

   if(title==4&&i==count-1)continue;

   unsigned energy=title==3&&i==count-1?peek(c,0xdf05):(unsigned(peek(c,health()+i))+7)/8;

   for(unsigned col=0;col<5;++col){unsigned tile=title==3&&i==count-1?(energy?0xfe:0):data.menu_energy[std::min(energy,4u)];put(data.menu_bottom[i]+2+col,i,uint8_t(tile),2);energy-=std::min(energy,title==3&&i==count-1?1u:4u);}

  }

  uint8_t shade=c.ppu->bgp;unsigned normal=title==3?0x4168:0x4170,highlight=0x4100+std::min(selected,count-2)*8;

  auto background=colored(uint16_t(normal),shade,0,false,0xff000000u);pixels.assign(160*144,background);

  auto stage_palettes=palettes(c);

  for(unsigned y=0;y<144;++y)for(unsigned x=0;x<160;++x){unsigned at=(y/8)*32+x/8,tile=map[at],address=unsigned(0x1000+int(int8_t(tile))*16),source=kind[at];const uint8_t* art=c.vram+address;

   if(y<16){pixels[y*160+x]=window_color(c,stage_palettes,x,y,0xe9,shade,native[y*160+x]);continue;}

   if(source==1)art=data.menu_boot_vram.data()+address;

   else if(source==2){if(tile>=0xe0&&tile<=0xfe)art=data.menu_font.data()+(tile-0xe0)*16;else if(!tile){pixels[y*160+x]=background;continue;}}

   unsigned row=(y&7)*2,bit=7-(x&7),ink=((art[row]>>bit)&1)|(((art[row+1]>>bit)&1)<<1);

   unsigned pointer=selection[at]?highlight:normal;pixels[y*160+x]=colored(uint16_t(pointer),shade,ink,false,native[y*160+x]);

  }

  return {160,false,pixels};

 }

 int left(const ModContext& c)const {

  int room_width=int(peek(c,0xd84c))*16,origin=int16_t(word(c,0xd711)),edge=origin-int(width-160)/2;

  return room_width<int(width)?(room_width-int(width))/2:std::clamp(edge,0,room_width-int(width));

 }

 bool gameplay(const ModContext& c)const {return !credits_scene(c)&&!thanks_scene(c)&&!iv_ending_scene(c)&&native_gameplay(c);}

 bool death_picture(const ModContext& c)const {
  // The original pit/death wait keeps displaying its room after input and
  // actor logic stop. It is a presentation domain, not a gameplay CPU budget.
  return c.wram&&c.ppu&&(c.ppu->lcdc&128)&&!state.menu&&peek(c,0xd702)==3&&
   !peek(c,health())&&
   peek(c,0xd84c)&&peek(c,0xd84d)&&peek(c,0xd84d)<=32;
 }

 bool entry_picture(const ModContext& c)const {
  if(!c.wram||!c.ppu||!(c.ppu->lcdc&128)||state.menu||peek(c,0xde44)||peek(c,0xd702)>1||
     !peek(c,health())||peek(c,health())>0x98||!peek(c,0xd84c)||!peek(c,0xd84d)||peek(c,0xd84d)>32)return false;
  unsigned stage=peek(c,title==3?0xdf08:0xdf0a);
  return !(title==4&&(stage==22||stage==23))&&!gameplay(c)&&!scripted_scene(c);
 }

 bool native_gameplay(const ModContext& c)const {

  auto mode=c.wram?c.wram[0x1e44]:255;unsigned stage=c.wram?c.wram[0x1f0a]:0;
  // Original IV stages22/23 are the rescue and space-escape scripts.
  // Their loaders can retain live-mode flags before installing the script.
  if(title==4&&(stage==22||stage==23))return false;

  // Original1C:51DD enters this live custom-controller mode in the two

  // stage19 Hunter arenas as well as the earlier machine arenas.

  bool custom=title==4&&((mode==13&&(stage==14||stage==18||stage==19))||(mode==12&&stage==21));

  bool world_mode=mode==0||custom||(title==3?mode==8:(mode==7||mode==8));

  return c.wram&&c.ppu&&(c.ppu->lcdc&0x80)&&c.wram[0x1702]==1&&

   c.wram[health()-0xc000]&&c.wram[health()-0xc000]<=0x98&&

   world_mode&&!c.wram[title==3?0x1e92:0x1eaa]&&!state.menu;

 }

 // Original stage16 is the Dr.Light laboratory conversation. It has normal

 // native room/pose producers while DEAA locks the conversation. Admit only

 // its presentation; input, world writeback and CPU budget keep gameplay().

 bool scripted_scene(const ModContext& c)const {return color&&title==4&&c.wram&&c.ppu&&(c.ppu->lcdc&128)&&peek(c,0xdf0a)==16&&peek(c,0xdeaa)==1&&peek(c,0xde44)==0&&peek(c,0xd702)==1&&peek(c,0xd84c)==10&&peek(c,0xd84d)==8&&word(c,0xd850)==0x5e71;}

 bool presentation(const ModContext& c)const {return gameplay(c)||scripted_scene(c)||credits_scene(c)||iv_ending_scene(c);}

 // A physical single-screen room may intentionally stage actors on another page.

 // Preserve its original simulation even when the host view is wider.

 bool wide_world(const ModContext& c)const {return width>160&&gameplay(c)&&peek(c,0xd84c)>10;}

 static uint8_t peek(const ModContext& c,unsigned a,unsigned bank=UINT32_MAX) {

  if(a<0x4000)return a<c.rom_size?c.rom[a]:0;

  if(a<0x8000){size_t p=size_t(bank==UINT32_MAX?c.rom_bank:bank)*0x4000+a-0x4000;return p<c.rom_size?c.rom[p]:0;}

  if(a>=0xc000&&a<0xe000)return c.wram[a-0xc000];

  if(a>=0xff80&&a<0xffff)return c.hram[a-0xff80];

  return 0;

 }

 static unsigned word(const ModContext& c,unsigned a,unsigned bank=UINT32_MAX){return peek(c,a,bank)|(peek(c,a+1,bank)<<8);}

 static int weapon_index(unsigned base){return base>=0xc23b&&base<=0xc29b&&(base-0xc23b)%32==0?int((base-0xc23b)/32):-1;}

 bool tracked_buster(const ModContext& c,unsigned base)const{return weapon_index(base)>=0&&!peek(c,weapon())&&peek(c,base)==7&&(peek(c,base+9)&3)<(title==3?2u:1u);}

 void resource_background(const ModContext& c,std::span<uint16_t,6> cpu){

  if(!color)return;

  if(cpu[5]==(title==3?0x1abf:0x2162)){

   unsigned resource=cpu[0]>>8;

   state.resource_copy={};

   if(resource<data.resource_native_counts.size()&&resource<data.art_counts.size()&&data.art_counts[resource]>data.resource_native_counts[resource]&&cpu[2]>=0x8000&&unsigned(cpu[2])+data.art_counts[resource]*16<=0x9800)

    state.resource_copy={uint8_t(resource),uint8_t(cpu[2]),uint8_t(cpu[2]>>8)};

  }else if(cpu[5]==(title==3?0x1ae4:0x2187)&&state.resource_copy[0]){

   unsigned resource=state.resource_copy[0],destination=unsigned(state.resource_copy[1])|(unsigned(state.resource_copy[2])<<8);

   unsigned prefix=data.resource_native_counts[resource]*16,count=(data.art_counts[resource]-data.resource_native_counts[resource])*16,at=destination-0x8000+prefix,source=data.art_offsets[resource]+prefix;

   if(source+count<=data.art.size()){

    // The original copy has finished. Added authored tiles occupy only the

    // host texture cache; each native write invalidates its shadow byte.

    std::copy_n(c.vram+at,count,state.dx_source.data()+at);

    std::copy_n(data.art.data()+source,count,state.dx_art.data()+at);

    std::fill_n(state.dx_valid.data()+at,count,2);

   }

   state.resource_copy={};

  }

 }

 void copy_background(const ModContext& c,unsigned source,unsigned destination,unsigned count){

  if(!color||destination<0x8000||destination+count>0x9800||!count)return;

  unsigned at=destination-0x8000;std::fill_n(state.dx_valid.data()+at,count,0);

  for(size_t i=0;i<data.background_records.size();i+=4){auto record=data.background_records.data()+i;

   if((record[1]>=0x4000&&record[0]!=c.rom_bank)||source<record[1]||uint64_t(source)+count>record[1]+record[2])continue;

   unsigned offset=record[3]+source-record[1];std::copy_n(data.background_original.data()+offset,count,state.dx_source.data()+at);std::copy_n(data.background_art.data()+offset,count,state.dx_art.data()+at);std::fill_n(state.dx_valid.data()+at,count,1);return;

  }

 }

 const uint8_t* background_texture(const ModContext& c,unsigned at)const{

  if(!color||at+16>0x1800)return c.vram+at;

  if(std::all_of(state.dx_valid.begin()+at,state.dx_valid.begin()+at+16,[](uint8_t v){return v!=0;})&&std::equal(c.vram+at,c.vram+at+16,state.dx_source.begin()+at))return state.dx_art.data()+at;

  if(credits_scene(c)&&state.credits.initialized&&data.ending_art.size()==8192&&((at>=0x420&&at<0x440)||(at>=0xfd0&&at<0x1010))&&std::all_of(c.vram+at,c.vram+at+16,[](uint8_t v){return !v;}))return data.ending_art.data()+at;

  if(at>=0x1000&&at<0x1800){unsigned key=peek(c,title==3?0xdf08:0xdf0a)*256+(at-0x1000)/16;

   auto found=std::lower_bound(data.background_extra_keys.begin(),data.background_extra_keys.end(),key);

   if(key&&found!=data.background_extra_keys.end()&&*found==key&&std::all_of(c.vram+at,c.vram+at+16,[](uint8_t v){return !v;}))return data.background_extra_art.data()+size_t(found-data.background_extra_keys.begin())*16;

  }

  return c.vram+at;

 }

 void credits_palette(unsigned set) {

  if(data.credits_palettes.size()!=80||set>=5)return;

  for(unsigned n=0;n<16;++n){auto p=data.credits_palettes[set*16+n];if(p)state.credits.palettes[n]=p;}

 }

 void credits_instruction(const ModContext& c,std::span<uint16_t,6> cpu) {

  if(!credits_scene(c)||data.credits_palettes.size()!=80)return;

  auto& cache=state.credits;unsigned pc=cpu[5],a=cpu[0]>>8;

  if(c.rom_bank==6&&pc==0x7826){cache={};cache.initialized=1;credits_palette(0);}

  if(!cache.initialized)return;

  if(pc==0x020c&&a<data.resources.size()/16)for(unsigned n=0;n<16;++n){auto value=data.resources[a*16+n];if(value)cache.palettes[n]=value;}

  if(c.rom_bank==6){

   if(pc==0x7b20&&a==0x30)credits_palette(1);

   if(pc==0x7bdd)credits_palette(2);

   if(pc==0x7bc3){unsigned fade=peek(c,0xcc8e);if(fade==8||fade==4)credits_palette(fade==8?3:4);}

   if(pc==0x7b33){cache.palettes[7]=0x4588;cache.shade_valid=1;cache.shade=255;}

   if(pc==0x7a85||pc==0x7b06||pc==0x7d30)cache.shade_valid=0;

  }

  if(pc!=0x178b)return;

  unsigned source=cpu[3],destination=cpu[2],count=cpu[1];

  if(destination<0x9800||uint64_t(destination)+count>0xa000)return;

  unsigned offset=destination-0x9800;std::fill_n(cache.map_valid.begin()+offset,count,0);

  if(c.rom_bank==6&&source==0x9c00&&destination==0x9800&&count==1024&&word(c,cpu[4])==0x7994){

   // Authored11:4372 clears fourteen visible cells in each of eighteen rows.

   // Keep the original copy and conditionally replace its actual result.

   for(unsigned y=0;y<18;++y)for(unsigned x=0;x<14;++x){unsigned at=y*32+x;cache.map_source[at]=c.vram[0x1c00+at];cache.map_art[at]=0;cache.map_valid[at]=1;cache.attrs[at]=0;}

  }

  if(c.rom_bank!=6)return;

  for(size_t n=0;n<data.credits_map_sources.size();n+=3){auto r=data.credits_map_sources.data()+n;

   if(source<r[0]||uint64_t(source)+count>unsigned(r[0])+r[1])continue;

   unsigned at=r[2]+source-r[0];

   if(at+count<=data.credits_map_attributes.size()&&at+count<=data.credits_map_original.size()&&at+count<=data.credits_map_art.size()){

    std::copy_n(data.credits_map_attributes.data()+at,count,cache.attrs.data()+offset);

    std::copy_n(data.credits_map_original.data()+at,count,cache.map_source.data()+offset);

    std::copy_n(data.credits_map_art.data()+at,count,cache.map_art.data()+offset);

    std::fill_n(cache.map_valid.data()+offset,count,1);

   }

   break;

  }

 }

 ModView credits_view(const ModContext& c,std::span<const uint32_t> native) {

  auto frame=c;frame.wram=state.display_wram.data();frame.vram=state.vram.data();auto& cache=state.display_credits;

  pixels.resize(160*144);opaque.assign(pixels.size(),0);

  std::array<std::array<std::array<uint32_t,4>,16>,256> colors{};std::array<bool,256> ready{};

  auto palette=[&](uint8_t shade)->const std::array<std::array<uint32_t,4>,16>&{if(cache.shade_valid)shade=cache.shade;if(!ready[shade]){ready[shade]=true;for(unsigned slot=0;slot<16;++slot)for(unsigned ink=0;ink<4;++ink)colors[shade][slot][ink]=colored(cache.palettes[slot],shade,ink,slot>=8,0);}return colors[shade];};

  for(unsigned y=0;y<144;++y)for(unsigned x=0;x<160;++x){const auto& r=state.display_raster[y];unsigned sx=(x+r.scx)&255,sy=(y+r.scy)&255,map=(r.lcdc&8)?0x400:0,index=map+(sy/8)*32+sx/8;

   unsigned tile=state.display_bg_maps[y][sx/8];if(cache.map_valid[index]&&frame.vram[0x1800+index]==cache.map_source[index])tile=cache.map_art[index];unsigned bit=7-(sx&7);

   const auto& planes=state.display_bg_rows[y];unsigned ink=((planes[tile*2]>>bit)&1)|(((planes[tile*2+1]>>bit)&1)<<1),at=y*160+x,attr=cache.attrs[index];opaque[at]=uint8_t(ink?((attr&128)?2:1):0);

   auto rgb=palette(r.bgp)[attr&7][ink];pixels[at]=rgb?rgb:c.ppu->bg_rgb[(r.bgp>>(ink*2))&3];

  }

  auto draw=[&](int x,int y,uint8_t attr,const uint8_t* tile,unsigned color){for(int yy=0;yy<8;++yy)for(int xx=0;xx<8;++xx){int px=x+xx,py=y+yy;if(px<0||px>=160||py<0||py>=144)continue;unsigned row=unsigned((attr&64)?7-yy:yy)*2,bit=unsigned((attr&32)?xx:7-xx),ink=((tile[row]>>bit)&1)|(((tile[row+1]>>bit)&1)<<1);if(!ink)continue;unsigned at=unsigned(py)*160+unsigned(px);if(opaque[at]==2||((attr&128)&&opaque[at]))continue;const auto& r=state.display_raster[unsigned(py)];unsigned pal=(attr&16)?r.obp1:r.obp0;auto rgb=palette(r.bgp)[8+(color&7)][ink];pixels[at]=rgb?rgb:c.ppu->obj_rgb[((attr&16)?4:0)+((pal>>(ink*2))&3)];}};

  unsigned sprites=0;

  for(int n=39;n>=0;--n)if(!state.display_owned[unsigned(n)]){auto p=state.display_wram.data()+n*4;if(!p[0]||p[0]>=160||!p[1]||p[1]>=168)continue;draw(int(p[1])-8,int(p[0])-16,p[3],background_texture(frame,unsigned(p[2])*16),(p[3]&16)?1:0);++sprites;}

  for(int n=int(state.display_count)-1;n>=0;--n){const auto& p=state.display_pieces[unsigned(n)];draw(int(p.x)-int(word(frame,0xd711)),p.y,p.attr,p.tile.data(),p.color);++sprites;}

  ModView view;view.width=160;view.pixels=pixels;view.sprites=sprites;view.uncapped_sprites=state.display_overflow;return view;

 }

 bool iv_ending_scene(const ModContext& c)const {return color&&state.iv_credits.initialized&&state.iv_credits.kind&&!state.menu&&!native_gameplay(c)&&c.wram&&c.ppu&&(c.ppu->lcdc&128);}

 void iv_style_clear(){

  auto& cache=state.iv_credits;cache.initialized=cache.kind=cache.fade_override=cache.fade_valid=0;cache.pending_style=0;cache.overlay=cache.map_variant=cache.object_fade_override=cache.object_fade_valid=0;cache.palettes={};cache.attrs.fill(255);

 }

 void iv_style_init(const ModContext& c){

  auto& cache=state.iv_credits;auto layout=palettes(c);cache.initialized=1;cache.kind=1;cache.attrs.fill(255);cache.palettes=layout;

  if(!cache.shade_valid&&data.menu_boot_vram.size()>=cache.vram1.size()){std::copy_n(data.menu_boot_vram.begin(),cache.vram1.size(),cache.vram1.begin());cache.shade_valid=1;}

 }

 void iv_style_apply(const ModContext& c,unsigned style){

  auto found=std::lower_bound(data.iv_style_addresses.begin(),data.iv_style_addresses.end(),style);if(found==data.iv_style_addresses.end()||*found!=style)return;

  if(!state.iv_credits.initialized)iv_style_init(c);auto& cache=state.iv_credits;cache.kind=native_gameplay(c)?0:2;cache.overlay=0;cache.map_variant=uint8_t(title==3?(style==0x45ca?1:style==0x49a4?2:0):0);unsigned index=unsigned(found-data.iv_style_addresses.begin());

  for(unsigned n=0;n<16;++n){auto value=data.iv_style_palettes[index*16+n];if(value!=65535)cache.palettes[n]=value;}

  for(unsigned n=0;n<2048;++n){auto value=data.iv_style_attributes[index*2048+n];if(value!=255)cache.attrs[n]=value;}

  for(size_t n=0;n<data.iv_style_texture_records.size();n+=4){auto r=data.iv_style_texture_records.data()+n;if(r[0]==index)std::copy_n(data.iv_style_textures.data()+r[3],r[2],cache.vram1.data()+r[1]);}

  // The only callback in these six III styles is27:4120: four source BG

  // pointers selected from27:4F60 by the original weapon-award stage.

  if(title==3&&style==0x4066){unsigned stage=peek(c,0xdf08);if(stage<data.story_award_palettes.size()/4)std::copy_n(data.story_award_palettes.data()+stage*4,4,cache.palettes.begin()+1);}

 }

 void story_instruction(const ModContext& c,std::span<uint16_t,6> cpu){

  if(!color||title!=3||data.story_map_styles.size()!=16)return;

  auto& cache=state.iv_credits;

  if(cpu[5]==0x08e8){iv_style_clear();return;}

  if(cpu[5]==0x1cee){

   unsigned key=c.rom_bank<<16|word(c,cpu[4]),resource=cpu[0]>>8;

   bool wrapped=key==0x0b4060||key==0x0b42bd||key==0x0b454e||key==0x0b488c||key==0x0f40af||key==0x0f40b4||key==0x0f4176||key==0x0f417b||key==0x0f48ed;

   cache.pending_style=uint16_t(wrapped&&resource<data.story_map_styles.size()?data.story_map_styles[resource]:0);

  }else if(cpu[5]==0x1d48&&cache.pending_style){unsigned style=cache.pending_style;cache.pending_style=0;iv_style_apply(c,style);}
  // The source0F:7FDD/7FE6 wrappers call11:40E4 after the unchanged native
  // scroll/store or OAM clear. Its seven bank1 pieces supply the award face.
  if(c.rom_bank==15&&(cpu[5]==0x4a52||cpu[5]==0x4a81)&&cache.initialized&&data.story_overlay_oam.size()==28)cache.overlay=2;

 }

 void iv_managed_instruction(const ModContext& c,std::span<uint16_t,6> cpu){

  if(!color||title!=4||data.iv_style_addresses.empty())return;

  unsigned style=0,key=c.rom_bank<<16|cpu[5];

  // Each boundary follows the unchanged original call named by its authored

  // wrapper. Pure attribute-only tail calls bind before the original JP.

  switch(key){

   case 0x0b41c3:style=0x4ef2;break;case 0x0b466e:style=0x512a;break;

   case 0x0b4dd0:style=0x5335;break;case 0x0b565c:style=0x5d39;break;

   case 0x0f4079:style=0x4986;break;case 0x0f56dd:style=0x4332;break;

   case 0x104c80:style=0x40a2;break;case 0x104e0e:style=0x4083;break;

   case 0x10797b:style=0x54bd;break;case 0x107a97:style=0x5658;break;case 0x107b75:style=0x5981;break;

   case 0x124970:style=0x5ae3;break;case 0x1258fe:case 0x125909:case 0x1259ce:style=0x5b69;break;

   case 0x125e24:case 0x125e2f:style=0x5bab;break;case 0x124b77:style=0x5bed;break;

   case 0x124b62:style=(cpu[3]&255)==0x5a?0x5cb5:0x5cd3;break;case 0x124d30:style=0x5cf1;break;

   case 0x14454b:iv_style_apply(c,0x4751);style=0x47b4;break;case 0x1446d9:style=0x4751;break;

   case 0x1463f8:style=0x4935;break;case 0x14615e:style=0x47bb;break;

   case 0x1540ba:style=0x4d31;break;case 0x1979bb:style=peek(c,0xd17f)?0x4591:0x43f6;break;

   case 0x1b49a7:style=0x4a66;break;case 0x1b4c05:style=0x4bad;break;

   case 0x1b4a8d:style=0x4df0;break;case 0x1b4de4:style=0x4e64;break;

   case 0x1d6f21:style=0x4f0d;break;case 0x1e5a2d:style=0x5e1d;break;case 0x1f4e87:style=0x5123;break;

  }

  if(style)iv_style_apply(c,style);
  if(key==0x0f56dd&&data.iv_award_object_palettes.size()==20){
   // Authored fixed3F46 additionally calls20:615F: OBJ4..7 are constant;
   // original stage1..8 selects the two OBJ2/3 pointers, defaulting to stage1.
   unsigned stage=peek(c,0xdf0a),index=stage>=1&&stage<=8?stage-1:0;
   std::copy_n(data.iv_award_object_palettes.begin()+16,4,state.iv_credits.palettes.begin()+12);
   std::copy_n(data.iv_award_object_palettes.begin()+index*2,2,state.iv_credits.palettes.begin()+10);
  }
  // Original0F:4997 calls0294. Its authored wrapper appends the exact
  //20:61E6 eye/skin tile in OAM slot10, using the original blink state.
  if(key==0x0f499a&&state.iv_credits.initialized)state.iv_credits.overlay=1;

 }

 void iv_ending_instruction(const ModContext& c,std::span<uint16_t,6> cpu) {

  if(!color||title!=4||data.iv_cast_palettes.size()!=8)return;

  auto& cache=state.iv_credits;unsigned pc=cpu[5],a=cpu[0]>>8,stage=peek(c,0xdf0a);

  // Charging may initialize this palette shadow before a later actor loads.
  // Follow each original resource registration, so its authored OBJ colors
  // replace the corresponding slots without undoing intervening style edits.
  if(pc==0x21c4&&cache.initialized&&a<data.resources.size()/16)
   for(unsigned n=0;n<16;++n)if(auto value=data.resources[a*16+n])cache.palettes[n]=value;

  if(c.rom_bank==0x14&&pc==0x5590){if(cache.initialized)cache.kind=1;else iv_style_init(c);}

  if(c.rom_bank==0x14&&pc==0x6b1d){iv_style_clear();return;}

  if(pc==0x0a01&&stage!=13&&stage!=22){iv_style_clear();return;}

  if(pc==0x3370&&(cache.initialized||stage==22)){

   iv_style_init(c);

   if(stage<data.layouts.size()/16)std::copy_n(data.layouts.data()+stage*16,16,cache.palettes.begin());

   for(unsigned n=0;n<6;++n)cache.palettes[10+n]=data.iv_cast_palettes[2+n];

   // Authored32:79F8 explicitly preserves both source pointers for stage22.

   if(stage!=22){unsigned selected=peek(c,0xdeab);if(selected<13){cache.palettes[1]=uint16_t((cache.palettes[1]&0xff00)+selected*8);cache.palettes[9]=uint16_t((cache.palettes[9]&0xff00)+selected*8);}}

  }

  // Finite source palette helpers:6:7D43 selects the charged OBJ1 low
  // byte;1C:6ABB restores only OBJ palettes before native531E.
  if(c.rom_bank==6&&pc==0x5a41&&data.iv_charge_palette_lows.size()==8){
   if(!cache.initialized){iv_style_init(c);cache.kind=native_gameplay(c)?0:1;}
   unsigned index=(peek(c,0xd75e)>=0x5a?4:0)+(cpu[2]&3);
   cache.palettes[9]=uint16_t(0x4a00+data.iv_charge_palette_lows[index]);
  }
  if(c.rom_bank==0x1c&&pc==0x531e){
   if(!cache.initialized){iv_style_init(c);cache.kind=native_gameplay(c)?0:1;}
   cache.object_fade_override=0x1b;cache.object_fade_valid=1;
  }
  //6:7D53 intentionally leaves the capsule's existing fade untouched.
  if(c.rom_bank==6&&pc==0x4adf&&stage==14){
   if(!cache.initialized){iv_style_init(c);cache.kind=native_gameplay(c)?0:1;}
   if(!cache.fade_valid)cache.fade_override=uint8_t(peek(c,0xdea6));cache.fade_valid=1;
  }
  if(c.rom_bank==6&&pc==0x4482&&cache.initialized&&stage!=22){
   unsigned selected=peek(c,0xdeab);if(selected<13){cache.palettes[1]=uint16_t(0x4100+selected*8);cache.palettes[9]=uint16_t(0x4a00+selected*8);}
  }

  if(!cache.initialized)return;

  if(pc==0x0207||pc==0x020a){if(a<data.resources.size()/16)for(unsigned n=0;n<16;++n){auto value=data.resources[a*16+n];if(value)cache.palettes[n]=value;}}

  if(c.rom_bank==0x12&&pc==0x7233){

   // Authored12:7FCC selects its color fade before the unchanged CA13 store.

   // Keep guest palette/audio/timing state original; only the host palette

   // uploader sees the authored shadow until the next native DEA6 write.

   cache.fade_override=uint8_t(a==6||a==0x1a?0x5b:a==1||a==0x15?0xab:!a?4:a);cache.fade_valid=1;

  }

  if(c.rom_bank==0x12&&pc==0x71b3)for(unsigned y=0;y<5;++y)std::copy_n(data.iv_portrait_attributes.data()+y*6,6,cache.attrs.begin()+0x200+y*32);

  if(c.rom_bank==0x14&&pc==0x56b1){cache.palettes[0]=data.iv_cast_palettes[0];cache.palettes[1]=data.iv_cast_palettes[1];std::fill_n(cache.attrs.begin()+0x400,3*32,0);}

 }

 ModView iv_ending_view(const ModContext& c,std::span<const uint32_t> native) {

  auto frame=c;frame.wram=state.display_wram.data();frame.vram=state.vram.data();const auto& cache=state.iv_display_credits;

  pixels.resize(160*144);opaque.assign(pixels.size(),0);unsigned stage=peek(frame,title==3?0xdf08:0xdf0a);
  bool award_map=false;
  if(title==4&&data.iv_award_map_original.size()==380&&data.iv_award_map_art.size()==380)for(size_t n=0;n<data.background_records.size();n+=4){auto r=data.background_records.data()+n;
   if(r[0]==15&&r[1]==0x40c5&&r[2]==0x6d0){award_map=std::equal(frame.vram+0x1000,frame.vram+0x16d0,data.background_original.data()+r[3]);break;}}


  std::array<std::array<uint32_t,4>,16> colors{};for(unsigned slot=0;slot<16;++slot)for(unsigned ink=0;ink<4;++ink)colors[slot][ink]=colored(cache.palettes[slot],slot>=8&&cache.object_fade_valid?cache.object_fade_override:cache.shade,ink,slot>=8,0);

  for(unsigned y=0;y<144;++y)for(unsigned x=0;x<160;++x){const auto& r=state.display_raster[y];

   bool window=(r.lcdc&32)&&y>=r.wy&&r.wx<=166&&int(x)>=int(r.wx)-7;

   unsigned sx=window?unsigned(int(x)-int(r.wx)+7):(x+r.scx)&255,sy=window?y-r.wy:(y+r.scy)&255;

   unsigned map=(r.lcdc&(window?64:8))?0x400:0,index=map+(sy/8)*32+sx/8;

   unsigned tile=window?frame.vram[0x1800+index]:state.display_bg_maps[y][sx/8],attr=cache.attrs[index];
   if(title==3&&cache.map_variant){auto r=data.story_map_records.data()+unsigned(cache.map_variant-1)*4;
    if(index>=r[0]){unsigned relative=index-r[0],row=relative/32,col=relative%32;if(row<r[2]&&col<r[1]){unsigned cell=r[3]+row*r[1]+col;if(tile==data.story_map_original[cell])tile=data.story_map_art[cell];}}}
   if(award_map&&!map&&sy/8<19&&sx/8<20){unsigned cell=(sy/8)*20+sx/8;if(tile==data.iv_award_map_original[cell])tile=data.iv_award_map_art[cell];}

   if(attr==255)attr=stage<data.attributes.size()/256?data.attributes[stage*256+tile]:0;

   unsigned bit=(attr&32)?sx&7:7-(sx&7),ink=0;

   if(window||(attr&0x68)){unsigned address=(r.lcdc&16)?tile*16:unsigned(0x1000+int(int8_t(tile))*16);const auto* texture=(attr&8)?cache.vram1.data()+address:background_texture(frame,address);unsigned row=((attr&64)?7-(sy&7):sy&7)*2;ink=((texture[row]>>bit)&1)|(((texture[row+1]>>bit)&1)<<1);}

   else{const auto& planes=state.display_bg_rows[y];ink=((planes[tile*2]>>bit)&1)|(((planes[tile*2+1]>>bit)&1)<<1);}

   unsigned at=y*160+x;opaque[at]=uint8_t(ink?((attr&128)?2:1):0);auto rgb=colors[attr&7][ink];pixels[at]=rgb?rgb:native[at];

  }

  auto draw=[&](int x,int y,uint8_t attr,const uint8_t* tile,unsigned color){for(int yy=0;yy<8;++yy)for(int xx=0;xx<8;++xx){int px=x+xx,py=y+yy;if(px<0||px>=160||py<0||py>=144)continue;unsigned row=unsigned((attr&64)?7-yy:yy)*2,bit=unsigned((attr&32)?xx:7-xx),ink=((tile[row]>>bit)&1)|(((tile[row+1]>>bit)&1)<<1);if(!ink)continue;unsigned at=unsigned(py)*160+unsigned(px);if(opaque[at]==2||((attr&128)&&opaque[at]))continue;const auto& r=state.display_raster[unsigned(py)];unsigned shade=(attr&16)?r.obp1:r.obp0;auto rgb=colors[8+(color&7)][ink];pixels[at]=rgb?rgb:c.ppu->obj_rgb[((attr&16)?4:0)+((shade>>(ink*2))&3)];}};

  unsigned sprites=0;

  for(int n=39;n>=0;--n){
   if(cache.overlay==1&&n==10){unsigned tile=0x90+(peek(frame,0xd0a6)&1);draw(0x55-8,int(uint8_t(peek(frame,0xd792)+0x48))-16,8,cache.vram1.data()+tile*16,0);++sprites;continue;}
   if(cache.overlay==2&&n>=8&&n<15){auto piece=data.story_overlay_oam.data()+unsigned(n-8)*4;unsigned tile=piece[2],attr=piece[3];
    draw(int(uint8_t(piece[1]+(peek(frame,0xd791)^255)))-8,int(piece[0])-16,uint8_t(attr),cache.vram1.data()+tile*16,attr&7);++sprites;continue;}
   if(!state.display_owned[unsigned(n)]){auto p=state.display_wram.data()+n*4;if(!p[0]||p[0]>=160||!p[1]||p[1]>=168)continue;draw(int(p[1])-8,int(p[0])-16,p[3],background_texture(frame,unsigned(p[2])*16),(p[3]&16)?1:0);++sprites;}
  }

  for(int reserved=0;reserved<2;++reserved)for(int n=int(state.display_count)-1;n>=0;--n){const auto& p=state.display_pieces[unsigned(n)];if((p.reserved&1)!=reserved)continue;draw(int(p.x)-int(word(frame,0xd711)),p.y,p.attr,p.tile.data(),p.color);++sprites;}

  ModView view;view.width=160;view.pixels=pixels;view.sprites=sprites;view.uncapped_sprites=state.display_overflow;return view;

 }



 struct Pose {std::span<const uint8_t> bytes{};unsigned address=0;};

 Pose pose(unsigned bank,unsigned address,unsigned kind=0)const {

  if(!color)return {};uint32_t key=kind<<24|bank<<16|address;auto found=std::lower_bound(data.pose_keys.begin(),data.pose_keys.end(),key);

  if(found==data.pose_keys.end()||*found!=key)return {};unsigned index=unsigned(found-data.pose_keys.begin()),begin=data.pose_offsets[index],end=index+1<data.pose_offsets.size()?data.pose_offsets[index+1]:unsigned(data.pose_records.size());

  return {data.pose_records.subspan(begin,end-begin),data.pose_addresses[index]};

 }

 bool texture(const ModContext& c,unsigned base,unsigned tile,std::array<uint8_t,16>& pixels)const {

  if(!color)return false;

  for(unsigned i=0;i<12;++i){unsigned at=(title==3?0xc3c5:0xc3c6)+i*2,resource=peek(c,at);if(!resource)break;

   if(peek(c,at+1)!=base||resource>=data.art_counts.size()||tile>=data.art_counts[resource])continue;

   unsigned offset=data.art_offsets[resource]+tile*16;if(offset+16>data.art.size())return false;std::copy_n(data.art.data()+offset,16,pixels.begin());return true;}

  return false;

 }

 unsigned own(const ModContext& c,unsigned logical,unsigned count) {

  unsigned handle=peek(c,0xd000+peek(c,0xd100+logical)+1),first=handle;

  unsigned allocated=0;for(;allocated<count&&handle<254;++allocated){unsigned slot=peek(c,0xcf00+handle)/4;if(slot<40)state.build_owned[slot]=1;handle=peek(c,0xce00+handle);}

  state.build_overflow=uint16_t(std::min(1024u,unsigned(state.build_overflow)+count-allocated));return first;

 }

 unsigned take_slot(const ModContext& c,unsigned& handle)const {

  unsigned slot=40;if(handle<254){slot=peek(c,0xcf00+handle)/4;handle=peek(c,0xce00+handle);}return std::min(slot,40u);

 }

 void capture(const ModContext& c,std::span<uint16_t,6> cpu) {

  if(!state.building||(!presentation(c)&&!thanks_scene(c))||cpu[0]>>8>=0x40)return;

  unsigned source=cpu[3],original_source=source,origin_x=title==3?0xffa2:0xff9b,origin_y=origin_x+1;

  auto authored=pose(c.rom_bank,source);unsigned cursor=0;

  auto next=[&](){return authored.bytes.empty()?peek(c,original_source+cursor++):(cursor<authored.bytes.size()?authored.bytes[cursor++]:uint8_t(128));};

  auto address=[&](){return (authored.bytes.empty()?original_source:authored.address)+cursor;};

  int raw_x=peek(c,origin_x),ox=raw_x;if(ox>=208)ox-=256;

  int oy=peek(c,origin_y);if(oy>=208)oy-=256;

  int dx=int8_t(next()),dy=int8_t(next());unsigned shape=next(),cols=shape>>4,rows=shape&15;

  uint8_t global=title==3?peek(c,0xd3ea):peek(c,0xffab),base=title==3?peek(c,0xce55):peek(c,0xffac);

  bool mirror=title==4&&(peek(c,0xd3ea)&32),large=peek(c,title==3?0xce57:0xce56)&1;

  int world_origin=int(word(c,0xd711))+ox;unsigned actor=cpu[1];

  if(actor>=0xc200&&actor<=0xd4c0&&peek(c,actor+0x0d)==raw_x&&peek(c,actor+0x10)==cpu[0]>>8)world_origin=int(word(c,actor+0x0a));

  bool hidden=color&&title==4&&actor==state.hurt_actor&&(state.hurt_timer&2);

  int x=ox+(mirror?-dx-8:dx),y=oy+dy;

  unsigned native_count=0,native_shape=peek(c,original_source+2),native_at=original_source+3;

  for(unsigned i=0;i<(native_shape>>4)*(native_shape&15);++i)native_count+=peek(c,native_at++)!=255;
  unsigned grid_count=native_count;bool terminated=false;
  for(unsigned i=0;i<128;++i){if(peek(c,native_at++)==128){terminated=true;break;}++native_at;native_count+=peek(c,native_at++)!=255;}
  // Unterminated fixed-allocation poses rely on their owned native group to
  // stop reading. Preserve that bound instead of interpreting following ROM
  // code as more pieces; complete terminated logical lists remain uncapped.
  unsigned extra_limit=128;
  if(!terminated){
   unsigned current=peek(c,0xd000+peek(c,0xd100+(cpu[0]>>8))+1),allocated=0;
   for(;allocated<40&&current<254;++allocated)current=peek(c,0xce00+current);
   native_count=std::max(grid_count,allocated);extra_limit=allocated>grid_count?allocated-grid_count:0;
  }
  unsigned handle=own(c,cpu[0]>>8,native_count);

  auto emit=[&](int px,int py,uint8_t code,unsigned source_address) {

   if(code==255||hidden||state.build_count>=state.build.size())return;

   auto& p=state.build[state.build_count++];p={};p.reserved=uint8_t(take_slot(c,handle)<<2);p.x=int16_t(world_origin+px-ox);p.y=int16_t(py);

   p.color=piece_color(c.rom_bank,source_address);

   p.attr=uint8_t(global|(color?p.color:0));bool fx=code&128;if(mirror)fx=!fx;

   if(color&&title==3&&!(p.attr&16)){--state.build_count;return;}

   if(fx)p.attr|=32;

   if(!large&&(code&64))p.attr|=64;

   unsigned tile=uint8_t(base+(code&(large?127:63)));

   p.tile_id=uint8_t(tile);

   if(color&&(p.color&8)){std::copy_n((state.iv_credits.initialized?state.iv_credits.vram1.data():data.menu_boot_vram.data())+tile*16,16,p.tile.begin());p.reserved|=2;}

   else{std::copy_n(c.vram+tile*16,16,p.tile.begin());if(texture(c,base,code&(large?127:63),p.tile))p.reserved|=2;}

   ++state.captures;

  };

  for(unsigned row=0;row<rows;++row)for(unsigned col=0;col<cols;++col){unsigned at=address();emit(x+(mirror?-1:1)*int(col*8),y+int(row*8),next(),at);}

  bool bounded=authored.bytes.empty()&&!terminated;
  for(unsigned i=0;i<128&&(!bounded||extra_limit);++i){uint8_t sx=next();if(sx==128)break;int sy=int8_t(next());unsigned at=address();uint8_t tile=next();emit(x+(mirror?-int(int8_t(sx)):int(int8_t(sx))),y+sy,tile,at);if(bounded&&tile!=255)--extra_limit;}

 }

 void list(const ModContext& c,std::span<uint16_t,6> cpu) {

  if(title!=4||!state.building||!presentation(c)||cpu[0]>>8>=0x40)return;

  unsigned source=cpu[1],actor=word(c,cpu[4]+2);int raw_x=peek(c,0xff9b),x=raw_x,y=peek(c,0xff9c);if(x>=208)x-=256;if(y>=208)y-=256;

  int origin=int(word(c,0xd711))+x;

  bool context=word(c,cpu[4])==0x2b4d&&actor>=0xc200&&actor<=0xd4c0&&!(actor&63)&&peek(c,actor+0x0d)==raw_x&&peek(c,actor+0x10)==cpu[0]>>8;

  if(context)origin=int(word(c,actor+0x0a));

  bool hidden=color&&context&&actor==state.hurt_actor&&(state.hurt_timer&2);unsigned base=peek(c,0xffac),global=peek(c,0xffab),native_count=0;

  for(unsigned i=0;i<128&&peek(c,source+i*4)!=128;++i)++native_count;unsigned handle=own(c,cpu[0]>>8,native_count);

  auto authored=pose(c.rom_bank,source,1);unsigned cursor=0;

  auto next=[&](){return authored.bytes.empty()?peek(c,source+cursor++):(cursor<authored.bytes.size()?authored.bytes[cursor++]:uint8_t(128));};

  x=origin;

  for(unsigned i=0;i<128;++i){uint8_t dy=next();if(dy==128)break;y+=int8_t(dy);x+=int8_t(next());unsigned tile=next(),attribute=next();if(hidden||state.build_count>=state.build.size())continue;

   auto& p=state.build[state.build_count++];p={};p.reserved=uint8_t(take_slot(c,handle)<<2);p.x=int16_t(x);p.y=int16_t(y);p.attr=uint8_t(attribute^global);p.color=color?uint8_t(attribute&7):0;p.tile_id=uint8_t(base+tile);

   if(color&&(p.attr&8)){std::copy_n((state.iv_credits.initialized?state.iv_credits.vram1.data():data.menu_boot_vram.data())+unsigned(p.tile_id)*16,16,p.tile.begin());p.reserved|=2;}

   else{std::copy_n(c.vram+p.tile_id*16,16,p.tile.begin());if(texture(c,base,tile,p.tile))p.reserved|=2;}++state.captures;

  }

 }

 void body(const ModContext& c) {

  if(!state.building||!presentation(c))return;

  unsigned pose=peek(c,0xd70a),index=title==3?pose+peek(c,0xd703):pose/2;

  if(index>=(title==3?52u:29u)||(title==4&&(pose&1)))return;

  unsigned address=color?data.body_addresses[index]:word(c,(title==3?0x7783:0x6f83)+index*2,1);

  unsigned record=color?data.body_offsets[index]:0,at=0;

  auto next=[&](){return color?(record+at<data.body_records.size()?data.body_records[record+at++]:uint8_t(128)):peek(c,address+at++,1);};

  bool mirror=title==4&&peek(c,0xd703)&&(pose<0x10||pose>=0x14);

  int dx=int8_t(next()),dy=int8_t(next());unsigned shape=next(),cols=shape>>4,rows=shape&15;

  int x=int(word(c,0xd70d))+(mirror?-dx-7:dx),y=int(peek(c,title==3?0xd724:0xd726))+2+dy;

  uint8_t original_attr=peek(c,0xd70b);unsigned slot=0;

  auto emit=[&](int px,int py,uint8_t code,unsigned source_address){

   if(code==255||state.build_count>=state.build.size())return;

   auto& p=state.build[state.build_count++];p={};p.x=int16_t(px);p.y=int16_t(py);p.reserved=uint8_t(1|(std::min(slot++,40u)<<2));p.color=piece_color(1,source_address);

   p.attr=uint8_t(original_attr|(p.color&0xe0));bool fx=code&128;if(mirror)fx=!fx;if(fx)p.attr|=32;

   unsigned tile=code&127;p.tile_id=uint8_t(tile);const uint8_t* texture=c.vram+tile*16;

   // CGB attribute bit3 selects the immutable OBJ atlas loaded by the DX

   // boot. Keep that source texture through the completed-frame VRAM latch.

   if(color&&(p.color&8)){texture=(state.iv_credits.initialized?state.iv_credits.vram1.data():data.menu_boot_vram.data())+tile*16;p.reserved|=2;}

   else if(color&&(tile+1)*16<=data.body_original_art.size()&&std::equal(texture,texture+16,data.body_original_art.data()+tile*16))texture=data.body_color_art.data()+tile*16;

   std::copy_n(texture,16,p.tile.begin());++state.captures;

  };

  for(unsigned row=0;row<rows;++row)for(unsigned col=0;col<cols;++col){unsigned source=address+at;uint8_t tile=next();emit(x+(mirror?-1:1)*int(col*8),y+int(row*8),tile,source);}

  for(unsigned i=0;i<32;++i){uint8_t dx2=next();if(dx2==128)break;int dy2=int8_t(next());unsigned source=address+at;uint8_t tile=next();emit(x+(mirror?-int(int8_t(dx2)):int(int8_t(dx2))),y+dy2,tile,source);}

  std::fill_n(state.build_owned.begin(),10,1);

 }

 void publish(const ModContext& c) {

  if(!presentation(c)&&!thanks_scene(c)){state.valid=state.building=0;state.build_count=0;state.build_owned={};return;}

  if(state.building){state.count=state.build_count;state.overflow=state.build_overflow;state.pieces=state.build;state.owned=state.build_owned;state.valid=1;

   std::copy_n(c.wram,8192,state.wram.begin());}

  state.building=1;state.build_count=state.build_overflow=0;state.build={};state.build_owned={};

 }

 void select_dma() {

  if(!state.valid)return;state.active_valid=1;state.native_wait_flags&=2;

  state.active_count=state.count;state.active_overflow=state.overflow;state.active_pieces=state.pieces;state.active_owned=state.owned;state.active_wram=state.wram;

 }

 void capture_native_wait(const ModContext& c){
   std::copy_n(c.wram,8192,state.active_wram.begin());std::copy_n(c.oam,160,state.active_wram.begin());
   std::copy_n(c.vram,8192,state.active_vram.begin());
   // These native waits use their current OAM for death pieces or the retry
   // beam. Discard the previous pose rather than retaining a ghost player.
   auto previous=state.active_pieces;unsigned previous_count=state.active_count;
   state.active_count=state.active_overflow=0;state.active_owned={};state.active_valid=1;
   int origin=int16_t(word(c,0xd70f))-80;
   for(unsigned slot=0;slot<40;++slot){const auto* o=c.oam+slot*4;if(!o[0]||o[0]>=160)continue;
    auto& p=state.active_pieces[state.active_count++];p={};p.x=int16_t(origin+int(o[1])-8);p.y=int16_t(int(o[0])-16);p.attr=o[3];p.tile_id=o[2];p.color=1;
    for(unsigned old=0;old<previous_count;++old)if(previous[old].tile_id==p.tile_id){p.color=previous[old].color;break;}
    const uint8_t* art=c.vram+unsigned(p.tile_id)*16;
    if(color&&(unsigned(p.tile_id)+1)*16<=data.body_original_art.size()&&std::equal(art,art+16,data.body_original_art.data()+unsigned(p.tile_id)*16)){
     art=data.body_color_art.data()+unsigned(p.tile_id)*16;p.color=1;p.reserved=1;
    }
    std::copy_n(art,16,p.tile.begin());state.active_owned[slot]=1;
   }
   state.native_wait_flags|=1;
 }

 void first_line(const ModContext& c) {

  state.fixed_active=uint8_t(title_matches(c)?1:select_matches(c));if(!state.fixed_active&&thanks_matches(c))state.fixed_active=4;if(!state.fixed_active&&password_matches(c))state.fixed_active=7;state.fixed_active_recoil=state.recoil_disabled;

  if(!state.fixed_active&&credits_scene(c)&&state.credits.initialized)state.fixed_active=8;

  if(state.fixed_active==8){state.active_credits=state.credits;for(unsigned n=0;n<state.active_count;++n){auto& p=state.active_pieces[n];if(!(p.reserved&2))std::copy_n(background_texture(c,unsigned(p.tile_id)*16),16,p.tile.begin());}}

  if(!state.fixed_active&&iv_ending_scene(c))state.fixed_active=9;

  if(state.iv_credits.initialized)state.iv_credits.shade=state.iv_credits.fade_valid?state.iv_credits.fade_override:peek(c,title==3?0xde95:0xdea6);state.iv_active_credits=state.iv_credits;

  if(state.fixed_active==9){

   // The authored palette uploader reads DEA6, before the native HBlank

   // callback changes BGP for its name bar. Latch the same source shadow.

   state.iv_credits.shade_valid=1;state.iv_credits.shade=state.iv_credits.fade_valid?state.iv_credits.fade_override:peek(c,title==3?0xde95:0xdea6);state.iv_active_credits=state.iv_credits;

   for(unsigned n=0;n<state.active_count;++n){auto& p=state.active_pieces[n];

    if((p.color&8)||(!(p.reserved&1)&&(p.attr&8))){std::copy_n(state.iv_active_credits.vram1.data()+unsigned(p.tile_id)*16,16,p.tile.begin());continue;}

    if(p.reserved&2)continue;const uint8_t* tile=background_texture(c,unsigned(p.tile_id)*16);

    if((p.reserved&1)&&(unsigned(p.tile_id)+1)*16<=data.body_original_art.size()&&std::equal(c.vram+p.tile_id*16,c.vram+p.tile_id*16+16,data.body_original_art.data()+p.tile_id*16))tile=data.body_color_art.data()+p.tile_id*16;

    std::copy_n(tile,16,p.tile.begin());}

  }

  if(state.fixed_active)state.native_wait_flags&=2;
  if(state.fixed_active==1)std::copy_n(c.oam,4,state.fixed_active_arrow.begin());

  if(state.fixed_active>=2){std::copy_n(c.wram,8192,state.active_wram.begin());std::copy_n(c.oam,160,state.active_wram.begin());std::copy_n(c.vram,8192,state.active_vram.begin());return;}

  if(death_picture(c)||(!state.fixed_active&&(entry_picture(c)||((state.native_wait_flags&1)&&gameplay(c))))){
   capture_native_wait(c);return;
  }
  if(!state.active_valid)return;

  // Retain the preceding descriptor at the completed-picture boundary. Two
  // reserved state bytes carry this presentation-only link across save/load.
  if(state.display_valid){auto before=c,after=c;before.wram=state.display_wram.data();after.wram=state.active_wram.data();
   unsigned at=title==3?0xd7b9:0xd7bb,old=word(before,at),next=word(after,at),bank=title==3?0xde94:0xdf01;
   if(old!=next){unsigned previous=peek(before,bank)==peek(after,bank)&&old>=0x4000&&old<0x8000?old:0;state.previous_room_low=uint8_t(previous);state.previous_room_high=uint8_t(previous>>8);}
  }

  std::copy_n(c.vram,8192,state.active_vram.begin());

  // Poses are committed before the native VBlank DMA; the following physical

  // frame uses those poses with the textures uploaded by that interrupt.

  for(unsigned i=0;i<state.active_count;++i){auto& p=state.active_pieces[i];if(p.reserved&2)continue;const uint8_t* texture=c.vram+p.tile_id*16;

   if(color&&(p.reserved&1)&&(unsigned(p.tile_id)+1)*16<=data.body_original_art.size()&&std::equal(texture,texture+16,data.body_original_art.data()+p.tile_id*16))texture=data.body_color_art.data()+p.tile_id*16;

   std::copy_n(texture,16,p.tile.begin());}

 }

 bool collision(const ModContext& c,std::span<uint16_t,6> cpu,bool high,int& relative)const {

  unsigned stack=unsigned(cpu[4])+(high?2u:0u);if(stack>0xfff8)return false;

  if(word(c,stack)!=(title==3?0x0c19u:0x0e6du))return false;

  unsigned caller=word(c,stack+2),saved=word(c,stack+4),bank=high?peek(c,cpu[4]+1):c.rom_bank,offset=0;

  bool known=false;

  if(title==3){

   known=(bank==9&&(caller==0x5ce2||caller==0x5d5b))||(bank==0x0d&&(caller==0x53c0||caller==0x70ce));

   if(bank==3&&(caller==0x5d90||caller==0x6292)){known=true;offset=0x0f;}

  }else known=(bank==3&&caller==0x6109)||(bank==9&&(caller==0x448f||caller==0x4508||caller==0x70f5))||

   (bank==0x0d&&(caller==0x47c5||caller==0x7820||caller==0x7a88||caller==0x7cdd))||(bank==0x1f&&(caller==0x7604||caller==0x7ae4));

  if(!known||saved<offset)return false;unsigned actor=saved-offset;

  if(actor<0xc200||actor>0xd4c0||(actor&63))return false;

  unsigned screen=peek(c,actor+0x0d),sample=high?cpu[1]>>8:cpu[2]&255;

  int base=int16_t(word(c,actor+0x0a)-word(c,0xd711)),movement=int8_t(screen-uint8_t(base)),delta=int8_t(sample-screen);

  if(movement<-32||movement>32||delta<-32||delta>32)return false;

  relative=base+movement+delta;int world=int(word(c,0xd711))+relative;

  return (relative<0||relative>=160)&&world>=0&&world<int(peek(c,0xd84c))*16;

 }

 bool separate_body(const ModContext& c)const {

  unsigned actor=word(c,0xcc40),frame=word(c,0xce53);

  if(actor<0xc200||actor>0xd4c0||(actor&63)||frame<0x4000||frame>0x7ffc)return false;

  int camera=int(word(c,0xd711)),world=int(word(c,actor+0x0a)),screen=peek(c,actor+0x0d),movement=int8_t(screen-uint8_t(world-camera));

  if(movement<-32||movement>32)return false;world+=movement;

  int left=int8_t(peek(c,frame)),right=int8_t(peek(c,frame+1));

  if(title==4&&(peek(c,0xd3ea)&32)){int old=left;left=-right;right=-old;}

  int player=int(word(c,0xd70d)),player_screen=peek(c,title==3?0xd721:0xd723),bias=title==3?0:32;

  int player_left=player+int8_t(peek(c,title==3?0xd741:0xd747)-player_screen-bias);

  int player_right=player+int8_t(peek(c,title==3?0xd742:0xd748)-player_screen-bias);

  return world+right<player_left||world+left>=player_right;

 }

 static void push(std::span<uint16_t,6> cpu,std::span<uint8_t> ram,std::span<uint8_t> hram,uint16_t value) {

  auto address=unsigned(cpu[4])-2;

  if(address>=0xc000&&address+1<0xe000&&address-0xc000+1<ram.size()) {

   ram[address-0xc000]=uint8_t(value);ram[address-0xc000+1]=uint8_t(value>>8);

  } else if(address>=0xff80&&address+1<0xffff&&address-0xff80+1<hram.size()) {

   hram[address-0xff80]=uint8_t(value);hram[address-0xff80+1]=uint8_t(value>>8);

  } else throw std::runtime_error("Mega Man native selection stack is outside writable RAM");

  cpu[4]=uint16_t(address);

 }

};

Mm3NativeMods::Mm3NativeMods(std::span<const NativeFeature> f,bool c):Mm3NativeMods(3,f,c){}

Mm3NativeMods::Mm3NativeMods(unsigned title,std::span<const NativeFeature> features,bool):impl_(std::make_unique<Impl>(title)) {

 const std::string prefix="gbcllr.mm"+std::to_string(title);

 for(const auto& f:features) {

  if(f.abi!=1||(f.plugin!=prefix+".adaptive-view"&&f.plugin!=prefix+".dx"))throw std::runtime_error("Unsupported Mega Man native package");

  if(f.view_width!=-1&&(f.view_width<160||f.view_width>4096))throw std::runtime_error("Invalid Mega Man native view width");

  if(f.colorization&&!f.asset_input.empty()&&f.asset_input!=GBCLLR_MM34_DX_ASSET_ID)throw std::runtime_error("Mega Man DX asset pin differs from the compiled presentation data");

  if(f.plugin==prefix+".adaptive-view")impl_->requested=f.view_width;

  impl_->shoulders|=f.weapon_cycle;impl_->smooth|=f.smooth_gameplay;impl_->deflicker|=f.no_flicker;impl_->color|=f.colorization;

 }

 if(impl_->color&&impl_->data.fades.empty())throw std::runtime_error("Mega Man DX requires its pinned native presentation assets");

}

Mm3NativeMods::~Mm3NativeMods()=default;

uint8_t Mm3NativeMods::read(ModContext& c,uint16_t address,uint8_t value){

 auto& m=*impl_;auto& s=m.state;

 if(c.sp==UINT16_MAX||!m.wide_world(c))return value;

 unsigned ip=c.instruction_pc;

 bool scan=m.title==3?((ip>=0x235f&&ip<=0x23b9)||(ip>=0x26e5&&ip<0x2816)):((ip>=0x246e&&ip<0x24c9)||(ip>=0x2803&&ip<=0x292b));

 if(scan&&(address==0xd788||address==0xd789)){

  ++s.spawn_reads;int edge=m.left(c),block=address==0xd788?(std::max(0,edge-16)/16):(edge+int(m.width))/16+1;

  return uint8_t(std::clamp(block,0,255));

 }

 if(address==0xd78a&&ip==(m.title==3?0x26f1:0x284c)){

  ++s.spawn_scans;unsigned center=m.peek(c,0xd787);

  if(s.resize_scans){unsigned phase=s.resize_scans--;if(phase==2&&center<255)return uint8_t(center+1);if(phase==1&&center)return uint8_t(center-1);}

 }

 return value;

}

void Mm3NativeMods::write(ModContext& c,uint16_t address,uint8_t value){

 if(impl_->title==4&&address==0xdea6){
  auto& cache=impl_->state.iv_credits;
  bool capsule=impl_->color&&c.rom_bank==6&&c.instruction_pc==0x4adf&&Impl::peek(c,0xdf0a)==14;
  if(!capsule){cache.fade_valid=cache.fade_override=cache.object_fade_valid=cache.object_fade_override=0;}
 }
 if(impl_->color&&impl_->title==4&&address==0xdeab&&value<13&&Impl::peek(c,0xdf0a)!=22&&impl_->state.iv_credits.initialized){
  // The selected weapon is read directly, so Bright Stopper cannot leave a
  // queued old OBJ pointer behind when the original menu changes DEAB.
  auto& cache=impl_->state.iv_credits;cache.palettes[1]=uint16_t(0x4100+value*8);cache.palettes[9]=uint16_t(0x4a00+value*8);
 }

 if(address==0xff46&&value==0xc0)impl_->select_dma();

 if(address>=0x8000&&address<0x9800&&impl_->state.dx_valid[address-0x8000]==2)impl_->state.dx_valid[address-0x8000]=0;

}

void Mm3NativeMods::inputs(uint8_t first,uint8_t second) {

 if((first|second)&~3u)throw std::runtime_error("Invalid Mega Man virtual input");

 auto& s=impl_->state;auto fresh=uint8_t(first&~s.previous);

 if(!impl_->shoulders||first==3||s.menu)s.pending=0;else if(fresh)s.pending=fresh;

 s.previous=first;

}

void Mm3NativeMods::instruction(ModContext& c,std::span<uint16_t,6> cpu,std::span<uint8_t> ram,std::span<uint8_t> hram) {

 auto& m=*impl_;auto& s=m.state;

 m.credits_instruction(c,cpu);

 m.resource_background(c,cpu);

 m.iv_ending_instruction(c,cpu);

 m.iv_managed_instruction(c,cpu);

 m.story_instruction(c,cpu);
 // Authored red-trap pose9:7419 adds the closed platform rectangle to
 // logical pose2/3. Change only the consumed A after each original header
 // read; native bounds arithmetic, rectangle publication and contact remain.
 if(m.color&&m.title==3&&m.peek(c,0xde93)==9&&m.word(c,0xce53)==0x7419){
  unsigned pc=cpu[5],hl=cpu[3],value=256;
  if(pc==0x288d&&hl==0x741a)value=0xfa;
  else if(pc==0x2896&&hl==0x741b)value=0x11;
  else if(pc==0x28a0&&hl==0x741c)value=0xf8;
  if(value<256)cpu[0]=uint16_t((value<<8)|(cpu[0]&255));
 }


 // Authored Dust stage8 scrolling room keeps its original mutable-map loader.
 // At the original completed512-byte copy, change only ten source-qualified
 // map cells in WRAM before native row pointers/collision/rendering consume it.
 // The cartridge and its bus reads remain unchanged.
 if(m.color&&m.title==3&&cpu[5]==0x3888&&c.rom_bank==10&&
    m.peek(c,0xdf08)==8&&m.word(c,0xd7bb)==0x6a9b&&
    m.peek(c,0xd7e6)==128&&m.peek(c,0xd84c)==60&&m.peek(c,0xd84d)==8&&
    m.word(c,cpu[4])==0xd500){
  constexpr std::array<unsigned,10> offsets{64,65,92,93,124,125,152,153,212,213};
  constexpr std::array<uint8_t,10> original{42,43,42,43,44,45,42,43,44,45};
  constexpr std::array<uint8_t,10> authored{44,45,44,45,0,0,0,0,0,0};
  bool matches=true;for(unsigned n=0;n<offsets.size();++n)matches&=ram[0x1500+offsets[n]]==original[n];
  if(matches)for(unsigned n=0;n<offsets.size();++n)ram[0x1500+offsets[n]]=authored[n];
 }


 // Authored Packatto24 sequence9:62B5 changes duration8->10 and its last
 // pose4->2. Keep each original bus read/instruction, changing only the
 // consumed register at the exact original animation service boundaries.
 if(m.color&&m.title==3&&c.rom_bank==9){
  unsigned pc=cpu[5],de=cpu[2];
  if((pc==0x5a5f||pc==0x5a84)&&de==0x62b5)cpu[0]=uint16_t((10u<<8)|(cpu[0]&255));
  if((pc==0x5a6f||pc==0x5a93)&&de==0x62b8)cpu[0]=uint16_t((2u<<8)|(cpu[0]&255));
 }


 if(m.color&&m.title==3&&cpu[5]==0x1dd1&&c.rom_bank==3&&m.peek(c,0xcc96)==15&&m.peek(c,0xccb2)>=3&&m.peek(c,0xccb2)<=8){

  auto pose=std::find(m.data.intro_pose_addresses.begin(),m.data.intro_pose_addresses.end(),cpu[3]);

  if(pose!=m.data.intro_pose_addresses.end()&&m.peek(c,0xccad)==127){unsigned index=unsigned(pose-m.data.intro_pose_addresses.begin());auto source=m.data.intro_overlays.data()+index*5;s.fixed_active_arrow={uint8_t(index+1),uint8_t(m.peek(c,0xcca6)+source[0]),uint8_t(m.peek(c,0xcca8)+source[1]),m.peek(c,0xcc97)};}

 }

 if(cpu[5]==(m.title==3?0x178b:0x1b0d))m.copy_background(c,cpu[3],cpu[2],cpu[1]);

 {

  if(cpu[5]==(m.title==3?0x08e8:0x0a01)){s.weapons={};s.previous_room_low=s.previous_room_high=0;}

  unsigned bank=m.title==3?1:6;

  // The weak Buster writes OAM directly instead of calling the pose service.
  // Claim that hardware slot and render the tracked world coordinate, including
  // left-of-camera positions whose hardware X byte has wrapped through 255.
  if(c.rom_bank==bank&&cpu[5]==(m.title==3?0x43e3:0x47bb)&&s.building&&m.presentation(c)){
   unsigned base=0xc200|(cpu[1]&255);int index=Impl::weapon_index(base);
   if(index>=0&&m.tracked_buster(c,base)&&m.peek(c,base+9)==0&&s.weapons[unsigned(index)].valid){
    unsigned handle=m.own(c,m.peek(c,base+10),1);
    if(s.build_count<s.build.size()){
     auto& p=s.build[s.build_count++];p={};p.reserved=uint8_t(m.take_slot(c,handle)<<2);
     p.x=int16_t(s.weapons[unsigned(index)].world-4);p.y=int16_t(int(m.peek(c,base+8))-4);
     p.attr=m.title==3?uint8_t(m.peek(c,0xd70b)&128):m.peek(c,0xffab);p.tile_id=m.title==3?0x65:0x63;
     std::copy_n(c.vram+p.tile_id*16,16,p.tile.begin());++s.captures;
    }
   }
  }

  if(c.rom_bank==bank&&cpu[5]==(m.title==3?0x420e:0x45ff)){unsigned base=0xc200|(cpu[1]&255);int index=Impl::weapon_index(base);if(index>=0){s.weapons[unsigned(index)]={};if(m.tracked_buster(c,base))s.weapons[unsigned(index)]={int16_t(int(m.word(c,0xd70d))+int8_t(m.peek(c,base+4)-m.peek(c,m.title==3?0xd721:0xd723))),1,0};}}

  if(c.rom_bank==bank&&cpu[5]==(m.title==3?0x4352:0x4709)){unsigned base=0xc200|(cpu[1]&255);int index=Impl::weapon_index(base);if(m.tracked_buster(c,base)&&s.weapons[unsigned(index)].valid){auto& weapon=s.weapons[unsigned(index)];int camera=int(m.word(c,0xd711)),relative=int(weapon.world)-camera;relative+=int8_t(uint8_t(cpu[2])-uint8_t(relative));weapon.world=int16_t(camera+relative);

    // Preserve the native Y clip and controller, extending its X decision

    // only for genuine III weak/charged and IV weak Busters tracked in world.

    if(m.wide_world(c)){int left=m.left(c),right=std::min(left+int(m.width),int(m.peek(c,0xd84c))*16);bool inside=weapon.world>=std::max(0,left)-12&&weapon.world<right+12;if(m.title==3)cpu[5]=inside?0x435b:0x4367;else if(inside)cpu[5]=0x4716;else{cpu[1]|=0x0100;cpu[5]=0x4726;}}

  }}

 }

 if(m.color&&m.title==3&&c.rom_bank==0x0d&&cpu[5]==0x5236){

  // The authored Super Ball projectile restores this missing call to the

  // original animator. Keep its native table, state machine and BC save/restore.

  if(s.animation_stack){

   if(cpu[4]!=s.animation_stack)throw std::runtime_error("Invalid Mega Man animation continuation stack");

   cpu[1]=uint16_t(m.word(c,s.animation_stack));cpu[4]=uint16_t(cpu[4]+2);s.animation_stack=0;

  }else{

   Impl::push(cpu,ram,hram,cpu[1]);s.animation_stack=cpu[4];

   Impl::push(cpu,ram,hram,0x5236);cpu[2]=0x5242;cpu[5]=0x015f;return;

  }

 }

 if(m.color&&m.title==4){

  // Authored third title item: the original controller still provides fresh

  // input and its sound queue. Extend only its 1/2 selector to 1/2/4.

  if(c.rom_bank==1&&cpu[5]==0x5d62){

   unsigned before=m.peek(c,0xdf46),selected=before;if(selected!=1&&selected!=2&&selected!=4)selected=1;

   unsigned fresh=m.peek(c,0xdea2)&0xc4;

   if(fresh&4)selected=selected==4?1:selected*2;

   else if(fresh&64){if(selected>1)selected/=2;}

   else if(fresh&128){if(selected<4)selected*=2;}

   ram[0x1f46]=uint8_t(selected);cpu[3]=0xdf46;

   cpu[5]=selected!=before?0x5d81:0x5d83; // Original navigation sound or RET.

  }else if(c.rom_bank==1&&cpu[5]==0x5d54){

   unsigned selected=m.peek(c,0xdf46);cpu[2]=uint16_t((selected==4?0x7000:selected==2?0x6800:0x6000)|(cpu[2]&255));

  }else if(c.rom_bank==1&&cpu[5]==0x5d3d&&m.peek(c,0xdf46)==4){

   s.recoil_disabled^=1;cpu[5]=0x5d2f;

  }

  // The authored OFF path retains both original charged projectile services

  // but omits their fixed-point horizontal/vertical recoil additions.

  if(s.recoil_disabled&&c.rom_bank==6){

   if(cpu[5]==0x5b5e||cpu[5]==0x5d3d)cpu[2]=0;

   else if(cpu[5]==0x5b68||cpu[5]==0x5d47)++cpu[5];

   else if(cpu[5]==0x5b7d||cpu[5]==0x5d5c)cpu[5]=uint16_t(cpu[5]+2);

  }

 }

 if(c.rom_bank==1&&(cpu[5]==(m.title==3?0x62ab:0x5070)||(m.title==4&&cpu[5]==0x506c))){s.menu=1;s.pending=0;s.valid=0;}

 if(cpu[5]==m.tick()&&c.rom_bank==1)s.menu=0;

 if(cpu[5]==(m.title==3?0x1539:0x1849)||cpu[5]==(m.title==3?0x155d:0x1879))m.publish(c);

 if(c.rom_bank==1&&cpu[5]==(m.title==3?0x72ad:0x68c9))m.body(c);

 if(cpu[5]==(m.title==3?0x2e80:0x2f54))m.capture(c,cpu);

 if(m.title==4&&cpu[5]==0x2a95&&cpu[3]>=0xc221&&cpu[3]<=0xd4e1&&((cpu[3]-0x21)&63)==0){s.hurt_actor=uint16_t(cpu[3]-0x21);s.hurt_timer=m.peek(c,cpu[3]);}

 if(m.title==4&&cpu[5]==0x3325)m.list(c,cpu);

 if(m.wide_world(c)){

  // The original point-weapon lists use the physical camera's coordinate

  // system. Reject a different actor world page before the eight-bit test

  // can consume a real shot or damage an aliased enemy. Retain each original

  // weapon's own point lists, bounding offsets and travel limits.

  if(cpu[5]==(m.title==3?0x0fe3:0x11c9)){

   unsigned cursor=cpu[3],stack=cpu[4],box=0,actor=0;

   unsigned first_return=m.title==3?0x0fb8:0x1189,actor_return=m.title==3?0x2946:0x2a43;

   if(cursor>=0xc20a&&cursor<=0xc216&&(cursor-0xc20a)%3==0&&m.word(c,stack)==first_return&&m.word(c,stack+2)==actor_return){box=0xc204;actor=m.word(c,stack+4);}

   else if(cursor>=0xc21e&&cursor<=0xc227&&(cursor-0xc21e)%3==0&&m.word(c,stack)==actor_return){box=0xc218;actor=m.word(c,stack+2);}

   if(box&&actor>=0xc200&&actor<=0xd4c0&&!(actor&63)&&m.peek(c,cursor-1)==m.peek(c,0xc22a)){

    int camera=int(m.word(c,0xd711)),world=int(m.word(c,actor+0x0a)),screen=m.peek(c,actor+0x0d),movement=int8_t(screen-uint8_t(world-camera));

    unsigned base=0xc200+m.peek(c,cursor-1)-0x0b;int weapon=Impl::weapon_index(base);bool tracked=m.tracked_buster(c,base)&&s.weapons[unsigned(weapon)].valid;

    if(movement>=-32&&movement<=32&&(tracked||int16_t(world+movement-camera-screen))){

     unsigned bounds=m.title==3?0xff91:0xff8a;int actual=world+movement;

     int left=actual+int8_t(m.peek(c,bounds)-screen)-int8_t(m.peek(c,box+1));

     int right=actual+int8_t(m.peek(c,bounds+1)-screen)-int8_t(m.peek(c,box+2));

     unsigned sample=m.peek(c,cursor);int point=tracked?int(s.weapons[unsigned(weapon)].world)+int8_t(sample-m.peek(c,base+4)):camera+(sample>=0xe0?int(sample)-256:int(sample));

     if(point<left||point>=right){cpu[3]=uint16_t(cursor+2);cpu[5]=m.title==3?0x0fdc:0x11c2;}

     else if(tracked){cpu[3]=uint16_t(cursor+1);cpu[5]=m.title==3?0x0fea:0x11d0;}

    }

   }

  }

  // The original body-contact rectangles contain eight-bit screen X. Reject

  // only a physically separate body pair; weapon collision keeps its own path.

  if(cpu[5]==(m.title==3?0x2900:0x2a0f)&&m.separate_body(c))cpu[5]=m.title==3?0x2923:0x2a19;

  if(cpu[5]==(m.title==3?0x2d2b:0x2e87)){

   unsigned actor=unsigned(cpu[3])-0x0d,screen=cpu[2]&255;

   if(actor>=0xc200&&actor<=0xd4c0&&!(actor&63)&&uint8_t(actor)==uint8_t(cpu[1])){

    int world=int(m.word(c,actor+0x0a)),camera=int(m.word(c,0xd711)),previous=int16_t(world-camera),movement=int8_t(screen-uint8_t(previous));

    if(movement>=-32&&movement<=32&&world+movement>=0&&world+movement<int(m.peek(c,0xd84c))*16+32){

     int relative=previous+movement;cpu[2]=uint16_t((uint16_t(uint8_t((relative-int(screen))/256))<<8)|screen);

    }

   }

  }

  int relative=0;bool query=cpu[5]==(m.title==3?0x0cb4:0x0f2b),high=cpu[5]==(m.title==3?0x0cfd:0x0f6f);

  if((query||high)&&m.collision(c,cpu,high,relative)){

   if(query)cpu[5]=m.title==3?0x0cd6:0x0f4d;

   else{int sample=cpu[1]>>8;cpu[2]=uint16_t((uint16_t(uint8_t((relative-sample)/256))<<8)|(cpu[2]&255));}

  }

 }

 // The original culler stores its real modulo-screen coordinate. Extend

 // only its visibility decision; collision/world coordinates remain native.

 if(m.wide_world(c)&&cpu[5]==(m.title==3?0x26b5:0x27d3)){

  int world=int(m.word(c,unsigned(cpu[3])-2)),relative=int16_t(world-m.word(c,0xd711));

  if((relative<-32||relative>=192)&&world>=m.left(c)-32&&world<m.left(c)+int(m.width)+32){cpu[5]=m.title==3?0x26c6:0x27e4;++s.cull_extensions;}

 }

 if(s.continuation) {

  if(cpu[5]!=m.tick()||cpu[4]!=s.caller[4]||c.rom_bank!=1)return;

  if(s.continuation==1) {

   ram[m.weapon()-0xc000]=s.target;s.continuation=2;

   Impl::push(cpu,ram,hram,m.tick());cpu[5]=m.title==3?0x40cf:0x03e5;return;

  }

  std::copy(s.caller.begin(),s.caller.end(),cpu.begin());s.continuation=0;s.target=0;s.caller={};return;

 }

 if(cpu[5]!=m.tick()||c.rom_bank!=1)return;

 auto fresh=s.pending;s.pending=0;

 if(!m.shoulders||!m.gameplay(c)||(fresh!=virtual_l&&fresh!=virtual_r)||ram.size()<0x2000)return;

 unsigned count=m.title==3?12:13,current=ram[m.weapon()-0xc000];

 if(current>=count)return;

 unsigned next=current;auto owns=uint16_t(ram[m.weapon()-0xc000+1]|(ram[m.weapon()-0xc000+2]<<8));

 do{next=(next+(fresh==virtual_l?count-1:1))%count;}while(next!=current&&next&&!(owns&(1u<<next)));

 if(next==current)return;

 std::copy(cpu.begin(),cpu.end(),s.caller.begin());s.target=uint8_t(next);s.continuation=1;

 Impl::push(cpu,ram,hram,m.tick());cpu[5]=m.title==3?0x44ee:0x03f1;

}

void Mm3NativeMods::complete_frame(ModContext& c,bool cleared){auto& s=impl_->state;
 // A new room can become visible before its first logical pose publication.
 // At this boundary native OAM still belongs to the completed PPU picture.
 if(!cleared&&!s.active_valid&&!s.fixed_active&&impl_->gameplay(c))impl_->capture_native_wait(c);
 s.native_wait_flags=uint8_t((s.native_wait_flags&1)?3:0);

 s.display_valid=s.active_valid;if(s.active_valid||s.fixed_active>=2){s.display_count=s.active_count;s.display_overflow=s.active_overflow;s.display_pieces=s.active_pieces;s.display_owned=s.active_owned;s.display_wram=s.active_wram;s.vram=s.active_vram;}

 s.display_raster=s.raster;s.display_bg_rows=s.bg_rows;s.display_bg_maps=s.bg_maps;

 s.fixed_display=s.fixed_active;s.fixed_display_recoil=s.fixed_active_recoil;s.fixed_display_arrow=s.fixed_active_arrow;

 if(s.fixed_active==8)s.display_credits=s.active_credits;

 s.iv_display_credits=s.iv_active_credits;

 if(cleared){s.native_wait_flags=0;s.fixed_active=s.fixed_display=0;s.valid=s.building=s.display_valid=s.active_valid=0;s.build_count=s.count=s.display_count=s.active_count=0;s.build_owned={};}

}

void Mm3NativeMods::scanline(ModContext& c,uint8_t line){

 if(line>=144||!c.ppu)return;auto& s=impl_->state;if(!line)impl_->first_line(c);s.raster[line]={c.ppu->scx,c.ppu->scy,c.ppu->bgp,c.ppu->lcdc,c.ppu->obp0,c.ppu->obp1,c.ppu->wx,c.ppu->wy};

 unsigned row=((unsigned(line)+c.ppu->scy)&7)*2,stage=impl_->peek(c,impl_->title==3?0xdf08:0xdf0a);bool world_attributes=impl_->color&&(impl_->gameplay(c)||impl_->death_picture(c)||(!s.fixed_active&&impl_->entry_picture(c)))&&stage&&stage<impl_->data.attributes.size()/256;

 for(unsigned tile=0;tile<256;++tile){unsigned address=(c.ppu->lcdc&16)?tile*16:unsigned(0x1000+int(int8_t(tile))*16);auto source=impl_->background_texture(c,address);if(world_attributes&&(impl_->data.attributes[stage*256+tile]&8))source=impl_->data.menu_boot_vram.data()+address;s.bg_rows[line][tile*2]=source[row];s.bg_rows[line][tile*2+1]=source[row+1];}

 unsigned map=(c.ppu->lcdc&8)?0x1c00:0x1800,sy=(unsigned(line)+c.ppu->scy)&255;std::copy_n(c.vram+map+(sy/8)*32,32,s.bg_maps[line].begin());

}

void Mm3NativeMods::viewport(unsigned w,unsigned h) {

 if(!w||!h||w>16384||h>16384)throw std::runtime_error("Invalid Mega Man output viewport");

 impl_->width=impl_->requested<0?std::clamp(unsigned(uint64_t(w)*144/h),160u,4096u):unsigned(impl_->requested);

 if(impl_->state.width!=impl_->width)impl_->state.resize_scans=2;impl_->state.width=impl_->width;

}

ModView Mm3NativeMods::render(ModContext& c,std::span<const uint32_t> native){

 auto& m=*impl_;auto& s=m.state;

 if(s.fixed_display==8&&native.size()==160*144)return m.credits_view(c,native);

 if(s.fixed_display==9&&native.size()==160*144)return m.iv_ending_view(c,native);

 if(s.fixed_display&&native.size()==160*144)return m.title_view(c,native);

 if(m.color&&s.menu){
  if(m.peek(c,m.title==3?0xde95:0xdea6)==0x1b){auto menu=m.menu_view(c,native);return megaman_pause_canvas(menu,m.width,m.pause_canvas);}
  // Upload/fade frames have no complete menu yet. Keep the selected canvas
  // black until its original controller has finished, rather than exposing
  // partially replaced monochrome world tiles as a menu.
  m.pause_canvas.assign(size_t(m.width)*144,0xff000000u);return {m.width,m.width>160,m.pause_canvas};
 }

 auto frame=c;frame.wram=s.display_wram.data();frame.vram=s.vram.data();bool waiting=m.entry_picture(frame)||((s.native_wait_flags&2)&&!m.death_picture(frame)),scripted=m.scripted_scene(frame);unsigned render_width=scripted?160:m.width;

 if(!s.display_valid||(!m.gameplay(frame)&&!m.death_picture(frame)&&!waiting&&!scripted)||native.size()!=160*144||(!m.deflicker&&render_width==160&&!m.color)){
  if(m.color&&m.peek(frame,0xd84c)&&m.peek(frame,0xd84d)){m.pause_canvas.assign(size_t(m.width)*144,0xff000000u);return {m.width,m.width>160,m.pause_canvas};}
  return {160,false,native};
 }

 auto palettes=m.title==4&&s.iv_display_credits.initialized?s.iv_display_credits.palettes:m.palettes(frame);unsigned stage=m.peek(frame,m.title==3?0xdf08:0xdf0a);

 std::array<std::array<std::array<uint32_t,4>,16>,256> palette_colors{};std::array<bool,256> palette_ready{};

 auto colors=[&](uint8_t shade)->const std::array<std::array<uint32_t,4>,16>&{if(m.color&&m.title==4&&s.iv_display_credits.initialized&&s.iv_display_credits.fade_valid)shade=s.iv_display_credits.shade;if(m.color&&!palette_ready[shade]){palette_ready[shade]=true;for(unsigned slot=0;slot<16;++slot)for(unsigned ink=0;ink<4;++ink)palette_colors[shade][slot][ink]=m.colored(palettes[slot],m.title==4&&slot>=8&&s.iv_display_credits.object_fade_valid?s.iv_display_credits.object_fade_override:shade,ink,slot>=8,0);}return palette_colors[shade];};

 unsigned cols=m.peek(frame,0xd84c),rows=m.peek(frame,0xd84d),bank=m.peek(frame,m.title==3?0xde94:0xdf01),table=m.word(frame,0xd850);

 const uint32_t* blocks=nullptr;if(m.color)for(size_t i=0;i<m.data.metatile_records.size();i+=4)if(m.data.metatile_records[i]==stage&&m.data.metatile_records[i+1]==table){blocks=m.data.metatile_records.data()+i;break;}

 // D70F is the moving camera center. D711 remains frozen at the old room
 // boundary while the native room-scroll controller advances that center.
 bool ordinary=!scripted&&m.peek(frame,0xde44)==0;
 int room_width=int(cols)*16,native_left=ordinary?int16_t(m.word(frame,0xd70f))-80:int16_t(m.word(frame,0xd711)),left=native_left-int(render_width-160)/2;

 if(!cols||!rows||rows>16||table<0x4000||table>=0x8000)return {160,false,native};

 struct Adjacent {int left=0,right=0;unsigned map=0,columns=0,table=0;const uint32_t* blocks=nullptr;};std::array<Adjacent,2> adjacent{};unsigned adjacent_count=0;
 int bound_left=0,bound_right=room_width,origin=int(m.word(frame,m.title==3?0xd7bf:0xd7c1));
 auto neighbor=[&](unsigned descriptor){
  if(!ordinary||rows!=8||descriptor<0x4000||descriptor+7>0x8000)return;
  unsigned map=m.word(frame,descriptor+3,bank),columns=m.peek(frame,map,bank),height=m.peek(frame,map+1,bank);int x=int(m.word(frame,descriptor+5,bank))-origin;
  if(map<0x4000||map+2+columns*height>0x8000||!columns||height!=rows||table<0x4000||table>=0x8000||(x!=room_width&&x+int(columns)*16!=0))return;
  for(unsigned n=0;n<adjacent_count;n++)if(adjacent[n].left==x)return;
  // Room descriptors supply the block grid and actor list; the checkpoint's
  // active D850 table supplies the terrain metatiles for adjoining grids too.
  auto& a=adjacent[adjacent_count++];a={x,x+int(columns)*16,map+2,columns,table,blocks};
  bound_left=std::min(bound_left,a.left);bound_right=std::max(bound_right,a.right);
 };
 neighbor(unsigned(s.previous_room_low)|(unsigned(s.previous_room_high)<<8));
 neighbor(m.word(frame,m.title==3?0xd7cb:0xd7cd));
 if(ordinary&&rows==8&&waiting){bound_left=native_left;bound_right=native_left+160;adjacent_count=0;}
 if(bound_right-bound_left<int(render_width))left=bound_left+(bound_right-bound_left-int(render_width))/2;else left=std::clamp(left,bound_left,bound_right-int(render_width));

 // During an ordinary vertical link, the physical cache contains rows from
 // both adjoining rooms. Keep its native160px footprint tied to those real
 // rows; a wrapped copy of only the newly selected room duplicates ladders.
 // Custom machine/backdrop controllers retain their qualified paths.
 bool cached_pan=ordinary&&rows==8&&(m.death_picture(frame)||waiting||std::any_of(s.display_raster.begin(),s.display_raster.begin()+128,[](const auto& r){return (r.scy&127)!=0;}))&&!(m.title==4&&(stage==14||stage==18||stage==19||stage==21));
 std::array<uint8_t,0x800> cached_tiles{};
 if(cached_pan){std::copy_n(s.vram.data()+0x1800,cached_tiles.size(),cached_tiles.begin());
  if(blocks)for(unsigned block=0;block<blocks[2]/4;++block){unsigned base=blocks[3]+block*4;
   if(std::equal(m.data.metatile_original.begin()+base,m.data.metatile_original.begin()+base+4,m.data.metatile_art.begin()+base))continue;
   for(unsigned map:{0u,0x400u})for(unsigned y=0;y<32;y+=2)for(unsigned x=0;x<32;x+=2){unsigned at=map+y*32+x;
    if(s.vram[0x1800+at]!=m.data.metatile_original[base]||s.vram[0x1800+at+32]!=m.data.metatile_original[base+1]||s.vram[0x1800+at+1]!=m.data.metatile_original[base+2]||s.vram[0x1800+at+33]!=m.data.metatile_original[base+3])continue;
    for(unsigned cell=0;cell<4;++cell)cached_tiles[at+(cell&1)*32+(cell>>1)]=m.data.metatile_art[base+cell];
   }
  }
 }
 m.pixels.assign(size_t(render_width)*144,0xff000000u);m.opaque.assign(m.pixels.size(),0);

 for(int y=0;y<144;++y)for(unsigned x=0;x<render_width;++x){const auto& raster=s.display_raster[unsigned(y)];int wx=left+int(x)+int8_t(raster.scx-s.display_raster[0].scx),physical_y=(y+raster.scy)&255;
  // Ordinary eight-row rooms occupy half of the physical BG cache. Its
  // alternating Y page is a streaming origin, not another128 world pixels.
  // Keep physical coordinates for custom screen-map backdrops below.
  int wy=rows==8&&!scripted?physical_y&127:physical_y;

  // IV's final-machine controller (12:4876) draws the moving upper hull

  // directly into the PPU map. CA0D is its first raster boundary; these

  // screen-space rows are not described by the room's terrain metatiles.

  bool upper_hull=m.title==4&&stage==21&&y<int(m.peek(frame,0xca0d));

  // Hunter's original1C:51C4 also scrolls into screen-map backdrop rows

  // outside its eight terrain rows (the upper fifteen lines in room59BF).

  bool backdrop=scripted||(m.title==4&&((stage==14||stage==18||stage==21||(stage==19&&m.peek(frame,0xde44)==13))&&physical_y>=int(rows)*16||upper_hull));unsigned tile=0,bit=0;

  bool physical=cached_pan&&int(x)+left>=native_left&&int(x)+left<native_left+160;
  if(backdrop||physical){unsigned sx=unsigned(int(raster.scx)+left-native_left+int(x))&255;tile=s.display_bg_maps[unsigned(y)][sx/8];bit=7-(sx&7);
   if(physical&&blocks){unsigned at=((raster.lcdc&8)?0x400:0)+(unsigned(physical_y)/8)*32+sx/8,texture=(raster.lcdc&16)?tile*16:unsigned(0x1000+int(int8_t(tile))*16);
    if(s.vram[0x1800+at]==tile&&m.background_texture(frame,texture)!=frame.vram+texture)tile=cached_tiles[at];
   }
  }

  else{if(wy>=int(rows)*16)continue;
   unsigned block=0,tile_table=table;const uint32_t* visual_blocks=blocks;
   if(wx>=0&&wx<room_width){unsigned row=m.word(frame,(m.title==3?0xd7a9:0xd7ab)+unsigned(wy/16)*2);block=m.peek(frame,row+unsigned(wx/16),bank);}
   else{const Adjacent* source=nullptr;for(unsigned n=0;n<adjacent_count;++n)if(wx>=adjacent[n].left&&wx<adjacent[n].right){source=&adjacent[n];break;}if(!source)continue;
    block=m.peek(frame,source->map+unsigned(wy/16)*source->columns+unsigned((wx-source->left)/16),bank);tile_table=source->table;visual_blocks=source->blocks;
   }
   unsigned cell=block*4+unsigned((wx&8)?2:0)+unsigned((wy&8)?1:0);tile=m.peek(frame,tile_table+cell,bank);bit=7-(unsigned(wx)&7);

   unsigned texture=(raster.lcdc&16)?tile*16:unsigned(0x1000+int(int8_t(tile))*16);

   if(visual_blocks&&cell<visual_blocks[2]&&m.data.metatile_original[visual_blocks[3]+cell]==tile&&m.background_texture(frame,texture)!=frame.vram+texture)tile=m.data.metatile_art[visual_blocks[3]+cell];}

  if(scripted&&wx>=0&&wx<room_width&&wy<int(rows)*16){unsigned row=m.word(frame,0xd7ab+unsigned(wy/16)*2),block=m.peek(frame,row+unsigned(wx/16),bank),cell=block*4+unsigned((wx&8)?2:0)+unsigned((wy&8)?1:0);unsigned original=m.peek(frame,table+cell,bank),texture=(raster.lcdc&16)?original*16:unsigned(0x1000+int(int8_t(original))*16);if(tile==original&&blocks&&cell<blocks[2]&&m.data.metatile_original[blocks[3]+cell]==original&&m.background_texture(frame,texture)!=frame.vram+texture)tile=m.data.metatile_art[blocks[3]+cell];}

  const auto& planes=s.display_bg_rows[unsigned(y)];

  unsigned ink=((planes[tile*2]>>bit)&1)|(((planes[tile*2+1]>>bit)&1)<<1),at=unsigned(y)*render_width+x;

  unsigned attr=m.color&&stage<m.data.attributes.size()/256?m.data.attributes[stage*256+tile]:0;

  if(m.color&&m.title==4&&s.iv_display_credits.initialized){const auto& cache=s.iv_display_credits;unsigned sx=unsigned(int(raster.scx)+left-native_left+int(x))&255,index=((raster.lcdc&8)?0x400:0)+(physical_y/8)*32+sx/8;

   if(cache.attrs[index]!=255)attr=cache.attrs[index];bit=(attr&32)?sx&7:7-(sx&7);

   if(attr&0x68){unsigned address=(raster.lcdc&16)?tile*16:unsigned(0x1000+int(int8_t(tile))*16),row=((attr&64)?7-(wy&7):wy&7)*2;auto texture=(attr&8)?cache.vram1.data()+address:m.background_texture(frame,address);ink=((texture[row]>>bit)&1)|(((texture[row+1]>>bit)&1)<<1);}

   else ink=((planes[tile*2]>>bit)&1)|(((planes[tile*2+1]>>bit)&1)<<1);

  }

  m.opaque[at]=uint8_t(ink?((m.color&&(attr&128))?2:1):0);auto native_color=c.ppu->bg_rgb[(raster.bgp>>(ink*2))&3];auto color=colors(raster.bgp)[attr&7][ink];m.pixels[at]=color?color:native_color;

 }

 m.framing_background=m.pixels;
 auto draw=[&](int x,int y,uint8_t attr,const uint8_t* tile,uint8_t color){for(int yy=0;yy<8;++yy)for(int xx=0;xx<8;++xx){int px=x+xx,py=y+yy;if(px<0||px>=int(render_width)||py<0||py>=144)continue;

   unsigned row=unsigned((attr&64)?7-yy:yy),bit=unsigned((attr&32)?xx:7-xx),ink=((tile[row*2]>>bit)&1)|(((tile[row*2+1]>>bit)&1)<<1);if(!ink)continue;

   unsigned at=unsigned(py)*render_width+unsigned(px);if(!m.color){if(m.opaque[at]&4)continue;m.opaque[at]|=4;}if((m.color&&m.opaque[at]==2)||((attr&128)&&(m.opaque[at]&3)))continue;const auto& line=s.display_raster[unsigned(py)];uint8_t pal=(attr&16)?line.obp1:line.obp0;

   auto native_color=c.ppu->obj_rgb[((attr&16)?4:0)+((pal>>(ink*2))&3)];auto rgb=colors(line.bgp)[8+(color&7)][ink];m.pixels[at]=rgb?rgb:native_color;}};

 // HUD producers that do not use the native metasprite service retain their

 // original OAM. Complete observed actor poses replace their physical slots.

 if(!m.color){

  struct Sprite {int x,y;uint8_t attr,color,slot;const uint8_t* tile;};std::vector<Sprite> sprites;sprites.reserve(s.display_count+40);

  for(unsigned i=0;i<40;++i)if(!s.display_owned[i]){unsigned p=i*4;auto attr=s.display_wram[p+3];sprites.push_back({int(s.display_wram[p+1])-8+native_left-left,int(s.display_wram[p])-16,attr,uint8_t((attr&16)?1:0),uint8_t(i),s.vram.data()+unsigned(s.display_wram[p+2])*16});}

  for(unsigned i=0;i<s.display_count;++i){const auto& p=s.display_pieces[i];sprites.push_back({int(p.x)-left,p.y,p.attr,p.color,uint8_t(p.reserved>>2),p.tile.data()});}

  std::stable_sort(sprites.begin(),sprites.end(),[](const Sprite& a,const Sprite& b){return a.x!=b.x?a.x<b.x:a.slot<b.slot;});

  for(const auto& sprite:sprites)draw(sprite.x,sprite.y,sprite.attr,sprite.tile,sprite.color);

 }else{

 for(int i=39;i>=0;--i)if(!s.display_owned[unsigned(i)]){unsigned p=unsigned(i)*4;int x=int(s.display_wram[p+1])-8+native_left-left,y=int(s.display_wram[p])-16;unsigned tile=s.display_wram[p+2];draw(x,y,s.display_wram[p+3],s.vram.data()+tile*16,uint8_t((s.display_wram[p+3]&16)?1:0));}

 for(int reserved=0;reserved<2;++reserved)for(int i=int(s.display_count)-1;i>=0;--i){const auto& p=s.display_pieces[unsigned(i)];if((p.reserved&1)==reserved)draw(int(p.x)-left,p.y,p.attr,p.tile.data(),p.color);}

 }

 // Window pixels belong to the output canvas, independently of the room's
 // clamped camera. A world-relative origin made the HUD follow the player
 // near either room edge and left terrain beside the bottom status strip.
 for(unsigned y=0;y<144;++y){const auto& line=s.display_raster[y];if(!(line.lcdc&32)||y<line.wy)continue;
  if(line.wy>=128)std::fill_n(m.pixels.begin()+size_t(y)*render_width,render_width,0xff000000u);
  for(int x=std::max(0,int(line.wx)-7);x<160;++x){int out=int(render_width-160)/2+x;if(out>=0&&out<int(render_width)){auto original=native[size_t(y)*160+unsigned(x)];m.pixels[size_t(y)*render_width+unsigned(out)]=m.color?m.window_color(frame,palettes,unsigned(x-int(line.wx)+7),y-line.wy,line.lcdc,(m.title==4&&s.iv_display_credits.initialized&&s.iv_display_credits.fade_valid)?s.iv_display_credits.shade:line.bgp,original):original;}}}

 ModView view;view.width=render_width;view.expanded=render_width>160;view.pixels=m.pixels;view.camera_x=native_left;view.left=left;view.bound_right=room_width;view.sprites=s.display_count;view.uncapped_sprites=s.display_overflow;view.captures=s.captures;view.spawn_scans=s.spawn_scans;view.spawn_reads=s.spawn_reads;view.activation_adjustments=s.cull_extensions;if(ordinary)view.framing_width=unsigned(std::clamp(bound_right-bound_left,160,int(render_width)));view.framing_background=m.framing_background;return view;

}

uint32_t Mm3NativeMods::cpu_budget(const ModContext& c,bool cleared)const{return (impl_->smooth||impl_->color)&&!cleared&&impl_->gameplay(c)?280896u:0u;}

std::vector<uint8_t> Mm3NativeMods::snapshot()const {

 std::vector<uint8_t> out(sizeof impl_->state);std::memcpy(out.data(),&impl_->state,out.size());return out;

}

void Mm3NativeMods::restore(std::span<const uint8_t> bytes) {

 Impl::State staged{};if(bytes.size()!=sizeof staged)throw std::runtime_error("Invalid Mega Man native state size");

 std::memcpy(&staged,bytes.data(),bytes.size());
 unsigned previous_room=unsigned(staged.previous_room_low)|(unsigned(staged.previous_room_high)<<8);
 if(previous_room&&(previous_room<0x4000||previous_room>=0x8000))throw std::runtime_error("Invalid Mega Man preceding room descriptor");

 for(const auto* cache:{&staged.credits,&staged.active_credits,&staged.display_credits}){

  if(cache->initialized>1||cache->padding||cache->shade_valid>1||(cache->initialized&&(!impl_->color||impl_->title!=3||impl_->data.credits_palettes.size()!=80))||std::any_of(cache->map_valid.begin(),cache->map_valid.end(),[](uint8_t v){return v>1;}))throw std::runtime_error("Invalid Mega Man credits surface state");

 }

 if((staged.fixed_active==8||staged.fixed_display==8)&&(impl_->title!=3||impl_->data.credits_palettes.size()!=80))throw std::runtime_error("Invalid Mega Man credits profile");



 for(const auto* cache:{&staged.iv_credits,&staged.iv_active_credits,&staged.iv_display_credits})if(cache->initialized>1||cache->kind>2||cache->shade_valid>1||cache->fade_valid>1||cache->overlay>2||cache->object_fade_valid>1||(cache->object_fade_valid&&impl_->title!=4)||cache->map_variant>2||(cache->map_variant&&(impl_->title!=3||impl_->data.story_map_records.size()!=8))||(cache->overlay==1&&impl_->title!=4)||(cache->overlay==2&&(impl_->title!=3||impl_->data.story_overlay_oam.size()!=28))||(!cache->initialized&&cache->fade_valid)||(cache->initialized&&(!impl_->color||impl_->data.iv_style_addresses.empty())))throw std::runtime_error("Invalid Mega Man IV ending cache");

 for(const auto* cache:{&staged.iv_credits,&staged.iv_active_credits,&staged.iv_display_credits})if(cache->pending_style&&(impl_->title!=3||!std::binary_search(impl_->data.iv_style_addresses.begin(),impl_->data.iv_style_addresses.end(),cache->pending_style)))throw std::runtime_error("Invalid Mega Man pending scene style");

 if((staged.fixed_active==9||staged.fixed_display==9)&&impl_->data.iv_style_addresses.empty())throw std::runtime_error("Invalid Mega Man IV ending profile");

 if((staged.fixed_active||staged.fixed_display)&&(!impl_->color||impl_->data.title_original.size()!=8192))throw std::runtime_error("Invalid Mega Man fixed-surface state");

 if(((staged.fixed_active>=2&&staged.fixed_active<=3)||(staged.fixed_display>=2&&staged.fixed_display<=3))&&(impl_->title!=3||impl_->data.select_original_art.size()!=0x3000))throw std::runtime_error("Invalid Mega Man stage-select state");

 if(((staged.fixed_active>=5&&staged.fixed_active<=6)||(staged.fixed_display>=5&&staged.fixed_display<=6))&&(impl_->title!=3||impl_->data.intro_original_art.size()!=0x4000||impl_->data.intro_pose_addresses.size()!=20))throw std::runtime_error("Invalid Mega Man introduction surface state");

 if((staged.fixed_active==7||staged.fixed_display==7)&&(impl_->title!=3||impl_->data.password_original_art.size()!=0x1800))throw std::runtime_error("Invalid Mega Man password surface state");

 if(staged.animation_padding||(staged.animation_stack&&

   ((!impl_->color||impl_->title!=3)||!((staged.animation_stack>=0xc000&&staged.animation_stack<=0xdffe)||(staged.animation_stack>=0xff80&&staged.animation_stack<=0xfffc)))))throw std::runtime_error("Invalid Mega Man native animation continuation");

 if(staged.previous>3||staged.pending>2||staged.continuation>2||staged.target>=(impl_->title==3?12:13)||staged.count>1024||staged.build_count>1024||staged.display_count>1024||staged.active_count>1024||staged.overflow>1024||staged.build_overflow>1024||staged.display_overflow>1024||staged.active_overflow>1024||staged.valid>1||staged.building>1||staged.menu>1||staged.recoil_disabled>1||staged.fixed_active>9||staged.fixed_display>9||staged.fixed_active_recoil>1||staged.fixed_display_recoil>1||staged.display_valid>1||staged.active_valid>1||staged.native_wait_flags>3||(staged.hurt_actor&&(staged.hurt_actor<0xc200||staged.hurt_actor>0xd4c0||(staged.hurt_actor&63)))||staged.resize_scans>2||staged.width<160||staged.width>4096||

  (!staged.continuation&&(staged.target||staged.caller!=std::array<uint16_t,6>{}))||

  (staged.continuation&&(staged.caller[5]!=impl_->tick()||staged.caller[4]!=0xfffe)))throw std::runtime_error("Invalid Mega Man native selection continuation");

 for(const auto& p:staged.pieces)if((p.reserved>>2)>40)throw std::runtime_error("Invalid Mega Man published pose");

 for(const auto& p:staged.build)if((p.reserved>>2)>40)throw std::runtime_error("Invalid Mega Man pending pose");

 for(const auto& p:staged.display_pieces)if((p.reserved>>2)>40)throw std::runtime_error("Invalid Mega Man physical-frame pose");

 for(const auto& p:staged.active_pieces)if((p.reserved>>2)>40)throw std::runtime_error("Invalid Mega Man active display pose");

 for(unsigned i=0;i<40;++i)if(staged.owned[i]>1||staged.build_owned[i]>1||staged.display_owned[i]>1||staged.active_owned[i]>1)throw std::runtime_error("Invalid Mega Man native OAM ownership");

 if(staged.resource_copy[0]){

  unsigned resource=staged.resource_copy[0],destination=unsigned(staged.resource_copy[1])|(unsigned(staged.resource_copy[2])<<8);

  if(!impl_->color||resource>=impl_->data.resource_native_counts.size()||resource>=impl_->data.art_counts.size()||impl_->data.art_counts[resource]<=impl_->data.resource_native_counts[resource]||destination<0x8000||destination+impl_->data.art_counts[resource]*16>0x9800)throw std::runtime_error("Invalid Mega Man resource copy");

 }else if(staged.resource_copy[1]||staged.resource_copy[2])throw std::runtime_error("Invalid Mega Man resource copy");

 for(auto v:staged.dx_valid)if(v>2)throw std::runtime_error("Invalid Mega Man background binding");

 for(const auto& weapon:staged.weapons)if(weapon.valid>1||weapon.padding)throw std::runtime_error("Invalid Mega Man weapon source");

 if((staged.fixed_active==4||staged.fixed_display==4)&&(impl_->data.ending_original.size()!=8192||(impl_->title==4&&impl_->data.ending_actor_map.size()!=57)))throw std::runtime_error("Invalid Mega Man ending surface state");

 impl_->state=staged;impl_->width=staged.width;

}

}
