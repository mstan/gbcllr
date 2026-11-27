// Original USA cartridge services; no cartridge write or donor-code execution.
#include "mm1_native_mods.hpp"
#include "native_mods.hpp"
#include "megaman_pause_canvas.hpp"
#include <algorithm>
#include <cstring>
#include <iterator>
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
struct Mm1NativeMods::Impl {
 static constexpr unsigned max_sprites=1024;
 struct Sprite {int x,y;uint8_t attr,color,tile,authored,tiles[16];};
 struct Raster {uint8_t scx=0,scy=0,lcdc=0,bgp=0xe4,obp[2]{0xe4,0xe4};};
 struct Frame {
  uint8_t valid=0,lcdc=0,bgp=0,obp[2]{},bank=0,columns=0,rows=0,scroll_x=0,scroll_y=0,profile=0,weapon=0;
  uint16_t row[32]{},metatiles=0;
  int camera=0,left=0,bound_left=0,bound_right=0;
  unsigned count=0;
  uint8_t vram[0x2000]{},blocks[256]{};
  uint32_t bg[4]{},obj[8]{};
  uint8_t dx_background[64]{},dx_objects[64]{},dx_attributes[256]{};
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
  unsigned spawn_scans=0,spawn_reads=0;
  unsigned terrain_reads=0;
  int sensor_x=0;
  uint8_t sensor_y=0,sensor_valid=0;
  uint64_t logic_updates=0;
  int shot_x[3]{};
  uint8_t shot_raw[3]{},shot_valid[3]{};
  uint16_t shot_actor=0,shot_box=0;
  uint8_t dx_source[0x2000]{},dx_art[0x2000]{},dx_valid[0x2000]{};
  uint8_t object_attributes[512]{},credits_attributes[512]{},fixed_credits_attributes[512]{};
  uint8_t object_palettes[64]{},background_palettes[64]{},background_buffer[256]{},background_attributes[256]{},face_index[256]{},face_records[256]{};
  uint8_t fixed_background_palettes[64]{},fixed_object_palettes[64]{};
  uint8_t resource_style=0,palette_profile=0,palette_delay=0,palette_phase=0,palette_active=0,ship_special=0;
  uint8_t fixed_vram[0x2000]{},fixed_oam[160]{},fixed_scene=0,fixed_kind=0,fixed_valid=0,fixed_wx=7,fixed_wy=144,fixed_profile=0,fixed_weapon=0;
  unsigned width=160;
  uint8_t gameplay=0,previous=0,pending=0,phase=0,target=0;
  uint8_t resize_scans=2;
  uint16_t caller[6]{};
 } s{};
 int requested=160,width=160;
 bool flicker=false,smooth=false,cycling=false,color=false;
 struct Data {std::span<const uint32_t> records;std::span<const uint8_t> original,colored,bg_pal,obj_pal,attributes,obj_attributes,weapon_pal,extra,face_index,face_records;} data;
 std::vector<uint32_t> pixels;
 std::vector<uint32_t> pause_canvas;
 std::vector<uint32_t> framing_background;
 std::vector<uint8_t> background;
 std::vector<unsigned> order;
 static uint8_t peek(const ModContext& c,unsigned a){
  if(a<0x4000)return a<c.rom_size?c.rom[a]:0;
  if(a<0x8000){size_t o=size_t(c.rom_bank)*0x4000+a-0x4000;return o<c.rom_size?c.rom[o]:0;}
  if(a>=0xc000&&a<0xd000)return c.wram[a-0xc000];
  if(a>=0xd000&&a<0xe000)return c.wram[size_t(c.wram_bank)*0x1000+a-0xd000];
  if(a>=0xff80&&a<0xffff)return c.hram[a-0xff80];
  return 0;
 }
 static unsigned word(const ModContext& c,unsigned a){return peek(c,a)|(unsigned(peek(c,a+1))<<8);}
 static uint8_t rom(const ModContext& c,unsigned bank,unsigned a){size_t o=a<0x4000?a:size_t(bank)*0x4000+a-0x4000;return a<0x8000&&o<c.rom_size?c.rom[o]:0;}
 size_t asset(unsigned bank,unsigned address,unsigned count=1)const{
  for(size_t i=data.records.size();i;i-=4){auto at=i-4;if(data.records[at]==bank&&address>=data.records[at+1]&&uint64_t(address)+count<=data.records[at+1]+data.records[at+2])return data.records[at+3]+address-data.records[at+1];}
  return SIZE_MAX;
 }
 uint8_t presentation(const ModContext& c,unsigned bank,unsigned address)const{auto at=color?asset(bank,address):SIZE_MAX;return at==SIZE_MAX?rom(c,bank,address):data.colored[at];}
 void copy_art(const ModContext& c,unsigned source,unsigned destination,unsigned count){
  if(!color||destination<0x8000||destination+count>0x9800||!count)return;
  auto at=asset(source<0x4000?0:c.rom_bank,source,count);if(at==SIZE_MAX)return;
  destination-=0x8000;std::copy_n(data.original.data()+at,count,s.dx_source+destination);std::copy_n(data.colored.data()+at,count,s.dx_art+destination);std::fill_n(s.dx_valid+destination,count,1);
 }
 const uint8_t* texture(const ModContext& c,unsigned at,unsigned count)const{
  bool changed=color&&at+count<=0x2000&&std::all_of(s.dx_valid+at,s.dx_valid+at+count,[](uint8_t v){return v!=0;})&&std::equal(c.vram+at,c.vram+at+count,s.dx_source+at);
  return changed?s.dx_art+at:c.vram+at;
 }
 void texture(const ModContext& c,unsigned at,unsigned count,uint8_t* out)const{std::memcpy(out,texture(c,at,count),count);}
 uint32_t rgb(const std::span<const uint8_t> palette,unsigned offset,unsigned ink,uint8_t fade)const{
  if(fade==0)return 0xffffffffu;
  if(fade==255)return 0xff000000u;
  unsigned at=offset+ink*2;if(at+1>=palette.size())return 0xff000000u;
  unsigned value=palette[at]|(unsigned(palette[at+1])<<8);return 0xff000000u|((value&31)*255/31<<16)|(((value>>5)&31)*255/31<<8)|(((value>>10)&31)*255/31);
 }
 static int camera(const ModContext& c){return int(word(c,0xd80f))-80;}
 int view_left(const ModContext& c)const{
  int low=int(word(c,0xd861))-80,high=int(word(c,0xd863))+80,left=camera(c)-(width-160)/2;
  return high-low>=width?std::clamp(left,low,high-width):low-(width-(high-low))/2;
 }
 bool expanded_domain(const ModContext& c)const{
  // Single-screen domains keep the cartridge's original staging/page rules.
  int low=int(word(c,0xd861))-80,high=int(word(c,0xd863))+80;
  return width>160&&high-low>160;
 }
 static unsigned shot_slot(unsigned base){return base>=0x39&&base<=0x79&&((base-0x39)&31)==0?(base-0x39)/32:3;}
 int shot_position(const ModContext& c,unsigned slot){
  unsigned base=0xc239+slot*32;int projected=peek(c,base+4),cam=camera(c);uint8_t raw=uint8_t(cam+projected);
  if(!s.shot_valid[slot]){
   int world=cam+projected,player=int(word(c,0xd80d));
   while(world-player>128)world-=256;
   while(player-world>128)world+=256;
   s.shot_x[slot]=world;s.shot_valid[slot]=1;
  }else s.shot_x[slot]+=int8_t(raw-s.shot_raw[slot]);
  s.shot_raw[slot]=raw;return s.shot_x[slot];
 }
 void emit(const ModContext& c,int x,int y,unsigned tile,uint8_t attr){
  if(tile>255||s.count==max_sprites)return;
  auto& p=s.building[s.count++];p.x=x;p.y=y;
  p.attr=attr;p.tile=uint8_t(tile);p.authored=0;
  p.color=color?s.object_attributes[((p.attr&16)?256:0)+tile]:0;
  texture(c,tile*16,16,p.tiles);s.captures++;
  if(color&&s.count<max_sprites){unsigned index=s.face_index[tile];
   if(index&&index+2<256){
    unsigned face=s.face_records[index+2];
    if(face*16+16<=data.extra.size()){
     auto& overlay=s.building[s.count++];overlay.x=p.x+((p.attr&32)?16-int8_t(s.face_records[index+1]):int8_t(s.face_records[index+1])-16);overlay.y=p.y+int8_t(s.face_records[index]);overlay.attr=uint8_t(p.attr|8);overlay.color=1;overlay.tile=uint8_t(face);overlay.authored=1;
     std::copy_n(data.extra.data()+face*16,16,overlay.tiles);++s.captures;
    }
   }
  }
 }
 void piece(const ModContext& c,int x,int y,uint8_t code){
  if(code==255)return;
  unsigned mode=peek(c,0xce57)&1,tile=uint8_t(peek(c,0xce55)+(code&(mode?127:63)));
  emit(c,x,y,tile,uint8_t(peek(c,0xd3ea)|((code&128)?32:0)|(!mode&&(code&64)?64:0)));
 }
 void body(const ModContext& c,unsigned pointer){
  if(pointer<0x4000||pointer>0x7ffc)return;
  int x=int(word(c,0xd80d))+int8_t(peek(c,pointer++)),y=int(peek(c,0xd824))+2+int8_t(peek(c,pointer++));
  unsigned size=peek(c,pointer++),w=size>>4,h=size&15,ordinal=0;if(w*h>128)return;
  auto draw=[&](int px,int py,uint8_t code){
   if(code==255)return;
   uint8_t attr=uint8_t(peek(c,0xd80b)|((code&128)?32:0));
   if(peek(c,0xd80a)==0x1c&&ordinal>=6&&ordinal<9)attr|=64;
   emit(c,px,py,code&127,attr);++ordinal;
  };
  for(unsigned yy=0;yy<h;yy++)for(unsigned xx=0;xx<w;xx++)draw(x+int(xx)*8,y+int(yy)*8,peek(c,pointer++));
  for(unsigned n=0;n<32&&pointer+2<0x8000;n++){uint8_t dx=peek(c,pointer++);if(dx==128)break;int dy=int8_t(peek(c,pointer++));draw(x+int8_t(dx),y+dy,peek(c,pointer++));}
 }
 void mapping(const ModContext& c,unsigned pointer,bool tail_only,unsigned actor,unsigned frame){
  if(pointer<0x4000||pointer>=0x8000)return;
  int x,y,origin=int(peek(c,0xffa2)),world=camera(c)+origin;
  if(origin>=216)world-=256;
  // The original actor carries a full world coordinate alongside its modulo
  // projection. Bind only when both its current origin and frame agree with
  // this producer; player/HUD/special-script emitters keep their own origin.
  if(actor>=0xc200&&actor<=0xdcc0&&peek(c,actor+0x0d)==origin&&peek(c,actor+0x10)==frame)world=int(word(c,actor+0x0a));
  if(tail_only){x=int(peek(c,0xffa4))-8;y=int(peek(c,0xffa5))-16;}
  else {
   x=int(peek(c,0xffa2))+int8_t(peek(c,pointer++));y=int(peek(c,0xffa3))+int8_t(peek(c,pointer++));
   unsigned size=peek(c,pointer++),w=size>>4,h=size&15;
   if(w*h>128)return;
   int offset=x-origin;if(y>=216)y-=256;
   for(unsigned yy=0;yy<h;yy++)for(unsigned xx=0;xx<w;xx++)piece(c,world+offset+int(xx)*8,y+int(yy)*8,peek(c,pointer++));
  }
  if(tail_only&&x>=216)x-=256;
  for(unsigned n=0;n<128&&pointer+2<0x8000;n++){
   uint8_t dx=peek(c,pointer++);if(dx==128)break;int dy=int8_t(peek(c,pointer++));uint8_t tile=peek(c,pointer++);
   piece(c,(tail_only?camera(c)+x:world+x-origin)+int8_t(dx),y+dy,tile);
  }
 }
 void publish(const ModContext& c){
  auto& f=s.frame;f.valid=uint8_t(s.gameplay&&c.ppu&&(c.ppu->lcdc&128)&&!peek(c,0xd869));
  f.columns=peek(c,0xd8c0);f.rows=peek(c,0xd8c1);f.bank=peek(c,0xdf9b);f.metatiles=uint16_t(word(c,0xd8c4));
  if(!f.columns||!f.rows||f.rows>32||f.metatiles<0x4000||f.metatiles>0x7c00||f.bank>=c.rom_size/0x4000)f.valid=0;
  if(!f.valid)return;
  f.lcdc=c.ppu->lcdc;f.bgp=c.ppu->bgp;f.obp[0]=c.ppu->obp0;f.obp[1]=c.ppu->obp1;f.camera=camera(c);f.scroll_y=peek(c,0xd860);
  f.profile=uint8_t(std::min(12u,unsigned(peek(c,0xdf8c))+(peek(c,0xdfc8)?8:0)));f.weapon=uint8_t(std::min(10u,unsigned(peek(c,0xdfa0))));
  f.bound_left=int(word(c,0xd861))-80;f.bound_right=int(word(c,0xd863))+80;
  for(unsigned n=0;n<f.rows;n++)f.row[n]=uint16_t(word(c,0xd877+n*2));
  std::memcpy(f.vram,c.vram,sizeof f.vram);std::memcpy(f.blocks,c.wram+0x1e00,sizeof f.blocks);
  if(color)for(unsigned tile=0;tile<512;tile++)texture(c,tile*16,16,f.vram+tile*16);
  std::copy_n(c.ppu->bg_rgb,4,f.bg);std::copy_n(c.ppu->obj_rgb,8,f.obj);
  f.count=s.count;std::copy_n(s.building,s.count,f.sprite);
 }
 void first_line(const ModContext& c){
  auto& f=s.dma;if(!f.valid||!c.ppu)return;
  f.lcdc=c.ppu->lcdc;f.scroll_x=c.ppu->scx;f.scroll_y=c.ppu->scy;
  std::copy_n(c.ppu->bg_rgb,4,f.bg);std::copy_n(c.ppu->obj_rgb,8,f.obj);std::copy_n(c.vram,0x2000,f.vram);
  if(color)for(unsigned tile=0;tile<512;tile++)texture(c,tile*16,16,f.vram+tile*16);
  if(color){std::copy_n(s.background_palettes,64,f.dx_background);std::copy_n(s.object_palettes,64,f.dx_objects);std::copy_n(s.background_attributes,256,f.dx_attributes);}
  for(unsigned n=0;n<f.count;n++)if(!f.sprite[n].authored){texture(c,f.sprite[n].tile*16,16,f.sprite[n].tiles);if(color)f.sprite[n].color=s.object_attributes[((f.sprite[n].attr&16)?256:0)+f.sprite[n].tile];}
 }
 void load_background(std::span<const uint8_t> base){for(unsigned n=0;n<64;n++){auto value=base[n];s.background_buffer[n]=value;s.background_buffer[64+n]=value&0xcc;s.background_buffer[128+n]=value|0xcc;}}
 void reset_style(const ModContext& c){
#if GBCLLR_MM12_DX_ASSETS
  if(!color)return;
  s.palette_profile=uint8_t(std::min(12u,unsigned(peek(c,0xdf8c))+(peek(c,0xdfc8)?8:0)));s.resource_style=0;s.palette_active=1;
  std::copy_n(data.obj_pal.data()+s.palette_profile*64,64,s.object_palettes);std::copy_n(data.bg_pal.data()+s.palette_profile*64,64,s.background_palettes);
  std::copy_n(data.attributes.data()+s.palette_profile*256,256,s.background_attributes);std::copy_n(data.obj_attributes.data(),512,s.object_attributes);
  std::copy_n(data.face_index.data(),256,s.face_index);std::copy_n(data.face_records.data(),256,s.face_records);
  for(unsigned n=0;n<64;n++){auto value=s.background_palettes[n];s.background_buffer[n]=value;s.background_buffer[64+n]=value&0xcc;s.background_buffer[128+n]=value|0xcc;}
#else
  (void)c;
#endif
 }
 void style_colors(const ModContext& c,bool tick){
#if GBCLLR_MM12_DX_ASSETS
  if(!color||!c.ppu)return;
  if(s.resource_style>=3){bool credits=s.resource_style==4;auto mode=credits?mm12_dx::mm1_credits_fade_modes[c.ppu->bgp]:mm12_dx::mm1_ship_fade_modes[c.ppu->bgp];const uint8_t* palette=s.background_buffer+(mode<3?mode*64:192);
   if(mode==3)palette=mm12_dx::mm1_boss_fixed_background_palettes;else if(mode==4)palette=credits?mm12_dx::mm1_credits_background_palettes+4*64:mm12_dx::mm1_boss_fixed_background_palettes+64;else if(mode==15)palette=mm12_dx::mm1_ship_background_palettes+5*64;else if(credits&&mode==14)palette=mm12_dx::mm1_credits_background_palettes+6*64;std::copy_n(palette,64,s.background_palettes);
  }
  if(s.palette_active){auto weapon=std::min(10u,unsigned(peek(c,0xdfa0)));std::copy_n(data.weapon_pal.data()+weapon*8,8,s.object_palettes);if(weapon==6)std::copy_n(mm12_dx::mm1_weapon6_colors+mm12_dx::mm1_weapon6_color_offsets[c.ppu->obp0],8,s.object_palettes);}
  if(s.resource_style>0&&s.resource_style<3&&s.palette_active&&(s.fixed_scene==0||s.fixed_scene==3)){
   auto mode=mm12_dx::mm1_boss_fade_modes[(s.resource_style-1)*256+c.ppu->bgp];const uint8_t* palette=s.background_buffer+(mode<3?mode*64:192);
   if(mode==3||mode==4||mode==5)palette=mm12_dx::mm1_boss_fixed_background_palettes+(mode-3)*64;
   std::copy_n(palette,48,s.background_palettes);
   if(s.resource_style==1&&(mode==0||mode==4||mode==5))std::fill_n(s.object_attributes+256+0x8b,11,mode==0?3:mode==4?4:5);
  }
  if(tick&&s.palette_active&&(c.ppu->lcdc&128)){
   auto delay=mm12_dx::mm1_stage_animation_delays[s.palette_profile];if(delay){if(delay<s.palette_delay){s.palette_delay=0;s.palette_phase=(s.palette_phase+1)&7;std::copy_n(mm12_dx::mm1_stage_animation_palettes+s.palette_profile*128+s.palette_phase*16,16,s.background_palettes+32);}else ++s.palette_delay;}
  }
#else
  (void)c;(void)tick;
#endif
 }
 void resource(const ModContext& c,unsigned kind){
#if GBCLLR_MM12_DX_ASSETS
  if(!color||kind>=62)return;
  unsigned key=(peek(c,0xdfc8)?64:0)+kind,start=peek(c,0xc3b3);
  for(unsigned n=0;n<std::size(mm12_dx::mm1_resource_records);n+=5){
   const auto* record=mm12_dx::mm1_resource_records+n;if(record[0]!=key)continue;
   for(unsigned plane=0;plane<2;plane++){
    unsigned at=record[1+plane*2],count=std::min<unsigned>(record[2+plane*2],256-start);
    if(at+count<=std::size(mm12_dx::mm1_resource_attributes))std::copy_n(mm12_dx::mm1_resource_attributes+at,count,s.object_attributes+plane*256+start);
   }
   break;
  }
  for(unsigned n=0;n<std::size(mm12_dx::mm1_boss_style_records);n+=5){const auto* record=mm12_dx::mm1_boss_style_records+n;if(record[0]!=kind)continue;auto* source=mm12_dx::mm1_boss_style_data+record[4];auto offset=record[2],count=record[3];
   switch(record[1]){case 0:std::copy_n(source,count,s.object_attributes+offset);break;case 1:std::copy_n(source,count,s.face_index+offset);break;case 2:std::copy_n(source,count,s.face_records+offset);break;case 3:std::copy_n(source,count,s.object_palettes+offset);break;case 4:std::copy_n(source,count,s.background_buffer+offset);break;case 5:std::copy_n(source,count,s.background_attributes+offset);break;}
  }
  if(kind==48)s.resource_style=1;else if(kind==56)s.resource_style=2;
  style_colors(c,false);
#else
  (void)c;(void)kind;
#endif
 }
 void fixed_frame(const ModContext& c,bool cleared){
  bool room_pan=s.gameplay&&!s.fixed_scene&&peek(c,0xd802)==1&&peek(c,0xd869)&&peek(c,0xd8c0)&&peek(c,0xd8c1);
  s.fixed_valid=uint8_t(color&&((s.fixed_scene>=1&&s.fixed_scene<=11)||room_pan)&&!cleared&&c.ppu&&(c.ppu->lcdc&128));
  if(!s.fixed_valid)return;
  s.fixed_kind=room_pan?12:s.fixed_scene;
  std::copy_n(s.background_palettes,64,s.fixed_background_palettes);std::copy_n(s.object_palettes,64,s.fixed_object_palettes);
  s.fixed_profile=uint8_t(std::min(12u,unsigned(peek(c,0xdf8c))+(peek(c,0xdfc8)?8:0)));s.fixed_weapon=uint8_t(std::min(10u,unsigned(peek(c,0xdfa0))));
  s.fixed_wx=c.ppu->wx;s.fixed_wy=c.ppu->wy;
  std::copy_n(c.vram,0x2000,s.fixed_vram);std::copy_n(c.oam,160,s.fixed_oam);
  for(unsigned tile=0;tile<512;tile++)texture(c,tile*16,16,s.fixed_vram+tile*16);
#if GBCLLR_MM12_DX_ASSETS
  if(s.fixed_kind==9||s.fixed_kind==11)std::copy_n(s.object_attributes,512,s.fixed_credits_attributes);
  if(s.fixed_kind==6){
   // Authored17:7036 preserves the prior OBJ palettes/attributes unless
   // existing OBJ color14 has the explicit FD discriminator.
   auto special=s.ship_special!=0;
   if(special)std::copy_n(mm12_dx::mm1_ship_object_attributes,512,s.fixed_credits_attributes);else std::copy_n(s.object_attributes,512,s.fixed_credits_attributes);
  }
  if(s.fixed_kind==5){
   // Authored15:4E00 updates each native OBP plane only while role4C
   // selects mode4. Preserve the previous attributes through other fades.
   const auto& r=s.display_raster[0];if(mm12_dx::mm1_credits_fade_modes[r.bgp]==4)for(unsigned plane=0;plane<2;plane++){
    unsigned mode=mm12_dx::mm1_credits_object_modes[plane*256+r.obp[plane]];auto* at=s.credits_attributes+plane*256;
    if(!mode)std::copy_n(mm12_dx::mm1_credits_object_attributes,256,at);else if(mode<4)std::fill_n(at,256,mode==1?6:mode==2?3:7);
   }
   std::copy_n(s.credits_attributes,512,s.fixed_credits_attributes);
  }
  if(s.fixed_kind==1){for(unsigned cell=0;cell<1024;cell++)if(s.fixed_vram[0x1800+cell]==mm12_dx::mm1_title_original_map[cell])s.fixed_vram[0x1800+cell]=mm12_dx::mm1_title_map[cell];}
  else if(s.fixed_kind==2||s.fixed_kind==10){for(unsigned cell=0;cell<2048;cell++)if(s.fixed_vram[0x1800+cell]==mm12_dx::mm1_select_original_map[cell])s.fixed_vram[0x1800+cell]=mm12_dx::mm1_select_map[cell];}
  else if(s.fixed_kind==4){for(unsigned cell=0;cell<1024;cell++)if(s.fixed_vram[0x1800+cell]==mm12_dx::mm1_password_original_map[cell])s.fixed_vram[0x1800+cell]=mm12_dx::mm1_password_map[cell];}
  else if(s.fixed_kind==3){
   // Author5E06/5E11 changes the native menu blank glyph70 to73. Keep
   // every native energy/name update and replace only that finite glyph.
   for(unsigned cell=0;cell<512;cell++)if(s.fixed_vram[0x1c00+cell]==0x70)s.fixed_vram[0x1c00+cell]=0x73;
   for(unsigned row=0;row<2;row++)for(unsigned x=0;x<2;x++)s.fixed_vram[0x1c05+row*32+x]=mm12_dx::mm1_hud_icons[s.fixed_weapon*6+row*2+x];
  }
#endif
 }
 ModView fixed_view(std::span<const uint32_t> native){
#if GBCLLR_MM12_DX_ASSETS
  if(!s.fixed_valid)return {160,false,native};
  pixels.resize(160*144);background.assign(160*144,0);
  for(unsigned y=0;y<144;y++)for(unsigned x=0;x<160;x++){
   const auto& raster=s.display_raster[y];bool window=(raster.lcdc&32)&&y>=s.fixed_wy&&x+7>=s.fixed_wx;
   unsigned wx=window?x+7-s.fixed_wx:(x+raster.scx)&255,wy=window?y-s.fixed_wy:(y+raster.scy)&255;
   unsigned cell=(wy>>3)*32+(wx>>3),map=window?((raster.lcdc&64)?0x1c00:0x1800):((raster.lcdc&8)?0x1c00:0x1800),tile=s.fixed_vram[map+cell];
   unsigned attr=s.fixed_kind==1?(map==0x1800?mm12_dx::mm1_title_attributes[cell]:0):(s.fixed_kind==2||s.fixed_kind==4||s.fixed_kind==8||s.fixed_kind==10)?mm12_dx::mm1_select_attributes[tile]:s.background_attributes[tile];
   if(s.fixed_kind==11)attr=map==0x1800&&cell<768?7:0;
   if(s.fixed_kind==9)attr=map==0x1800&&cell<768?mm12_dx::mm1_fortress_background_attributes[cell]:0;
   if(s.fixed_kind==10&&((map==0x1800&&cell<256)||(map==0x1c00&&cell>=32&&cell<512)))attr=4;
   if(s.fixed_kind==7)attr=map==0x1800&&cell<768?mm12_dx::mm1_getweapon_background_attributes[cell]:0;
   if(s.fixed_kind==6)attr=map==0x1800&&cell<768?mm12_dx::mm1_ship_background_attributes[cell]:0;
   if(s.fixed_kind==5)attr=mm12_dx::mm1_credits_background_attributes[tile];
   if((s.fixed_kind==3||s.fixed_kind==12)&&map==0x1c00&&cell<512){attr=7;if(cell<5||(cell>=32&&cell<37)||cell==12||cell==13)attr=6;if(cell==5||cell==6||cell==37||cell==38)attr=14;}
   if(s.fixed_kind==12&&map==0x1c00&&cell<64){unsigned column=cell&31;
    if(tile==0x70)tile=0x73;
    if(column==5||column==6)tile=mm12_dx::mm1_hud_icons[s.fixed_weapon*6+(cell/32)*2+column-5];
   }
   unsigned at=(raster.lcdc&16)?tile*16:unsigned(0x1000+int8_t(tile)*16),row=(wy&7)*2,bit=7-(wx&7);
   const uint8_t* image=(attr&8)?data.extra.data()+at:s.fixed_vram+at;
   unsigned ink=((image[row]>>bit)&1)|(((image[row+1]>>bit)&1)<<1);background[y*160+x]=uint8_t(ink);
   // The weapon gauge uses its zero-background palette. The portrait's
   // helmet shares slot6 but must retain its white eye/background color.
   bool header=(s.fixed_kind==3||s.fixed_kind==12)&&map==0x1c00&&(attr&7)==6&&cell<64&&(cell&31)<5;pixels[y*160+x]=header?rgb(mm12_dx::mm1_hud_weapon_palettes,s.fixed_weapon*8,ink,0xe4):rgb(s.fixed_background_palettes,(attr&7)*8,ink,s.fixed_kind==12?raster.bgp:0xe4);
  }
  auto draw=[&](int ox,int oy,unsigned tile,uint8_t attr,const uint8_t* image,unsigned palette){
   for(int yy=0;yy<16;yy++){int y=oy+yy;if(y<0||y>=144)continue;const auto& raster=s.display_raster[y];unsigned height=(raster.lcdc&4)?16:8;if(!(raster.lcdc&2)||unsigned(yy)>=height)continue;unsigned row=unsigned((attr&64)?int(height)-1-yy:yy)*2;const uint8_t* tiles=image-((height==16)?(tile&1)*16:0);
    for(int xx=0;xx<8;xx++){int x=ox+xx;if(x<0||x>=160)continue;unsigned bit=unsigned((attr&32)?xx:7-xx),ink=((tiles[row]>>bit)&1)|(((tiles[row+1]>>bit)&1)<<1);if(!ink||((attr&128)&&background[y*160+x]))continue;pixels[y*160+x]=rgb(s.fixed_object_palettes,palette*8,ink,0xe4);}
   }
  };
  // Author3C00 fills free OAM tail slots with faces, below every native base
  // piece. Later face allocations have earlier OAM slots. Preserve that order
  // independently of the original40-object capacity.
  if(s.fixed_kind==2||s.fixed_kind==3||s.fixed_kind==5||s.fixed_kind==8||s.fixed_kind==10||s.fixed_kind==11||s.fixed_kind==12)for(unsigned n=0;n<40;n++){const auto* object=s.fixed_oam+n*4;const auto* indices=(s.fixed_kind==3||s.fixed_kind==12)?s.face_index:s.fixed_kind==5?mm12_dx::mm1_credits_face_index:mm12_dx::mm1_select_face_index;const auto* records=(s.fixed_kind==3||s.fixed_kind==12)?s.face_records:s.fixed_kind==5?mm12_dx::mm1_credits_face_records:mm12_dx::mm1_select_face_records;unsigned at=indices[object[2]];
   if(at&&at+2<256){const auto* face=records+at;unsigned image=face[2];if(image*16+16<=data.extra.size())draw(int(object[1])-8+((object[3]&32)?16-int8_t(face[1]):int8_t(face[1])-16),int(object[0])-16+int8_t(face[0]),image,uint8_t(object[3]|8),data.extra.data()+image*16,1);}
  }
  // Authored CGB order keeps the actual cursor/star/head base OAM.
  for(unsigned n=40;n-->0;){const auto* object=s.fixed_oam+n*4;int ox=int(object[1])-8,oy=int(object[0])-16;unsigned tile=object[2],palette=s.fixed_kind==1?0:mm12_dx::mm1_select_object_attributes[((object[3]&16)?256:0)+tile]&7;
   if(s.fixed_kind==3||s.fixed_kind==12)palette=s.object_attributes[((object[3]&16)?256:0)+tile]&7;if((s.fixed_kind==2||s.fixed_kind==4||s.fixed_kind==8||s.fixed_kind==10||s.fixed_kind==11)&&!tile)palette=((s.display_raster[0].obp[0]&8)?((s.display_raster[0].obp[0]&128)?6:7):0);if(s.fixed_kind==8&&tile)palette=1;if(s.fixed_kind==9||s.fixed_kind==11)palette=s.fixed_credits_attributes[((object[3]&16)?256:0)+tile]&7;if(s.fixed_kind==5||s.fixed_kind==6)palette=s.fixed_credits_attributes[((object[3]&16)?256:0)+tile]&7;draw(ox,oy,tile,object[3],s.fixed_vram+tile*16,palette);
  }
  return {160,false,pixels};
#else
  return {160,false,native};
#endif
 }
 static unsigned texel(const Frame& f,unsigned tile,unsigned x,unsigned y){
  int a=(f.lcdc&16)?int(tile)*16:0x1000+int(int8_t(tile))*16;
  return ((f.vram[a+y*2]>>(7-x))&1)|(((f.vram[a+y*2+1]>>(7-x))&1)<<1);
 }
 static bool stack(uint16_t sp){return (sp>=0xc040&&sp<=0xe000)||(sp>=0xff84&&sp<=0xfffe);}
 static void push(std::span<uint16_t,6> cpu,std::span<uint8_t> ram,std::span<uint8_t> hram,uint16_t v){
  cpu[4]-=2;auto put=[&](unsigned a,uint8_t b){if(a>=0xff80)hram[a-0xff80]=b;else ram[a-0xc000]=b;};
  put(cpu[4],uint8_t(v));put(cpu[4]+1,uint8_t(v>>8));
 }
};
Mm1NativeMods::Mm1NativeMods(std::span<const NativeFeature> features,bool):impl_(std::make_unique<Impl>()){
#if GBCLLR_MM12_DX_ASSETS
 using namespace mm12_dx;impl_->data={mm1_records,mm1_original,mm1_colored,mm1_background_palettes,mm1_object_palettes,mm1_stage_attributes,mm1_object_attributes,mm1_weapon_palettes,mm1_extra_art,mm1_face_index,mm1_face_records};
#endif
 for(const auto& f:features){
  if(f.plugin=="gbcllr.mm1.adaptive-view"){impl_->requested=f.view_width;impl_->width=f.view_width<0?256:f.view_width;impl_->flicker=f.no_flicker;impl_->smooth=f.smooth_gameplay;impl_->cycling=f.weapon_cycle;}
  if(f.colorization&&f.asset_input!=GBCLLR_MM12_DX_ASSET_ID)throw std::runtime_error("Mega Man I DX presentation asset pin mismatch");
  impl_->color|=f.colorization;
 }
 if(impl_->color&&impl_->data.records.empty())throw std::runtime_error("Mega Man I DX requires its pinned native presentation assets");
 if(impl_->color){std::copy_n(impl_->data.obj_attributes.data(),512,impl_->s.object_attributes);std::copy_n(impl_->data.obj_pal.data(),64,impl_->s.object_palettes);std::copy_n(impl_->data.bg_pal.data(),64,impl_->s.background_palettes);std::copy_n(impl_->data.attributes.data(),256,impl_->s.background_attributes);std::copy_n(impl_->data.face_index.data(),256,impl_->s.face_index);std::copy_n(impl_->data.face_records.data(),256,impl_->s.face_records);}
 impl_->s.width=unsigned(impl_->width);
}
Mm1NativeMods::~Mm1NativeMods()=default;
uint8_t Mm1NativeMods::read(ModContext& c,uint16_t address,uint8_t v){
 auto& p=*impl_;auto& s=p.s;if(c.sp==UINT16_MAX||p.width<=160||!s.gameplay)return v;
 unsigned ip=c.instruction_pc;
 if(ip==0x1041&&address>=0xde00&&address<0xdf00&&s.sensor_valid&&p.expanded_domain(c)){
  int low=int(Impl::word(c,0xd861))-80,high=int(Impl::word(c,0xd863))+80;
  unsigned rows=Impl::peek(c,0xd8c1),columns=Impl::peek(c,0xd8c0);
  if(s.sensor_x>=low&&s.sensor_x<high&&s.sensor_x>=0&&s.sensor_x<int(columns)*16&&s.sensor_y<rows*16&&
     (s.sensor_x<(Impl::camera(c)&~15)-16||s.sensor_x>=(Impl::camera(c)&~15)+176)){
   ++s.terrain_reads;unsigned row=Impl::word(c,0xd877+(s.sensor_y>>4)*2);
   return Impl::rom(c,Impl::peek(c,0xdf9b),row+(s.sensor_x>>4));
  }
 }
 bool scan=(ip>=0x267d&&ip<0x26c0)||(ip>=0x26f0&&ip<0x2700)||(ip>=0x28e2&&ip<0x29ee);
 if(scan&&(address==0xd856||address==0xd857)&&p.expanded_domain(c)){
  int left=p.view_left(c),low=int(Impl::word(c,0xd861))-80,high=int(Impl::word(c,0xd863))+80;++s.spawn_reads;
  int block=address==0xd856?std::max(low,left-16)/16:(std::min(high,left+p.width)+15)/16+1;
  return uint8_t(std::clamp(block,0,255));
 }
 if(address==0xd858&&ip==0x28ee&&p.expanded_domain(c)){
  ++s.spawn_scans;unsigned center=Impl::peek(c,0xd855);
  if(s.resize_scans){unsigned phase=s.resize_scans--;if(phase==2&&center<255)return uint8_t(center+1);if(phase==1&&center)return uint8_t(center-1);}
 }
 return v;
}
void Mm1NativeMods::write(ModContext&,uint16_t address,uint8_t value){
 // The original VBlank ISR selects its OAM source before main work resumes.
 // CPU headroom can finish another pose before physical line0; that newer
 // pose belongs to the following DMA, not the display being prepared now.
 if(address==0xff46&&value==0xc0)impl_->s.dma=impl_->s.frame;
}
void Mm1NativeMods::inputs(uint8_t a,uint8_t b){
 if((a|b)&~3u)throw std::runtime_error("Invalid Mega Man virtual inputs");
 auto& s=impl_->s;
 if(!impl_->cycling||!s.gameplay||a==3||s.phase)s.pending=0;
 else s.pending|=uint8_t(a&~s.previous);
 s.previous=a;
}
void Mm1NativeMods::instruction(ModContext& c,std::span<uint16_t,6> cpu,std::span<uint8_t> ram,std::span<uint8_t> hram){
 auto& p=*impl_;auto& s=p.s;unsigned pc=cpu[5];
#if GBCLLR_MM12_DX_ASSETS
 if(p.color){
  if(c.rom_bank==1&&pc==0x60af){std::copy_n(mm12_dx::mm1_title_object_palettes,64,s.object_palettes);std::copy_n(mm12_dx::mm1_title_palettes,64,s.background_palettes);p.load_background(mm12_dx::mm1_title_palettes);}
  if(pc==0x1afc||(c.rom_bank==1&&pc==0x64dd)||(c.rom_bank==12&&(pc==0x69da||pc==0x698c))){if(s.background_buffer[0]!=0x92)p.load_background(mm12_dx::mm1_select_palettes);s.resource_style=3;if(pc==0x1afc)std::copy_n(mm12_dx::mm1_select_palettes,64,s.background_palettes);}
  if(c.rom_bank==1&&pc==0x62d1){p.load_background(mm12_dx::mm1_getweapon_background_palettes);s.resource_style=3;}

  if(pc==0x1afc||(c.rom_bank==1&&pc==0x64dd)||(c.rom_bank==12&&(pc==0x69da||pc==0x698c))){std::copy_n(mm12_dx::mm1_select_object_palettes,64,s.object_palettes);if(pc==0x1afc)std::copy_n(mm12_dx::mm1_select_object_attributes,512,s.object_attributes);}
  if(c.rom_bank==11&&pc==0x4538)std::fill_n(s.object_attributes+1,511,1);
  if(c.rom_bank==11&&pc==0x4b00)for(unsigned plane=0;plane<2;plane++)std::fill_n(s.object_attributes+plane*256+0x43,7,3);
  if(c.rom_bank==11&&pc==0x460b){std::copy_n(mm12_dx::mm1_first_castle_object_palettes,64,s.object_palettes);std::copy_n(mm12_dx::mm1_fortress_background_palettes,64,s.background_palettes);p.load_background(mm12_dx::mm1_fortress_background_palettes);s.resource_style=3;}

  if(pc==0x2245){std::copy_n(mm12_dx::mm1_fortress_object_palettes,64,s.object_palettes);p.load_background(mm12_dx::mm1_fortress_background_palettes);}
  if(pc==0x227a){p.load_background(mm12_dx::mm1_ship_background_palettes);s.resource_style=3;s.ship_special=uint8_t(s.object_palettes[28]==0xfd);if(s.ship_special){std::copy_n(mm12_dx::mm1_ship_object_palettes,64,s.object_palettes);std::copy_n(mm12_dx::mm1_ship_object_attributes,512,s.object_attributes);}}
  if(c.rom_bank==14&&pc==0x4b2f){std::copy_n(mm12_dx::mm1_credits_object_palettes,64,s.object_palettes);p.load_background(mm12_dx::mm1_credits_background_palettes);s.resource_style=4;}
 }
#endif
 if(c.rom_bank==11&&pc==0x4b00){s.gameplay=0;s.pending=0;s.fixed_scene=p.color?11:0;s.fixed_valid=0;s.frame.valid=s.dma.valid=s.display.valid=0;s.count=0;}
 if(pc==0x227a){s.gameplay=0;s.pending=0;s.fixed_scene=p.color?6:0;s.fixed_valid=0;s.frame.valid=s.dma.valid=s.display.valid=0;s.count=0;}
 if(c.rom_bank==1&&(pc==0x40b7||pc==0x4429))std::fill_n(s.shot_valid,3,0);
 if(pc==0x1afc||(c.rom_bank==1&&(pc==0x60af||pc==0x62d1))||(c.rom_bank==12&&(pc==0x69da||pc==0x698c))||(c.rom_bank==14&&pc==0x4b2f))s.palette_active=0;
 if(c.rom_bank==1&&pc==0x60af)s.fixed_scene=1;
 // The original ending begins before its later credits graphics module.
 if(c.rom_bank==14&&pc==0x455b){s.gameplay=0;s.pending=0;s.fixed_scene=p.color?6:0;s.fixed_valid=0;s.frame.valid=s.dma.valid=s.display.valid=0;s.count=0;}
 if(c.rom_bank==1&&pc==0x62d1){s.gameplay=0;s.pending=0;s.fixed_scene=p.color?7:0;s.fixed_valid=0;s.frame.valid=s.dma.valid=s.display.valid=0;s.count=0;}
 if(pc==0x0aa3&&s.fixed_scene==7){s.fixed_scene=0;s.fixed_valid=0;}
 if(c.rom_bank==14&&pc==0x4b2f){s.fixed_scene=5;s.gameplay=0;s.pending=0;s.frame.valid=s.dma.valid=s.display.valid=0;std::fill_n(s.credits_attributes,512,1);}
  if(c.rom_bank==1&&pc==0x5c9c)s.fixed_scene=3;
 if(c.rom_bank==12&&(pc==0x69da||pc==0x698c)){s.fixed_scene=4;s.gameplay=0;s.frame.valid=s.dma.valid=s.display.valid=0;s.pending=0;}
 if((pc==0x0c8d||pc==0x0c10)&&s.fixed_scene==4){s.fixed_scene=0;s.fixed_valid=0;}
  if(c.rom_bank==1&&pc==0x5d0f){s.fixed_scene=0;s.fixed_valid=0;}
 if(pc==0x1afc){s.fixed_scene=2;s.gameplay=0;s.frame.valid=s.dma.valid=s.display.valid=0;s.pending=0;}
 // Native fortress and boss-intro groups have distinct authored bindings.
 if(c.rom_bank==11&&(pc==0x4538||pc==0x5313)){s.gameplay=0;s.pending=0;s.fixed_scene=p.color?(pc==0x4538?8:9):0;s.fixed_valid=0;s.frame.valid=s.dma.valid=s.display.valid=0;s.count=0;}
 if(pc==0x2245){s.gameplay=0;s.pending=0;s.fixed_scene=p.color?9:0;s.fixed_valid=0;s.frame.valid=s.dma.valid=s.display.valid=0;s.count=0;}
 if(c.rom_bank==1&&pc==0x64dd){s.fixed_scene=p.color?10:0;s.gameplay=0;s.pending=0;s.fixed_valid=0;}
 if(pc==0x2ff6||(c.rom_bank==1&&(pc==0x6109||pc==0x6178))){s.fixed_scene=0;s.fixed_valid=0;}
 if(pc==0x3000)p.reset_style(c);
 if(pc==0x2412)p.resource(c,cpu[1]>>8);
 if(pc==0x102a){
  s.sensor_valid=0;unsigned actor=Impl::word(c,0xcc40);
  if(p.expanded_domain(c)&&s.gameplay&&actor>=0xc600&&actor<0xcc00&&!(actor&63)&&c.rom_bank==Impl::peek(c,actor+0x1b)&&!Impl::peek(c,0xd869)){
   s.sensor_x=int(Impl::word(c,actor+0x0a))+int8_t(uint8_t(cpu[2])-Impl::peek(c,actor+0x0d));
   s.sensor_y=uint8_t((cpu[2]>>8)+Impl::peek(c,0xd860));s.sensor_valid=1;
  }
 }
 if(pc==0x0162||pc==0x167e||pc==0x18b3)p.copy_art(c,cpu[3],cpu[2],cpu[1]);
 if(pc==0x1e9c&&!(cpu[1]&15))p.copy_art(c,cpu[3],cpu[1],16);
 if(pc==0x2ff6||pc==0x0b87||(c.rom_bank==1&&pc==0x5c9c)){s.gameplay=0;s.frame.valid=s.dma.valid=s.display.valid=0;s.pending=0;s.count=0;if(pc==0x2ff6){s.resize_scans=2;std::fill_n(s.shot_valid,3,0);s.shot_actor=s.shot_box=0;}}
 if(s.phase&&pc==0x0a17&&cpu[4]==s.caller[4]){std::copy_n(s.caller,6,cpu.begin());s.phase=0;std::fill_n(s.caller,6,0);return;}
 // The player producer runs inside672D, before0A17's actor work. A complete
 // pose list begins after publishing the previous update at its real wait.
 if(pc==0x0a05){s.gameplay=1;p.publish(c);s.count=0;}
 if(pc==0x0a49&&s.gameplay)++s.logic_updates;
 if(pc==0x0a17&&!s.phase){
  unsigned edge=s.pending;s.pending=0;
  if(p.cycling&&(edge==1||edge==2)&&c.rom_bank==1&&ram.size()>=0x2000&&hram.size()>=127&&Impl::stack(cpu[4])&&ram[0x1802]==1&&!ram[0x1869]&&!ram[0x1fb1]){
   unsigned current=ram[0x1fa0],next=current;
   auto owned=[&](unsigned n){return ram[0x1fa1+(n>=8)]&(1u<<(n&7));};
   if(current<11){do{next=(next+(edge==1?10:1))%11;}while(next!=current&&!owned(next));
    if(next!=current){std::copy(cpu.begin(),cpu.end(),s.caller);s.target=uint8_t(next);s.phase=1;ram[0x1fa0]=s.target;Impl::push(cpu,ram,hram,0x0a17);cpu[5]=0x40b7;return;}}
  }
 }
 if(!s.gameplay)return;
 // Original destruction/pickup effects use this separate rectangular grid
 // writer. Capture its list before the linked OAM allocator can reject it.
 if(c.rom_bank==1&&pc==0x5c25){
  unsigned rows=cpu[1]>>8,cols=cpu[1]&255,source=cpu[2];
  if(rows&&rows<=8&&cols&&cols<=8)for(unsigned y=0;y<rows;++y)for(unsigned x=0;x<cols;++x){
   unsigned code=Impl::peek(c,source++);if(code==255)continue;
   int sx=int(Impl::peek(c,0xff91))+int(x*8);if(sx>=224)sx-=256;
   unsigned attr=Impl::peek(c,0xd3ea)|((code&128)?32:0)|((code&64)?64:0);
   p.emit(c,Impl::camera(c)+sx-8,int(Impl::peek(c,0xff92))+int(y*8)-16,
          uint8_t(Impl::peek(c,0xff93)+(code&63)),uint8_t(attr));
  }
 }
 if(c.rom_bank==1&&pc==0x4345&&!Impl::peek(c,0xdfa0)&&p.expanded_domain(c)){
  unsigned slot=Impl::shot_slot(cpu[1]&255);if(slot<3){
   int world=p.shot_position(c,slot),low=int(Impl::word(c,0xd861))-80,high=int(Impl::word(c,0xd863))+80;
   int left=std::max(low,p.view_left(c))-12,right=std::min(high,p.view_left(c)+p.width)+12;
   // Original4345 only culls the Buster at the viewport edge. Its motion,
   // damage, native Y check and all other weapons' authored limits remain.
   cpu[5]=world>=left&&world<right?0x434e:0x4356;return;
  }
 }
 if(c.rom_bank==1&&(pc==0x42cb||pc==0x4358||pc==0x4412)){
  unsigned slot=Impl::shot_slot(cpu[1]&255);if(slot<3){if(pc==0x4412||!Impl::peek(c,0xc239+slot*32))s.shot_valid[slot]=0;else p.shot_position(c,slot);}
 }
 if(pc==0x1093){
  s.shot_actor=s.shot_box=0;
  if(p.expanded_domain(c)&&Impl::word(c,cpu[4])==0x2b3a){unsigned actor=Impl::word(c,cpu[4]+2);if(actor>=0xc600&&actor<0xcc00&&!(actor&63))s.shot_actor=uint16_t(actor);}
 }
 if(pc==0x10ae)s.shot_box=uint16_t(cpu[3]==0xc204||cpu[3]==0xc218?cpu[3]:0);
 if(pc==0x10d0&&s.shot_actor&&s.shot_box&&p.expanded_domain(c)){
  unsigned pointer=Impl::peek(c,0xc22a),slot=pointer>=11?Impl::shot_slot(pointer-11):3;
  if(slot<3&&s.shot_valid[slot]){
   unsigned rectangle=Impl::word(c,0xce53);int world=int(Impl::word(c,s.shot_actor+10));
   int left=world+int8_t(Impl::peek(c,rectangle))-int8_t(Impl::peek(c,s.shot_box+1));
   int right=world+int8_t(Impl::peek(c,rectangle+1))-int8_t(Impl::peek(c,s.shot_box+2));
   // Original10D0 compares modulo256 X. Select only that overlap in the
   // real world, then retain the original Y, weapon damage and HP services.
   cpu[3]=uint16_t((cpu[3]&0xff00)|uint8_t(cpu[3]+1));
   cpu[5]=s.shot_x[slot]>=left&&s.shot_x[slot]<right?0x10d7:0x10e0;return;
  }
 }
 if(pc==0x2b0e&&p.expanded_domain(c)){
  unsigned actor=Impl::word(c,cpu[4]);
  if(actor>=0xc600&&actor<0xcc00&&!(actor&63)){
   int world=int(Impl::word(c,actor+0x0a)),player=int(Impl::word(c,0xd80d));
   unsigned box=Impl::word(c,0xce53);
   int left=world+int8_t(Impl::peek(c,box)),right=world+int8_t(Impl::peek(c,box+1));
   int player_left=player+int8_t(Impl::peek(c,0xd841)-Impl::peek(c,0xd821));
   int player_right=player+int8_t(Impl::peek(c,0xd842)-Impl::peek(c,0xd821));
   // Original2AF9..2B06 has populated CC46 and rectangle registers.
   // Saved BC at SP still identifies the actor. Contact uses modulo256 X.
   // Keep the
   // real world separation; continue at the original weapon-collision path.
   if(right<player_left||left>=player_right){cpu[5]=0x2b2a;return;}
  }
 }
 if(c.rom_bank==1&&pc==0x6f5d)p.body(c,cpu[3]);
 // Original Buster pellets use their own one-tile OAM writer rather than
 // the generic mapping service. Capture before its hardware slot lookup so
 // an exhausted OAM list cannot discard an otherwise visible pellet.
 if(c.rom_bank==1&&pc==0x4388){
  unsigned actor=0xc200+(cpu[1]&255),slot=Impl::shot_slot(cpu[1]&255);int x=Impl::peek(c,actor+4);if(x>=216)x-=256;
  p.emit(c,(slot<3&&s.shot_valid[slot]?s.shot_x[slot]:Impl::camera(c)+x)-4,int(Impl::peek(c,actor+8))-4,0x65,uint8_t(Impl::peek(c,0xd80b)&128));
 }
 // The twelve native enemy-shot records use a separate single-tile writer,
 // outside both the actor mapping and player Buster services. Capture at its
 // allocation boundary so an exhausted/alternating OAM list cannot hide them.
 if(pc==0x3846&&s.gameplay){
  unsigned actor=0xcd00|(cpu[1]&255);int x=Impl::peek(c,actor+4);if(x>=224)x-=256;
  p.emit(c,Impl::camera(c)+x-3,int(Impl::peek(c,actor+8))-4,Impl::peek(c,actor+9),0x10);
 }
 if(pc==0x2cf0&&(cpu[0]>>8)<64)p.mapping(c,cpu[3],false,cpu[1],cpu[0]>>8);
 if(pc==0x2cd6&&(cpu[0]>>8)<64)p.mapping(c,cpu[3],true,cpu[1],cpu[0]>>8);
}
void Mm1NativeMods::complete_frame(ModContext& c,bool cleared){auto& s=impl_->s;s.display=s.dma;std::copy_n(s.raster,144,s.display_raster);std::memcpy(s.display_planes,s.planes,sizeof s.planes);impl_->fixed_frame(c,cleared);if(cleared)s.frame.valid=s.dma.valid=s.display.valid=0;}
void Mm1NativeMods::scanline(ModContext& c,uint8_t line){
 if(line>=144||!c.ppu)return;
 auto& p=*impl_;auto& s=p.s;if(!line){p.style_colors(c,true);p.first_line(c);}
 s.raster[line]={c.ppu->scx,c.ppu->scy,c.ppu->lcdc,c.ppu->bgp,{c.ppu->obp0,c.ppu->obp1}};
 unsigned row=((unsigned(line)+c.ppu->scy)&7)*2;
 for(unsigned tile=0;tile<256;tile++){unsigned at=(c.ppu->lcdc&16)?tile*16:unsigned(0x1000+int(int8_t(tile))*16);auto source=p.texture(c,at,16);s.planes[line][tile*2]=source[row];s.planes[line][tile*2+1]=source[row+1];}
}
uint32_t Mm1NativeMods::cpu_budget(const ModContext& c,bool cleared)const{
 // The authored CGB boot requests double speed (I15:4057; II16:4002).
 // Reuse the gameplay-only budget while retaining the physical PPU/APU clock.
 return (impl_->smooth||impl_->color)&&!cleared&&impl_->s.gameplay&&c.ppu&&(c.ppu->lcdc&128)&&Impl::peek(c,0xd802)==1&&!Impl::peek(c,0xd869)&&!Impl::peek(c,0xdfb1)?280896u:0;
}
void Mm1NativeMods::viewport(unsigned w,unsigned h){if(impl_->requested<0&&h){int width=std::clamp<unsigned>((uint64_t(w)*144+h/2)/h,160,4096);if(width!=impl_->width)impl_->s.resize_scans=2;impl_->width=width;impl_->s.width=unsigned(width);}}
ModView Mm1NativeMods::render(ModContext& c,std::span<const uint32_t> native){
 auto& p=*impl_;auto& f=p.s.display;if(p.color&&p.s.fixed_valid){auto view=p.fixed_view(native);return p.s.fixed_kind==3||p.s.fixed_kind==12?megaman_pause_canvas(view,unsigned(p.width),p.pause_canvas):view;}
 if(p.color&&p.s.fixed_scene==3){p.pause_canvas.assign(size_t(p.width)*144,0xff000000u);return {unsigned(p.width),p.width>160,p.pause_canvas};}
 if(!f.valid){if(p.color&&!p.s.fixed_scene){p.pause_canvas.assign(size_t(p.width)*144,0xff000000u);return {unsigned(p.width),p.width>160,p.pause_canvas};}return {160,false,native};}
 if(p.width==160&&!p.flicker&&!p.color)return {160,false,native};
 int width=p.width,left=f.camera-(width-160)/2;
 if(f.bound_right-f.bound_left>=width)left=std::clamp(left,f.bound_left,f.bound_right-width);
 else left=f.bound_left-(width-(f.bound_right-f.bound_left))/2;
 p.pixels.assign(size_t(width)*144,0xff000000u);p.background.assign(size_t(width)*144,0);
 for(int y=0;y<128;y++)for(int x=0;x<width;x++){
  const auto& raster=p.s.display_raster[y];int wx=left+x+int8_t(raster.scx-f.scroll_x),physical_y=(y+raster.scy)&255,wy=physical_y&127;if(wx<f.bound_left||wx>=f.bound_right||wx<0||wx>=f.columns*16||wy>=f.rows*16)continue;
  unsigned original=Impl::rom(c,f.bank,f.row[wy>>4]+(wx>>4)),block=p.presentation(c,f.bank,f.row[wy>>4]+(wx>>4));
  // Original writable collision/tile ring keeps destructible blocks current.
  // The ring streams whole16-pixel columns. A fractional camera position
  // does not make the next column valid; reading it exposed stale/zero cells
  // as a moving strip at the physical screen boundary.
  // Room coordinates reset at a link; the physical collision ring retains
  // both its X origin and alternating128px Y page. Preserve those cache
  // coordinates when applying native destructible-block edits to room art.
  if(wx>=(f.camera&~15)-16&&wx<(f.camera&~15)+176){int cache_x=wx+int(f.scroll_x)-f.camera;unsigned changed=f.blocks[(physical_y&0xf0)|((cache_x>>4)&15)];if(changed!=original)block=changed;}
  unsigned tile=p.presentation(c,f.bank,f.metatiles+block*4+((wx&8)?2:0)+((wy&8)?1:0));
  unsigned bit=7-(wx&7);const auto& planes=p.s.display_planes[y];unsigned ink=((planes[tile*2]>>bit)&1)|(((planes[tile*2+1]>>bit)&1)<<1);p.background[y*width+x]=uint8_t(ink);
  unsigned attr=p.color?f.dx_attributes[tile]:0;
  p.pixels[y*width+x]=p.color?p.rgb(f.dx_background,(attr&7)*8,ink,0xe4):f.bg[(raster.bgp>>(ink*2))&3];
 }
 int hudx=(width-160)/2;
 for(int y=128;y<144;y++)std::copy_n(native.begin()+y*160,160,p.pixels.begin()+y*width+hudx);
#if GBCLLR_MM12_DX_ASSETS
 if(p.color)for(int y=128;y<144;y++){
  std::fill_n(p.pixels.begin()+y*width,width,0xff000000u);
  for(unsigned x=0;x<160;x++){
   unsigned row=unsigned(y-128),column=x/8,tile=f.vram[0x1c00+(row/8)*32+column],palette=column==12||column==13?(row<8?6:7):7;
   bool weapon=column<5||column==5||column==6;bool alternate=column==5||column==6;
   if(tile==0x70)tile=0x73;
   if(alternate)tile=mm12_dx::mm1_hud_icons[f.weapon*6+(row/8)*2+column-5];
   unsigned at=0x1000+tile*16,line=(row&7)*2,bit=7-(x&7);const auto* image=alternate?p.data.extra.data()+at:f.vram+at;
   unsigned ink=((image[line]>>bit)&1)|(((image[line+1]>>bit)&1)<<1);
   p.pixels[y*width+hudx+x]=weapon?p.rgb(mm12_dx::mm1_hud_weapon_palettes,f.weapon*8,ink,0xe4):p.rgb(f.dx_background,palette*8,ink,0xe4);
  }
 }
#endif
 p.framing_background=p.pixels;p.order.clear();
 if(p.color)for(unsigned n=f.count;n-->0;)p.order.push_back(n);
 else {
  for(unsigned n=0;n<f.count;n++)p.order.push_back(n);
  // The original monochrome cartridge uses DMG X priority even on a CGB.
  // First select the opaque winning OBJ; its behind-BG bit cannot reveal a
  // lower-priority OBJ through an opaque background pixel.
  std::stable_sort(p.order.begin(),p.order.end(),[&](unsigned a,unsigned b){return f.sprite[a].x<f.sprite[b].x;});
 }
 for(unsigned n:p.order){auto& s=f.sprite[n];int sx=s.x-left;
  for(int dy=0;dy<8;dy++){int yy=s.y+dy;if(yy<0||yy>=128)continue;const auto& raster=p.s.display_raster[yy];if(!(raster.lcdc&2))continue;int ty=(s.attr&64)?7-dy:dy;
   for(int dx=0;dx<8;dx++){int xx=sx+dx;if(xx<0||xx>=width||left+xx<f.bound_left||left+xx>=f.bound_right)continue;int tx=(s.attr&32)?dx:7-dx;
    unsigned color=((s.tiles[ty*2]>>tx)&1)|(((s.tiles[ty*2+1]>>tx)&1)<<1);if(!color)continue;
    auto& bg=p.background[yy*width+xx];if(!p.color){if(bg&128)continue;bg|=128;}if((s.attr&128)&&(bg&3))continue;
    unsigned pal=(s.attr>>4)&1;
    p.pixels[yy*width+xx]=p.color?(s.color&7?p.rgb(f.dx_objects,(s.color&7)*8,color,0xe4):p.rgb(
#if GBCLLR_MM12_DX_ASSETS
      f.weapon==6?std::span<const uint8_t>(mm12_dx::mm1_weapon6_colors):p.data.weapon_pal,f.weapon==6?mm12_dx::mm1_weapon6_color_offsets[raster.obp[0]]:f.weapon*8,
#else
      p.data.weapon_pal,f.weapon*8,
#endif
      color,0xe4)):f.obj[pal*4+((raster.obp[pal]>>(color*2))&3)];
   }
  }
 }
 ModView v;v.width=unsigned(width);v.expanded=width>160;v.pixels=p.pixels;v.camera_x=f.camera;v.left=left;v.bound_left=f.bound_left;v.bound_right=f.bound_right;v.sprites=f.count;v.captures=p.s.captures;v.spawn_scans=p.s.spawn_scans;v.spawn_reads=p.s.spawn_reads;v.logic_updates=p.s.logic_updates;v.activation_adjustments=p.s.terrain_reads;v.framing_width=unsigned(std::clamp(f.bound_right-f.bound_left,160,width));v.framing_background=p.framing_background;return v;
}
std::vector<uint8_t> Mm1NativeMods::snapshot()const{std::vector<uint8_t> b(sizeof impl_->s);std::memcpy(b.data(),&impl_->s,b.size());return b;}
void Mm1NativeMods::restore(std::span<const uint8_t> b){
 if(b.size()!=sizeof impl_->s)throw std::runtime_error("Invalid Mega Man I native state size");
 auto staged=std::make_unique<Impl::State>();std::memcpy(staged.get(),b.data(),b.size());
 for(auto valid:staged->shot_valid)if(valid>1)throw std::runtime_error("Invalid Mega Man I projectile state");
 if((staged->shot_actor&&(staged->shot_actor<0xc600||staged->shot_actor>=0xcc00||(staged->shot_actor&63)))||(staged->shot_box&&staged->shot_box!=0xc204&&staged->shot_box!=0xc218))throw std::runtime_error("Invalid Mega Man I projectile owner");
 if(staged->resource_style>4||staged->palette_profile>12||staged->palette_phase>7||staged->palette_active>1||staged->ship_special>1)throw std::runtime_error("Invalid Mega Man I authored resource state");
 if(staged->fixed_scene>11||staged->fixed_kind>12||staged->fixed_valid>1||staged->fixed_profile>12||staged->fixed_weapon>10)throw std::runtime_error("Invalid Mega Man I fixed-scene state");
 if(staged->count>Impl::max_sprites||staged->dma.count>Impl::max_sprites||staged->dma.rows>32||staged->dma.profile>12||staged->dma.weapon>10||staged->dma.valid>1||staged->frame.count>Impl::max_sprites||staged->display.count>Impl::max_sprites||staged->frame.rows>32||staged->display.rows>32||staged->frame.profile>12||staged->display.profile>12||staged->frame.weapon>10||staged->display.weapon>10||staged->phase>1||staged->target>10||staged->pending>2||staged->previous>3||staged->gameplay>1||staged->frame.valid>1||staged->display.valid>1||staged->resize_scans>2||staged->sensor_valid>1||staged->width<160||staged->width>4096||(staged->phase&&!Impl::stack(staged->caller[4])))throw std::runtime_error("Invalid Mega Man I native state");
 impl_->s=*staged;impl_->width=int(staged->width);
}
}
