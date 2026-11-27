// Exact USA cartridge interfaces, documented by kak2x/rw2 (read-only reference).
#include "mm2_native_mods.hpp"
#include "native_mods.hpp"
#include "megaman_pause_canvas.hpp"
#include <algorithm>
#include <array>
#include <climits>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#ifndef GBCLLR_MM12_DX_ASSETS
#define GBCLLR_MM12_DX_ASSETS 0
#endif
#ifndef GBCLLR_MM12_DX_ASSET_ID
#define GBCLLR_MM12_DX_ASSET_ID ""
#endif
#if GBCLLR_MM12_DX_ASSETS
#include "mm12_dx_data.hpp"
#endif
namespace gbcllr {
#if GBCLLR_MM12_DX_ASSETS
static_assert([]{
 for(unsigned stage=0;stage<10;++stage)for(unsigned block=0;block<64;++block){unsigned a=stage*256+block*4;
  if(std::equal(mm12_dx::mm2_original_blocks+a,mm12_dx::mm2_original_blocks+a+4,mm12_dx::mm2_blocks+a))continue;
  for(unsigned other=0;other<64;++other){unsigned b=stage*256+other*4;
   if(std::equal(mm12_dx::mm2_original_blocks+a,mm12_dx::mm2_original_blocks+a+4,mm12_dx::mm2_original_blocks+b)&&!std::equal(mm12_dx::mm2_blocks+a,mm12_dx::mm2_blocks+a+4,mm12_dx::mm2_blocks+b))return false;
  }
 }return true;
}(),"Mega Man II authored metatiles need unambiguous native four-tile patterns");
#endif
struct Mm2NativeMods::Impl {
 static constexpr unsigned max_sprites=1024;
 struct Sprite {int x,y;uint8_t attributes,color,tile,authored,tiles[16];};
 struct Raster {uint8_t scx=0,scy=0,lcdc=0,bgp=0xe4,obp[2]{0xe4,0xe4},wx=7,wy=144;};
 struct Frame {
  uint8_t valid=0,lcdc=0,bgp=0,obp[2]{},stage=0,room=0,scroll_x=0,scroll_y=0,weapon=0;
  int camera=0,left=0,bound_left=0,bound_right=4096;
  unsigned count=0;
  uint8_t map[0x800]{},blocks[0x100]{},vram[0x2000]{};
  uint8_t object_palettes[64]{},background_palettes[64]{},background_attributes[256]{};
  uint32_t bg[4]{},obj[8]{};
  Sprite sprite[max_sprites]{};
 };
 struct State {
  Frame frame{};
  Frame dma{};
  Frame display{};
  Raster raster[144]{},display_raster[144]{};
  uint8_t planes[144][512]{},display_planes[144][512]{};
  Sprite building[max_sprites]{};
  unsigned count=0,captures=0;
  uint64_t logic_updates=0;
  uint8_t dx_source[0x2000]{},dx_art[0x2000]{},dx_valid[0x2000]{};
  uint8_t object_attributes[512]{},object_palettes[64]{},face_index[256]{},face_records[256]{};
  uint8_t background_palettes[64]{},background_attributes[256]{},effect_tile[16]{};
  uint16_t effect_tile_offset=0;
  uint8_t effect_active=0,effect_profile=0,effect_clock=0,effect_phase=0,effect_changed=1,effect_disabled=0,effect_tile_valid=0,blink_clock=0,bank_clock=0,skin_control=0,wily_fade=0;
  uint8_t fixed_object_attributes[512]{},fixed_object_palettes[64]{},fixed_face_index[256]{},fixed_face_records[256]{};
  uint8_t remix=1;
  uint8_t menu=0,menu_valid=0,menu_lcdc=0,menu_wx=7,menu_wy=144,fixed_scene=0,fixed_valid=0,fixed_remix=1;
  uint8_t menu_ram[0x2000]{},menu_vram[0x2000]{},menu_oam[160]{};
  unsigned width=160;
  uint8_t gameplay=0,capture_kind=0,previous=0,pending=0,phase=0,target=0,old_weapon=0;
  uint8_t spawn_skip=0,spawn_column=0;
  uint16_t spawn_columns[256]{};
  unsigned spawn_count=0,spawn_index=0,spawn_scans=0,spawn_reads=0,activation_adjustments=0;
  int actor_x[16]{};
  uint8_t actor_id[16]{},actor_raw[16]{},actor_valid[16]{};
  int shot_x[4]{};
  uint8_t shot_raw[4]{},shot_id[4]{},shot_valid[4]{},current_shot=255;
  uint8_t shot_overlap=0,shot_distance=0;
  uint16_t caller[6]{};
 } s{};
 bool flicker=false,smooth=false,cycling=false,color=false;
 struct Data {std::span<const uint32_t> records;std::span<const uint8_t> original,colored,profiles,bg_pal,obj_pal,attributes,obj_attributes,weapon_pal,extra,face_index,face_records;std::span<const uint16_t> resource_keys;std::span<const uint8_t> resource_attributes,resource_palettes,remix_score;} data;
 int requested=160,width=160;
 std::vector<uint32_t> pixels;
 std::vector<uint32_t> pause_canvas;
 std::vector<uint32_t> framing_background;
 std::vector<uint8_t> background;
 static uint8_t peek(const ModContext& c,unsigned a) {
  if(a<0x4000)return a<c.rom_size?c.rom[a]:0;
  if(a<0x8000){size_t o=size_t(c.rom_bank)*0x4000+a-0x4000;return o<c.rom_size?c.rom[o]:0;}
  if(a>=0xc000&&a<0xd000)return c.wram[a-0xc000];
  if(a>=0xd000&&a<0xe000)return c.wram[size_t(c.wram_bank)*0x1000+a-0xd000];
  if(a>=0xff80&&a<0xffff)return c.hram[a-0xff80];
  return 0;
 }
 static int camera(const ModContext& c){return int(peek(c,0xcf0c))*16+(peek(c,0xff91)&15);}
 void copy_art(const ModContext& c,unsigned source,unsigned destination,unsigned count){
  if(!color||destination<0x8000||destination+count>0x9800||!count||source<0x4000)return;
  unsigned key=c.rom_bank*256+(source>>8);
  if(!(source&255))for(size_t i=0;i<data.resource_keys.size();i++)if(data.resource_keys[i]==key){
   std::copy_n(data.resource_attributes.data()+i*128,128,s.object_attributes+128);
   std::copy_n(data.resource_palettes.data()+i*48,48,s.object_palettes+16);
   if(key!=0x858&&key!=0x968&&key!=0x848)std::fill_n(s.face_index+128,128,0);if(key==0x850)s.face_index[0x91]=s.face_index[0x94]=s.face_index[0x97]=0x84;if(key==0x868){s.face_index[0x85]=s.face_index[0xa4]=0x7c;s.face_index[0x8e]=0x8c;}if(key==0x878)s.face_index[0xfc]=s.face_index[0xe9]=s.face_index[0xdc]=0x94;if(s.effect_profile==1){if(key==0x948)s.background_attributes[0x70]=2;else if(key==0x840||key==0x868||key==0x878)s.background_attributes[0x70]=9;}break;
  }
 #if GBCLLR_MM12_DX_ASSETS
   if(!(source&255))for(size_t i=0;i<std::size(mm12_dx::mm2_boss_resource_keys);i++)if(mm12_dx::mm2_boss_resource_keys[i]==key){
    std::copy_n(mm12_dx::mm2_boss_resource_attributes+i*128,128,s.object_attributes+128);
    unsigned start=mm12_dx::mm2_boss_resource_palette_starts[i];std::copy_n(mm12_dx::mm2_boss_resource_palettes+i*64+start,64-start,s.object_palettes+start);
    for(unsigned tile=0;tile<256;tile++){auto face=mm12_dx::mm2_boss_resource_faces[i*256+tile];if(face!=255)s.face_index[tile]=face;}
    if(mm12_dx::mm2_boss_resource_record_loads[i])std::copy_n(mm12_dx::mm2_boss_resource_records+i*64,64,s.face_records+192);break;
   }
 #endif
  for(size_t i=0;i<data.records.size();i+=4)if(data.records[i]==c.rom_bank&&source>=data.records[i+1]&&uint64_t(source)+count<=data.records[i+1]+data.records[i+2]){
   bool credits_background=c.rom_bank==11&&destination==source+0x1e00;
   auto at=data.records[i+3]+source-data.records[i+1];destination-=0x8000;
   std::copy_n(data.original.data()+at,count,s.dx_source+destination);std::copy_n(data.colored.data()+at,count,s.dx_art+destination);
   // Native5944 reuses the credits OBJ sheet as a background font. Its
   // original7900 clear tile is zero; extra authored OBJ glyphs there must
   // not turn the native background clear into a repeated letter pattern.
   if(credits_background){unsigned begin=std::max(source,0x7900u),end=std::min(source+count,0x7910u);if(begin<end)std::copy_n(data.original.data()+at+begin-source,end-begin,s.dx_art+destination+begin-source);}
   std::fill_n(s.dx_valid+destination,count,1);return;
  }
 }
 const uint8_t* texture(const ModContext& c,unsigned at,unsigned count)const{
  if(color&&s.effect_tile_valid&&at==s.effect_tile_offset&&count==16)return s.effect_tile;
  bool changed=color&&at+count<=0x2000&&std::all_of(s.dx_valid+at,s.dx_valid+at+count,[](uint8_t v){return v!=0;})&&std::equal(c.vram+at,c.vram+at+count,s.dx_source+at);
  return changed?s.dx_art+at:c.vram+at;
 }
 void texture(const ModContext& c,unsigned at,unsigned count,uint8_t* out)const{std::memcpy(out,texture(c,at,count),count);}
 uint32_t rgb(std::span<const uint8_t> palette,unsigned offset,unsigned ink,uint8_t fade)const{
  if(fade==0)return 0xffffffffu;
  if(fade==255)return 0xff000000u;
  unsigned at=offset+ink*2;if(at+1>=palette.size())return 0xff000000u;
  unsigned value=palette[at]|(unsigned(palette[at+1])<<8);return 0xff000000u|((value&31)*255/31<<16)|(((value>>5)&31)*255/31<<8)|(((value>>10)&31)*255/31);
 }
 static std::array<int,2> domain(const ModContext& c){
  int room=std::clamp((camera(c)+80)/160,1,24),l=room,r=room;
  while(l>1&&(peek(c,0xcc00+l)&2))--l;
  while(r<24&&(peek(c,0xcc00+r)&1))++r;
  return {l*160,(r+1)*160};
 }
 int view_left(const ModContext& c)const{
  auto b=domain(c);int left=camera(c)-(width-160)/2;
  return b[1]-b[0]>=width?std::clamp(left,b[0],b[1]-width):b[0]-(width-(b[1]-b[0]))/2;
 }
 // Single-screen domains keep the cartridge's original staging/page rules.
 bool field_active()const{return s.gameplay||s.fixed_scene==6;}
 bool presentation_active()const{return field_active()||s.fixed_scene==11;}
 bool expanded_domain(const ModContext& c)const{auto bounds=domain(c);return width>160&&bounds[1]-bounds[0]>160;}
 int actor_position(const ModContext& c){
  // A locked one-screen room has no adjacent world page. Its native staging
  // coordinate is authoritative even when a recycled projectile slot retains
  // a historical extended-view page from an earlier update.
  if(!expanded_domain(c))return camera(c)+int(peek(c,0xffa5))-8;
  unsigned slot=peek(c,0xcf2f)>>4;uint8_t id=peek(c,0xffa0),raw=uint8_t(camera(c)+peek(c,0xffa5)-8);
  int hint=camera(c)+int(peek(c,0xffa5))-8;
  if(!s.actor_valid[slot]||s.actor_id[slot]!=id){
   unsigned origin=peek(c,0xffa3);if(origin)hint=int(origin)*16+7;
   int candidate=camera(c)+int(peek(c,0xffa5))-8;
   while(candidate-hint>128)candidate-=256;
   while(hint-candidate>128)candidate+=256;
   s.actor_x[slot]=candidate;s.actor_valid[slot]=1;s.actor_id[slot]=id;
  }else s.actor_x[slot]+=int8_t(raw-s.actor_raw[slot]);
  s.actor_raw[slot]=raw;return s.actor_x[slot];
 }
 int contact_position(const ModContext& c,unsigned slot)const{
  unsigned base=0xcd00+slot*16;uint8_t id=peek(c,base),raw=uint8_t(camera(c)+peek(c,base+5)-8);
  if(s.actor_valid[slot]&&s.actor_id[slot]==id)return s.actor_x[slot]+int8_t(raw-s.actor_raw[slot]);
  int world=camera(c)+int(peek(c,base+5))-8,origin=peek(c,base+3),hint=origin?origin*16+7:world;
  while(world-hint>128)world-=256;
  while(hint-world>128)world+=256;
  return world;
 }
 int shot_position(const ModContext& c,unsigned slot,uint8_t relative,uint8_t id){
  int cam=camera(c),world=cam+int(relative)-8;uint8_t raw=uint8_t(world);
  if(!s.shot_valid[slot]||s.shot_id[slot]!=id){
   int player=cam+int(peek(c,0xcf15))-8;
   while(world-player>128)world-=256;
   while(player-world>128)world+=256;
   s.shot_x[slot]=world;s.shot_valid[slot]=1;s.shot_id[slot]=id;
  }else s.shot_x[slot]+=int8_t(raw-s.shot_raw[slot]);
  s.shot_raw[slot]=raw;return s.shot_x[slot];
 }
 static bool owned(const ModContext& c,unsigned w){
  if(!w)return true;
  if(w==12)return peek(c,0xcfdd)&8;
  if(w<4)return peek(c,0xcfdd)&(1u<<(w-1));
  return peek(c,0xcfde)&(1u<<(w-4));
 }
 void capture(ModContext& c,std::span<uint16_t,6> cpu,unsigned pointer,unsigned count,bool flip,int x,int y,uint8_t attr,int world_origin=INT_MIN,unsigned mirror_bias=8){
  if(count>64||pointer<0x4000||pointer+count*4>0x8000)return;
  x-=8;y-=16;
  // Original relative coordinates have a reserved offscreen interval. Native
  // pose coordinates stay signed; individual pieces never wrap modulo 256.
  if(x>=216)x-=256;
  for(unsigned n=0;n<count&&s.count<max_sprites;n++,pointer+=4){
   auto& p=s.building[s.count++];int dx=int8_t(peek(c,pointer+1));
   p.x=(world_origin==INT_MIN?camera(c)+x:world_origin)+(flip?-dx-int(mirror_bias):dx);p.y=y+int8_t(peek(c,pointer));
   p.attributes=uint8_t((peek(c,pointer+3)|(attr&0x90))^(flip?0x20:0));
   unsigned tile=peek(c,pointer+2);
   p.tile=uint8_t(tile);p.authored=0;
   p.color=color?s.object_attributes[((p.attributes&16)?256:0)+tile]:0;
   texture(c,tile*16,16,p.tiles);s.captures++;
   if(color&&s.count<max_sprites){unsigned index=s.face_index[tile];
     if(index&&index+3<std::size(s.face_records)){
      unsigned face=s.face_records[index+2];
     if(face*16+16<=data.extra.size()){
       auto& overlay=s.building[s.count++];overlay.x=p.x+((p.attributes&32)?16-int8_t(s.face_records[index+1]):int8_t(s.face_records[index+1])-16);overlay.y=p.y+int8_t(s.face_records[index]);overlay.attributes=uint8_t(p.attributes|8);overlay.color=s.face_records[index+3];overlay.tile=uint8_t(face);overlay.authored=1;
      std::copy_n(data.extra.data()+face*16,16,overlay.tiles);++s.captures;
     }
    }
   }
  }
  (void)cpu;
 }
 void publish(ModContext& c,bool physical_wait=false){
  auto& f=s.frame;f.valid=uint8_t((presentation_active()||physical_wait)&&c.ppu&&(c.ppu->lcdc&128)&&peek(c,0xff96)==128);
  if(!f.valid)return;
  f.lcdc=c.ppu->lcdc;f.bgp=c.ppu->bgp;f.obp[0]=c.ppu->obp0;f.obp[1]=c.ppu->obp1;
  f.stage=peek(c,0xcf0a);f.room=peek(c,0xcf0b);f.scroll_y=peek(c,0xff90);f.camera=camera(c);
  f.weapon=uint8_t(std::min(12u,unsigned(peek(c,0xcfdf))));
  std::memcpy(f.map,c.wram,sizeof f.map);std::memcpy(f.blocks,c.wram+0xa00,sizeof f.blocks);
  std::memcpy(f.vram,c.vram,sizeof f.vram);std::copy_n(c.ppu->bg_rgb,4,f.bg);std::copy_n(c.ppu->obj_rgb,8,f.obj);
  if(color)for(unsigned tile=0;tile<512;tile++)texture(c,tile*16,16,f.vram+tile*16);
  std::copy_n(s.object_palettes,64,f.object_palettes);
  f.count=s.count;std::copy_n(s.building,s.count,f.sprite);
  if(physical_wait){
   f.camera=0;f.bound_left=0;f.bound_right=160;f.count=0;
   for(unsigned n=0;n<40;++n){const auto* o=c.oam+n*4;if(!o[0]||o[0]>=160)continue;
    auto& piece=f.sprite[f.count++];piece={};piece.x=f.camera+int(o[1])-8;piece.y=int(o[0])-16;piece.attributes=o[3];piece.tile=o[2];
    piece.color=s.object_attributes[((o[3]&16)?256:0)+o[2]];
    unsigned at=s.face_index[o[2]];
    if(color&&at&&at+3<std::size(s.face_records)){
     unsigned tile=s.face_records[at+2];if(tile*16+16>data.extra.size())continue;
     auto& face=f.sprite[f.count++];face={};face.x=piece.x+((o[3]&32)?16-int8_t(s.face_records[at+1]):int8_t(s.face_records[at+1])-16);face.y=piece.y+int8_t(s.face_records[at]);face.attributes=uint8_t(o[3]|8);face.color=s.face_records[at+3];face.tile=uint8_t(tile);face.authored=1;std::copy_n(data.extra.data()+tile*16,16,face.tiles);
    }
   }
  }
  // Raw room locks bind real horizontal neighbors. Their unpacked half-room
  // flags describe where the player may move the camera, not scenery edges.
  // Treating those flags as edges cuts five blocks off a scrolling room.
  if(!physical_wait){auto bounds=domain(c);f.bound_left=bounds[0];f.bound_right=bounds[1];}
 }
 void reset_effects(const ModContext& c){
#if GBCLLR_MM12_DX_ASSETS
  if(!color)return;
  auto stage=std::min(9u,unsigned(peek(c,0xcf0a)));s.effect_profile=data.profiles[stage];s.effect_active=1;s.effect_clock=s.effect_phase=s.effect_disabled=s.effect_tile_valid=0;s.effect_changed=1;s.skin_control=152;s.wily_fade=0;
  std::copy_n(data.bg_pal.data()+s.effect_profile*64,64,s.background_palettes);std::copy_n(data.attributes.data()+s.effect_profile*256,256,s.background_attributes);s.object_attributes[0x6d]=s.object_attributes[0x6e]=s.object_attributes[0x6f]=1;
#else
  (void)c;
#endif
 }
 void queued_style(const ModContext& c,unsigned bank){
#if GBCLLR_MM12_DX_ASSETS
  if(!color)return;unsigned page=peek(c,0xcf08);
  if(bank==11&&page==0x60){std::copy_n(mm12_dx::mm2_wily_background_palettes,40,s.background_palettes);std::copy_n(mm12_dx::mm2_wily_background_attributes,256,s.background_attributes);s.effect_disabled=1;s.effect_tile_valid=0;}
  if(bank==11&&page==0x50){std::copy_n(mm12_dx::mm2_quint_object_attributes,128,s.object_attributes+128);std::copy_n(mm12_dx::mm2_quint_object_palettes,48,s.object_palettes+16);std::copy_n(mm12_dx::mm2_quint_face_indices,128,s.face_index+128);for(auto tile:{0x18,0x19,0x28,0x29})s.background_attributes[tile]=4;for(auto tile:{0x38,0x39,0x48,0x49})s.background_attributes[tile]=3;}
  if(bank==11&&page==0x68){
   std::copy_n(mm12_dx::mm2_wily_object_attributes,128,s.object_attributes+128);std::copy_n(mm12_dx::mm2_wily_object_palettes,48,s.object_palettes+16);std::fill_n(s.face_index+128,128,0);s.face_index[0xfc]=s.face_index[0xe9]=s.face_index[0xdc]=0x94;if(!peek(c,0xcfdf))std::fill_n(s.object_attributes+0x50,12,6);
   bool faces=s.effect_profile!=8||s.effect_disabled;
   if(!faces){if(s.background_palettes[9]==0x34)s.wily_fade=31;if(s.wily_fade==1)faces=true;else if(s.wily_fade>1){--s.wily_fade;std::copy_n(mm12_dx::mm2_wily_fade_colors+s.wily_fade*2,2,s.background_palettes+8);}}
   if(faces){s.face_index[0xa2]=0xc0;s.face_index[0xb2]=0xc4;std::copy_n(mm12_dx::mm2_wily_face_records,16,s.face_records+192);}
  }
#else
  (void)c;(void)bank;
#endif
 }
 void effect_tick(const ModContext& c){
#if GBCLLR_MM12_DX_ASSETS
  if(!color||!c.ppu||!(c.ppu->lcdc&128))return;
  auto bank=uint8_t(s.blink_clock>=0x90?9:0);for(auto tile:{0x34,0x35,0x36,0x37,0x38,0x39,0x70,0x71,0x73})s.object_attributes[tile]=bank;s.blink_clock+=0x60;s.bank_clock+=0x10;s.object_attributes[0x7c]=s.object_attributes[0x7d]=(s.bank_clock&128)?8:0;
  if(s.effect_active&&!s.skin_control)s.object_attributes[0x6d]=s.object_attributes[0x6e]=s.object_attributes[0x6f]=0;
  if(!s.effect_active||s.effect_disabled)return;
  if(s.effect_profile==0){auto room=peek(c,0xcf0b);if(room<32)for(unsigned palette=0;palette<5;palette++)std::copy_n(mm12_dx::mm2_crash_room_colors+room*2,2,s.background_palettes+palette*8);return;}
  unsigned speed=0,palette=0,tile=0;
  switch(s.effect_profile){case 1:speed=8;palette=1;break;case 2:speed=4;tile=1;break;case 3:speed=4;palette=2;break;case 4:speed=4;palette=4;break;case 5:speed=32;palette=3;break;case 8:speed=4;tile=2;break;default:return;}
  if(s.effect_changed){if(palette)std::copy_n(mm12_dx::mm2_stage_effect_palettes+(palette-1)*64+s.effect_phase,16,s.background_palettes+24);else{std::copy_n(mm12_dx::mm2_stage_effect_tiles+(tile-1)*64+s.effect_phase,16,s.effect_tile);s.effect_tile_offset=tile==1?0x1480:0x14b0;s.effect_tile_valid=1;}s.effect_changed=0;}
  s.effect_clock=uint8_t(s.effect_clock+speed);if(!(s.effect_clock&63)){s.effect_phase=uint8_t((s.effect_clock>>6)*16);s.effect_changed=1;}
#else
  (void)c;
#endif
 }
 void first_line(const ModContext& c){
  // Retry has visible native room/beam frames after its loading service has
  // returned, before the gameplay loop starts publishing. Follow that actual
  // PPU/OAM picture rather than replacing the wait with an empty canvas.
  if(!s.gameplay&&!s.menu&&!s.fixed_scene&&c.ppu&&(c.ppu->lcdc&0xa0)==0xa0&&
     c.ppu->wy==128&&peek(c,0xff96)==128&&peek(c,0xcf0b)){
   auto completed=c;publish(completed,true);s.dma=s.frame;
  }
  auto& f=s.dma;if(!f.valid||!c.ppu)return;
  f.lcdc=c.ppu->lcdc;f.scroll_x=c.ppu->scx;f.scroll_y=c.ppu->scy;
  std::copy_n(c.ppu->bg_rgb,4,f.bg);std::copy_n(c.ppu->obj_rgb,8,f.obj);std::copy_n(c.vram,0x2000,f.vram);
  if(color)for(unsigned tile=0;tile<512;tile++)texture(c,tile*16,16,f.vram+tile*16);
  if(color){std::copy_n(s.background_palettes,64,f.background_palettes);std::copy_n(s.background_attributes,256,f.background_attributes);std::copy_n(s.object_palettes,64,f.object_palettes);}
  for(unsigned n=0;n<f.count;n++)if(!f.sprite[n].authored){auto& object=f.sprite[n];object.color=color?s.object_attributes[((object.attributes&16)?256:0)+object.tile]:0;if(color&&(object.color&8)&&object.tile*16+16<=data.extra.size())std::copy_n(data.extra.data()+object.tile*16,16,object.tiles);else texture(c,object.tile*16,16,object.tiles);}
 }
 void menu_frame(const ModContext& c,bool cleared){
  bool visible=color&&!cleared&&c.ppu&&(c.ppu->lcdc&128);s.menu_valid=uint8_t(visible&&s.menu);s.fixed_valid=uint8_t(visible&&s.fixed_scene&&s.fixed_scene!=6&&s.fixed_scene!=11);if(!s.menu_valid&&!s.fixed_valid)return;
  s.fixed_remix=s.remix;
  std::copy_n(s.object_attributes,512,s.fixed_object_attributes);std::copy_n(s.object_palettes,64,s.fixed_object_palettes);std::copy_n(s.face_index,256,s.fixed_face_index);std::copy_n(s.face_records,256,s.fixed_face_records);
  s.menu_lcdc=c.ppu->lcdc;s.menu_wx=c.ppu->wx;s.menu_wy=c.ppu->wy;
  std::copy_n(c.wram,0x2000,s.menu_ram);std::copy_n(c.vram,0x2000,s.menu_vram);std::copy_n(c.oam,160,s.menu_oam);
  for(unsigned tile=0;tile<384;tile++)texture(c,tile*16,16,s.menu_vram+tile*16);
  if(s.menu_valid)for(unsigned cell=0;cell<1024;cell++){
   unsigned tile=c.vram[0x1c00+cell],at=(c.ppu->lcdc&16)?tile*16:unsigned(0x1000+int8_t(tile)*16);
   // The original clears to its white monochrome blank. The authored NES
   // surface clears to the boot's solid bank1 blank instead; otherwise empty
   // icon slots sample unrelated bank1 graphics from the old room.
   if(std::all_of(c.vram+at,c.vram+at+16,[](uint8_t v){return v==0;}))s.menu_vram[0x1c00+cell]=0x73;
  }
 #if GBCLLR_MM12_DX_ASSETS
  if(s.fixed_scene==5){
   // Match the exact original completed pose, then apply only its authored
   // sprite-map data to the host snapshot. Guest OAM and code stay original.
   const auto* before=mm12_dx::mm2_credits_original_map;const auto* after=mm12_dx::mm2_credits_map;unsigned count=before[0];
   for(unsigned slot=0;slot+count<=40;slot++){
    auto* objects=s.menu_oam+slot*4;uint8_t y=uint8_t(objects[0]-before[1]),x=uint8_t(objects[1]-before[2]),flags=objects[3]^before[4];bool matches=true;
    for(unsigned n=0;n<count;n++){const auto* a=objects+n*4;const auto* b=before+1+n*4;if(a[0]!=uint8_t(y+b[0])||a[1]!=uint8_t(x+b[1])||a[2]!=b[2]||a[3]!=(flags^b[3])){matches=false;break;}}
    if(!matches)continue;
    for(unsigned n=0;n<count;n++){auto* a=objects+n*4;const auto* b=after+1+n*4;a[0]=uint8_t(y+b[0]);a[1]=uint8_t(x+b[1]);a[2]=b[2];a[3]=flags^b[3];}break;
   }
  }
  if(s.fixed_scene==4&&std::equal(std::begin(mm12_dx::mm2_password_original_cursor),std::end(mm12_dx::mm2_password_original_cursor),s.menu_vram))std::copy_n(mm12_dx::mm2_password_cursor,16,s.menu_vram);
 #endif
 }
 ModView fixed_view(std::span<const uint32_t> native){
 #if GBCLLR_MM12_DX_ASSETS
  if(!s.fixed_valid)return {160,false,native};
  bool space=s.fixed_scene==5,award=s.fixed_scene==7,castle=s.fixed_scene==8,station=s.fixed_scene==9,gameover=s.fixed_scene==10,select=s.fixed_scene>=2&&s.fixed_scene<=4;std::array<uint8_t,2048> map{};std::copy_n(s.menu_vram+0x1800,2048,map.begin());
  const auto* original=castle?mm12_dx::mm2_castle_original_map:station?mm12_dx::mm2_station_original_map:award?mm12_dx::mm2_getweapon_original_map:s.fixed_scene==4?mm12_dx::mm2_password_original_map:select?mm12_dx::mm2_select_original_map:mm12_dx::mm2_title_original_map;const auto* authored=castle?mm12_dx::mm2_castle_map:station?mm12_dx::mm2_station_map:award?mm12_dx::mm2_getweapon_map:s.fixed_scene==4?mm12_dx::mm2_password_map:select?mm12_dx::mm2_select_map:mm12_dx::mm2_title_map;
  if(!space&&!gameover)for(unsigned cell=0;cell<(select&&s.fixed_scene!=4?2048u:1024u);cell++)if(map[cell]==original[cell])map[cell]=authored[cell];
  // Author BE00's finite soundtrack label cells. Native soundtrack selection
  // is retained by the independent native remix service.
  if(s.fixed_scene==1)for(unsigned cell=0;cell<4;cell++){map[cell]=uint8_t(0x5c+cell);map[16+cell]=mm12_dx::mm2_title_music_labels[(s.fixed_remix?0:4)+cell];}
  auto asset=[&](unsigned bank,unsigned address,unsigned count)->size_t{
   for(size_t i=0;i<data.records.size();i+=4)if(data.records[i]==bank&&address>=data.records[i+1]&&uint64_t(address)+count<=data.records[i+1]+data.records[i+2])return data.records[i+3]+address-data.records[i+1];
   return SIZE_MAX;
  };
  auto font=asset(11,0x7400,64),registered_glyph=asset(11,0x7900,16);
  bool credits_font=space&&font!=SIZE_MAX&&registered_glyph!=SIZE_MAX&&std::equal(s.menu_vram+0x1200,s.menu_vram+0x1240,data.colored.data()+font);
  pixels.resize(160*144);background.assign(160*144,0);
  for(unsigned y=0;y<144;y++)for(unsigned x=0;x<160;x++){
   const auto& r=s.display_raster[y];bool window=(r.lcdc&32)&&y>=s.menu_wy&&x+7>=s.menu_wx;
   unsigned wx=window?x+7-s.menu_wx:(x+r.scx)&255,wy=window?y-s.menu_wy:(y+r.scy)&255,base=window?((r.lcdc&64)?0x1c00:0x1800):((r.lcdc&8)?0x1c00:0x1800),cell=(wy>>3)*32+(wx>>3),map_cell=base-0x1800+cell;bool registered=credits_font&&map_cell==0x32f&&map[map_cell]==0x3c;unsigned tile=registered?0x70:map[map_cell],attr=castle?mm12_dx::mm2_castle_attributes[tile]:station?mm12_dx::mm2_station_attributes[tile]:gameover?mm12_dx::mm2_select_attributes[tile]:award?mm12_dx::mm2_getweapon_attributes[tile]:space?mm12_dx::mm2_space_background_attributes[tile]:select?mm12_dx::mm2_select_attributes[tile]:mm12_dx::mm2_title_attributes[tile];
   unsigned at=(r.lcdc&16)?tile*16:unsigned(0x1000+int8_t(tile)*16),row=(wy&7)*2,bit=7-(wx&7);
   // Authored1:6206 changes the registered-mark cell9B2F from3C to70.
   // That explicit glyph uses authored data; ordinary clear cells stay blank.
   const auto* image=registered?data.colored.data()+registered_glyph:s.menu_vram+at;unsigned ink=((image[row]>>bit)&1)|(((image[row+1]>>bit)&1)<<1);background[y*160+x]=uint8_t(ink);
   // Author BD[C0]=1 selects the second complete64-byte title palette;
   // native E4 uses the first. Preserve its real random title flashes.
   auto palette=castle?std::span<const uint8_t>(mm12_dx::mm2_castle_palettes+(r.bgp==0xc0?64:0),64):station?std::span<const uint8_t>(mm12_dx::mm2_station_palettes):gameover?std::span<const uint8_t>(mm12_dx::mm2_select_palettes):award?std::span<const uint8_t>(mm12_dx::mm2_getweapon_palettes):space?std::span<const uint8_t>(mm12_dx::mm2_space_background_palettes):select?(r.bgp==0xd8?std::span<const uint8_t>(mm12_dx::mm2_select_flash_palettes):std::span<const uint8_t>(mm12_dx::mm2_select_palettes)):(r.bgp==0xc0?std::span<const uint8_t>(mm12_dx::mm2_title_flash_palettes):std::span<const uint8_t>(mm12_dx::mm2_title_palettes));pixels[y*160+x]=rgb(palette,(attr&7)*8,ink,space||castle||station||award?0xe4:r.bgp);
  }
  auto draw=[&](int ox,int oy,unsigned tile,uint8_t flags,const uint8_t* image,unsigned palette){
   for(int yy=0;yy<16;yy++){int y=oy+yy;if(y<0||y>=144)continue;const auto& r=s.display_raster[y];unsigned height=(r.lcdc&4)?16:8;if(!(r.lcdc&2)||unsigned(yy)>=height)continue;unsigned row=unsigned((flags&64)?int(height)-1-yy:yy)*2;const auto* tiles=image-(height==16?(tile&1)*16:0);
    for(int xx=0;xx<8;xx++){int x=ox+xx;if(x<0||x>=160)continue;unsigned bit=unsigned((flags&32)?xx:7-xx),ink=((tiles[row]>>bit)&1)|(((tiles[row+1]>>bit)&1)<<1);if(!ink||((flags&128)&&background[y*160+x]))continue;auto colors=castle||station?std::span<const uint8_t>(s.fixed_object_palettes):award?std::span<const uint8_t>(mm12_dx::mm2_getweapon_object_palettes):s.fixed_scene==3||space?std::span<const uint8_t>(s.fixed_object_palettes):select?std::span<const uint8_t>(mm12_dx::mm2_select_object_palettes):std::span<const uint8_t>(mm12_dx::mm2_title_object_palettes);pixels[y*160+x]=rgb(colors,palette*8,ink,space||castle||station||award?0xe4:r.obp[(flags>>4)&1]);}
   }
  };
  // Native author allocations place added face pieces in free OAM tail slots,
  // below the original actor pieces. Preserve the complete logical faces.
  if(s.fixed_scene==3||space)for(unsigned n=0;n<40;n++){const auto* o=s.menu_oam+n*4;unsigned index=s.fixed_face_index[o[2]];
   if(index&&index+3<std::size(s.fixed_face_records)){const auto* f=s.fixed_face_records+index;unsigned face=f[2];if(face*16+32<=data.extra.size())draw(int(o[1])-8+((o[3]&32)?16-int8_t(f[1]):int8_t(f[1])-16),int(o[0])-16+int8_t(f[0]),face,uint8_t(o[3]|8),data.extra.data()+face*16,f[3]&7);}
  }
  for(unsigned n=40;n-->0;){const auto* o=s.menu_oam+n*4;unsigned tile=o[2],palette=s.fixed_scene==3||space||castle||station?s.fixed_object_attributes[((o[3]&16)?256:0)+tile]&7:select&&tile==0x3f&&(o[3]&16)?2:select&&(tile==0x26||tile==0x27)?1:0;const uint8_t* image=s.menu_vram+tile*16;if((s.fixed_scene==3||space||castle||station)&&(s.fixed_object_attributes[((o[3]&16)?256:0)+tile]&8)&&tile*16+16<=data.extra.size())image=data.extra.data()+tile*16;draw(int(o[1])-8,int(o[0])-16,tile,o[3],image,palette);}
  return {160,false,pixels};
 #else
  return {160,false,native};
 #endif
 }
 ModView menu_view(std::span<const uint32_t> native){
#if GBCLLR_MM12_DX_ASSETS
  if(!s.menu_valid||!(s.menu_lcdc&32)||s.menu_wx!=7)return {160,false,native};
  unsigned selected=std::min(13u,unsigned(s.menu_ram[0xfdf])),stage=std::min(9u,unsigned(s.menu_ram[0xf0a])),profile=data.profiles[stage];
  std::array<uint8_t,1024> map{},attributes{};std::copy_n(s.menu_vram+0x1c00,1024,map.begin());
  for(unsigned row=3;row<18;row++)std::copy_n(mm12_dx::mm2_menu_attributes,20,attributes.begin()+row*32);
  // Original ownership is the sole availability source. The native author
  // service adds2x2 icons while retaining the original names and ammo bars.
  auto available=[&](unsigned weapon){if(!weapon)return true;if(weapon<4)return (s.menu_ram[0xfdd]&(1u<<(weapon-1)))!=0;if(weapon<12)return (s.menu_ram[0xfde]&(1u<<(weapon-4)))!=0;if(weapon==12)return (s.menu_ram[0xfdd]&8)!=0;return false;};
  auto original_map=map;
  // Author3B2B moves each right-column native name+five-tile bar one cell.
  // Use actual original dynamic bars/flashes, then the authored finite data.
  for(unsigned weapon=0;weapon<14;weapon++)if(available(weapon)||weapon==13){
   const auto* before=mm12_dx::mm2_menu_original_cells+weapon*4;const auto* after=mm12_dx::mm2_menu_cells+weapon*4;
   unsigned source=(unsigned(before[0])<<8|before[1])-0x9c00,destination=(unsigned(after[0])<<8|after[1])-0x9c00;
   if(source+7>1024||destination+7>1024)throw std::runtime_error("Invalid authored pause cell binding");
   std::copy_n(original_map.begin()+source,7,map.begin()+destination);
   for(unsigned glyph=0;glyph<2;glyph++){
    unsigned tile=original_map[source+glyph];if(tile==before[glyph+2])tile=after[glyph+2];else if(tile==0x70)tile=0x73;
    map[destination+glyph]=uint8_t(tile);
   }
   if(weapon==13)for(unsigned glyph=2;glyph<7;glyph++)if(map[destination+glyph]==0x70)map[destination+glyph]=0x73;
  }
  unsigned cell=0x60;
  for(unsigned weapon=0;weapon<14;weapon++){
   if(available(weapon)||weapon==13){unsigned tile=mm12_dx::mm2_hud_icons[weapon];map[cell]=uint8_t(tile);map[cell+1]=uint8_t(tile+1);map[cell+32]=uint8_t(tile+16);map[cell+33]=uint8_t(tile+17);}
   if(weapon==selected){attributes[cell]=attributes[cell+1]=attributes[cell+32]=attributes[cell+33]=14;std::fill_n(attributes.begin()+cell+2,8,6);}
   cell+=mm12_dx::mm2_menu_slot_steps[weapon];
  }
  // The native status band uses the authored per-tile attributes as well.
  for(unsigned row=0;row<2;row++)for(unsigned x=0;x<20;x++){unsigned cell=row*32+x;if(map[cell]==0x5d)map[cell]=0x73;attributes[cell]=data.attributes[profile*256+map[cell]];}
  // Native7600 also updates the real top status band for the selected entry.
  unsigned icon=mm12_dx::mm2_hud_icons[selected];map[5]=uint8_t(icon);map[6]=uint8_t(icon+1);map[37]=uint8_t(icon+16);map[38]=uint8_t(icon+17);
  attributes[5]=attributes[6]=attributes[37]=attributes[38]=14;
  for(unsigned row=0;row<2;row++)for(unsigned x=0;x<5;x++)attributes[row*32+x]=6;
  for(unsigned row=0;row<2;row++)for(unsigned x=7;x<20;x++)attributes[row*32+x]=uint8_t((x==12||x==13)&&row==0?6:7);
  for(unsigned row=0;row<2;row++)for(unsigned x=0;x<20;x++)if(map[row*32+x]==0x5d)map[row*32+x]=0x73;
  pixels.resize(160*144);background.resize(160*144);
  for(unsigned y=0;y<144;y++)for(unsigned x=0;x<160;x++){
   const auto& raster=s.display_raster[y];bool window=(raster.lcdc&32)&&y>=s.menu_wy;
   unsigned wx=window?x:(x+raster.scx)&255,wy=window?y-s.menu_wy:(y+raster.scy)&255,cell=(wy>>3)*32+(wx>>3),base=window?((raster.lcdc&64)?0x1c00:0x1800):((raster.lcdc&8)?0x1c00:0x1800);
   unsigned tile=base==0x1c00?map[cell]:s.menu_vram[base+cell],attr=base==0x1c00?attributes[cell]:data.attributes[profile*256+tile];
   unsigned at=(raster.lcdc&16)?tile*16:unsigned(0x1000+int8_t(tile)*16),line=(wy&7)*2,bit=7-(wx&7);bool blank=base==0x1c00&&(tile==0x73||tile==0x5d);const auto* image=(attr&8)||blank?data.extra.data()+at:s.menu_vram+at;
   unsigned ink=((image[line]>>bit)&1)|(((image[line+1]>>bit)&1)<<1);
   background[y*160+x]=uint8_t(ink);
   bool portrait=base==0x1c00&&cell<64&&((cell&31)==12||(cell&31)==13);pixels[y*160+x]=(attr&7)==6&&!portrait?rgb(mm12_dx::mm2_menu_palettes,selected*16+8,ink,raster.bgp):rgb(data.bg_pal,profile*64+(attr&7)*8,ink,raster.bgp);
  }
  // Original partial-menu scrolling retains the actual player OAM. Keep it
  // until the original per-line LCDC policy hides it behind the opened menu.
  for(unsigned n=0;n<40;n++){const auto* o=s.menu_oam+n*4;unsigned index=s.face_index[o[2]];if(!index||index+3>=256)continue;const auto* face=s.face_records+index;unsigned tile=face[2];if(tile*16+16>data.extra.size())continue;
   int ox=int(o[1])-8+((o[3]&32)?16-int8_t(face[1]):int8_t(face[1])-16),oy=int(o[0])-16+int8_t(face[0]);auto image=data.extra.data()+tile*16;
   for(int dy=0;dy<8;dy++){int y=oy+dy;if(y<0||y>=144)continue;const auto& raster=s.display_raster[y];if(!(raster.lcdc&2))continue;unsigned line=unsigned((o[3]&64)?7-dy:dy)*2;
    for(int dx=0;dx<8;dx++){int x=ox+dx;if(x<0||x>=160)continue;unsigned bit=unsigned((o[3]&32)?dx:7-dx),ink=((image[line]>>bit)&1)|(((image[line+1]>>bit)&1)<<1);if(!ink||((o[3]&128)&&background[y*160+x]))continue;pixels[y*160+x]=rgb(s.object_palettes,(face[3]&7)*8,ink,0xe4);}
   }
  }
  for(unsigned n=40;n-->0;){const auto* object=s.menu_oam+n*4;int ox=int(object[1])-8,oy=int(object[0])-16;const auto* image=s.menu_vram+object[2]*16;unsigned palette=s.object_attributes[((object[3]&16)?256:0)+object[2]]&7;
   for(int dy=0;dy<8;dy++){int y=oy+dy;if(y<0||y>=144)continue;const auto& raster=s.display_raster[y];if(!(raster.lcdc&2))continue;unsigned line=unsigned((object[3]&64)?7-dy:dy)*2;
    for(int dx=0;dx<8;dx++){int x=ox+dx;if(x<0||x>=160)continue;unsigned bit=unsigned((object[3]&32)?dx:7-dx),ink=((image[line]>>bit)&1)|(((image[line+1]>>bit)&1)<<1);if(!ink||((object[3]&128)&&background[y*160+x]))continue;pixels[y*160+x]=palette?rgb(s.object_palettes,palette*8,ink,raster.obp[(object[3]>>4)&1]):rgb(mm12_dx::mm2_menu_palettes,selected*16,ink,raster.obp[(object[3]>>4)&1]);}
   }
  }
  return {160,false,pixels};
#else
  return {160,false,native};
#endif
 }
 static unsigned texel(const uint8_t* tiles,unsigned tile,unsigned x,unsigned y,bool unsigned_tiles){
  int a=unsigned_tiles?int(tile)*16:0x1000+int(int8_t(tile))*16;
  return ((tiles[a+y*2]>>(7-x))&1)|(((tiles[a+y*2+1]>>(7-x))&1)<<1);
 }
 void push(std::span<uint16_t,6> cpu,std::span<uint8_t> ram,uint16_t v){cpu[4]-=2;ram[cpu[4]-0xc000]=uint8_t(v);ram[cpu[4]-0xc000+1]=uint8_t(v>>8);}
 bool spawn_next(const ModContext& c,std::span<uint16_t,6> cpu,std::span<uint8_t> ram){
  bool free=false;for(unsigned n=0;n<16;n++)if(!ram[0xd00+n*16])free=true;
  if(!free)return false;
  while(s.spawn_index<s.spawn_count){
   unsigned col=s.spawn_columns[s.spawn_index++],flags=peek(c,0xc800+col);++s.spawn_reads;
   if((flags&0x90)!=0x10)continue;
   s.spawn_column=uint8_t(col);std::copy_n(s.caller,6,cpu.begin());
   cpu[1]=uint16_t((uint8_t(int(col)*16+7-camera(c))<<8)|(cpu[1]&255));cpu[3]=uint16_t(0xc800+col);
   push(cpu,ram,0x27f8);cpu[5]=0x1c56;s.phase=4;++s.activation_adjustments;return true;
  }
  return false;
 }
};
Mm2NativeMods::Mm2NativeMods(std::span<const NativeFeature> features,bool):impl_(std::make_unique<Impl>()){
#if GBCLLR_MM12_DX_ASSETS
 using namespace mm12_dx;impl_->data={mm2_records,mm2_original,mm2_colored,mm2_profiles,mm2_background_palettes,mm2_object_palettes,mm2_stage_attributes,mm2_object_attributes,mm2_weapon_palettes,mm2_extra_art,mm2_face_index,mm2_face_records,mm2_resource_keys,mm2_resource_attributes,mm2_resource_palettes,mm2_remix_score};
#endif
 for(const auto& f:features){
  if(f.plugin=="gbcllr.mm2.adaptive-view"){impl_->requested=f.view_width;impl_->width=f.view_width<0?256:f.view_width;impl_->flicker=f.no_flicker;impl_->smooth=f.smooth_gameplay;impl_->cycling=f.weapon_cycle;}
  if(f.colorization&&f.asset_input!=GBCLLR_MM12_DX_ASSET_ID)throw std::runtime_error("Mega Man II DX presentation asset pin mismatch");
  impl_->color|=f.colorization;
 }
 if(impl_->color&&impl_->data.records.empty())throw std::runtime_error("Mega Man II DX requires its pinned native presentation assets");
 impl_->s.width=unsigned(impl_->width);
}
Mm2NativeMods::~Mm2NativeMods()=default;
uint8_t Mm2NativeMods::read(ModContext& c,uint16_t a,uint8_t v){
 auto& p=*impl_;
 // Domain score data only, read by verified original bank7 driver instructions.
 // Never override an opcode/operand fetch or the excluded guest trampolines.
 if(p.color&&p.s.remix&&c.sp!=UINT16_MAX&&c.rom_bank==7&&c.instruction_pc>=0x4000&&c.instruction_pc<0x44db&&a>=0x44db&&a<0x8000&&!(a>=0x7460&&a<0x74d0)){
  unsigned source=7*0x4000+c.instruction_pc-0x4000;if(source>=c.rom_size)return v;
  unsigned opcode=c.rom[source];
  if(opcode==0x2a||opcode==0x7e||opcode==0x1a||opcode==0x66||opcode==0x56||opcode==0x5e||opcode==0x46||opcode==0x4e)return p.data.remix_score[a-0x44db];
 }
 // Expand only the renderer's capacity queries. Let every original actor
 // producer run, reusing the bounded hardware mirror if it is already full;
 // the compositor retains all complete native mappings independently.
 if(p.field_active()&&c.sp!=UINT16_MAX&&a==0xff97&&v>=160&&
    ((c.rom_bank==2&&c.instruction_pc==0x4031)||c.instruction_pc==0x249c)&&(p.flicker||p.expanded_domain(c)))return 0;
 return v;
}
void Mm2NativeMods::write(ModContext&,uint16_t address,uint8_t value){
 // Match the list selected by original ISR023C, before CPU-only headroom can
 // complete a newer update in this same VBlank interval.
 if(address==0xff46&&value==0xdf)impl_->s.dma=impl_->s.frame;
}
void Mm2NativeMods::inputs(uint8_t a,uint8_t b){
 if((a|b)&~3u)throw std::runtime_error("Invalid Mega Man virtual inputs");
 auto& s=impl_->s;
 if(!impl_->cycling||!s.gameplay||a==3||(s.phase&&s.phase!=4))s.pending=0;
 else s.pending|=uint8_t(a&~s.previous);
 s.previous=a;
}
void Mm2NativeMods::instruction(ModContext& c,std::span<uint16_t,6> cpu,std::span<uint8_t> ram,std::span<uint8_t> hram){
 auto& p=*impl_;auto& s=p.s;unsigned pc=cpu[5];
 if(pc==0x03cc){s.effect_active=0;s.effect_tile_valid=0;}
 if(pc==0x04e0)p.reset_effects(c);
 if(pc==0x3605)s.skin_control=0;
 if(pc==0x02df){p.queued_style(c,cpu[0]>>8);if(p.color&&s.gameplay&&s.fixed_scene==0&&(cpu[0]>>8)==11&&Impl::peek(c,0xcf08)==0x50)hram[0x18]=5;}
 if(pc==0x39a0||pc==0x2ac7){std::fill_n(s.shot_valid,4,0);s.current_shot=255;s.shot_overlap=0;}
 if(pc==0x05d2){s.fixed_scene=5;s.gameplay=0;s.frame.valid=s.dma.valid=s.display.valid=0;s.pending=0;
 #if GBCLLR_MM12_DX_ASSETS
  if(p.color){std::copy_n(mm12_dx::mm2_space_object_attributes,256,s.object_attributes);std::copy_n(mm12_dx::mm2_space_object_palettes,64,s.object_palettes);std::fill_n(s.face_index,256,0);std::copy_n(mm12_dx::mm2_space_face_records,24,s.face_records+0xc8);s.face_index[0xa7]=0xc8;s.face_index[0xad]=0xcc;s.face_index[0xae]=0xd0;}
 #endif
 }
 if(pc==0x375c)s.menu=1;
 if(pc==0x2b99)s.fixed_scene=1;
 if(pc==0x2c0c){s.fixed_scene=2;s.gameplay=0;s.frame.valid=s.dma.valid=s.display.valid=0;s.pending=0;
 #if GBCLLR_MM12_DX_ASSETS
  if(p.color){std::copy_n(p.data.obj_attributes.data(),512,s.object_attributes);std::copy_n(mm12_dx::mm2_select_object_palettes,64,s.object_palettes);std::copy_n(p.data.face_index.data(),256,s.face_index);std::copy_n(p.data.face_records.data(),256,s.face_records);s.object_attributes[0x26]=s.object_attributes[0x27]=1;s.object_attributes[256+0x3f]=2;}
 #endif
 }
 if(pc==0x2d15)s.fixed_scene=3;
 if(c.rom_bank==1&&(pc==0x4cdd||pc==0x4f69)){s.fixed_scene=4;s.gameplay=0;s.frame.valid=s.dma.valid=s.display.valid=0;s.pending=0;}
 if(pc==0x2c0b||pc==0x2ac7||pc==0x2862){s.fixed_scene=0;s.fixed_valid=0;}
 if(pc==0x27f8||pc==0x2ac7||pc==0x2862||pc==0x299f){s.menu=0;s.menu_valid=0;}
 if(p.color&&pc==0x2bf3&&(hram[0x0b]&4)){
  s.remix^=1;hram[0x0b]&=uint8_t(~4u);hram[0x18]=hram[0x1a];hram[0x1a]=255;
 }
 if(p.color&&s.remix&&c.rom_bank==7){
  if(pc==0x403c||pc==0x404c){cpu[0]=uint16_t((cpu[0]&255)|((pc==0x403c?0x1a:3)<<8));cpu[5]+=2;return;}
  if(pc==0x41a9||pc==0x4282||pc==0x439a){
   unsigned track=hram[0x1a],value=cpu[0]>>8;
   bool lower=pc==0x41a9?(track!=5&&track!=12):pc==0x4282?(track!=5&&track!=11&&track!=12):(track==2||track==3||track==5);
   if(lower)cpu[0]=uint16_t(((value>=2?value-1:value)<<8)|(value<2?16:0));
  }
 }
 if(pc==0x06b9)p.copy_art(c,cpu[3],cpu[2],cpu[1]);
 if(pc==0x0307)p.copy_art(c,cpu[3],cpu[2],(cpu[1]>>8)*16);
 // Boss-victory rendering uses the original2A97 wait/update loop. Reuse
 // world/pose presentation there while CPU headroom and controls stay inactive.
 if(pc==0x053c||pc==0x0567||pc==0x0586){
  s.fixed_scene=p.color?(pc==0x053c?8:pc==0x0567?9:10):0;s.fixed_valid=0;s.gameplay=0;s.pending=0;s.frame.valid=s.dma.valid=s.display.valid=0;
 #if GBCLLR_MM12_DX_ASSETS
  if(p.color&&pc==0x053c){std::copy_n(mm12_dx::mm2_castle_object_attributes,128,s.object_attributes);std::copy_n(mm12_dx::mm2_castle_object_palettes,64,s.object_palettes);std::fill_n(s.face_index,256,0);}
 #endif
 }
 if(pc==0x0508){s.fixed_scene=7;s.fixed_valid=0;s.gameplay=0;s.pending=0;s.frame.valid=s.dma.valid=s.display.valid=0;}
 if(pc==0x299f){s.fixed_scene=6;s.fixed_valid=0;s.gameplay=0;s.pending=0;s.spawn_skip=0;s.current_shot=255;s.shot_overlap=0;std::fill_n(s.shot_valid,4,0);std::fill_n(s.actor_valid,16,0);}
 if(pc==0x2a97&&s.fixed_scene==6)p.publish(c);
 if(pc==0x2a9b&&s.fixed_scene==6){s.count=0;s.capture_kind=0;}
 if(pc==0x03cc&&(s.fixed_scene==6||s.fixed_scene==7)){s.fixed_scene=0;s.fixed_valid=0;s.frame.valid=0;}
 if(pc==0x2825&&s.gameplay){++s.logic_updates;p.publish(c);}
 if(pc==0x2862){
  // The original death controller keeps the room, shots and actors visible
  // while it animates two large explosions. It has its own frame wait and
  // must not retain the last live player pose or lose its completed picture.
  s.fixed_scene=11;s.fixed_valid=0;s.gameplay=0;s.pending=0;s.spawn_skip=0;
  s.count=0;s.capture_kind=0;
 }
 if(pc==0x0008&&s.fixed_scene==11){
  // This controller calls RST08 after completing its previous native work.
  // The first wait still owns the preceding gameplay OAM; subsequent waits
  // publish full logical effect lists before the original ISR selects DMA.
  if(s.count||!Impl::peek(c,0xdf00))p.publish(c);
  s.count=0;s.capture_kind=0;
 }
 if(pc==0x375c||pc==0x2ac7){s.gameplay=0;s.frame.valid=0;s.pending=0;s.spawn_skip=0;if(pc==0x2ac7){std::fill_n(s.actor_valid,16,0);
    if(p.color){unsigned stage=std::min(9u,unsigned(Impl::peek(c,0xcf0a))),profile=p.data.profiles[stage];std::copy_n(p.data.obj_attributes.data(),512,s.object_attributes);std::copy_n(p.data.obj_pal.data()+profile*64,64,s.object_palettes);std::copy_n(p.data.face_index.data(),256,s.face_index);std::copy_n(p.data.face_records.data(),256,s.face_records);}
  }}
 if(s.phase){
  if(s.phase==2&&pc==0x3952){cpu[5]=0x20;s.phase=3;return;}
  if(pc==0x27f8&&cpu[4]==s.caller[4]){
   if(s.phase==4){
    unsigned slot=(cpu[3]&255)>>4;
    if((cpu[3]&0xff00)==0xcd00&&ram[0xd00+slot*16]){
     s.actor_x[slot]=int(s.spawn_column)*16+7;s.actor_valid[slot]=1;s.actor_id[slot]=ram[0xd00+slot*16];s.actor_raw[slot]=uint8_t(s.actor_x[slot]);
    }
    if(p.spawn_next(c,cpu,ram))return;
    std::copy_n(s.caller,6,cpu.begin());s.phase=0;s.spawn_skip=1;std::fill_n(s.caller,6,0);
   }else if(s.phase==1){
    ram[0xfdf]=s.target;p.push(cpu,ram,0x27f8);p.push(cpu,ram,uint16_t(s.old_weapon)<<8);cpu[5]=0x38e0;s.phase=2;
   }else{std::copy_n(s.caller,6,cpu.begin());s.phase=0;std::fill_n(s.caller,6,0);}
   return;
  }
 }
 if(pc==0x27f8&&!s.phase){
  s.gameplay=1;s.count=0;s.capture_kind=0;
  unsigned edge=s.pending;s.pending=0;
  if(p.cycling&&(edge==1||edge==2)&&ram.size()>=0x2000&&ram[0xfd0]&&!ram[0xf60]&&!ram[0xf00]&&cpu[4]>=0xc040&&cpu[4]<=0xe000){
   unsigned current=ram[0xfdf],next=current;
   if(current<13){do{next=(next+(edge==1?12:1))%13;}while(next!=current&&!Impl::owned(c,next));
    if(next!=current){std::copy(cpu.begin(),cpu.end(),s.caller);s.target=uint8_t(next);s.old_weapon=uint8_t(current);s.phase=1;p.push(cpu,ram,0x27f8);cpu[5]=0x39ef;return;}}
  }
  if(s.spawn_skip)s.spawn_skip=0;
  else if(p.expanded_domain(c)&&ram.size()>=0x2000&&!ram[0xf00]&&!ram[0xf60]&&cpu[4]>=0xc040&&cpu[4]<=0xe000){
   for(unsigned n=0;n<16;n++)if(!ram[0xd00+n*16])s.actor_valid[n]=0;
   auto bounds=Impl::domain(c);int left=p.view_left(c),right=left+p.width,cam=Impl::camera(c);s.spawn_count=s.spawn_index=0;++s.spawn_scans;
   int first=std::max({0,bounds[0]/16,left/16}),last=std::min({256,(bounds[1]+15)/16,(right+15)/16});
   for(int col=first;col<last;col++)if(col*16<cam||col*16>=cam+160)s.spawn_columns[s.spawn_count++]=uint16_t(col);
   if(s.spawn_count){std::copy(cpu.begin(),cpu.end(),s.caller);if(p.spawn_next(c,cpu,ram))return;std::fill_n(s.caller,6,0);}
  }
 }
 if(!p.presentation_active())return;
 if(s.gameplay){
 if(c.rom_bank==1&&pc==0x43e4){
  unsigned base=cpu[3];s.current_shot=255;
  if(base>=0xcc60&&base<=0xcc90&&!(base&15)){unsigned slot=(base-0xcc60)/16;s.current_shot=uint8_t(slot);p.shot_position(c,slot,Impl::peek(c,base+5),Impl::peek(c,base));}
 }
 if(c.rom_bank==1&&pc==0x47bb&&s.current_shot<4){
  int world=p.shot_position(c,s.current_shot,Impl::peek(c,0xffa5),Impl::peek(c,0xffa0));
  // Only the ordinary Buster receives the expanded viewport cull. Keep all
  // authored timers, return distances and other weapon-specific services.
  if(p.expanded_domain(c)&&!Impl::peek(c,0xcfdf)){
   auto bounds=Impl::domain(c);int left=std::max(bounds[0],p.view_left(c))-16,right=std::min(bounds[1],p.view_left(c)+p.width);
   cpu[5]=world>=left&&world<right?0x47c3:0x4812;return;
  }
 }
 if(c.rom_bank==1&&pc==0x4406){if(s.current_shot<4&&!Impl::peek(c,0xffa0))s.shot_valid[s.current_shot]=0;s.current_shot=255;s.shot_overlap=0;}
 if(pc==0x33ad&&s.shot_overlap){cpu[0]=uint16_t((cpu[0]&255)|(unsigned(s.shot_distance)<<8));s.shot_overlap=0;}
 if(pc==0x3394&&(cpu[2]&0xff0f)==0xcd00&&p.expanded_domain(c)){
  s.shot_overlap=0;
  unsigned slot=(cpu[2]&255)>>4;int relative=Impl::peek(c,0xffa5);if(relative>=248)relative-=256;
  int shot=s.current_shot<4&&s.shot_valid[s.current_shot]?s.shot_x[s.current_shot]:Impl::camera(c)+relative-8;
  unsigned radius=Impl::peek(c,0xce00+slot*16)+Impl::peek(c,0xcfec);
  // This loop visits CD slots before loading their HRAM workspace. Keep its
  // original Y/damage services, rejecting only a false modulo256 X overlap.
  unsigned distance=unsigned(std::abs(p.contact_position(c,slot)-shot));
  if(distance>radius){cpu[5]=0x33cf;return;}
  if(s.current_shot<4&&s.shot_valid[s.current_shot]){s.shot_distance=uint8_t(std::min(distance,255u));s.shot_overlap=1;}
 }
 if(pc==0x357f&&(cpu[2]&0xff0f)==0xcd00&&p.expanded_domain(c)){
  unsigned slot=(cpu[2]&255)>>4;int player=Impl::camera(c)+int(Impl::peek(c,0xcf15))-8;
  unsigned radius=Impl::peek(c,0xce00+slot*16)+7;
  // Resolve the original slot directly: this loop runs before ActS_Do has
  // copied that actor into HRAM. Do not use the previous actor's HRAM origin.
  if(unsigned(std::abs(p.contact_position(c,slot)-player))>radius){cpu[5]=0x35b3;return;}
 }
 // Actor terrain sensors share an eight-bit screen coordinate with the
 // original renderer. Use the retained world position for the column; keep
 // the original Y, block-type and half-platform collision code intact.
 if(pc==0x3334&&c.rom_bank==2&&p.expanded_domain(c)){
  int world=p.actor_position(c)+int8_t(Impl::peek(c,0xcf0d)-Impl::peek(c,0xffa5));auto bounds=Impl::domain(c);
  if(world>=bounds[0]&&world<bounds[1]&&world>=0&&world<4096){cpu[1]=uint16_t((cpu[1]&0xff00)|(world>>4));cpu[5]=0x334f;++s.activation_adjustments;return;}
 }
 // Original actor offscreen guards reserve theD0 interval in an eight-bit
 // relative coordinate. Use retained world coordinates for these exact guards
 // so a visible margin actor survives without wrapping onto the other side.
 if((pc==0x216a||pc==0x217c||pc==0x21aa||pc==0x21ca||pc==0x242e)&&p.expanded_domain(c)){
  int world=p.actor_position(c);
  if(pc==0x21aa||pc==0x21ca)world+=int8_t(Impl::peek(c,0xcf53)-Impl::peek(c,0xffa5));
  auto bounds=Impl::domain(c);int left=std::max(bounds[0],p.view_left(c))-32,right=std::min(bounds[1],p.view_left(c)+p.width)+32;
  bool inside=world>=left&&world<right;bool old=(cpu[0]&0x80)==0;
  if(inside!=old){cpu[0]=uint16_t((cpu[0]&~0x80u)|(inside?0:0x80));++s.activation_adjustments;}
 }
 }
 if(pc==0x1ae9)s.capture_kind=1;
 if(pc==0x244c)s.capture_kind=2;
 if(c.rom_bank==1&&pc==0x47b2)s.capture_kind=3;
 if(s.capture_kind==1&&(pc==0x1b4a||pc==0x1b6d)){
  p.capture(c,cpu,cpu[2],cpu[1]>>8,pc==0x1b6d,Impl::peek(c,0xcf15),Impl::peek(c,0xcf16),Impl::peek(c,0xcf67),INT_MIN,7);s.capture_kind=0;
 }else if(s.capture_kind==2&&(pc==0x24ae||pc==0x24d4)){
  int world=p.actor_position(c);p.capture(c,cpu,cpu[2],cpu[1]>>8,pc==0x24d4,Impl::peek(c,0xffa5),Impl::peek(c,0xffa7),Impl::peek(c,0xcf52),world);s.capture_kind=0;
 }else if(s.capture_kind==3&&c.rom_bank==1&&pc==0x47e3){
  int world=s.current_shot<4&&s.shot_valid[s.current_shot]?s.shot_x[s.current_shot]:INT_MIN;
  p.capture(c,cpu,cpu[3],cpu[1]>>8,Impl::peek(c,0xffa2)==1,Impl::peek(c,0xffa5),Impl::peek(c,0xffa7),Impl::peek(c,0xcf67),world);s.capture_kind=0;
 }
 // Only2825 completes original game work.01DB is also called by selection,
 // loading and other native helpers; those waits must not publish empty lists.
}
void Mm2NativeMods::complete_frame(ModContext& c,bool cleared){auto& s=impl_->s;s.display=s.dma;std::copy_n(s.raster,144,s.display_raster);std::memcpy(s.display_planes,s.planes,sizeof s.planes);impl_->menu_frame(c,cleared);if(cleared)s.frame.valid=s.dma.valid=s.display.valid=0;}
void Mm2NativeMods::scanline(ModContext& c,uint8_t line){
 if(line>=144||!c.ppu)return;
 auto& p=*impl_;auto& s=p.s;if(!line){p.effect_tick(c);p.first_line(c);}
 s.raster[line]={c.ppu->scx,c.ppu->scy,c.ppu->lcdc,c.ppu->bgp,{c.ppu->obp0,c.ppu->obp1},c.ppu->wx,c.ppu->wy};
 unsigned row=((unsigned(line)+c.ppu->scy)&7)*2;
 for(unsigned tile=0;tile<256;tile++){unsigned at=(c.ppu->lcdc&16)?tile*16:unsigned(0x1000+int(int8_t(tile))*16);auto source=p.texture(c,at,16);s.planes[line][tile*2]=source[row];s.planes[line][tile*2+1]=source[row+1];}
}
uint32_t Mm2NativeMods::cpu_budget(const ModContext& c,bool cleared)const{
 // The authored CGB boot requests double speed (I15:4057; II16:4002).
 // Reuse the gameplay-only budget while retaining the physical PPU/APU clock.
 return (impl_->smooth||impl_->color)&&!cleared&&impl_->s.gameplay&&c.ppu&&(c.ppu->lcdc&128)&&!Impl::peek(c,0xcf00)&&!Impl::peek(c,0xcf60)?280896u:0;
}
void Mm2NativeMods::viewport(unsigned w,unsigned h){if(impl_->requested<0&&h){impl_->width=std::clamp<unsigned>((uint64_t(w)*144+h/2)/h,160,4096);impl_->s.width=unsigned(impl_->width);}}
ModView Mm2NativeMods::render(ModContext&,std::span<const uint32_t> native){
 auto& p=*impl_;auto& f=p.s.display;
  if(p.color&&p.s.fixed_valid)return p.fixed_view(native);
 if(p.color&&p.s.menu_valid){auto menu=p.menu_view(native);return megaman_pause_canvas(menu,unsigned(p.width),p.pause_canvas);}
 if(p.color&&p.s.menu){p.pause_canvas.assign(size_t(p.width)*144,0xff000000u);return {unsigned(p.width),p.width>160,p.pause_canvas};}
 if(!f.valid){if(p.color&&!p.s.fixed_scene){p.pause_canvas.assign(size_t(p.width)*144,0xff000000u);return {unsigned(p.width),p.width>160,p.pause_canvas};}return {160,false,native};}
 if(p.width==160&&!p.flicker&&!p.color)return {160,false,native};
 int width=p.width,left=f.camera-(width-160)/2;
 if(f.bound_right-f.bound_left>=width)left=std::clamp(left,f.bound_left,f.bound_right-width);
 else left=f.bound_left-(width-(f.bound_right-f.bound_left))/2;
 p.pixels.assign(size_t(width)*144,0xff000000u);p.background.assign(size_t(width)*144,0);
 // The native BG cache can contain parts of two vertically adjoining rooms.
 // C000 holds the logical room layout; wrapping that layout at128px selects
 // the wrong room for half the picture during a vertical camera pan. Keep
 // the original160px footprint tied to its completed physical tile cache.
 std::array<uint8_t,0x800> cached_tiles;std::copy_n(f.vram+0x1800,cached_tiles.size(),cached_tiles.begin());
#if GBCLLR_MM12_DX_ASSETS
 if(p.color&&f.stage<10)for(unsigned block=0;block<64;++block){unsigned base=f.stage*256+block*4;
  if(std::equal(mm12_dx::mm2_original_blocks+base,mm12_dx::mm2_original_blocks+base+4,mm12_dx::mm2_blocks+base))continue;
  // Authored variants are unambiguous for complete four-tile metatiles,
  // including the stages whose individual native tile IDs are ambiguous.
  for(unsigned map:{0u,0x400u})for(unsigned y=0;y<32;y+=2)for(unsigned x=0;x<32;x+=2){unsigned at=map+y*32+x;
   if(f.vram[0x1800+at]!=f.blocks[block*4]||f.vram[0x1800+at+32]!=f.blocks[block*4+1]||f.vram[0x1800+at+1]!=f.blocks[block*4+2]||f.vram[0x1800+at+33]!=f.blocks[block*4+3])continue;
   for(unsigned cell=0;cell<4;++cell){unsigned tile=at+(cell&1)*32+(cell>>1);if(cached_tiles[tile]==mm12_dx::mm2_original_blocks[base+cell])cached_tiles[tile]=mm12_dx::mm2_blocks[base+cell];}
  }
 }
#endif
 for(int y=0;y<128;y++)for(int x=0;x<width;x++){
  const auto& raster=p.s.display_raster[y];int worldx=left+x+int8_t(raster.scx-f.scroll_x),worldy=(y+raster.scy)&127,native_x=left+x-f.camera;
  unsigned tile=0,bit=7-(worldx&7);
  if(native_x>=0&&native_x<160){unsigned sx=unsigned(int(raster.scx)+native_x)&255,sy=(unsigned(y)+raster.scy)&255;
   tile=cached_tiles[((raster.lcdc&8)?0x400:0)+(sy/8)*32+sx/8];bit=7-(sx&7);
  }else{
   if(worldx<f.bound_left||worldx>=f.bound_right||worldx<0||worldx>=4096)continue;
   unsigned block=f.map[(worldy>>4)*256+(worldx>>4)],block_cell=(block&63)*4+((worldx&8)?2:0)+((worldy&8)?1:0);tile=f.blocks[block_cell];
#if GBCLLR_MM12_DX_ASSETS
   if(p.color&&f.stage<10&&tile==mm12_dx::mm2_original_blocks[f.stage*256+block_cell])tile=mm12_dx::mm2_blocks[f.stage*256+block_cell];
#endif
  }
  const auto& planes=p.s.display_planes[y];unsigned shade=((planes[tile*2]>>bit)&1)|(((planes[tile*2+1]>>bit)&1)<<1);
  p.background[y*width+x]=uint8_t(shade);
  unsigned attr=p.color?f.background_attributes[tile]:0;
  if(p.color&&(attr&8)){unsigned at=(raster.lcdc&16)?tile*16:unsigned(0x1000+int8_t(tile)*16),row=(worldy&7)*2;auto* image=p.data.extra.data()+at;shade=((image[row]>>bit)&1)|(((image[row+1]>>bit)&1)<<1);p.background[y*width+x]=uint8_t(shade);}
  if(p.color&&(attr&128)&&shade)p.background[y*width+x]|=128;
  p.pixels[y*width+x]=p.color?((attr&7)==6?p.rgb(p.data.weapon_pal,f.weapon*16+8,shade,0xe4):p.rgb(f.background_palettes,(attr&7)*8,shade,0xe4)):f.bg[(raster.bgp>>(shade*2))&3];
 }
 // The status band stays screen anchored and follows its real window
 // raster. Original dynamic bar tiles remain the source; author7600 supplies
 // the selected2x2 icon, blank cells and palette6 from its weapon table.
 int hudx=(width-160)/2;
 for(int y=128;y<144;y++)std::copy_n(native.begin()+y*160,160,p.pixels.begin()+y*width+hudx);
#if GBCLLR_MM12_DX_ASSETS
 if(p.color){unsigned profile=f.stage<p.data.profiles.size()?p.data.profiles[f.stage]:0;
  for(unsigned y=128;y<144;y++)for(unsigned x=0;x<160;x++){
   const auto& r=p.s.display_raster[y];if(!(r.lcdc&32)||y<r.wy||x+7<r.wx)continue;
   unsigned wx=x+7-r.wx,wy=y-r.wy,cell=(wy>>3)*32+(wx>>3),base=(r.lcdc&64)?0x1c00:0x1800;if(base!=0x1c00||cell>=64)continue;
   unsigned tile=f.vram[base+cell],attr=p.data.attributes[profile*256+tile],column=cell&31;
   if(column<5){if(tile==0x5d)tile=0x73;attr=6;}
   else if(column==5||column==6){tile=mm12_dx::mm2_hud_icons[f.weapon]+(column-5)+(cell>=32?16:0);attr=14;}
   else if(tile==0x5d){tile=0x73;attr=p.data.attributes[profile*256+tile];}
   if(column>=7)attr=uint8_t((column==12||column==13)&&wy<8?6:7);
   unsigned at=(r.lcdc&16)?tile*16:unsigned(0x1000+int8_t(tile)*16),row=(wy&7)*2,bit=7-(wx&7);const auto* image=(attr&8)||tile==0x73?p.data.extra.data()+at:f.vram+at;
   unsigned ink=((image[row]>>bit)&1)|(((image[row+1]>>bit)&1)<<1);bool portrait=column==12||column==13;p.pixels[y*width+hudx+x]=(attr&7)==6&&!portrait?p.rgb(mm12_dx::mm2_menu_palettes,f.weapon*16+8,ink,r.bgp):p.rgb(p.data.bg_pal,profile*64+(attr&7)*8,ink,r.bgp);
  }
 }
#endif
 p.framing_background=p.pixels;
 for(unsigned n=f.count;n-->0;){auto& s=f.sprite[n];int sx=s.x-left;
  for(int dy=0;dy<8;dy++){int yy=s.y+dy;if(yy<0||yy>=128)continue;const auto& raster=p.s.display_raster[yy];if(!(raster.lcdc&2))continue;int ty=(s.attributes&64)?7-dy:dy;
   for(int dx=0;dx<8;dx++){int xx=sx+dx;if(xx<0||xx>=width||left+xx<f.bound_left||left+xx>=f.bound_right)continue;int tx=(s.attributes&32)?dx:7-dx;
    unsigned color=((s.tiles[ty*2]>>tx)&1)|(((s.tiles[ty*2+1]>>tx)&1)<<1);if(!color)continue;auto bg=p.background[yy*width+xx];if(p.color?((raster.lcdc&1)&&((bg&128)||(s.attributes&128))&&(bg&3)):((s.attributes&128)&&bg))continue;
    unsigned pal=(s.attributes>>4)&1;
    p.pixels[yy*width+xx]=p.color?((s.color&7)?p.rgb(f.object_palettes,(s.color&7)*8,color,0xe4):p.rgb(p.data.weapon_pal,f.weapon*16,color,0xe4)):f.obj[pal*4+((raster.obp[pal]>>(color*2))&3)];
   }
  }
 }
 ModView v;v.width=unsigned(width);v.expanded=width>160;v.pixels=p.pixels;v.camera_x=f.camera;v.left=left;v.bound_left=f.bound_left;v.bound_right=f.bound_right;v.sprites=f.count;v.captures=p.s.captures;v.spawn_scans=p.s.spawn_scans;v.spawn_reads=p.s.spawn_reads;v.activation_adjustments=p.s.activation_adjustments;v.logic_updates=p.s.logic_updates;v.framing_width=unsigned(std::clamp(f.bound_right-f.bound_left,160,width));v.framing_background=p.framing_background;return v;
}
std::vector<uint8_t> Mm2NativeMods::snapshot()const{std::vector<uint8_t> b(sizeof impl_->s);std::memcpy(b.data(),&impl_->s,b.size());return b;}
void Mm2NativeMods::restore(std::span<const uint8_t> b){
 if(b.size()!=sizeof impl_->s)throw std::runtime_error("Invalid Mega Man II native state size");
 auto staged=std::make_unique<Impl::State>();std::memcpy(staged.get(),b.data(),b.size());
 for(auto valid:staged->shot_valid)if(valid>1)throw std::runtime_error("Invalid Mega Man II projectile state");
 if((staged->current_shot!=255&&staged->current_shot>=4)||staged->shot_overlap>1)throw std::runtime_error("Invalid Mega Man II projectile owner");
 if(staged->effect_profile>10||staged->effect_active>1||staged->effect_disabled>1||staged->effect_tile_valid>1||staged->effect_changed>1||staged->effect_phase>48||(staged->effect_phase&15)||staged->wily_fade>31||(staged->effect_tile_valid&&staged->effect_tile_offset!=0x1480&&staged->effect_tile_offset!=0x14b0))throw std::runtime_error("Invalid Mega Man II authored effects state");
 if(staged->menu>1||staged->menu_valid>1||staged->fixed_scene>11||staged->fixed_valid>1||staged->fixed_remix>1)throw std::runtime_error("Invalid Mega Man II native menu state");
 if(staged->count>Impl::max_sprites||staged->dma.count>Impl::max_sprites||staged->dma.weapon>12||staged->dma.valid>1||staged->frame.count>Impl::max_sprites||staged->display.count>Impl::max_sprites||staged->frame.weapon>12||staged->display.weapon>12||staged->frame.valid>1||staged->display.valid>1||staged->remix>1||staged->phase>4||staged->pending>2||staged->previous>3||staged->capture_kind>3||staged->target>12||staged->old_weapon>12||staged->spawn_count>256||staged->spawn_index>staged->spawn_count||staged->spawn_skip>1||staged->width<160||staged->width>4096||(staged->phase&&(staged->caller[4]<0xc040||staged->caller[4]>0xe000)))throw std::runtime_error("Invalid Mega Man II native state");
 impl_->s=*staged;impl_->width=int(staged->width);
}
}
