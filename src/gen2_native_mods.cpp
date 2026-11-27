#include "gen2_native_mods.hpp"
#include "native_mods.hpp"
#include "gen2_gifts.hpp"
#include <gbcllr/machine.hpp>
#include <algorithm>
#include <array>
#include <cstring>
#include <stdexcept>
#include <type_traits>

namespace gbcllr {
namespace {
constexpr std::string_view gold="fb0016d27b1e5374e1ec9fcad60e6628d8646103b5313ca683417f52b97e7e4e";
constexpr std::string_view silver="72b190859a59623cbef6c49d601f8de52c1d2331b4f08a8d2acc17274fc19a8c";
constexpr std::string_view crystal="fdcc3c8c43813cf8731fc037d2a6d191bac75439c34b24ba1c27526e6acdc8a2";
constexpr int maximum_width=4096,block_bytes=1300;
// Both exact4b57 symbol maps agree on these callers/data. The source contract
// is pret/pokegold f2cde36f: home/map.asm, engine/tilesets/map_palettes.asm,
// engine/overworld/map_objects.asm and data/sprites/facings.asm.
// Crystal v1.1 uses its own3b13 symbols and pret/pokecrystal3c813b0b source.
// In particular its palette nibbles encode VRAM banks rather than Gold's
// map-group palette sentinel, and its object tile high bit selects OBJ bank.
struct Bindings {
 uint16_t map,environment,height,width,border,battle,blocks,anchor,meta_y,meta_x,bg_anchor;
 uint16_t blocks_bank,blocks_address,palettes,objects,scroll_x,scroll_y,facings,caller,flags;
 uint8_t map_anims,palette_bank;
 uint16_t map_objects,object_masks,used_sprites,movement_data,sprite_headers;
 unsigned sprite_slots;
};
constexpr Bindings gold_silver_bindings{
 0xda00,0xd083,0xd087,0xd088,0xd086,0xd116,0xc700,0xd07d,0xd07f,0xd080,0xd05b,
 0xd0c5,0xd0c6,0xd0cf,0xd1fd,0xce81,0xce82,0x404a,0x5896,0xcfe5,0x60,2,
 0xd445,0xd545,0xd05d,0x4274,0x47de,12};
constexpr Bindings crystal_bindings{
 0xdcb5,0xd19a,0xd19e,0xd19f,0xd19d,0xd22d,0xc800,0xd194,0xd196,0xd197,0xd152,
 0xd1dc,0xd1dd,0xd1e6,0xd4d6,0xd14c,0xd14d,0x4049,0x5920,0xd0ed,0x5e,0x13,
 0xd71e,0xd81e,0xd154,0x4273,0x4736,32};
uint8_t ram(const ModContext& c,unsigned address,unsigned bank=1) {
 return c.wram[(address<0xd000?0:bank*0x1000)+(address&0xfff)];
}
unsigned word(const ModContext& c,unsigned address){return ram(c,address)|(unsigned(ram(c,address+1))<<8);}
uint8_t rom(const ModContext& c,unsigned bank,unsigned address) {
 if(address<0x4000||address>=0x8000||bank>=c.rom_size/0x4000)return 0;
 return c.rom[bank*0x4000+address-0x4000];
}
int floor_div(int value,int divisor){return value>=0?value/divisor:-((-value+divisor-1)/divisor);}
struct Sprite {int x,y;uint8_t tile,attributes;};
struct ActorPose {
 uint8_t valid=0;
 std::array<uint8_t,15> identity{}; // Original map object, excluding live slot ID.
 int x=0,y=0; // Last original native pose, in world pixels.
 uint8_t facing=0,attributes=0,flags=0;
};
struct State {
 uint32_t valid=0,captures=0,callers=0; //0 unavailable,1 field,2 native text boxes,3 native map-name sign.
 int camera_x=0,camera_y=0,count=0;
 uint16_t blocks_address=0,palettes_address=0;
 uint8_t blocks_bank=0,border=0,lcdc=0;
 std::array<uint8_t,block_bytes> blocks{};
 std::array<uint8_t,0x4000> vram{};
 std::array<uint32_t,32> bg{},obj{};
 std::array<uint8_t,256> palette{};
 std::array<Sprite,128> sprites{};
 uint32_t pose_map=0;
 std::array<ActorPose,16> poses{};
};
// Derived presentation metadata. None of this enters the legacy module state.
struct MapInfo {
 unsigned group=0,id=0,tileset=0,environment=0,attributes_bank=0,attributes=0;
 unsigned width=0,height=0,border=0,bank=0,blocks=0,connections=0;
};
struct DialogLayout {
 // Native tile rectangles, including full-width dialogue and smaller field
 // choices/menus. Only full-width boxes stretch; other UI stays native.
 std::array<std::array<int,4>,4> boxes{};
 unsigned count=0;
 int description_left=0;
 bool covers(int column,int row)const {
  if(description_left&&column<description_left&&row>=13)return true;
  for(unsigned n=0;n<count;++n) {
   const auto& b=boxes[n];if(column>=b[0]&&column<=b[2]&&row>=b[1]&&row<=b[3])return true;
  }
  return false;
 }
};
struct Geometry {
 MapInfo current;
 std::array<MapInfo,4> neighbors{};
 std::array<int,4> x_offset{},y_offset{};
};
}
struct Gen2NativeMods::Impl {
 unsigned variant=0;
 const Bindings& b;
 int requested=-1,stored_width=256;
 uint32_t gift=0,gift_consumed=0;
 State s{};
 State completed{};
 Geometry pending_geometry{},geometry{};
 std::array<int,2> host_size{256,144};
 std::vector<uint32_t> pixels;
 std::vector<uint8_t> background_nonzero;
 bool pending_field=false; // Local capture result; never retained across calls.
 explicit Impl(std::string_view identity,const NativeFeature& feature,bool cgb):b(identity==crystal?crystal_bindings:gold_silver_bindings) {
  if(identity!=gold&&identity!=silver&&identity!=crystal)throw std::runtime_error("Gen2 view requires a qualified Gold, Silver or Crystal revision");
  if(!cgb)throw std::runtime_error("This Gen2 view requires CGB hardware");
  if(feature.plugin!="gbcllr.gen2.adaptive-view"||feature.abi!=1||feature.colorization||!feature.asset_input.empty()||feature.gift>gen2_gifts.size()||
     (feature.view_width!=-1&&(feature.view_width<160||feature.view_width>maximum_width)))
   throw std::runtime_error("Unsupported Gen2 native view profile");
  variant=identity==crystal?2:identity==silver?1:0;requested=feature.view_width;
  if(requested>=0)host_size[0]=requested;
  gift=feature.gift;
  // Snapshot bytes include this build-specific aggregate. Keep padding stable
  // as well as fields; never copy padding from a temporary sprite aggregate.
  std::memset(&s,0,sizeof s);
  std::memset(&completed,0,sizeof completed);
  std::memset(&pending_geometry,0,sizeof pending_geometry);
  std::memset(&geometry,0,sizeof geometry);
 }
 void capture(ModContext& c) {
  s.valid=0;pending_field=false;
  if(c.wram&&s.pose_map!=word(c,b.map)) {
   std::memset(s.poses.data(),0,sizeof s.poses);s.pose_map=word(c,b.map);
  }
  std::memset(&pending_geometry,0,sizeof pending_geometry);
  capture_presentation(c);
 }
 // Crystal's field UI occupies the BG itself. Recognize complete authored
 // frames, then still qualify every uncovered world tile. Battles, partial
 // frames and unrelated BG changes retain their ordinary guards.
 DialogLayout dialog_tiles(const uint8_t* tiles,unsigned stride)const {
  DialogLayout out;
  if(variant!=2||!tiles)return out;
  auto tile=[&](int x,int y){return tiles[unsigned(y)*stride+unsigned(x)];};
  auto box=[&](int left,int top,int right,int bottom) {
   if(tile(right,top)!=0x7b||tile(left,bottom)!=0x7d||tile(right,bottom)!=0x7e)return false;
   for(int x=left+1;x<right;++x)if(tile(x,top)!=0x7a||(tile(x,bottom)!=0x7a&&!(x==right-1&&tile(x,bottom)==0xee)))return false;
   for(int y=top+1;y<bottom;++y)if(tile(left,y)!=0x7c||tile(right,y)!=0x7c)return false;
   return true;
  };
  for(int top=0;top<16;++top)for(int left=0;left<18;++left)if(tile(left,top)==0x79)
   for(int bottom=top+2;bottom<18;++bottom)for(int right=left+2;right<20;++right)
    if(box(left,top,right,bottom)&&out.count<out.boxes.size()) {
     out.boxes[out.count++]={left,top,right,bottom};
     // The native field menu includes an unframed description below its
     // left field area. Its complete right frame owns that bounded footer.
     if(top==0&&right==19&&bottom==15)out.description_left=left;
    }
  return out;
 }
 DialogLayout dialog_layout(const uint8_t* vram,uint8_t lcdc)const {
  return dialog_tiles(vram?vram+((lcdc&8)?0x1c00:0x1800):nullptr,32);
 }
 bool map_name_sign(const uint8_t* vram,uint8_t lcdc)const {
  if(variant!=2||!vram)return false;
  unsigned map=(lcdc&64)?0x1c00:0x1800;
  constexpr unsigned left[]{0x61,0x65,0x66,0x67},right[]{0x64,0x6b,0x6c,0x6a};
  for(unsigned y=0;y<4;++y)if(vram[map+y*32]!=left[y]||vram[map+y*32+19]!=right[y])return false;
  for(unsigned x=1;x<19;++x) {
   unsigned ornament=(x-1)%4<2;
   if(vram[map+x]!=0x62+ornament||vram[map+3*32+x]!=0x68+ornament||vram[map+32+x]!=0x6d)return false;
  }
  return true;
 }
 // The published Window can outlive its gameplay timer by a frame.
 bool map_name_sign(const ModContext& c)const {
  return c.ppu&&c.wram&&c.ppu->wx==7&&c.ppu->wy==112&&(c.ppu->lcdc&32)&&
   map_name_sign(c.vram,c.ppu->lcdc);
 }
 bool field_window(const ModContext& c)const {
  return variant==2&&c.ppu&&c.wram&&(c.ppu->lcdc&32)&&c.ppu->wx==7&&c.ppu->wy==0&&completed.valid&&
   geometry.current.group==ram(c,b.map)&&geometry.current.id==ram(c,b.map+1);
 }
 void capture_frame(ModContext& c,State& s,unsigned map_width,unsigned map_height) {
  s.valid=0;
  if(!c.wram||!c.vram||!c.hram||!c.rom||!c.ppu)return;
  bool window=field_window(c);
  uint8_t lcdc=window?uint8_t((c.ppu->lcdc&~8)|((c.ppu->lcdc&64)?8:0)):c.ppu->lcdc;
  unsigned scx=window?0:c.ppu->scx,scy=window?0:c.ppu->scy;
  const auto dialog=!scx&&!scy?dialog_layout(c.vram,lcdc):DialogLayout{};
  bool sign=map_name_sign(c);
  if(ram(c,b.battle)||
     !(c.ppu->lcdc&0x80)||(c.ppu->lcdc&4)||
     ((c.ppu->lcdc&32)&&c.ppu->wx<167&&c.ppu->wy<144&&!sign&&!window))return;
  const unsigned stride=map_width+6,rows=map_height+6;
  if(!map_width||!map_height||stride*rows>s.blocks.size())return;
  unsigned anchor=word(c,b.anchor),bg_anchor=word(c,b.bg_anchor);
  unsigned blocks_address=word(c,b.blocks_address),palettes_address=word(c,b.palettes),bank=ram(c,b.blocks_bank);
  if(anchor<b.blocks||anchor>=b.blocks+stride*rows||bg_anchor<0x9800||bg_anchor>=0xa000||
     bank>=c.rom_size/0x4000||blocks_address<0x4000||blocks_address>0x7800||
     palettes_address<0x4000||palettes_address>0x7f80)return;
  auto cell=anchor-b.blocks;
  // The VRAM anchor moves a whole16px step before SCX/SCY finish scrolling.
  // Their signed distance supplies the in-progress pixel offset, including
  // wraparound, without predicting or modifying player/guest state.
  s.camera_x=(int(cell%stride)-3)*32+ram(c,b.meta_x)*16+int8_t(c.ppu->scx-((bg_anchor&31)*8));
  s.camera_y=(int(cell/stride)-3)*32+ram(c,b.meta_y)*16+int8_t(c.ppu->scy-(((bg_anchor>>5)&31)*8));
  // During field-menu setup the hardware Window temporarily owns the same
  // field. BG scroll belongs to its clear plane, not that retained picture.
  // Keep the last validated same-map camera, then prove every visible tile.
  if(window){s.camera_x=completed.camera_x;s.camera_y=completed.camera_y;}
  s.blocks_bank=uint8_t(bank);s.blocks_address=uint16_t(blocks_address);
  s.palettes_address=uint16_t(palettes_address);s.border=ram(c,b.border);s.lcdc=lcdc;
  std::copy_n(c.wram+(b.blocks-0xc000),block_bytes,s.blocks.begin());std::copy_n(c.vram,0x4000,s.vram.begin());
  std::copy_n(c.ppu->bg_rgb,32,s.bg.begin());std::copy_n(c.ppu->obj_rgb,32,s.obj.begin());
  for(unsigned tile=0;tile<256;++tile) {
   unsigned packed=rom(c,b.palette_bank,palettes_address+tile/2),palette=(tile&1?packed>>4:packed)&15;
   s.palette[tile]=uint8_t(variant!=2&&(palette&8)?rom(c,2,0x45d7+ram(c,b.map)-1):palette);
  }
  // Menus/text reuse the background layer, not always the hardware window.
  // Qualify the visible tilemap against the actual world before extending it.
  // This also centers partially transferred maps during native transitions.
  unsigned map=(s.lcdc&8)?0x1c00:0x1800;
  pending_field=true;
  for(int y=0;y<18;++y)for(int x=0;x<20;++x) {
   if(dialog.covers(x,y)||(sign&&y>=14))continue;
   unsigned address=map+(((scy/8+y)&31)*32)+((scx/8+x)&31);
   unsigned tile=native_tile(c,s,stride,rows,floor_div(s.camera_x,8)*8+x*8,floor_div(s.camera_y,8)*8+y*8);
   if(s.vram[address]!=(variant==2?tile&127:tile))return;
  }
  s.count=0;
  for(unsigned object=1;object<16;++object) {
   auto& pose=s.poses[object];unsigned p=b.map_objects+object*16;
   bool same=true;for(unsigned n=0;n<15;++n)same&=pose.identity[n]==ram(c,p+1+n);
   if(!same||ram(c,b.object_masks+object))pose.valid=0;
   for(unsigned n=0;n<15;++n)pose.identity[n]=ram(c,p+1+n);
  }
  int player_map_x=ram(c,b.objects+16),player_x=ram(c,b.objects+23);
  int player_map_y=ram(c,b.objects+17),player_y=ram(c,b.objects+24);
  int scroll_x=int8_t(ram(c,b.scroll_x)),scroll_y=int8_t(ram(c,b.scroll_y));
  uint64_t used_oam=0;
  for(unsigned object=0;object<13;++object) {
   unsigned base=b.objects+object*40;
   if(!ram(c,base))continue;
   unsigned index=ram(c,base+1);
   ActorPose* pose=index&&index<16&&!ram(c,b.object_masks+index)&&ram(c,b.map_objects+index*16+1)==ram(c,base)?&s.poses[index]:nullptr;
   if(ram(c,base+4)&1){if(pose)pose->valid=0;continue;}
   unsigned facing=ram(c,base+13);
   // Native OFF_SCREEN objects retain their state but suppress their facing.
   // This is presentation culling, unlike the independent INVISIBLE flag.
   if(facing==255&&(ram(c,base+5)&64))facing=ram(c,base+8)&12;
   if(facing>=16) {
    // Original allocation starts with facing FF before its first OAM packet.
    // Allocation is not publication: bridge that native return frame with
    // the same retained body that was visible while this slot was dormant.
    if(facing==255&&pose&&pose->valid)emit_actor(c,s,pose->x-s.camera_x,pose->y-s.camera_y,
     pose->facing,ram(c,base+2),pose->attributes,pose->flags);
    continue;
   }
   int expected_x=(int(ram(c,base+16))-player_map_x)*16+player_x;
   int expected_y=(int(ram(c,base+17))-player_map_y)*16+player_y;
   int x=expected_x+int8_t(ram(c,base+23)-expected_x)+int8_t(ram(c,base+25))+scroll_x;
   int y=expected_y+int8_t(ram(c,base+24)-expected_y)+int8_t(ram(c,base+26))+scroll_y-4;
   // Native sprite/scroll sums are eight-bit coordinates. Project that
   // wrapped value beside the object's original map cell before anchoring
   // OAM; crossing x/y=0 must not move its retained world pose by256 pixels.
   int map_x=(int(ram(c,base+16))-4)*16-s.camera_x;
   int map_y=(int(ram(c,base+17))-4)*16-s.camera_y-4;
   x=map_x+int8_t(x-map_x);y=map_y+int8_t(y-map_y);
   unsigned flags=ram(c,base+5);
   unsigned attributes=(ram(c,base+6)&7)|((flags&0x80)?0x80:0)|((flags&16)?16:0);
   // Match Crystal InitSprites' actual branch: a clear object-tile bit7
   // adds OAM_BANK1; the emitted relative tile base always clears bit7.
   unsigned sprite_tile=ram(c,base+2);
   unsigned first=unsigned(s.count);
   emit_actor(c,s,x,y,facing,sprite_tile,attributes,flags);
   bool published=anchor_oam(c,s,first,x,y,facing,sprite_tile,attributes,flags,used_oam);
   if(!published&&c.oam&&pose&&pose->valid) {
    // The guest may already have built next frame's pose, or suppressed its
    // facing after native culling. Keep the last published world pose until
    // its next actual DMA packet, independently of viewport visibility.
    s.count=int(first);x=pose->x-s.camera_x;y=pose->y-s.camera_y;
    emit_actor(c,s,x,y,pose->facing,sprite_tile,pose->attributes,pose->flags);
   } else if(pose) {
    pose->valid=1;pose->x=x+s.camera_x;pose->y=y+s.camera_y;
    pose->facing=uint8_t(facing);pose->attributes=uint8_t(attributes);pose->flags=uint8_t(flags);
   }
  }
  // Original CopyMapObjectToObjectStruct/GetSpriteVTile inputs, without
  // allocating guest objects, running their AI or changing native culling.
  // A culled ordinary NPC retains its last native world pose, or its authored
  // initial pose if not yet visited. Native offscreen AI remains dormant.
  // Native masks own event/time/script visibility. Original map/object/mask
  // changes invalidate these sixteen fixed poses, independent of host width.
  for(unsigned object=1;object<16;++object) {
   unsigned p=b.map_objects+object*16,sprite=ram(c,p+1),movement=ram(c,p+4);
   if(ram(c,p)!=255||ram(c,b.object_masks+object)||!sprite||sprite>(variant==2?0x66u:0x5fu)||movement>10)continue;
   unsigned tile=0;bool loaded=false;
   for(unsigned n=1;n<b.sprite_slots;++n)if(ram(c,b.used_sprites+n*2)==sprite) {
    tile=ram(c,b.used_sprites+n*2+1);loaded=true;break;
   }
   if(!loaded)continue; // Never borrow another actor's fallback graphics.
   unsigned data=b.movement_data+movement*6;
   if(rom(c,1,data+3)&1)continue;
   unsigned facing=(rom(c,1,data+1)&3)*4,flags=rom(c,1,data+4);
   unsigned palette=ram(c,p+8)&0xf0;
   palette=palette?(palette>>4)&7:rom(c,5,b.sprite_headers+(sprite-1)*6+5)&7;
   unsigned attributes=palette|((flags&128)?128:0)|((flags&16)?16:0);
   int x=(int(ram(c,p+3))-4)*16-s.camera_x;
   int y=(int(ram(c,p+2))-4)*16-s.camera_y-4;
   const auto& pose=s.poses[object];
   if(pose.valid) {
    x=pose.x-s.camera_x;y=pose.y-s.camera_y;facing=pose.facing;
    attributes=pose.attributes;flags=pose.flags;
   }
   emit_actor(c,s,x,y,facing,tile,attributes,flags);
  }
  s.valid=sign?3:dialog.count?2:1;++s.captures;
 }
 unsigned actor_packet(ModContext& c,std::array<Sprite,4>& packet,int x,int y,unsigned facing,unsigned sprite_tile,unsigned attributes,unsigned flags) {
  unsigned pointer=rom(c,1,b.facings+facing*2)|(unsigned(rom(c,1,b.facings+1+facing*2))<<8);
  unsigned count=rom(c,1,pointer++);
  if(variant==2){attributes=(attributes&~8u)|((sprite_tile&128)?0:8);sprite_tile&=127;}
  if(count>4)return 0;
  for(unsigned n=0;n<count;++n) {
    int dy=int8_t(rom(c,1,pointer++)),dx=int8_t(rom(c,1,pointer++));
    unsigned relative=rom(c,1,pointer++),tile=rom(c,1,pointer++);
    if(!(relative&4))tile+=sprite_tile;
    unsigned extra=(relative&2)&&(flags&8)?0x80:0;
    auto& sprite=packet[n];
    sprite.x=x+dx;sprite.y=y+dy;sprite.tile=uint8_t(tile);
    sprite.attributes=uint8_t(((relative|extra)&0xf0)|attributes);
  }
  return count;
 }
 void emit_actor(ModContext& c,State& s,int x,int y,unsigned facing,unsigned tile,unsigned attributes,unsigned flags) {
  std::array<Sprite,4> packet{};unsigned count=actor_packet(c,packet,x,y,facing,tile,attributes,flags);
  for(unsigned n=0;n<count&&s.count<int(s.sprites.size());++n)s.sprites[s.count++]=packet[n];
 }
 bool anchor_oam(ModContext& c,State& s,unsigned first,int& x,int& y,unsigned& facing,unsigned tile,unsigned& attributes,unsigned& flags,uint64_t& used) {
  if(!c.oam||s.count!=int(first+4))return false;
  unsigned best=40,best_face=0;int best_score=33,best_x=0,best_y=0;
  std::array<Sprite,4> selected{};
  // InitSprites emits each ordinary actor's four native pieces contiguously.
  // Match its resident tile family, original facing geometry and palette to
  // the physical published OAM, not the next shadow packet in guest RAM.
  // Near-position matching disambiguates actors sharing loaded graphics.
  for(unsigned face=0;face<16;++face) {
   std::array<Sprite,4> packet{};if(actor_packet(c,packet,x,y,face,tile,attributes,flags)!=4)continue;
   for(unsigned start=0;start<=36;++start) {
    if(used&(uint64_t(15)<<start))continue;
    int dx=int8_t(int(c.oam[start*4+1])-8-packet[0].x),dy=int8_t(int(c.oam[start*4])-16-packet[0].y);
    int score=std::abs(dx)+std::abs(dy);if(score>=best_score||std::abs(dx)>16||std::abs(dy)>16)continue;
    bool same=true;
    for(unsigned n=0;n<4;++n) {
     const auto* o=c.oam+(start+n)*4;const auto& p=packet[n];
     same&=o[2]==p.tile&&((o[3]^p.attributes)&0x6f)==0&&
      uint8_t(p.x+dx+8)==o[1]&&uint8_t(p.y+dy+16)==o[0];
    }
    if(!same)continue;
    best=start;best_face=face;best_score=score;best_x=x+dx;best_y=y+dy;
    for(unsigned n=0;n<4;++n){selected[n]=packet[n];selected[n].x+=dx;selected[n].y+=dy;selected[n].attributes=c.oam[(start+n)*4+3];}
   }
  }
  if(best==40)return false;
  used|=uint64_t(15)<<best;x=best_x;y=best_y;facing=best_face;
  attributes=c.oam[best*4+3]&0x9f;
  flags=(flags&~8u)|((!(attributes&128)&&(c.oam[(best+2)*4+3]&128))?8u:0u);
  for(unsigned n=0;n<4;++n)s.sprites[first+n]=selected[n];
  return true;
 }
 unsigned native_tile(ModContext& c,const State& s,unsigned stride,unsigned rows,int x,int y)const {
  int bx=floor_div(x,32)+3,by=floor_div(y,32)+3;
  unsigned block=bx>=0&&unsigned(bx)<stride&&by>=0&&unsigned(by)<rows?s.blocks[size_t(by)*stride+bx]:s.border;
  if(!block)block=s.border;
  // Preserve the original8-bit doubled block-index wrap in LoadMetatiles.
  return rom(c,s.blocks_bank,s.blocks_address+(block&127)*16+((y&31)/8)*4+(x&31)/8);
 }
 bool map_info(ModContext& c,unsigned group,unsigned id,MapInfo& out)const {
  out={};if(!c.rom||group<1||group>26||id<1||id>128)return false;
  const unsigned table=variant==2?0x4000:0x40ed;
  auto rword=[&](unsigned bank,unsigned p){return unsigned(rom(c,bank,p))|(unsigned(rom(c,bank,p+1))<<8);};
  unsigned list=rword(0x25,table+(group-1)*2),header=list+(id-1)*9;
  if(list<0x4000||header>0x8000-9)return false;
  unsigned bank=rom(c,0x25,header),a=rword(0x25,header+3);
  if(bank>=c.rom_size/0x4000||a<0x4000||a>0x8000-12)return false;
  unsigned width=rom(c,bank,a+2),height=rom(c,bank,a+1);
  unsigned blocks_bank=rom(c,bank,a+3),blocks=rword(bank,a+4);
  if(!width||!height||blocks_bank>=c.rom_size/0x4000||blocks<0x4000||
     blocks+width*height>0x8000)return false;
  out={group,id,rom(c,0x25,header+1),rom(c,0x25,header+2),bank,a,
       width,height,rom(c,bank,a),blocks_bank,blocks,rom(c,bank,a+11)};
  return true;
 }
 void capture_presentation(ModContext& c) {
  if(!c.wram||!c.vram||!c.ppu||!c.rom||!c.hram)return;
  const auto dialog=c.ppu->scx==0&&c.ppu->scy==0?dialog_layout(c.vram,c.ppu->lcdc):DialogLayout{};
  // Animation can pause while a complete connected field is still visible.
  // The native metadata and visible BG match below qualify the projection.
  bool sign=map_name_sign(c);
  if(!(c.ppu->lcdc&0x80)||(c.ppu->lcdc&4)||ram(c,b.battle)||
     ((c.ppu->lcdc&32)&&c.ppu->wx<167&&c.ppu->wy<144&&!sign&&!field_window(c))) {
   return;
  }
  unsigned group=ram(c,b.map),id=ram(c,b.map+1);
  // Current-map capability comes from the exact cartridge's authored header,
  // loaded outdoor attributes and complete visible BG match below. It does
  // not authorize guest activation or scripts in neighboring maps.
  if(!map_info(c,group,id,pending_geometry.current))return;
  const auto& map=pending_geometry.current;
  if((map.environment!=1&&map.environment!=2)||ram(c,b.environment)!=map.environment||
     ram(c,b.width)!=map.width||ram(c,b.height)!=map.height||ram(c,b.environment-1)!=map.tileset||
     ram(c,b.environment-2)!=map.attributes_bank||word(c,b.environment+1)!=map.attributes||
     (map.width+6)*(map.height+6)>s.blocks.size())return;
  capture_frame(c,s,map.width,map.height);
  if(!s.valid)return;
  // Original attributes contain12-byte records in N/S/W/E order. The loaded
  // connection structs own the active offsets; verify them against authored
  // ROM metadata before extending beyond the live three-block padding.
  const unsigned directions[]{8,4,2,1};unsigned record=map.attributes+12;
  unsigned active=ram(c,b.environment+14);
  for(unsigned d=0;d<4;++d)if(map.connections&directions[d]) {
   if(record>0x8000-12){s.valid=0;return;}
   unsigned p=b.environment+15+d*12;
   bool same=(active&directions[d])!=0;
   for(unsigned i:{0u,1u,7u,8u,9u})same&=ram(c,p+i)==rom(c,map.attributes_bank,record+i);
   MapInfo neighbor;
   if(same&&map_info(c,ram(c,p),ram(c,p+1),neighbor)&&neighbor.tileset==map.tileset&&
      (neighbor.environment==1||neighbor.environment==2)&&neighbor.width==ram(c,p+7)) {
    int x=int8_t(ram(c,p+9)),y=int8_t(ram(c,p+8));
    if((d<2&&!(x&1))||(d>=2&&!(y&1))) {
     pending_geometry.neighbors[d]=neighbor;pending_geometry.x_offset[d]=d<2?x/2:0;pending_geometry.y_offset[d]=d>=2?y/2:0;
    }
   }
   record+=12;
  }
 }
 // Connection setup changes the logical map before replacing its resident
 // block buffer. Reuse only the previous, fully qualified connected scene:
 // its physical graphics must be unchanged and every visible tile must still
 // match after the original connection's coordinate translation.
 void retain_connected_field(ModContext& c) {
  if(s.valid||!pending_field||completed.valid!=1||!c.vram||!c.ppu||
     (c.ppu->lcdc&0x7f)!=(completed.lcdc&0x7f)||
     std::memcmp(c.vram,completed.vram.data(),completed.vram.size()))return;
  const auto& next=pending_geometry.current;const auto& old=geometry.current;
  for(unsigned d=0;d<4;++d) {
   const auto& neighbor=geometry.neighbors[d];
   if(!neighbor.group||next.group!=neighbor.group||next.id!=neighbor.id)continue;
   State source=completed;
   source.camera_x=s.camera_x-geometry.x_offset[d]*32-(d==2?int(neighbor.width)*32:d==3?-int(old.width)*32:0);
   source.camera_y=s.camera_y-geometry.y_offset[d]*32-(d==0?int(neighbor.height)*32:d==1?-int(old.height)*32:0);
   unsigned map=(source.lcdc&8)?0x1c00:0x1800,stride=old.width+6,rows=old.height+6;
   bool matches=true;
   for(int y=0;y<18&&matches;++y)for(int x=0;x<20;++x) {
    unsigned address=map+(((c.ppu->scy/8+y)&31)*32)+((c.ppu->scx/8+x)&31);
    unsigned tile=native_tile(c,source,stride,rows,floor_div(source.camera_x,8)*8+x*8,floor_div(source.camera_y,8)*8+y*8);
    if(c.vram[address]!=(variant==2?tile&127:tile)){matches=false;break;}
   }
   if(!matches)return;
   for(int n=0;n<source.count;++n) {
    source.sprites[n].x+=completed.camera_x-source.camera_x;
    source.sprites[n].y+=completed.camera_y-source.camera_y;
   }
   std::copy_n(c.ppu->bg_rgb,32,source.bg.begin());std::copy_n(c.ppu->obj_rgb,32,source.obj.begin());
   source.callers=s.callers;source.captures=s.captures+1;s=source;pending_geometry=geometry;
   return;
  }
 }
 unsigned extended_tile(ModContext& c,const State& frame,int x,int y,bool& unsupported)const {
  const auto& map=geometry.current;int bx=floor_div(x,32),by=floor_div(y,32);
  int px=bx+3,py=by+3;unsigned stride=map.width+6,rows=map.height+6;
  unsigned block=frame.border;
  if(px>=0&&unsigned(px)<stride&&py>=0&&unsigned(py)<rows)block=frame.blocks[size_t(py)*stride+px];
  else {
   int d=by<0?0:by>=int(map.height)?1:bx<0?2:bx>=int(map.width)?3:-1;
   if(d>=0&&(map.connections&(8u>>d))) {
    const auto& n=geometry.neighbors[size_t(d)];
    if(!n.group)unsupported=true;
    else {
     int nx=bx+geometry.x_offset[size_t(d)],ny=by+geometry.y_offset[size_t(d)];
     if(d==0)ny+=int(n.height);else if(d==1)ny-=int(map.height);
     else if(d==2)nx+=int(n.width);else nx-=int(map.width);
     if(nx>=0&&unsigned(nx)<n.width&&ny>=0&&unsigned(ny)<n.height)
      block=rom(c,n.bank,n.blocks+unsigned(ny)*n.width+unsigned(nx));
     else {
      unsigned next=ny<0?8u:ny>=int(n.height)?4u:nx<0?2u:1u;
      // More distant connected maps need their own bounded qualification.
      if(n.connections&next)unsupported=true;
     }
    }
   }
  }
  if(!block)block=frame.border;
  return rom(c,frame.blocks_bank,frame.blocks_address+(block&127)*16+((y&31)/8)*4+(x&31)/8);
 }
 std::array<int,2> output_size(unsigned w,unsigned h)const {
  if(requested>=0)return {requested,144};
  if(!h)return {256,144};if(!w)return {160,144};
  if(uint64_t(w)*144>=uint64_t(h)*160)
   return {int(std::clamp<uint64_t>((uint64_t(w)*144+h/2)/h,160,maximum_width)),144};
  return {160,int(std::clamp<uint64_t>((uint64_t(h)*160+w/2)/w,144,maximum_width))};
 }
 ModView render(ModContext& c,std::span<const uint32_t> native,int width,int height) {
  const State* source=completed.valid?&completed:nullptr;
  ModView out;out.width=unsigned(width);out.height=unsigned(height);
  out.expanded=source&&(width>160||height>144);
  pixels.assign(size_t(width)*height,0xff000000u);background_nonzero.assign(pixels.size(),0);
  int center_x=(width-160)/2,center_y=(height-144)/2;
  if(out.expanded) {
   const auto& frame=*source;bool unsupported=false;
   // Portable tile-row spans: resolve map/connection/tile/palette once for up
   // to8 adjacent pixels. Logical geometry never changes the sample density.
   for(int y=0;y<height&&!unsupported;++y)for(int x=0;x<width;) {
    int wx=frame.camera_x+x-center_x,wy=frame.camera_y+y-center_y;
    unsigned tile=extended_tile(c,frame,wx,wy,unsupported),palette=(frame.palette[tile]&7)*4;
    unsigned bank=variant==2&&(frame.palette[tile]&8)?0x2000:0;
    if(variant==2)tile&=127;
    int offset=(frame.lcdc&16)?int(tile)*16:0x1000+int8_t(tile)*16;
    unsigned row=bank+unsigned(offset+(wy&7)*2),count=std::min(unsigned(width-x),8u-unsigned(wx&7));
    unsigned lower=frame.vram[row],upper=frame.vram[row+1];
    for(unsigned i=0;i<count;++i) {
     unsigned bit=7-((wx+int(i))&7),color=((lower>>bit)&1)|(((upper>>bit)&1)<<1);
     size_t at=size_t(y)*width+x+i;pixels[at]=frame.bg[palette+color];background_nonzero[at]=color!=0;
    }
    x+=int(count);
   }
   if(unsupported){out.expanded=false;std::fill(pixels.begin(),pixels.end(),0xff000000u);}
   else for(int n=frame.count-1;n>=0;--n) {
    const auto& sprite=frame.sprites[n];unsigned bank=(sprite.attributes&8)?0x2000:0,palette=(sprite.attributes&7)*4;
    for(int dy=0;dy<8;++dy)for(int dx=0;dx<8;++dx) {
     int x=sprite.x+dx+center_x,y=sprite.y+dy+center_y;
     if(x<0||x>=width||y<0||y>=height||(x>=center_x&&x<center_x+160&&y>=center_y&&y<center_y+144))continue;
     unsigned row=bank+unsigned(sprite.tile)*16+((sprite.attributes&64)?7-dy:dy)*2;
     unsigned bit=(sprite.attributes&32)?dx:7-dx;
     unsigned color=((frame.vram[row]>>bit)&1)|(((frame.vram[row+1]>>bit)&1)<<1);
     if(!color)continue;
     if((sprite.attributes&128)&&background_nonzero[size_t(y)*width+x])continue;
     pixels[size_t(y)*width+x]=frame.obj[palette+color];
    }
   }
  }
  // Preserve the authentic field center. Qualified text boxes are composed
  // across the wider canvas below, retaining their native text and corners.
  for(unsigned y=0;y<144;++y)std::copy_n(native.data()+y*160,160,pixels.data()+size_t(y+center_y)*width+center_x);
  if(out.expanded&&width>160&&source->valid==2) {
   const auto dialog=dialog_layout(source->vram.data(),source->lcdc);
   const unsigned map=(source->lcdc&8)?0x1c00:0x1800;
   for(unsigned n=0;n<dialog.count;++n) {
    const auto box=dialog.boxes[n];
    if(box[0]!=0||box[2]!=19)continue;
    for(int y=box[1]*8;y<(box[3]+1)*8;++y) {
     auto* row=pixels.data()+size_t(y+center_y)*width;
     const auto* original=native.data()+size_t(y)*160;
     const bool edge=y/8==box[1]||y/8==box[3];
     const uint32_t* fill=original+8;
     if(!edge) {
      // A completely filled text line need not contain a space. Use the
      // authored blank tile from any interior row, at the same pixel phase.
      bool found=false;
      for(int by=box[1]+1;by<box[3]&&!found;++by)for(int bx=1;bx<19;++bx)
       if(source->vram[map+unsigned(by)*32+unsigned(bx)]==0x7f) {
        fill=native.data()+size_t(by*8+(y&7))*160+bx*8;found=true;break;
       }
     }
     for(int x=8;x<width-8;++x)row[x]=fill[x&7];
     std::copy_n(original,8,row);std::copy_n(original+152,8,row+width-8);
     if(!edge)std::copy_n(original+8,144,row+8);
     else if(y/8==box[3]&&source->vram[map+unsigned(y/8)*32+18]==0xee)
      std::copy_n(original+144,8,row+width-16);
    }
   }
  }
  if(out.expanded&&width>160&&source->valid==3) {
   // The original four-row Window frame owns its graphics and palette.
   // Repeat only its border/blank pattern; keep the native landmark text
   // centered, moving the two native side rails to the canvas edges.
   for(int y=112;y<144;++y) {
    auto* row=pixels.data()+size_t(y+center_y)*width;
    const auto* original=native.data()+size_t(y)*160;
    bool border=y<120||y>=136;
    const auto* blank=native.data()+size_t(120+(y&7))*160+8;
    for(int x=8;x<width-8;++x)row[x]=border?original[8+((x-8)&31)]:blank[x&7];
    std::copy_n(original,8,row);std::copy_n(original+152,8,row+width-8);
    if(y>=128&&y<136)std::copy_n(original+8,144,row+center_x+8);
   }
  }
  const auto& frame=source?*source:completed;
  out.pixels=pixels;out.camera_x=frame.camera_x;out.camera_y=frame.camera_y;out.left=frame.camera_x-center_x;
  out.bound_left=0;out.bound_right=int(geometry.current.width*32);out.sprites=unsigned(frame.count);out.captures=frame.captures;out.spawn_reads=s.callers;
  return out;
 }
};
Gen2NativeMods::Gen2NativeMods(std::string_view rom,const NativeFeature& feature,bool cgb):impl_(std::make_unique<Impl>(rom,feature,cgb)){}
Gen2NativeMods::~Gen2NativeMods()=default;
uint8_t Gen2NativeMods::read(ModContext& c,uint16_t address,uint8_t value) {
 if(c.rom_bank==1&&c.instruction_pc==impl_->b.caller&&address==impl_->b.flags)++impl_->s.callers;
 return value;
}
void Gen2NativeMods::scanline(ModContext& c,uint8_t line){if(!line)impl_->capture(c);}
void Gen2NativeMods::complete_frame(ModContext& c,bool cleared) {
 // Prefer a qualified source from the picture's first scanline. If a map
 // transfer was still incomplete there, resolve it again at true VBlank.
 // Both sources remain bound to native metadata and the visible tilemap.
 if(!cleared&&!impl_->s.valid){impl_->capture(c);impl_->retain_connected_field(c);}
 impl_->completed=impl_->s;impl_->geometry=impl_->pending_geometry;
 if(cleared)impl_->completed.valid=0;
}
void Gen2NativeMods::frame(Machine& machine) {
 auto& m=*impl_;if(!m.gift||m.gift_consumed||!m.s.callers)return;
 auto c=machine.presentation_context();
 // Wait for a real loaded, idle field. Never race Continue's restore of the
 // native mailbox, a script, a save, or a battle. Unlock is the original
 // in-game Carrie interaction; this feature does not synthesize it.
 const uint16_t save=m.variant==2?0xcfcd:0xd19a;
 const uint16_t paused=m.variant==2?0xc2cd:0xc1cc;
 const uint16_t script=m.variant==2?0xd437:0xd15e;
 if(ram(c,save)!=1||ram(c,paused)||ram(c,script)||ram(c,m.b.battle)||ram(c,m.b.environment)!=1)return;
 auto sram=machine.native_gift_ram();
 if(sram.size()<0x2000||sram[0xbe3]!=0||sram[0xbe2])return;
 const auto& input=gen2_gifts[m.gift-1];
 if(input.item)sram[0xbe2]=input.item;
 else sram[0xbf0+input.decoration/8]|=uint8_t(1u<<(input.decoration%8));
 m.gift_consumed=1;
}
void Gen2NativeMods::viewport(unsigned w,unsigned h) {
 impl_->host_size=impl_->output_size(w,h);
}
void Gen2NativeMods::preserve_host_presentation(const Gen2NativeMods& from) noexcept {impl_->host_size=from.impl_->host_size;}
ModView Gen2NativeMods::render(ModContext& c,std::span<const uint32_t> native){return impl_->render(c,native,impl_->host_size[0],impl_->host_size[1]);}
ModView Gen2NativeMods::render_host(ModContext& c,std::span<const uint32_t> native,unsigned w,unsigned h){auto size=impl_->output_size(w,h);return impl_->render(c,native,size[0],size[1]);}
bool Gen2NativeMods::supports(std::string_view rom){return rom==gold||rom==silver||rom==crystal;}
bool Gen2NativeMods::linked_pair(std::string_view left,std::string_view right){return(left==gold&&right==silver)||(left==silver&&right==gold);}
bool Gen2NativeMods::linked_profile(std::span<const NativeFeature> features,bool cgb) {
 if(!cgb||features.size()!=1)return false;const auto& f=features[0];
 return f.plugin=="gbcllr.gen2.adaptive-view"&&f.abi==1&&f.view_width==-1&&!f.colorization&&f.asset_input.empty()&&!f.gift;
}
std::vector<uint8_t> Gen2NativeMods::snapshot()const {
 static_assert(std::is_trivially_copyable_v<State>);
 static_assert(std::is_trivially_copyable_v<Geometry>);
 std::array<uint32_t,4> header{4,impl_->variant,uint32_t(impl_->requested),uint32_t(impl_->stored_width)};
 std::vector<uint8_t> out(sizeof header+2*sizeof(State)+2*sizeof(Geometry));
 auto* p=out.data();
 auto append=[&](const auto& value){std::memcpy(p,&value,sizeof value);p+=sizeof value;};
 append(header);append(impl_->s);append(impl_->completed);append(impl_->pending_geometry);append(impl_->geometry);
 if(impl_->gift){std::array<uint32_t,2> gift{impl_->gift,impl_->gift_consumed};const auto* p=reinterpret_cast<const uint8_t*>(gift.data());out.insert(out.end(),p,p+sizeof gift);}
 return out;
}
void Gen2NativeMods::checkpoint(void* context,void (*write)(void*,const uint8_t*,size_t))const {
 static_assert(std::is_trivially_copyable_v<State>);
 static_assert(std::is_trivially_copyable_v<Geometry>);
 std::array<uint32_t,4> header{4,impl_->variant,uint32_t(impl_->requested),uint32_t(impl_->stored_width)};
 const uint32_t size=sizeof header+2*sizeof(State)+2*sizeof(Geometry)+(impl_->gift?8:0);
 auto put=[&](const auto& value){write(context,reinterpret_cast<const uint8_t*>(&value),sizeof value);};
 put(size);put(header);put(impl_->s);put(impl_->completed);put(impl_->pending_geometry);put(impl_->geometry);
 if(impl_->gift){std::array<uint32_t,2> gift{impl_->gift,impl_->gift_consumed};put(gift);}
}
void Gen2NativeMods::restore(std::span<const uint8_t> bytes) {
 std::array<uint32_t,4> header{};std::array<State,2> staged{};std::array<Geometry,2> geometry{};
 const size_t base=sizeof header+sizeof staged+sizeof geometry;
 if(bytes.size()!=base+(impl_->gift?8:0))throw std::runtime_error("Invalid Gen2 view state size");
 std::array<uint32_t,2> gift{};
 if(impl_->gift){std::memcpy(gift.data(),bytes.data()+base,sizeof gift);if(gift[0]!=impl_->gift||gift[1]>1)throw std::runtime_error("Invalid Gen2 gift state/profile");}
 std::memcpy(header.data(),bytes.data(),sizeof header);
 std::memcpy(staged.data(),bytes.data()+sizeof header,sizeof staged);
 std::memcpy(geometry.data(),bytes.data()+sizeof header+sizeof staged,sizeof geometry);
 if(header[0]!=4||header[1]!=impl_->variant||int32_t(header[2])!=impl_->requested||header[3]<160||header[3]>maximum_width)
  throw std::runtime_error("Invalid Gen2 view state/profile");
 auto valid_map=[](const MapInfo& map) {
  return !map.group||(map.group<=26&&map.id&&map.id<=128&&map.tileset<=255&&map.environment<=255&&
   map.attributes_bank<128&&map.attributes>=0x4000&&map.attributes<=0x8000-12&&map.width&&map.width<=255&&
   map.height&&map.height<=255&&map.border<=255&&map.bank<128&&map.blocks>=0x4000&&
   map.blocks+map.width*map.height<=0x8000&&map.connections<=15);
 };
 for(unsigned i=0;i<2;++i) {
  const auto& state=staged[i];const auto& g=geometry[i];const auto& map=g.current;
  if(state.valid>3||(state.valid==3&&(!(state.lcdc&32)||!impl_->map_name_sign(state.vram.data(),state.lcdc)))||state.count<0||state.count>int(state.sprites.size())||
     state.camera_x< -4096||state.camera_x>8192||state.camera_y< -4096||state.camera_y>8192||
     !valid_map(map)||(state.valid&&(state.blocks_bank>=128||state.blocks_address<0x4000||state.blocks_address>0x7800||
     state.palettes_address<0x4000||state.palettes_address>0x7f80||!map.group||(map.environment!=1&&map.environment!=2)||
     (map.width+6)*(map.height+6)>state.blocks.size())))throw std::runtime_error("Invalid Gen2 view source state");
  for(const auto& sprite:state.sprites)if(sprite.x< -4096||sprite.x>8192||sprite.y< -4096||sprite.y>8192)throw std::runtime_error("Invalid Gen2 sprite state");
  if(state.pose_map>65535)throw std::runtime_error("Invalid Gen2 actor map");
  for(const auto& pose:state.poses)if(pose.valid>1||pose.facing>=16||pose.x< -4096||pose.x>8192||pose.y< -4096||pose.y>8192)
   throw std::runtime_error("Invalid Gen2 actor pose");
  for(unsigned n=0;n<4;++n)if(!valid_map(g.neighbors[n])||g.x_offset[n]< -64||g.x_offset[n]>63||g.y_offset[n]< -64||g.y_offset[n]>63)
   throw std::runtime_error("Invalid Gen2 connection state");
 }
 impl_->s=staged[0];impl_->completed=staged[1];impl_->pending_geometry=geometry[0];impl_->geometry=geometry[1];
 impl_->stored_width=int(header[3]);impl_->gift_consumed=gift[1];
}
}
