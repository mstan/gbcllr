// Native room-layout/metasprite contract adapted from Oracle Recomp's
// oracle_world.c. Bindings follow oracles-disasm21c924af, wram.s/hram.s and
// generateOam.s. Native neighbor copies own only room-local presentation/AI;
// the original machine owns input, progress, audio and native saves.
#include "oracle_native_mods.hpp"
#include "native_mods.hpp"
#include "oracle_graphics_data.hpp"
#include <gbcllr/machine.hpp>
#include <algorithm>
#include <array>
#include <cstring>
#include <cstdlib>
#include <stdexcept>
#include <type_traits>
namespace gbcllr {
namespace {
constexpr std::string_view ages="0b56b78a9e45452e98c33edd111234931f1e034dc097f6f23082eb8db6055474";
constexpr std::string_view seasons="862a51368fb30539279d336b3fe193b43876d2cb15c87a36f5da517804ab3971";
constexpr unsigned max_width=4096;
constexpr uint8_t explosion_proxy=0xec; // Reserved owner-only Item.var03 tag.
constexpr std::array<int,4> room_delta{-16,1,16,-1};
bool adjacent(unsigned from,int to,unsigned direction) {
 if(from>=256||to<0||to>=256||direction>3)return false;
 return direction&1?(unsigned(to)>>4)==(from>>4):(unsigned(to)&15)==(from&15);
}
struct Binding {unsigned group,room,camera_x,camera_y,oam_bank,warp,disabled,tileset,safe_pc;};
constexpr Binding ages_binding{0xcc2d,0xcc30,0x2c,0x2a,0x13,0xcc47,0xcc8a,0xcc34,0x984};
constexpr Binding seasons_binding{0xcc49,0xcc4c,0x2a,0x28,0x12,0xcc63,0xcca4,0xcc50,0x960};
using State=OracleRoomState;
using World=OracleWorldState;
}
struct OracleNativeMods::Impl {
 const Binding& b;unsigned variant;int requested=-1;unsigned width=256;World world{};State& s=world.active;
 bool hide_closed_rooms=false; // Host presentation only; preparation never uses it.
 std::unique_ptr<Machine> scratch;
 std::vector<uint8_t> aligned_owner;bool share_owner=false; // One synchronous frame only; never serialized.
 std::vector<uint32_t> pixels;std::vector<uint8_t> opaque,priority;bool copy_native_center=true;const ModPpu* fade_palette=nullptr;
 Impl(std::string_view identity,const NativeFeature& f,bool cgb):b(identity==ages?ages_binding:seasons_binding),variant(identity==ages?0:1) {
  if(identity!=ages&&identity!=seasons)throw std::runtime_error("Oracle view requires a qualified cartridge revision");
  if(!cgb||f.plugin!="gbcllr.oracle.adaptive-view"||f.abi!=1||f.colorization||!f.asset_input.empty()||
    (f.view_width!=-1&&(f.view_width<160||f.view_width>int(max_width))))throw std::runtime_error("Unsupported Oracle native view profile");
  requested=f.view_width;std::memset(&world,0,sizeof world);
 }
 unsigned ram(unsigned a)const{return s.ram[a-0xc000];}
 bool mounted(const uint8_t* r)const {
  return r[b.group-1-0xc000]==0xd1&&r[0x1001]==9&&r[0x1004]==1&&
   (r[0x1100]&1)&&r[0x1101]>=0x0b&&r[0x1101]<=0x0d;
 }
 unsigned primary(const uint8_t* r)const{return mounted(r)?0x1100:0x1000;}
 unsigned follower(const uint8_t* r)const {
  unsigned pointer=variant?0xcfe:0xce8,page=r[pointer];
  if(r[pointer-1]!=0x40||page<0xd0||page>0xdf)return 0;
  unsigned at=(page-0xc0)*256+0x40;return (r[at]&3)==3?at:0;
 }
 bool following_slot(const uint8_t* r,unsigned page)const{return follower(r)==0x1040+page*256;}
 void clear_private_follower(uint8_t* r)const {
  if(unsigned at=follower(r))r[at]=0;
  unsigned pointer=variant?0xcfe:0xce8;r[pointer]=r[pointer-1]=r[pointer-2]=0;
 }
 void store(ModContext& c,State& to,int rw,int rh,int x,int y) {
  if(!to.captures||to.ram[b.group-0xc000]!=c.wram[b.group-0xc000]||to.ram[b.room-0xc000]!=c.wram[b.room-0xc000]||to.ram[b.room+2-0xc000]!=c.wram[b.room+2-0xc000]) {
   to.authority=0;to.enemy_origins.fill(0);to.enemy_origin_mask=0;to.interaction_origins.fill(0);to.interaction_origin_mask=0;
   for(unsigned page=0;page<16;++page){auto* e=c.wram+0x1080+page*256;if(e[0]&&!(e[0]&2)){to.enemy_origin_mask|=1u<<page;to.enemy_origins[page]=e[1]|(unsigned(e[2])<<8)|(unsigned(e[13])<<16)|(unsigned(e[11])<<24);}}
   for(unsigned page=0;page<16;++page){auto* o=c.wram+0x1040+page*256;if(o[0]&&!(o[0]&2)&&o[1]!=0xe0){to.interaction_origin_mask|=1u<<page;to.interaction_origins[page]=o[1]|(unsigned(o[2])<<8)|(unsigned(o[13])<<16)|(unsigned(o[11])<<24);}}
  }
  to.camera_x=x;to.camera_y=y;to.room_width=rw;to.room_height=rh;to.lcdc=c.ppu->lcdc;
  std::copy_n(c.wram,to.ram.size(),to.ram.begin());std::copy_n(c.vram,to.vram.size(),to.vram.begin());
  std::copy_n(c.ppu->bg_rgb,32,to.bg.begin());std::copy_n(c.ppu->obj_rgb,32,to.obj.begin());
  if(c.ppu->bg_palette_ram)std::copy_n(c.ppu->bg_palette_ram,64,to.bg_palette.begin());
  if(c.ppu->obj_palette_ram)std::copy_n(c.ppu->obj_palette_ram,64,to.obj_palette.begin());
  to.valid=1;++to.captures;
 }
 void owner(ModContext& c) {
  if(world.mode==4)return;
  world.owner.valid=0;
  if(world.mode&&ordinary(s))store(c,world.owner,s.room_width,s.room_height,0,0);
 }
 void capture(ModContext& c,State* target=nullptr) {
  auto& s=target?*target:this->s;
  std::array<uint32_t,16> retained_enemies{},retained_interactions{};uint32_t retained_enemy_mask=0,retained_interaction_mask=0;bool retain_origins=false;
  if(target)s.valid=0;else world.mode=0;
  if(!c.wram||!c.vram||!c.hram||!c.ppu)return;
  auto r=[&](unsigned a){return c.wram[a-0xc000];};
  // Menus retain their native layout. Dialogue and loading/fades retain the
  // last complete world instead of changing the viewport's aspect ratio.
  // Native menu states0/3 still display the field while fading out/in.
  if(!target&&ordinary(s)&&r(0xcbcb)&&(r(0xcbcc)==0||r(0xcbcc)==3)){world.mode=4;return;}
  if(!target&&ordinary(s)&&r(0xc2ee)==1&&r(0xc2ef)==0&&!r(0xcbcb)){world.mode=4;return;}
  if(r(0xc2ee)!=2||r(0xcbcb))return;
  if(!target&&ordinary(s)&&r(0xcba0)&&r(0xc2ef)==1&&r(b.group)==ram(b.group)&&r(b.room)==ram(b.room)) {
   world.mode=3;
   for(unsigned page=0;page<16;++page){unsigned at=0x1040+page*256;
    if(!c.wram[at])s.ram[at]=0;
    else if(c.wram[at+1]==s.ram[at+1]&&c.wram[at+2]==s.ram[at+2])std::copy_n(c.wram+at,64,s.ram.begin()+at);
   }return;
  }
  if(r(0xcba0))return;
  if(r(0xc4ab)||r(0xc2ef)==0) {
   if(!target&&ordinary(s))world.mode=4;
   return;
  }
  if(r(0xc2ef)!=1||(r(0xcd00)&127)!=1||r(0xcd04)!=2) {
   // Keep prepared collision-connected rooms while the original owner runs
   // its entire entry/scroll sequence. Never replace or rerun owner effects.
   unsigned direction=r(0xcd02)&3;
   int next=neighbor(s,direction);
   if(!target&&ordinary(s)&&next>=0&&
      r(b.group)==ram(b.group)&&r(b.room+2)==ram(b.room+2)&&
      (r(b.room)==ram(b.room)||r(b.room)==unsigned(next))&&
      r(0xc2ef)<=1&&(r(0xcd00)==4||r(0xcd00)==8)&&r(0xcd04)>=3&&r(0xcd04)<=5) {
    const auto* dest=find(unsigned(next));
    if(dest&&(overworld(s)||connected(*dest))) {
     int x=int16_t(unsigned(c.hram[b.camera_x])|(unsigned(c.hram[b.camera_x+1])<<8));
     int y=int16_t(unsigned(c.hram[b.camera_y])|(unsigned(c.hram[b.camera_y+1])<<8));
     if(x>=-s.room_width&&x<=s.room_width&&y>=-s.room_height&&y<=s.room_height){world.mode=2;world.native_x=x;world.native_y=y;
      unsigned actor=primary(c.wram);world.focus_x=int(c.wram[actor+13]);world.focus_y=int(c.wram[actor+11]);
      if(direction==3&&world.focus_x>s.room_width)world.focus_x-=256;
      if(direction==0&&world.focus_y>s.room_height)world.focus_y-=256;
     }
    }
   }
   if(!target&&!world.mode&&ordinary(s)&&r(0xc2ef)<=1)world.mode=4;
   return;
  }
  if(
    !((r(0xcd0a)==30&&r(0xcd0b)==22)||(r(0xcd0a)==20&&r(0xcd0b)==16))||
    !(c.ppu->lcdc&128))return;
  int x=c.hram[b.camera_x],y=c.hram[b.camera_y];
  int rw=r(0xcd0a)*8,rh=r(0xcd0b)*8;if(x>rw-160||y>rh-128)return;
  if(!target&&ordinary(s)&&r(b.group)==ram(b.group)&&r(b.room)!=ram(b.room)) {
   // The original destination is authoritative. Retain the outgoing room
   // for presentation, replacing its former destination-cache slot.
   for(auto& room:world.rooms)if(room.valid==1&&room.ram[b.room-0xc000]==r(b.room)&&(overworld(s)||connected(room))) {
    if(room.authority&1){retained_enemies=room.enemy_origins;retained_interactions=room.interaction_origins;retained_enemy_mask=room.enemy_origin_mask;retained_interaction_mask=room.interaction_origin_mask;retain_origins=true;}
    room=s;break;
   }
  }
  store(c,s,rw,rh,x,y);if(retain_origins){s.enemy_origins=retained_enemies;s.interaction_origins=retained_interactions;s.enemy_origin_mask=retained_enemy_mask;s.interaction_origin_mask=retained_interaction_mask;}if(!target){s.authority=1;world.mode=1;world.native_x=x;world.native_y=y;unsigned actor=primary(c.wram);world.focus_x=int(c.wram[actor+13]);world.focus_y=int(c.wram[actor+11]);}
 }
 bool ordinary(const State& room)const{return room.valid==1&&
  ((room.room_width==160&&room.room_height==128)||(room.room_width==240&&room.room_height==176));}
 bool same_extent(const State& a,const State& z)const {
  return ordinary(a)&&ordinary(z)&&a.room_width==z.room_width&&a.room_height==z.room_height;
 }
 bool overworld(const State& room)const {
  return ordinary(room)&&room.room_width==160&&
   room.ram[b.group-0xc000]<(variant?2:4)&&!(room.ram[b.tileset-0xc000]&0x3c);
 }
 // updateActiveRoom uses the native 8x8 dungeon floor, not room-ID
 // arithmetic. Its loaded w2DungeonLayout is authoritative for topology.
 unsigned position(const uint8_t* r)const {
  unsigned at=variant?0xc55:0xc39;
  if(r[at]==255)return r[b.room-0xc000];
  unsigned index=r[at+1];return ((index>>3)<<4)|(index&7);
 }
 unsigned position(const State& room)const{return position(room.ram.data());}
 bool same_map(const State& a,const State& z)const {
  unsigned at=variant?0xc55:0xc39;
  return a.ram[b.group-0xc000]==z.ram[b.group-0xc000]&&a.ram[at]==z.ram[at]&&
   (a.ram[at]==255||a.ram[at+2]==z.ram[at+2]);
 }
 int neighbor(const uint8_t* r,int dx,int dy)const {
  unsigned at=variant?0xc55:0xc39,p=position(r),extent=r[at]==255?16:8;
  int x=int(p&15)+dx,y=int(p>>4)+dy;
  if(x<0||x>=int(extent)||y<0||y>=int(extent))return -1;
  if(r[at]==255)return y*16+x;
  unsigned floor=r[at+2];if(floor>=8)return -1;
  unsigned id=r[0x2c00+floor*64+unsigned(y*8+x)];return id?int(id):-1;
 }
 int neighbor(const State& room,unsigned direction)const {
  return neighbor(room.ram.data(),direction==1?1:direction==3?-1:0,direction==2?1:direction==0?-1:0);
 }
 // A native doorway may close behind an entrant. An open outgoing edge
 // still connects the prepared room; current native entry/collision decides
 // when crossing is legal, and its closed-door effects remain authoritative.
 bool joined(const State& from,const State& to,int dx,int dy=0)const {
  if(!same_extent(from,to)||!same_map(from,to))return false;
  if(dy){for(unsigned col=1;col<unsigned(from.room_width/16)-1;++col)
   if((!from.ram[0xe00+(dy<0?0:(from.room_height/16-1)*16)+col]||!to.ram[0xe00+(dy<0?(to.room_height/16-1)*16:0)+col]))return true;
   return false;
  }
  for(unsigned row=1;row<unsigned(from.room_height/16)-1;++row)
   if((!from.ram[0xe00+row*16+(dx<0?0:from.room_width/16-1)]||!to.ram[0xe00+row*16+(dx<0?to.room_width/16-1:0)]))return true;
  return false;
 }
 const State* find(unsigned id)const {
  for(const auto& room:world.rooms)if(room.valid==1&&same_map(s,room)&&room.ram[b.room-0xc000]==id)return &room;
  return nullptr;
 }
 bool connected(const State& room)const {
  if(!same_extent(s,room)||!same_map(s,room))return false;
  std::array<const State*,17> queue{};std::bitset<256> visited;
  unsigned head=0,tail=1;queue[0]=&s;visited.set(ram(b.room));
  while(head<tail){auto* from=queue[head++];unsigned id=from->ram[b.room-0xc000];
   if(id==room.ram[b.room-0xc000])return true;
   for(const auto& to:world.rooms){unsigned next=to.ram[b.room-0xc000];if(to.valid!=1||visited[next])continue;
    unsigned p=position(to),q=position(*from);
    int dx=int(p&15)-int(q&15),dy=int(p>>4)-int(q>>4);
    if(std::abs(dx)+std::abs(dy)!=1||!joined(*from,to,dx,dy))continue;
    visited.set(next);if(tail<queue.size())queue[tail++]=&to;
   }
  }return false;
 }
 bool special(ModContext& c,unsigned group,unsigned room)const {
  // The original getNextActiveRoom table owns mazes, sword upgrades, the
  // Onox dungeon and eye puzzles. Never replace any table-selected route.
  if(group>=8)return true;
  unsigned at=variant?0x5db5:0x5f6f;
  if(at+group*2+1>=c.rom_size)return true;
  at=c.rom[at+group*2]|(unsigned(c.rom[at+group*2+1])<<8);
  if(at<0x4000||at>=0x8000)return true;
  for(unsigned n=0;n<128&&at+1<c.rom_size;++n,at+=2) {
   unsigned key=c.rom[at];if(!key)return false;if(key==room)return true;
  }return true;
 }
 bool stable_progress(const uint8_t* before,const uint8_t* after)const {
  unsigned maturity=variant?0x65c:0x65f;
  for(unsigned at=0x600;at<0xb00;++at) {
   if((at>=0x622&&at<0x626)||(at>=0x63a&&at<0x63e)||at==maturity||at==maturity+1||
      (!variant&&at>=0x6ed&&at<=0x6ef))continue;
   unsigned mask=at>=0x700?0xef:0xff;
   if((before[at]&mask)!=(after[at]&mask))return false;
  }return true;
 }
 bool registered_npc(const State& room,unsigned page)const {
  unsigned list=variant?0xcca:0xcb3;
  for(unsigned n=0;n<32;n+=2)if(room.ram[list+n]==0xd0+page&&room.ram[list+n+1]==0x71)return true;
  return false;
 }
 bool adoptable(ModContext& c,const State& room)const {
  // Admission follows the actual native initializer and persistent effects,
  // rather than a room/NPC/enemy whitelist. Effectful predictions cannot
  // replace current quest/inventory data or import a pending conversation.
  if((room.authority&2)||!ordinary(room)||(room.ram[0xd20]&&room.ram[0xd20]!=room.ram[0xd28]))return false;
  if(!stable_progress(c.wram,room.ram.data()))return false;
  for(unsigned page=0;page<16;++page)if(registered_npc(room,page)&&!following_slot(room.ram.data(),page)) {
   auto* o=room.ram.data()+0x1040+page*256;
   // Original room reloads can leave a registration for a deleted object.
   // It has no live conversation to adopt or pending event to consume.
   if(o[0]&&(o[0x31]||((c.wram[0x481]|c.wram[0x482])&1)))return false;
  }return true;
 }
 const State* destination(ModContext& c,unsigned direction,bool authorized=false)const {
  // Native riding Link and registered interaction followers retain the same
  // owner simulation. Unsupported primary actors keep their original route.
  if(width<=160||(!authorized&&world.mode!=1)||!ordinary(s)||direction>3||
     (c.wram[0xd00]&127)!=(authorized?4:1)||c.wram[0xd04]!=(authorized?3:2)||c.wram[0x2ee]!=2||c.wram[0x2ef]!=1||
     c.wram[0xbcb]||c.wram[0xba0]||c.wram[0x4ab]||c.wram[b.group-0xc000]!=ram(b.group)||
     c.wram[b.room-0xc000]!=ram(b.room)||(c.wram[b.group-1-0xc000]!=0xd0&&!mounted(c.wram))||
     c.wram[0x1004]!=1||c.wram[variant?0xc77:0xc5c]||
     (c.wram[0x1100]&&!mounted(c.wram))||(c.wram[variant?0xcfe:0xce8]&&!follower(c.wram)))return nullptr;
  // Authorize only the boundary clamp produced by the guarded walking
  // path above, not a forced/portal transition from another position.
  unsigned position=primary(c.wram)+(direction&1?13:11),extent=unsigned(direction&1?s.room_width:s.room_height);
  bool positive=direction==1||direction==2;
  if(authorized&&c.wram[position]!=(positive?extent-1:1))return nullptr;
  int id=neighbor(s,direction);
  if(id<0||
     special(c,ram(b.group),ram(b.room))||special(c,ram(b.group),unsigned(id)))return nullptr;
  const auto* dest=find(unsigned(id));
  if(!dest||!same_extent(s,*dest)||!adoptable(c,*dest)||(!overworld(s)&&!connected(*dest)))return nullptr;
  return dest;
 }
 bool adopted_npc(const State& room,unsigned page)const {
  auto* o=room.ram.data()+0x1040+page*256;
  return o[0]&&o[1]!=0xe0&&!following_slot(room.ram.data(),page)&&((room.authority&1)||(o[0x1a]&128)||registered_npc(room,page));
 }
 bool fresh_matches(ModContext& c,const State& room,std::array<int,16>& npc_map,std::array<int,16>& enemy_map,std::array<uint8_t,32>& registry)const {
  const auto* r=c.wram;
  if(r[b.group-0xc000]!=room.ram[b.group-0xc000]||r[b.room-0xc000]!=room.ram[b.room-0xc000]||
     r[b.room+2-0xc000]!=room.ram[b.room+2-0xc000]||(r[0xd20]&&r[0xd20]!=r[0xd28])||
     r[0xd0a]*8!=room.room_width||r[0xd0b]*8!=room.room_height)return false;
  // Entry geometry and invisible controllers belong to the real native
  // entry, including a doorway held open for the arriving player. A cold
  // warp may have closed that doorway or allocated different object slots.
  // Match visible interactions by native identity and occurrence, preserving
  // the fresh controllers and remapping their declared object references.
  npc_map.fill(-1);enemy_map.fill(-1);
  if(room.authority&1) {
   std::array<bool,16> reserved{},seen{};
   for(unsigned fresh=0;fresh<16;++fresh){auto* o=r+0x1040+fresh*256;
    if(following_slot(r,fresh)){reserved[fresh]=true;continue;}
    if(!o[0]||(o[0]&2))continue;
    reserved[fresh]=o[1]==0xe0||(o[1]==0x1e&&o[2]>=0x10&&o[2]<=0x13);
    if(reserved[fresh])continue;
    unsigned key=o[1]|(unsigned(o[2])<<8);int chosen=-1,distance=100000;
    for(unsigned page=0;page<16;++page)if(!seen[page]&&(room.interaction_origin_mask&(1u<<page))&&(room.interaction_origins[page]&65535)==key) {
     int d=std::abs(int((room.interaction_origins[page]>>16)&255)-int(o[13]))+std::abs(int(room.interaction_origins[page]>>24)-int(o[11]));if(d<distance){chosen=int(page);distance=d;}
    }
    if(chosen<0)return false;seen[unsigned(chosen)]=true;
   }
   auto used=reserved;
   for(unsigned page=0;page<16;++page)if(adopted_npc(room,page)) {
    auto* o=room.ram.data()+0x1040+page*256;bool incoming=false;
    if(o[1]==0x1e&&o[2]>=0x10&&o[2]<=0x13)for(unsigned fresh=0;fresh<16;++fresh){auto* f=r+0x1040+fresh*256;if(reserved[fresh]&&f[1]==0x1e&&door_cell(f)==door_cell(o))incoming=true;}
    if(incoming)continue;
    unsigned target=page;if(used[target]){target=0;while(target<16&&used[target])++target;}if(target==16)return false;
    npc_map[page]=int(target);used[target]=true;
   }
  }else {
  for(unsigned page=0;page<16;++page) {
   auto* cached=room.ram.data()+0x1040+page*256;if(!cached[0]||cached[1]==0xe0||following_slot(room.ram.data(),page))continue;
   unsigned ordinal=0;
   for(unsigned p=0;p<page;++p){auto* o=room.ram.data()+0x1040+p*256;if(o[0]&&!following_slot(room.ram.data(),p)&&o[1]==cached[1]&&o[2]==cached[2])++ordinal;}
   for(unsigned p=0;p<16;++p){auto* o=r+0x1040+p*256;if(o[0]&&!(o[0]&2)&&o[1]==cached[1]&&o[2]==cached[2]) {
    if(!ordinal--){npc_map[page]=int(p);break;}
   }}
   if(adopted_npc(room,page)&&npc_map[page]<0)return false;
  }
  }
  for(unsigned page=0;page<16;++page) {
   const auto* part=r+0x10c0+page*256;const auto* saved_part=room.ram.data()+0x10c0+page*256;
   if(part[0]&&!(part[0]&2)&&(!saved_part[0]||part[1]!=saved_part[1]||part[2]!=saved_part[2]))return false;
   for(unsigned type=1;type<4;++type){auto* actor=room.ram.data()+0x1000+page*256+type*64;
    if(!actor[0]||(type==1&&(!adopted_npc(room,page)||npc_map[page]<0)))continue;
    for(unsigned at:{0x16u,0x18u})if(actor[at]>=0x40&&actor[at]<0x80&&actor[at+1]>=0xd0&&actor[at+1]<=0xdf&&npc_map[actor[at+1]-0xd0]<0)return false;
   }
  }
  for(unsigned fresh=0;fresh<16;++fresh) {
   auto* o=r+0x1080+fresh*256;if(!o[0]||(o[0]&2))continue;
   unsigned key=o[1]|(unsigned(o[2])<<8),fresh_count=0,live_count=0;
   for(unsigned p=0;p<16;++p){auto* e=r+0x1080+p*256;if(e[0]&&!(e[0]&2)&&(e[1]|(unsigned(e[2])<<8))==key)++fresh_count;
    auto* saved=room.ram.data()+0x1080+p*256;if(saved[0]&&(saved[1]|(unsigned(saved[2])<<8))==key)++live_count;}
   int chosen=-1,distance=100000;
   for(unsigned page=0;page<16;++page)if((room.enemy_origin_mask&(1u<<page))&&enemy_map[page]<0&&(room.enemy_origins[page]&65535)==key) {
    auto* saved=room.ram.data()+0x1080+page*256;if(!saved[0]&&fresh_count<=live_count)continue;
    int d=std::abs(int((room.enemy_origins[page]>>16)&255)-int(o[13]))+std::abs(int(room.enemy_origins[page]>>24)-int(o[11]));
    if(d<distance){distance=d;chosen=int(page);}
   }
   if(chosen<0)return false;enemy_map[unsigned(chosen)]=int(fresh);
   auto* saved=room.ram.data()+0x1080+unsigned(chosen)*256;
   if(!saved[0]&&!(room.authority&1))return false;
   if(saved[0]&&(!(room.authority&1)&&saved[0x29]!=o[0x29]))return false;
  }
  for(unsigned page=0;page<16;++page)if(room.ram[0x1080+page*256]&&enemy_map[page]<0&&((room.enemy_origin_mask&(1u<<page))||!(room.authority&1)))return false;
  unsigned list=variant?0xcca:0xcb3;if(room.authority&1)registry.fill(0);else std::copy_n(r+list,32,registry.begin());
  for(unsigned n=0;n<32;n+=2){unsigned page=room.ram[list+n]-0xd0;
   if(page>=16||room.ram[list+n+1]!=0x71||!adopted_npc(room,page)||npc_map[page]<0)continue;
   unsigned target=unsigned(npc_map[page])+0xd0;bool found=false;
   for(unsigned k=0;k<32;k+=2)if(registry[k]==target&&registry[k+1]==0x71)found=true;
   if(found)continue;unsigned free=32;for(unsigned k=0;k<32;k+=2)if(!registry[k]){free=k;break;}
   if(free==32)return false;registry[free]=uint8_t(target);registry[free+1]=0x71;
  }return true;
 }
 bool palette_transition(ModContext& c)const {
  unsigned group=c.wram[b.group-0xc000],room=c.wram[b.room-0xc000],at;
  if(variant) {
   if(group>1)return false;at=0x4875+group*2;at=c.rom[at]|(unsigned(c.rom[at+1])<<8);
  }else {
   if(group>=8)return true;at=0x4886+group;at+=c.rom[at];
  }
  for(unsigned n=0;n<128;++n,at+=variant?4:6) {
   if(at<0x4000||at+1>=0x8000||at+1>=c.rom_size)return true;
   if(c.rom[at+(variant?1:0)]==255)return false;
   if(c.rom[at+(variant?0:1)]==room)return true;
  }return true;
 }
 void actors(ModContext& c,const State& room,const std::array<int,16>& npc_map,const std::array<int,16>& enemy_map,const std::array<uint8_t,32>& registry) {
  auto* r=const_cast<uint8_t*>(c.wram);
  // Keep the original loader's progress, stack, room scripts, collisions,
  // music and global bookkeeping. Import only validated room-local actors.
  auto import=[&](const uint8_t* actor,uint8_t* target){
   std::copy_n(actor,64,target);
   for(unsigned at:{0x16u,0x18u})if(target[0]&&target[at]>=0x40&&target[at]<0x80&&target[at+1]>=0xd0&&target[at+1]<=0xdf)
    target[at+1]=uint8_t(0xd0+npc_map[target[at+1]-0xd0]);
  };
  if(room.authority&1)for(unsigned page=0;page<16;++page) {
   auto* o=r+0x1040+page*256;bool native=o[0]&&(following_slot(r,page)||o[1]==0xe0||(o[1]==0x1e&&o[2]>=0x10&&o[2]<=0x13));if(!native)std::fill_n(o,64,0);
  }
  for(unsigned page=0;page<16;++page) {
   if(adopted_npc(room,page)&&npc_map[page]>=0)import(room.ram.data()+0x1040+page*256,r+0x1040+unsigned(npc_map[page])*256);
   for(unsigned type=2;type<4;++type)import(room.ram.data()+0x1000+page*256+type*64,r+0x1000+page*256+type*64);
  }
  for(unsigned fresh=0;fresh<16;++fresh) {
   bool imported=false;for(unsigned page=0;page<16;++page)if(adopted_npc(room,page)&&npc_map[page]>=0&&npc_map[page]==int(fresh))imported=true;
   if(imported||following_slot(r,fresh))continue;auto* actor=r+0x1040+fresh*256;
   for(unsigned at:{0x16u,0x18u})if(actor[0]&&actor[at]>=0x80&&actor[at]<0xc0&&actor[at+1]>=0xd0&&actor[at+1]<=0xdf) {
    auto found=std::find(enemy_map.begin(),enemy_map.end(),int(actor[at+1]-0xd0));if(found!=enemy_map.end())actor[at+1]=uint8_t(0xd0+(found-enemy_map.begin()));
   }
  }
  unsigned list=variant?0xcca:0xcb3;std::copy(registry.begin(),registry.end(),r+list);
 }
 void adopt(ModContext& c,const State& room,const std::array<int,16>& npc_map,const std::array<int,16>& enemy_map,const std::array<uint8_t,32>& registry,bool actors_ready=false) {
  auto* r=const_cast<uint8_t*>(c.wram);auto* v=const_cast<uint8_t*>(c.vram);
  if(!actors_ready)actors(c,room,npc_map,enemy_map,registry);
  unsigned link=r[b.group-1-0xc000];
  bool horizontal=world.handoff_direction&1,positive=world.handoff_direction==1||world.handoff_direction==2;
  unsigned component=horizontal?13:11,extent=unsigned(horizontal?room.room_width:room.room_height);
  int delta=positive?-int(extent):int(extent);
  if(link>=0xd0&&link<=0xdf)delta=(positive?0:int(extent))-r[(link-0xc0)*256+component];
  for(unsigned page=0;page<16;++page)if(r[0x1000+page*256]&&!world.items[page].room)r[0x1000+component+page*256]=uint8_t(int(r[0x1000+component+page*256])+delta);
  if(unsigned at=follower(r)) {
   r[at+component]=uint8_t(int(r[at+component])+delta);
   for(unsigned n=0;n<16;++n)r[0x2a90+n*3+(horizontal?2:1)]=uint8_t(int(r[0x2a90+n*3+(horizontal?2:1)])+delta);
  }
  for(unsigned page=7;page<16;++page)if(world.items[page].room==unsigned(room.ram[b.room-0xc000])+1) {
   std::copy_n(room.ram.data()+0x1000+page*256,64,r+0x1000+page*256);world.items[page]={};
  }
  // The boundary checks clamp one pixel inward before authorizing entry.
  // Restore the actual seam coordinate rather than that legacy inset.
  r[(horizontal?(variant?0xcc3e:0xcc22):(variant?0xcc3d:0xcc21))-0xc000]=positive?0:uint8_t(extent);
  // Retain native HUD map and the one live player's glyphs. All terrain
  // descriptors/graphics come from the same already-running room source.
  std::copy_n(room.vram.begin(),0x1400,v); // Cached native object/tree glyphs only.
  for(unsigned at:{0x48cu,0x492u})r[at]=uint8_t(r[0xd08]-16);
  for(unsigned at:{0x48du,0x493u})r[at]=r[0xd09];
  // Object graphics descriptors accompany the same cached glyph source.
  unsigned at=variant?0xc05:0xc06,end=variant?0xc19:0xc1a;
  std::copy(room.ram.begin()+at,room.ram.begin()+end,r+at);
  world.handoff_stage=0;++world.handoffs;
 }
 bool has_items(const State& room)const {
  if(!ordinary(room))return false;
  return std::any_of(world.items.begin()+7,world.items.end(),[&](const auto& item){return item.room==unsigned(room.ram[b.room-0xc000])+1;});
 }
 State* item_destination(ModContext& c,unsigned page,int x,int y,int& dx,int& dy) {
  if(page<7||page>15||!ordinary(s)||world.mode!=1||c.wram[b.group-0xc000]!=ram(b.group)||
     c.wram[b.room-0xc000]!=ram(b.room)||c.wram[0x1001+page*256]!=3||c.wram[0x1000+page*256]!=1)return nullptr;
  unsigned angle=c.wram[0x1009+page*256];
  if(x>=s.room_width&&angle>16&&angle<32)x-=256;
  if(y>=s.room_height&&(angle<8||angle>24)&&angle<32)y-=256;
  dx=x<0?-1:x>=s.room_width?1:0;dy=y<0?-1:y>=s.room_height?1:0;
  if(!dx&&!dy)return nullptr;
  int id=neighbor(s.ram.data(),dx,dy);if(id<0||special(c,ram(b.group),ram(b.room))||special(c,ram(b.group),unsigned(id)))return nullptr;
  for(auto& room:world.rooms)if(room.valid==1&&room.ram[b.room-0xc000]==unsigned(id)&&same_extent(s,room)&&same_map(s,room)&&
      (overworld(s)||connected(room))&&adoptable(c,room))return &room;
  return nullptr;
 }
 void item_proxy(ModContext& c,unsigned page) {
  auto* o=const_cast<uint8_t*>(c.wram)+0x1000+page*256;
  const auto* room=find(world.items[page].room-1);
  const auto* item=room?room->ram.data()+0x1000+page*256:nullptr;
  if(item&&item[0]&&item[4]==255) {
   unsigned from=position(*room),to=position(c.wram);
   int x=(int(from&15)-int(to&15))*room->room_width+item[13];
   int y=(int(from>>4)-int(to>>4))*room->room_height+item[11];
   if(x>=-32&&x<room->room_width+32&&y>=-32&&y<room->room_height+32) {
    // The normal owner item pass applies Link/enemy damage, including native
    // knockback, rings, potion/death and once-per-blast contact flags. Terrain
    // work stays with the room which owns the actual projectile.
    std::copy_n(item,64,o);o[3]=explosion_proxy;o[6]|=128;o[13]=uint8_t(x);o[11]=uint8_t(y);o[0x1a]=0;return;
   }
  }
  std::fill_n(o,64,0);o[0]=1;o[1]=3;o[4]=1;o[0x2f]=0xb0;
 }
 uint8_t item_read(ModContext& c,uint16_t address,uint8_t value,std::span<uint16_t,6> cpu) {
  if(!c.owner||!c.hram||!ordinary(s)||c.wram[b.group-0xc000]!=ram(b.group)||c.wram[b.room+2-0xc000]!=ram(b.room+2))return value;
  // Native collision differences are8-bit. Reject a wrapped far-side target
  // before its usual radius test, especially in240px dungeon rooms.
  auto near=[&](unsigned item_page,int x,int y) {
   auto* room=find(world.items[item_page].room-1);if(!room)return false;
   unsigned from=position(*room),to=position(c.wram),at=0x1000+item_page*256;
   int bx=(int(from&15)-int(to&15))*room->room_width+room->ram[at+13];
   int by=(int(from>>4)-int(to>>4))*room->room_height+room->ram[at+11];
   return std::abs(bx-x)<=64&&std::abs(by-y)<=64;
  };
  if(address>=0xd724&&address<=0xdf24&&(address&255)==0x24) {
   unsigned item_page=(address>>8)-0xd0,base=0x1000+item_page*256;
   unsigned active=c.hram[variant?0x2d:0x2f],type=c.hram[variant?0x2c:0x2e];
   if(world.items[item_page].room&&c.wram[base+3]==explosion_proxy&&active>=0xd0&&active<=0xdf&&type<=0xc0&&(type&63)==0&&!(active==0xd0+item_page&&!type)) {
    unsigned at=(active-0xc0)*256+type;
    if(!near(item_page,c.wram[at+13],c.wram[at+11]))return 0;
   }
  }
  unsigned page=c.d-0xd0;
  if(page<7||page>=16||c.hram[variant?0x2c:0x2e])return value; // Native type0 item only.
  if(world.items[page].room) {
   if(address==0xd024&&c.wram[0x1003+page*256]==explosion_proxy&&!near(page,c.wram[0x100d],c.wram[0x100b]))return 0;
   // Keep the native slot/ID reserved for object caps, while its room owns
   // simulation, collision and drawing. No native physics step runs twice.
   if(address==0xd000+page*256&&c.rom_bank==7&&
      (c.instruction_pc==(variant?0x488e:0x48a6)||c.instruction_pc==(variant?0x490c:0x4924))&&c.wram[0x1003+page*256]!=explosion_proxy)return 0;
   if(address>=0xc200&&address<0xc2f8&&c.instruction_pc==(variant?0x2c3f:0x2cf8))item_proxy(c,page);
   return value;
  }
  if(c.wram[0x1001+page*256]!=3||c.wram[0x1000+page*256]!=1)return value;
  if((address>=0xce00&&address<0xd000)&&
     (c.instruction_pc==(variant?0x14cc:0x14de)||c.instruction_pc==(variant?0x1442:0x1454))) {
   int x=cpu[1]&255,y=cpu[1]>>8,dx=0,dy=0;
   if(auto* room=item_destination(c,page,x,y,dx,dy)) {
    x=uint8_t(x-dx*s.room_width);y=uint8_t(y-dy*s.room_height);
    unsigned cell=unsigned(y/16)*16+unsigned(x/16);
    if(cell<256){unsigned tile=room->ram[0xf00+cell];
     if(address<0xcf00&&doorway(c,*room,unsigned(x/16),unsigned(y/16),tile)!=tile)return 0;
     return room->ram[(address<0xcf00?0xe00:0xf00)+cell];
    }
   }
  }
  unsigned edge=variant?0xcca0:0xcc86;
  if((address==edge||address==edge+1)&&
     (c.instruction_pc==(variant?0x2166:0x21a8)||c.instruction_pc==(variant?0x216c:0x21ae))) {
   int x=c.wram[0x100d+page*256],y=c.wram[0x100b+page*256],dx=0,dy=0;
   if(auto* room=item_destination(c,page,x,y,dx,dy)) {
    auto* o=room->ram.data()+0x1000+page*256;std::copy_n(c.wram+0x1000+page*256,64,o);
    o[0xd]=uint8_t(x-dx*s.room_width);o[0xb]=uint8_t(y-dy*s.room_height);
    room->authority|=1;world.items[page]={unsigned(room->ram[b.room-0xc000])+1,1};++world.item_transfers;
    // The original boundary/delete returns finish normally. Its RET installs
    // the reserved owner slot after all64 native clear writes have completed.
   }
  }
  return value;
 }
 void reconcile_items(ModContext& c) {
  for(unsigned page=7;page<16;++page)if(auto id=world.items[page].room) {
   const auto* room=find(id-1);
   if(!room||!room->ram[0x1000+page*256]) {
    auto* o=const_cast<uint8_t*>(c.wram)+0x1000+page*256;
    if(o[1]==3&&!o[0x1a]&&((o[0]==1&&o[4]==1&&o[0x2f]==0xb0)||o[3]==explosion_proxy))std::fill_n(o,64,0);
    world.items[page]={};
   }else item_proxy(c,page);
  }
 }
 bool entering_from_open_edge(ModContext& c,unsigned direction)const {
  if(!world.handoffs||world.handoff_stage||world.handoff_direction!=direction||
     c.wram[b.room-0xc000]!=world.handoff_room||(c.wram[0xd00]&127)!=1||c.wram[0xd04]!=2||
     c.wram[0x2ee]!=2||c.wram[0x2ef]!=1||c.wram[0xbcb]||c.wram[0xba0]||c.wram[0x4ab]||
     (c.wram[b.group-1-0xc000]!=0xd0&&!mounted(c.wram))||c.wram[0x1004]!=1||c.wram[variant?0xc77:0xc5c]||
     c.wram[0xd0a]*8!=s.room_width||c.wram[0xd0b]*8!=s.room_height)return false;
  bool horizontal=direction&1,positive=direction==1||direction==2;
  unsigned actor=primary(c.wram),along=c.wram[actor+(horizontal?13:11)],across=c.wram[actor+(horizontal?11:13)],extent=unsigned(horizontal?s.room_width:s.room_height);
  if(across<16||across>=unsigned((horizontal?s.room_height:s.room_width)-16)||c.wram[actor+9]!=direction*8||
     (positive?along>5:along<extent-6||along>extent))return false;
  int old=neighbor(c.wram,direction==3?1:direction==1?-1:0,direction==0?1:direction==2?-1:0);
  if(old<0)return false;
  const State* from=ram(b.room)==unsigned(old)?&s:find(unsigned(old));
  if(!from||!ordinary(*from)||from->ram[b.group-0xc000]!=c.wram[b.group-0xc000]||
     from->ram[b.room+2-0xc000]!=c.wram[b.room+2-0xc000])return false;
  unsigned near=horizontal?(across/16)*16:(across/16),far=near;
  if(horizontal){near+=positive?0:s.room_width/16-1;far+=positive?s.room_width/16-1:0;}
  else {near+=positive?0:(s.room_height/16-1)*16;far+=positive?(s.room_height/16-1)*16:0;}
  // Only remove the legacy screen inset while walking INTO an already
  // adopted room through reciprocal plain collision cells. The actual native
  // collision/movement routines still run; walls and outward exits are intact.
  return !c.wram[0xe00+near]&&!from->ram[0xe00+far];
 }
 uint8_t read(ModContext& c,uint16_t address,uint8_t value) {
  if(!c.wram||!c.rom)return value;
  if(c.rom_bank!=1)return value;
  if(address==0x4126&&c.instruction_pc==0x4125&&c.pc==0x4127&&value==5&&
     (destination(c,0)||entering_from_open_edge(c,2)))return 0;
  if(address==0xc000+primary(c.wram)+11&&c.instruction_pc==0x4127&&c.pc==0x4128&&c.hl==address&&
     !value&&entering_from_open_edge(c,2))return 1;
  if(address==0xcd0d&&c.instruction_pc==0x412a) {
   if(entering_from_open_edge(c,0))return uint8_t(s.room_height);
   if(destination(c,2))return uint8_t(s.room_height-1);
  }
  if(address==0x4133&&c.instruction_pc==0x4132&&c.pc==0x4134&&value==5&&
     (destination(c,3)||entering_from_open_edge(c,1)))return 0;
  if(address==0xc000+primary(c.wram)+13&&c.instruction_pc==0x4134&&c.pc==0x4135&&c.hl==address&&
     !value&&entering_from_open_edge(c,1))return 1;
  if(address==0xcd0c&&c.instruction_pc==0x4137) {
   if(entering_from_open_edge(c,3))return uint8_t(s.room_width);
   if(destination(c,1))return uint8_t(s.room_width-1);
  }
  return value;
 }
 uint8_t cpu_read(ModContext& c,uint16_t address,uint8_t value,std::span<uint16_t,6> cpu) {
  value=item_read(c,address,value,cpu);
  auto* owner=c.owner;
  if(owner&&c.wram&&c.rom_bank==1&&!world.handoff_stage&&address>=0xc200&&address<0xc2f8&&
     c.instruction_pc==(variant?0x41af:0x41e4)&&c.pc==c.instruction_pc+1&&cpu[4]==address+1) {
   unsigned direction=c.wram[0xd02]&3;
   // Horizontal checks tail-call @transition. Vertical checks CALL it and
   // still owe their native return to the horizontal checks. Keep that real
   // stack intact; admit only the source-proven caller chain for each axis.
   unsigned caller=value|(unsigned(c.wram[address+1-0xc000])<<8);
   bool chain=direction&1?caller==0x4007:
    caller==(direction==0?0x4147u:0x4151u)&&address+3<0xc2f8&&
    c.wram[address+2-0xc000]==7&&c.wram[address+3-0xc000]==0x40;
   if(chain)if(const auto* room=destination(c,direction,true))complete_entry(*owner,c,*room,address,cpu);
  }
  if(address>=0x8000||address==0x4133||address==0x4126)return read(c,address,value);
  return value;
 }
 bool boundary(Machine& machine)const {
  static const auto stops=[] {std::bitset<65536> result;result.set(0x960);result.set(0x984);return result;}();
  auto deadline=machine.stats().cycles+4000000;
  for(unsigned n=0;n<100000;++n) {
   auto status=machine.stats();if(status.pc==b.safe_pc&&status.rom_bank==0)return true;
   if(status.cycles>=deadline)return false;machine.presentation_advance_until(stops,deadline);
  }return false;
 }
 Machine* working_copy(Machine& owner) {
  if(!scratch)scratch=owner.presentation_copy();
  else try{scratch->presentation_refresh(owner);}catch(...){scratch.reset();throw;}
  return scratch.get();
 }
 Machine* aligned_copy(Machine& owner,bool preparing=false) {
  if(!preparing&&share_owner&&!aligned_owner.empty()) {
   if(!scratch)scratch=owner.presentation_copy();
   try{scratch->presentation_refresh(aligned_owner);}catch(...){scratch.reset();throw;}
   return scratch.get();
  }
  auto* guest=working_copy(owner);
  if(preparing) {
   // Reaching the foreground boundary must not first walk a copied player
   // over an edge. That would enter the original scroll before the requested
   // private warp, delaying cold preparation by an entire transition.
   auto context=guest->presentation_context();shadow(context,ram(b.room));guest->set_buttons(0);
  }
  if(!boundary(*guest))return nullptr;
  if(!preparing&&share_owner)aligned_owner=guest->snapshot();return guest;
 }
 struct Effect {
  std::array<uint8_t,0x500> before{},after{};
  unsigned killed_before=0,killed_after=0;bool flags_only=false;
 };
 unsigned killed_mask(const uint8_t* r,unsigned id)const {
  unsigned at=variant?0xc1f:0xdc0;for(unsigned n=0;n<16;n+=2)if(r[at+n]==id)return r[at+n+1];return 0;
 }
 bool apply_effect(ModContext& c,const State& room,const Effect& effect) {
  auto* r=const_cast<uint8_t*>(c.wram);unsigned first=effect.flags_only?0x700:0x600;
  for(unsigned at=first;at<0xb00;++at) {
   unsigned mask=at>=0x700?0xef:0xff,before=effect.before[at-0x600]&mask,after=effect.after[at-0x600]&mask;
   if(before!=after&&(r[at]&mask)!=before)return false;
  }
  for(unsigned at=first;at<0xb00;++at) {
   unsigned mask=at>=0x700?0xef:0xff;
   if((effect.before[at-0x600]&mask)!=(effect.after[at-0x600]&mask))r[at]=uint8_t((r[at]&~mask)|(effect.after[at-0x600]&mask));
  }
  unsigned bits=effect.killed_after&~effect.killed_before;
  if(bits) {
   unsigned base=variant?0xc1f:0xdc0,tail=variant?0xc2f:0xdd0,id=room.ram[b.room-0xc000],slot=16;
   for(unsigned n=0;n<16;n+=2)if(r[base+n]==id){slot=n;break;}
   if(slot==16){slot=r[tail]&14;r[tail]=uint8_t((slot+2)&15);r[base+slot]=uint8_t(id);r[base+slot+1]=0;}
   r[base+slot+1]|=uint8_t(bits);
  }return true;
 }
 unsigned door_cell(const uint8_t* o)const{return o[4]?o[0x3e]:o[11];}
 bool door_condition_open(const State& room,unsigned cell)const {
  if(!(room.authority&1)||room.ram[0xe00+cell])return false;
  for(unsigned page=0;page<16;++page)if(room.interaction_origin_mask&(1u<<page)) {
   unsigned origin=room.interaction_origins[page],id=origin&255,sub=(origin>>8)&255;
   unsigned pos=((origin>>16)&255)/16+((origin>>24)&0xf0);if(id==0x1e&&pos==cell&&sub>=8&&sub<=11&&!room.ram[variant?0xc30:0xdd1])return true;
  }
  for(unsigned page=0;page<16;++page){auto* o=room.ram.data()+0x1040+page*256;if(!o[0]||o[1]!=0x1e||door_cell(o)!=cell)continue;
   if(o[4]==2)return true;
   if(o[2]>=4&&o[2]<=7&&(room.ram[variant?0xcba:0xca0]&o[0x3d]))return true;
  }return false;
 }
 bool entry_data(Machine* guest,Registers registers,unsigned sp,unsigned first_entry,
   std::array<uint8_t,16>& music,unsigned& requests,bool object_pass=false,unsigned item_graphics=0,const State* room_source=nullptr,std::vector<Effect>* effects=nullptr,unsigned interaction_graphics=0) {
   // This disposable evaluator computes entry DATA, not elapsed gameplay.
   // Its documented LCD-off DMA path is safe; no PPU/FIFO/IRQ/device state
   // returns to the owner. Keep the original native callees and their RNG,
   // initialization, scroll-data and completion effects, without scheduling
   // forty-seven unrelated game/audio/Link frames.
   registers.ime=0;guest->debug_registers(registers);guest->debug_wake();
   auto write=[&](unsigned at,uint8_t value){std::array<uint8_t,1> bytes{value};guest->debug_write(uint16_t(at),bytes);};
   write(0xff40,uint8_t(guest->presentation_context().ppu->lcdc&127));
   unsigned sound=variant?0xc74:0xc98;auto start=guest->stats().cycles;
   bool cold=first_entry==(variant?0x5892u:0x5a4fu);
   auto stops=[] {
    std::bitset<65536> result;
    for(unsigned pc:{0x451,0x452,0x475,0x476,0x64e,0x672,0x7da,0x7fe,0xc74,0xc98,0x4007,0x4713,0x4734,0x27c2,0x27c6,0x27ca,0x27ce,0x287b,0x287f,0x2883,0x2887,0x5ffd,0x77f5,0x43bb,0x43d3})result.set(pc);
    return result;
   }();
   Effect pending;bool collecting=false;unsigned effect_return=0,effect_sp=0;
   auto call=[&](unsigned entry,unsigned bank,bool first=false,bool direct=false) {
    auto regs=first?registers:guest->debug_registers();auto context=guest->presentation_context();
    auto* stack=const_cast<uint8_t*>(context.wram)+sp-0xc000;stack[0]=7;stack[1]=0x40;
    regs.sp=uint16_t(sp);regs.ime=0;
    if(first)regs.pc=uint16_t(entry);
    else if(direct)regs.pc=uint16_t(entry);
    else {regs.pc=0x8a;regs.hl=uint16_t(entry);regs.de=uint16_t((regs.de&0xff00)|bank);}
    guest->debug_registers(regs);guest->debug_wake();
    for(unsigned n=0;n<(cold?1000000u:200000u);++n) {
     auto status=guest->stats();auto r=guest->debug_registers();
     if(effects&&room_source&&(room_source->authority&1)) {
      auto context=guest->presentation_context();unsigned id=room_source->ram[b.room-0xc000];
      if(collecting&&status.pc==effect_return&&r.sp==effect_sp+2) {
       std::copy_n(context.wram+0x600,0x500,pending.after.begin());pending.killed_after=killed_mask(context.wram,id);effects->push_back(pending);collecting=false;
      }
      bool tile=status.rom_bank==6&&status.pc==(variant?0x4713:0x4734);
      bool death=status.pc>=(variant?0x27c2:0x287b)&&status.pc<=(variant?0x27ce:0x2887)&&((status.pc-(variant?0x27c2:0x287b))%4==0);
      bool killed=status.rom_bank==(variant?0x10:2)&&status.pc==(variant?0x5ffd:0x77f5);
      bool flag=status.rom_bank==(variant?0xb:0xc)&&status.pc==(variant?0x43bb:0x43d3);
      unsigned active=context.hram[variant?0x2d:0x2f],type=context.hram[variant?0x2c:0x2e];
      bool real_bomb=active>=0xd7&&active<=0xdf&&!type&&world.items[active-0xd0].room==id+1;
      if(!collecting&&((tile&&real_bomb)||death||killed||flag)&&r.sp>=0xc180&&r.sp<0xc300) {
       pending={};pending.flags_only=flag;std::copy_n(context.wram+0x600,0x500,pending.before.begin());pending.killed_before=killed_mask(context.wram,id);
       effect_return=context.wram[r.sp-0xc000]|(unsigned(context.wram[r.sp+1-0xc000])<<8);effect_sp=r.sp;stops.set(effect_return);collecting=true;
      }
     }
     if(cold&&status.pc==(variant?0x64e:0x672)&&status.rom_bank==0)if(auto* directory=std::getenv("ORACLE_GFX_FIXTURES")) {
      static unsigned fixture=0;write_file_atomic(std::filesystem::path(directory)/(std::to_string(fixture++)+".state"),guest->snapshot());
     }
     if(cold&&status.pc==(variant?0x7da:0x7fe)&&status.rom_bank==0)if(auto* directory=std::getenv("ORACLE_LAYOUT_FIXTURES")) {
      static unsigned fixture=0;write_file_atomic(std::filesystem::path(directory)/(std::to_string(fixture++)+".state"),guest->snapshot());
     }
#if GBCLLR_ORACLE_GFX_DATA
     unsigned memory_entry=variant?0x451:0x475,gfx_entry=variant?0x64e:0x672,layout_entry=variant?0x7da:0x7fe;
     bool memory=status.pc==memory_entry||status.pc==memory_entry+1;
     if(cold&&(memory||status.pc==gfx_entry||(status.pc==layout_entry&&!std::getenv("ORACLE_LAYOUT_FIXTURES")))&&status.rom_bank==0) {
      auto context=guest->presentation_context();OracleGraphicsData data;auto banks=guest->presentation_banks();
      if(memory)if(auto* directory=std::getenv("ORACLE_MEMORY_FIXTURES")) {
       bool extent=r.bc&&((r.hl>=0x8000&&unsigned(r.hl)+r.bc<=0xa000)||(r.hl>=0xd000&&unsigned(r.hl)+r.bc<=0xe000));
       if(extent&&!(context.ppu->lcdc&128)){static unsigned fixture=0;write_file_atomic(std::filesystem::path(directory)/(std::to_string(fixture++)+".state"),guest->snapshot());}
      }
      if(memory?oracle_memory_data(context,r,banks[1],data):status.pc==gfx_entry?oracle_graphics_data(context,r,data):oracle_layout_data(context,r,data)) {
       unsigned base=data.video?0x8000:0xd000;
       auto* target=data.video?const_cast<uint8_t*>(context.vram)+data.bank_selector*8192:
        const_cast<uint8_t*>(context.wram)+(data.bank_selector?data.bank_selector:1)*4096;
       std::copy_n(data.bank.begin()+(data.destination-base),data.size,target+(data.destination-base));
       std::copy(data.hram.begin(),data.hram.end(),const_cast<uint8_t*>(context.hram));
       if(data.select_banks){std::array<uint8_t,1> bank{data.bank_selector};guest->debug_write(0xff70,bank);guest->debug_write(0xff4f,bank);
        bank[0]=data.rom_bank;guest->debug_write(0x2222,bank);}
       guest->debug_registers(data.registers);continue;
      }
     }
#endif
     if(status.pc==0x4007&&r.sp==sp+2)return true;
     if(status.pc==sound) {
      auto in=guest->presentation_context();unsigned id=r.af>>8;
      if(id&&!(in.hram[variant?0x35:0x37]&8)) {
       if(requests==music.size())return false;music[requests++]=uint8_t(id);
      }
     }
     if(status.cycles-start>(cold?12000000u:4000000u))return false;
     guest->presentation_advance_until(stops,start+(cold?12000000u:4000000u)+1);
    }return false;
   };
   auto drain=[&] {
    auto context=guest->presentation_context();unsigned tail=context.hram[variant?0x23:0x25];
    write(0xff40,uint8_t(context.ppu->lcdc&127));
    if(!tail)return true;if(tail>128)return false;
    // Native VBlank data callbacks, with their original bank restoration,
    // run safely while this private LCD is off. Owner queues stay untouched.
    auto [svbk,vbk]=guest->presentation_banks();
    if(!call(variant?0xa4d:0xa71,1))return false;
    write(0xff70,svbk);write(0xff4f,vbk);return !guest->presentation_context().hram[variant?0x23:0x25];
   };
   auto palettes=[&] {
    auto [svbk,vbk]=guest->presentation_banks();
    if(!call(0x4016,0x3f)||!call(variant?0xae3:0xb07,1))return false;
    write(0xff70,svbk);write(0xff4f,vbk);return true;
   };
   if(first_entry==0x4154) {
    // Graphics-only native allocation. No item physics, fuse or actor step.
    if(!call(0x4154,0x3f)||!drain())return false;
    if(interaction_graphics) {
     auto regs=guest->debug_registers();regs.de=uint16_t((0xc000+interaction_graphics)&0xff00);guest->debug_registers(regs);
     if(!call(0x4404,0x3f)||!drain())return false;
    }
    for(unsigned page=7;page<16;++page)if(item_graphics&(1u<<page)) {
     auto regs=guest->debug_registers();regs.de=uint16_t((0xd0+page)<<8);guest->debug_registers(regs);
     if(!call(0x4422,0x3f)||!drain())return false;
    }return true;
   }
   if(object_pass) {
    // Exactly one native room-object pass and draw, without unrelated global,
    // audio/PPU/idle scheduling. The original room code still owns AI, script
    // execution, collision, changed tiles, animation and OAM construction.
    if(room_source&&(room_source->authority&1)&&!call(variant?0x5fcb:0x77c3,variant?0x10:2))return false;
    if(room_source&&!overworld(*room_source)) {
     auto context=guest->presentation_context();
     for(unsigned row=0;row<unsigned(room_source->room_height/16);++row)for(unsigned col=0;col<unsigned(room_source->room_width/16);++col) {
      unsigned cell=row*16+col,old=context.wram[0xf00+cell];
      unsigned tile=doorway(context,*room_source,col,row,old);if(tile==old)continue;
      auto regs=guest->debug_registers();regs.af=uint16_t((tile<<8)|(regs.af&255));regs.bc=uint16_t(cell);guest->debug_registers(regs);
      // The actual native primitive updates layout, collision and its normal
      // changed-tile queue. The original object pass publishes its graphics.
      if(!call(variant?0x3a52:0x3a9c,0,false,true)||guest->presentation_context().wram[0xf00+cell]!=tile)return false;
     }
    }
    std::array<std::array<uint8_t,64>,16> arriving{};
    for(unsigned page=7;page<16;++page)if(item_graphics&(1u<<page)) {
     auto regs=guest->debug_registers();regs.de=uint16_t((0xd0+page)<<8);guest->debug_registers(regs);
     if(!call(0x4422,0x3f)||!drain())return false;
     auto* item=const_cast<uint8_t*>(guest->presentation_context().wram)+0x1000+page*256;
     std::copy_n(item,64,arriving[page].begin());arriving[page][0]=1;item[0]=0;
    }
    if(!call(first_entry,0))return false;
    for(unsigned page=7;page<16;++page)if(item_graphics&(1u<<page))
     std::copy_n(arriving[page].begin(),64,const_cast<uint8_t*>(guest->presentation_context().wram)+0x1000+page*256);
    return drain()&&call(variant?0xd76:0xd9a,0)&&call(variant?0x1a37:0x1a71,0)&&drain()&&palettes();
   }
   if(!call(first_entry,1,true)||!drain())return false;
   if(cold)if(auto* directory=std::getenv("ORACLE_COLD_FIXTURES"))if(auto* selected=std::getenv("ORACLE_COLD_FIXTURE_ROOM"))
    if(guest->presentation_context().wram[b.room-0xc000]==std::strtoul(selected,nullptr,0))write_file_atomic(std::filesystem::path(directory)/"loaded.state",guest->snapshot());
   if(!call(variant?0x2e86:0x2ea5,1)||!drain()||
      !call(variant?0x3b0a:0x3b36,1)||!drain())return false;
   bool ready=false;
   for(unsigned step=0;step<128;++step) {
    auto context=guest->presentation_context();
    if(context.wram[0xd00]==1&&context.wram[0xd04]==2){ready=true;break;}
    if(!call(0x4000,1)||!drain())return false;
   }
   if(!ready||!call(variant?0x5959:0x5b2c,1)||!drain())return false;
   if(room_source&&(room_source->authority&1)) {
    auto context=guest->presentation_context();std::array<bool,256> incoming{};
    bool completed_door=false;
    for(unsigned page=0;page<16;++page){auto* o=const_cast<uint8_t*>(context.wram)+0x1040+page*256;if(o[0]&&o[1]==0x1e&&o[2]>=0x10&&o[2]<=0x13) {
     unsigned cell=door_cell(o);if(door_condition_open(*room_source,cell)){std::fill_n(o,64,0);completed_door=true;}else incoming[cell]=true;
    }}
    if(completed_door)const_cast<uint8_t*>(context.wram)[variant?0xcad:0xc93]=room_source->ram[variant?0xcad:0xc93]&0x7f;
    for(unsigned row=0;row<unsigned(room_source->room_height/16);++row)for(unsigned col=0;col<unsigned(room_source->room_width/16);++col) {
     unsigned cell=row*16+col;if(incoming[cell])continue;
     auto* data=const_cast<uint8_t*>(context.wram);data[0x3f00+cell]=room_source->ram[0x3f00+cell];
     if(data[0xf00+cell]==room_source->ram[0xf00+cell])continue;
     auto regs=guest->debug_registers();regs.af=uint16_t((room_source->ram[0xf00+cell]<<8)|(regs.af&255));regs.bc=uint16_t(cell);guest->debug_registers(regs);
     if(!call(variant?0x3a52:0x3a9c,0,false,true)||!call(variant?0x6b25:0x6c32,4)||!drain())return false;
    }
    unsigned triggers=variant?0xcba:0xca0;const_cast<uint8_t*>(context.wram)[triggers]=room_source->ram[triggers];
    const_cast<uint8_t*>(context.wram)[variant?0xc30:0xdd1]=room_source->ram[variant?0xc30:0xdd1];
   }
   return palettes();
 }
 bool complete_entry(Machine& owner,ModContext& c,const State& room,unsigned sp,std::span<uint16_t,6> cpu) {
  // One original ordinary-entry caller. Only its declared data result is
  // committed; the disposable evaluator's CPU/clock/IRQ/Link/sound evolution
  // and native save are never imported. Foreign progress/scripts fall back.
   if(!c.hram||!c.ppu||!c.ppu->bg_palette_ram||!c.ppu->obj_palette_ram||
      c.hram[variant?0x23:0x25])return false;
  try {
   auto* guest=working_copy(owner);auto registers=guest->debug_registers();
   registers.af=cpu[0];registers.bc=cpu[1];registers.de=cpu[2];registers.hl=cpu[3];
   registers.sp=uint16_t(sp);registers.pc=uint16_t(variant?0x59a8:0x5b97);
   std::vector<uint8_t> battery;if(c.eram&&c.eram_size)battery.assign(c.eram,c.eram+c.eram_size);
   std::array<uint8_t,16> music{};unsigned requests=0;
   if(!entry_data(guest,registers,sp,variant?0x59a8:0x5b97,music,requests,false,0,&room))return false;
   auto result=guest->presentation_context();const auto* r=result.wram;
   unsigned following=follower(c.wram),following_pointer=variant?0xcfe:0xce8;
   if(follower(r)!=following)return false;
   std::array<uint8_t,48> following_path{};std::array<uint8_t,3> following_registration{};
   if(following) {
    std::copy_n(c.wram+0x2a90,48,following_path.begin());
    std::copy_n(c.wram+following_pointer-2,3,following_registration.begin());
   }
   std::array<int,16> npc_map,enemy_map;std::array<uint8_t,32> registry;
   if(r[0x2ef]!=1||r[0xd00]!=1||r[0xd04]!=2||r[0xd05]||r[0xd06]||r[0xba0]||r[0xbcb]||r[0x4ab]||
      result.hram[variant?0x23:0x25]||palette_transition(result)||!fresh_matches(result,room,npc_map,enemy_map,registry)||
      (!battery.empty()&&!std::equal(battery.begin(),battery.end(),result.eram)))return false;
   unsigned group=r[b.group-0xc000],id=r[b.room-0xc000],table=variant?0x9a8:0x9cc;
   if(group>=8||table+group>=c.rom_size)return false;
   unsigned flags=(unsigned(c.rom[table+group])<<8)|id;
   if(flags<0xc700||flags>=0xcb00)return false;flags-=0xc000;
   unsigned gasha=variant?0x65c:0x65f;
   unsigned maturity=c.wram[gasha]|(unsigned(c.wram[gasha+1])<<8);
   maturity=std::min(maturity+5,65535u);
   if((r[gasha]|(unsigned(r[gasha+1])<<8))!=maturity||r[flags]!=(c.wram[flags]|0x10))return false;
   for(unsigned at=0x600;at<0xb00;++at) {
    bool output=at==gasha||at==gasha+1||at==flags||(at>=0x63a&&at<0x63e)||(mounted(c.wram)&&at>=0x638&&at<0x63a);
    bool elapsed=at>=0x622&&at<0x626;
    if(!output&&!elapsed&&r[at]!=c.wram[at])return false;
   }
   unsigned tail=c.hram[variant?0x33:0x35],head=c.hram[variant?0x32:0x34];
   if(tail<0xa0||tail>0xaf||head<0xa0||head>0xaf||requests>=16-((tail-head)&15))return false;
   unsigned carried=0,descriptors=variant?0xc05:0xc06,descriptor_end=variant?0xc19:0xc1a;
   for(unsigned page=7;page<16;++page)if(c.wram[0x1000+page*256]&&!world.items[page].room)carried|=1u<<page;
   if(carried||following) {
    // The destination owns its live NPC atlas. Add real carried items through
    // the original allocator, which reserves every live actor's glyph slot.
    // Keep the owner's timers/pose/animation; only the resolved tile base moves.
    actors(result,room,npc_map,enemy_map,registry);
    auto* data=const_cast<uint8_t*>(result.wram);
    if(following)std::copy_n(c.wram+following,64,data+following);
    std::copy(room.ram.begin()+descriptors,room.ram.begin()+descriptor_end,data+descriptors);
    std::copy_n(room.vram.begin(),0x1400,const_cast<uint8_t*>(result.vram));
    for(unsigned page=7;page<16;++page) {
     auto* item=data+0x1000+page*256;
     if(carried&(1u<<page))std::copy_n(c.wram+0x1000+page*256,64,item);
     else if(world.items[page].room==id+1)std::copy_n(room.ram.data()+0x1000+page*256,64,item);
     else item[0]=0;
    }
    if(!entry_data(guest,guest->debug_registers(),sp,0x4154,music,requests,false,carried,nullptr,nullptr,following))return false;
    result=guest->presentation_context();r=result.wram;
    if(following) {
     auto* actor=const_cast<uint8_t*>(r)+following;auto base=actor[0x1d];
     std::copy_n(c.wram+following,64,actor);actor[0x1d]=base;
    }
   }
   // All refusal checks precede this owner commit. The owner stack, logical
   // counters, hardware, old OAM publication and live type0 actors stay owned.
   auto* out=const_cast<uint8_t*>(c.wram);auto* h=const_cast<uint8_t*>(c.hram);
   auto copy=[&](unsigned lo,unsigned hi){std::copy(r+lo,r+hi,out+lo);};
   copy(0x300,0x400);copy(0x48b,0x497);out[0xbca]=r[0xbca];
   copy(0xc01,0x1000);copy(0x2000,0x4000);copy(0x4000,0x4100);copy(0x4c00,0x5000);
   // The native scroll reset straightens the escort's trail. A seamless entry
   // instead retains its live ring and pose, translated with the player below.
   if(following) {
    std::copy(following_registration.begin(),following_registration.end(),out+following_pointer-2);
    std::copy(following_path.begin(),following_path.end(),out+0x2a90);
   }
   for(unsigned page=0;page<16;++page)copy(0x1040+page*256,0x1100+page*256);
   copy(gasha,gasha+2);if(mounted(c.wram))copy(0x638,0x63a);copy(0x63a,0x63e);out[flags]=r[flags];
   h[0x14]=result.hram[0x14];h[0x15]=result.hram[0x15];
   for(unsigned n=0;n<requests;++n){out[tail]=music[n];tail=0xa0|((tail+1)&15);}h[variant?0x33:0x35]=uint8_t(tail);
   // The original entry's map ring accompanies its own CD08/CD09 origin.
   // Keep live owner Link glyphs and HUD, rather than the clone's legacy
   // inset/animation publication. Native drawAllSprites later publishes the
   // current owner pose and adopted actors through the original OAM path.
   std::array<uint8_t,0x200> player;std::copy_n(c.vram+0x2600,player.size(),player.begin());
   std::array<uint8_t,0x400> hud,attrs;std::copy_n(c.vram+0x1c00,hud.size(),hud.begin());std::copy_n(c.vram+0x3c00,attrs.size(),attrs.begin());
   auto* video=const_cast<uint8_t*>(c.vram);std::copy_n(result.vram,0x4000,video);
   std::copy(player.begin(),player.end(),video+0x2600);std::copy(hud.begin(),hud.end(),video+0x1c00);std::copy(attrs.begin(),attrs.end(),video+0x3c00);
   std::copy_n(result.ppu->bg_palette_ram,64,const_cast<uint8_t*>(c.ppu->bg_palette_ram));
   std::copy_n(result.ppu->obj_palette_ram,64,const_cast<uint8_t*>(c.ppu->obj_palette_ram));
   std::copy_n(result.ppu->bg_rgb,32,const_cast<uint32_t*>(c.ppu->bg_rgb));
   std::copy_n(result.ppu->obj_rgb,32,const_cast<uint32_t*>(c.ppu->obj_rgb));
   world.handoff_room=id;world.handoff_direction=c.wram[0xd02]&3;
   adopt(c,room,npc_map,enemy_map,registry,bool(carried||following));
   if(carried||following) {
    std::copy_n(result.vram,0x1400,const_cast<uint8_t*>(c.vram));
    std::copy(r+descriptors,r+descriptor_end,out+descriptors);
    for(unsigned page=7;page<16;++page)if(carried&(1u<<page))out[0x101d+page*256]=r[0x101d+page*256];
   }
   guest->take_audio();return true;
  }catch(const std::exception&){scratch.reset();return false;}
 }
 void local(ModContext& c,const State& room)const {
  // Only this private isolated copy is writable. These are the donor's
  // room-local ranges: owner stack, sound and C600-C9FF progress stay live.
  auto* ram=const_cast<uint8_t*>(c.wram);auto* vram=const_cast<uint8_t*>(c.vram);
  std::array<uint8_t,0x200> player_gfx;std::copy_n(vram+0x2600,player_gfx.size(),player_gfx.begin());
  auto copy=[&](unsigned lo,unsigned hi){std::copy(room.ram.begin()+lo,room.ram.begin()+hi,ram+lo);};
  copy(b.group-0xc000,b.warp-0xc000);
  // Loaded object graphics and room update queues (exact source bindings).
  copy(variant?0xc05:0xc06,variant?0xc19:0xc1a);
  for(unsigned at:variant?std::array<unsigned,7>{0xc1c,0xc1d,0xcf5,0xcf6,0xcfa,0xcfb,0xc30}:
    std::array<unsigned,7>{0xc1d,0xc1e,0xcdf,0xce0,0xce4,0xce5,0xdd1})ram[at]=room.ram[at];
  // Owner VRAM jobs reference the owner's graphics buffers, not this room.
  // A captured room has already completed its data queue; retain only jobs
  // newly generated by the room-local pass below.
  const_cast<uint8_t*>(c.hram)[variant?0x23:0x25]=0;
  for(unsigned at:variant?std::array<unsigned,2>{0xcba,0xcad}:std::array<unsigned,2>{0xca0,0xc93})ram[at]=room.ram[at];
  copy(0x300,0x400);copy(0xd00,0x1000);
  for(unsigned page=0;page<16;++page)copy(0x1040+page*256,0x1100+page*256);
  for(unsigned page=7;page<16;++page)if(world.items[page].room==unsigned(room.ram[b.room-0xc000])+1)copy(0x1000+page*256,0x1040+page*256);
  unsigned list=variant?0xcca:0xcb3;copy(list,list+32);
  copy(0x2000,0x4000);copy(0x4000,0x4240);copy(0x4400,0x4640);
  std::copy(room.vram.begin(),room.vram.end(),vram);std::copy(player_gfx.begin(),player_gfx.end(),vram+0x2600);
  std::copy(room.bg_palette.begin(),room.bg_palette.end(),const_cast<uint8_t*>(c.ppu->bg_palette_ram));
  std::copy(room.obj_palette.begin(),room.obj_palette.end(),const_cast<uint8_t*>(c.ppu->obj_palette_ram));
  std::copy(room.bg.begin(),room.bg.end(),const_cast<uint32_t*>(c.ppu->bg_rgb));
  std::copy(room.obj.begin(),room.obj.end(),const_cast<uint32_t*>(c.ppu->obj_rgb));
 }
 void shadow(ModContext& c,unsigned room)const {
  auto* r=const_cast<uint8_t*>(c.wram);unsigned active=position(s),target=position(r);
  int x=(int(active&15)-int(target&15))*s.room_width+int(ram(0xd00d));
  int y=(int(active>>4)-int(target>>4))*s.room_height+int(ram(0xd00b));
  r[0x100d]=uint8_t(std::clamp(x,8,s.room_width-9));r[0x100b]=uint8_t(std::clamp(y,8,s.room_height-9));
  r[0x1004]=1;r[0x1000]=0;r[0x101a]=0;r[0x1024]=0;r[b.disabled-0xc000]=1;
  r[0x1100]=0;r[b.group-1-0xc000]=0xd0;
  clear_private_follower(r);
  for(unsigned page=2;page<16;++page)if(world.items[page].room!=room+1||world.items[page].arriving)r[0x1000+page*256]=0;
  r[variant?0xc45:0xc29]=0;r[variant?0xc46:0xc2a]=0;
 }
 bool initialize_original(Machine& owner,State& room,unsigned group,unsigned id) {
  auto* guest=aligned_copy(owner,true);if(!guest)return false;
  auto c=guest->presentation_context();auto* r=const_cast<uint8_t*>(c.wram);
  clear_private_follower(r);
  unsigned at=b.warp-0xc000;r[at]=uint8_t(128|group);r[at+1]=uint8_t(id);r[at+2]=0x84;r[at+3]=0x55;r[at+4]=1;
  for(unsigned frame=0;frame<120;++frame) {
   guest->run_frame(0);guest->take_audio();c=guest->presentation_context();
   if(c.wram[b.group-0xc000]!=group||c.wram[b.room-0xc000]!=id||c.wram[b.warp+4-0xc000])continue;
   if(c.wram[0xba0])return false;
   if((c.wram[0xd00]&127)!=1||c.wram[0xd04]!=2||c.wram[0x4ab]||c.wram[0x2ef]!=1)continue;
   shadow(c,id);guest->run_frame(0);guest->take_audio();c=guest->presentation_context();capture(c,&room);return ordinary(room);
  }return false;
 }
 bool initialize(Machine& owner,State& room,unsigned group,unsigned id) {
  auto c=owner.presentation_context();
  bool direct=variant&&group==0&&id==0x9a&&c.wram&&c.wram[b.room+2-0xc000]==3;
#if GBCLLR_ORACLE_GFX_DATA
  // Original ordinary room caller/data format, not a room/NPC list.
  // Readiness/progress/battery checks below refuse effectful native outcomes.
  direct=c.wram&&ordinary(s)&&group==ram(b.group)&&group<8&&
   c.wram[0x1004]==1&&
   !c.wram[variant?0xc6a:0xc4f]&&(!c.wram[0x1100]||mounted(c.wram))&&
   !special(c,group,id);
#endif
  if(direct) {
   try {if(initialize_direct(owner,room,group,id))return true;}
   catch(const std::exception&){scratch.reset();}
  }
  return initialize_original(owner,room,group,id);
 }
 bool cold_progress(std::span<const uint8_t,0x500> before,ModContext& c,unsigned group,unsigned id)const {
  // loadingRoom's native warp does not perform walking-entry maturity+5.
  // Retain only original RNG/visited data; scripts cannot predict inventory,
  // quest flags or any other persistent progress into a neighbor history.
  unsigned table=variant?0x9a8:0x9cc;
  if(group>=8||table+group>=c.rom_size)return false;
  unsigned flag=(unsigned(c.rom[table+group])<<8)|id;
  if(flag<0xc700||flag>=0xcb00)return false;flag-=0xc000;
  if(c.wram[flag]!=(before[flag-0x600]|0x10))return false;
  for(unsigned at=0x600;at<0xb00;++at)
   if(at!=flag&&!(at>=0x63a&&at<0x63e)&&!(at>=0x622&&at<0x626)&&c.wram[at]!=before[at-0x600])return false;
  return true;
 }
 bool initialize_direct(Machine& owner,State& room,unsigned group,unsigned id) {
  unsigned force=variant?0xc6a:0xc4f;auto owner_force=owner.presentation_context().wram[force];
  auto* guest=aligned_copy(owner,true);if(!guest)return false;
  auto c=guest->presentation_context();auto* r=const_cast<uint8_t*>(c.wram);
  if(mounted(r)){r[0x1100]=0;r[0x1001]=0;r[b.group-1-0xc000]=0xd0;}
  clear_private_follower(r);
  const char* fixtures=std::getenv("ORACLE_COLD_FIXTURES");const char* selected=std::getenv("ORACLE_COLD_FIXTURE_ROOM");
  bool record=fixtures&&selected&&std::strtoul(selected,nullptr,0)==id;
  auto save=[&](const char* name){if(record)write_file_atomic(std::filesystem::path(fixtures)/name,guest->snapshot());};
  save("before.state");
  std::array<uint8_t,0x500> progress;std::copy_n(c.wram+0x600,progress.size(),progress.begin());
  std::vector<uint8_t> battery;if(c.eram&&c.eram_size)battery.assign(c.eram,c.eram+c.eram_size);
  unsigned modifier=c.wram[b.room+2-0xc000];
  unsigned at=b.warp-0xc000;r[at]=uint8_t(128|group);r[at+1]=uint8_t(id);r[at+2]=0x84;r[at+3]=0x55;r[at+4]=1;
  // Enter the original loader through its actual foreground thread. Its own
  // scheduler selects the native stack/bank/control before DATA evaluation.
  auto begin=guest->stats().cycles;bool entering=false;
  static const auto loading=[] {std::bitset<65536> result;result.set(0x5892);result.set(0x5a4f);return result;}();
  for(unsigned step=0;step<500000;++step) {
   auto status=guest->stats();
   if(status.pc==(variant?0x5892:0x5a4f)&&status.rom_bank==1){entering=true;break;}
   if(status.cycles-begin>3000000)break;guest->presentation_advance_until(loading,begin+3000001);
  }
  if(!entering)return false;
  c=guest->presentation_context();auto registers=guest->debug_registers();unsigned sp=registers.sp;
  save("caller.state");
  if(sp!=0xc21e||c.hram[variant?0x1c:0x1e]!=0xe8)return false;
  unsigned parent=c.wram[sp-0xc000]|(unsigned(c.wram[sp+1-0xc000])<<8);
  if(parent!=(variant?0x326b:0x33c4))return false; // Original main-thread loadingRoom caller.
  // Reaching the real foreground caller may run ordinary game passes first
  // (Ages advances the pirate ship even while Link is indoors). Those elapsed
  // global updates are not cold-room DATA. Seed the callee with the owner's
  // original persistent inputs; keep the actual native stack/bank/thread and
  // reject any new persistent effects produced by the loader itself below.
  std::copy(progress.begin(),progress.end(),const_cast<uint8_t*>(c.wram)+0x600);
  save("input.state");
  auto lcdc=c.ppu->lcdc;std::array<uint8_t,16> music{};unsigned requests=0;
  if(!entry_data(guest,registers,sp,variant?0x5892:0x5a4f,music,requests))return false;
  save("data.state");c=guest->presentation_context();
  auto ready=[&]{return c.wram[b.group-0xc000]==group&&c.wram[b.room-0xc000]==id&&
   // Seasons determines the destination's own season from its room pack.
   // A legitimate neighboring autumn room need not retain the owner's winter.
   (variant||c.wram[b.room+2-0xc000]==modifier)&&c.wram[0x2ee]==2&&c.wram[0x2ef]==1&&
   c.wram[0xd00]==1&&c.wram[0xd04]==2&&!c.wram[0xd05]&&!c.wram[0xd06]&&
   c.wram[0xd0a]*8==s.room_width&&c.wram[0xd0b]*8==s.room_height&&
   !c.wram[0xba0]&&!c.wram[0xbcb]&&!c.wram[0x4ab]&&!c.hram[variant?0x23:0x25]&&
   cold_progress(progress,c,group,id)&&
   (battery.empty()||std::equal(battery.begin(),battery.end(),c.eram));};
  if(!ready()){
   if(record){std::array<uint8_t,4> checks{uint8_t(cold_progress(progress,c,group,id)),
    uint8_t(battery.empty()||std::equal(battery.begin(),battery.end(),c.eram)),
    uint8_t(modifier),c.hram[variant?0x23:0x25]};write_file_atomic(std::filesystem::path(fixtures)/"refusal.bin",checks);}
   return false;
  }
  auto resume=guest->debug_registers();resume.pc=uint16_t(parent);resume.sp=uint16_t(sp+2);resume.ime=registers.ime;
  guest->debug_registers(resume);std::array<uint8_t,1> video{lcdc};guest->debug_write(0xff40,video);
  c=guest->presentation_context();shadow(c,id);
  // Like local()+shadow() during ordinary ticks, retain owner Link control.
  // The setup warp's forced-Link request belongs to its disposable Link,
  // not to the room's actors, and must not stall their original game pass.
  const_cast<uint8_t*>(c.wram)[force]=uint8_t(owner_force);
  // Return through the real foreground caller. A hardware-frame duration
  // can end inside updateAllObjects after DATA crossed old deadlines. Stop
  // at its first completed native draw, rather than capturing hidden old OAM.
  guest->set_buttons(0);bool updated=false,published=false;auto finish=guest->stats().cycles;
  static const auto drawing=[] {std::bitset<65536> result;for(unsigned pc:{0x3382,0x345b,0x326e,0x33c7})result.set(pc);return result;}();
  for(unsigned n=0;n<500000;++n){auto status=guest->stats();
   if(status.pc==(variant?0x3382:0x345b))updated=true;
   if(updated&&status.pc==(variant?0x326e:0x33c7)&&guest->debug_registers().sp==sp+2){published=true;break;}
   if(status.cycles-finish>3000000)break;guest->presentation_advance_until(drawing,finish+3000001);
  }
  guest->take_audio();if(!published)return false;
  c=guest->presentation_context();save("ready.state");
  if(!ready())return false;capture(c,&room);
  return c.wram[b.group-0xc000]==group&&c.wram[b.room-0xc000]==id&&ordinary(room);
 }
 void tick_original(Machine& owner,State& room) {
  auto* guest=aligned_copy(owner);if(!guest)return;
  auto c=guest->presentation_context();
  if((c.wram[0xd00]&127)!=1||c.wram[0xd04]!=2||c.wram[0x2ee]!=2||c.wram[0x2ef]!=1)return;
  local(c,room);shadow(c,room.ram[b.room-0xc000]);
  guest->run_frame(0);guest->take_audio();c=guest->presentation_context();
  if(c.wram[b.group-0xc000]!=room.ram[b.group-0xc000]||c.wram[b.room-0xc000]!=room.ram[b.room-0xc000])return;
  capture(c,&room);++world.ticks;
 }
 void tick(Machine& owner,State& room) {
#if !GBCLLR_ORACLE_GFX_DATA
  if(has_items(room))
#endif
  try {
   auto* guest=aligned_copy(owner);if(!guest)return;
   auto c=guest->presentation_context();
   if((c.wram[0xd00]&127)!=1||c.wram[0xd04]!=2||c.wram[0x2ee]!=2||c.wram[0x2ef]!=1)return;
   auto actual=owner.presentation_context();
   for(unsigned page=7;page<16;++page)if(world.items[page].room==unsigned(room.ram[b.room-0xc000])+1&&actual.wram[0x1003+page*256]==explosion_proxy)
    room.ram[0x1037+page*256]|=actual.wram[0x1037+page*256]&0x40;
   local(c,room);
   {
    auto* data=const_cast<uint8_t*>(c.wram);std::copy_n(actual.wram+0x600,0x500,data+0x600);
    unsigned group=room.ram[b.group-0xc000],table=variant?0x9a8:0x9cc;
    unsigned flags=(unsigned(c.rom[table+group])<<8)|room.ram[b.room-0xc000];if(flags>=0xc700&&flags<0xcb00)data[flags-0xc000]|=0x10;
   }
   shadow(c,room.ram[b.room-0xc000]);
   // mainThreadStart increments the native play counter and publishes its
   // low byte to wFrameCounter before the native object pass. This is a
   // private input to one logical actor step, never an owner progress write.
   auto* r=const_cast<uint8_t*>(c.wram);
   for(unsigned at=0x622;at<0x626;++at)if(++r[at])break;r[0xc00]=r[0x622];
   std::array<uint8_t,1> thread{0xe8};guest->debug_write(uint16_t(variant?0xff9c:0xff9e),thread);
   auto lcdc=c.ppu->lcdc;std::array<uint8_t,16> music{};unsigned requests=0;std::vector<Effect> effects;
   unsigned arriving=0;for(unsigned page=7;page<16;++page)if(world.items[page].room==unsigned(room.ram[b.room-0xc000])+1&&world.items[page].arriving)arriving|=1u<<page;
   if(entry_data(guest,guest->debug_registers(),0xc21e,variant?0x3382:0x345b,music,requests,true,arriving,&room,&effects)) {
    std::array<uint8_t,1> video{lcdc};guest->debug_write(0xff40,video);guest->take_audio();
    c=guest->presentation_context();
    if(c.wram[b.group-0xc000]==room.ram[b.group-0xc000]&&c.wram[b.room-0xc000]==room.ram[b.room-0xc000]){
     bool real_items=bool(room.authority&1);capture(c,&room);
     auto live_effects=owner.presentation_context();
     for(const auto& effect:effects)if(!apply_effect(live_effects,room,effect)){room.authority|=2;++world.failures;}
     if(!stable_progress(live_effects.wram,room.ram.data()))room.authority|=2;
     for(unsigned page=7;page<16;++page)if(world.items[page].room==unsigned(room.ram[b.room-0xc000])+1)world.items[page].arriving=0;
     if(real_items&&requests) {
      auto live=owner.presentation_context();auto* h=const_cast<uint8_t*>(live.hram);auto* out=const_cast<uint8_t*>(live.wram);
      unsigned tail=h[variant?0x33:0x35],head=h[variant?0x32:0x34];
      if(tail>=0xa0&&tail<=0xaf&&head>=0xa0&&head<=0xaf&&requests<16-((tail-head)&15)) {
       for(unsigned n=0;n<requests;++n){out[tail]=music[n];tail=0xa0|((tail+1)&15);}h[variant?0x33:0x35]=uint8_t(tail);
      }
     }
     ++world.ticks;return;
    }
   }
  }catch(const std::exception&){scratch.reset();}
  if(has_items(room)){++world.failures;return;}
  tick_original(owner,room);
 }
 void frame(Machine& owner) {
  struct Scope {std::vector<uint8_t>& data;bool& enabled;~Scope(){data.clear();enabled=false;}} scope{aligned_owner,share_owner};
  ++world.frame;
  if(world.mode!=1||!ordinary(s)||(width<=160&&std::none_of(world.items.begin(),world.items.end(),[](const auto& item){return item.room!=0;})))return;
  // Line0 captured the displayed frame, while the CPU may already have
  // authorized a transition by this host boundary. Do not tick a neighbor
  // from that loading continuation or discard its prepared room.
  auto live=owner.presentation_context();
  if((live.wram[0xd00]&127)!=1||live.wram[0xd04]!=2||live.wram[0x2ee]!=2||live.wram[0x2ef]!=1||
     live.wram[0xbcb]||live.wram[0xba0]||live.wram[0x4ab]||
     live.wram[b.group-0xc000]!=ram(b.group)||live.wram[b.room-0xc000]!=ram(b.room))return;
  uint32_t progress=2166136261u;
  for(unsigned at=0x700;at<0xb00;++at)progress=(progress^(s.ram[at]&~0x10u))*16777619u;
  for(unsigned at=variant?0x6ca:0x6d0,n=0;n<16;++n)progress=(progress^s.ram[at+n])*16777619u;
  for(unsigned at=variant?0x692:0x69a,n=0;n<8;++n)progress=(progress^s.ram[at+n])*16777619u;
  for(unsigned at=variant?0xc55:0xc39,n=0;n<3;++n)if(n!=1)progress=(progress^ram(at+n+0xc000))*16777619u;
  progress=(progress^ram(b.group))*16777619u;progress=(progress^ram(b.room+2))*16777619u;
  if(progress!=world.progress){for(auto& room:world.rooms)if((!has_items(room)&&!(room.authority&1))||!same_map(s,room)||room.ram[b.room+2-0xc000]!=ram(b.room+2))std::memset(&room,0,sizeof room);world.retry.fill(0);world.progress=progress;scratch.reset();}
  if(width<=160){for(auto& room:world.rooms)if(room.valid==1&&has_items(room))tick(owner,room);reconcile_items(live);return;}
  unsigned active=ram(b.room),grid=position(s),group=ram(b.group),extent=!overworld(s)?(ram(variant?0xcc55:0xcc39)==255?16:8):variant?(group==1?11:16):(group>=2?16:14);
  int rw=s.room_width,rh=s.room_height;auto view=camera(false);
  auto floor_div=[](int n,int d){return n>=0?n/d:-((-n+d-1)/d);};
  int first=floor_div(view.left,rw)-1,last=floor_div(view.left+int(width)-1,rw)+1;
  int top=view.top;
  struct Candidate {unsigned id;int dx,dy,priority;bool visible;};std::vector<Candidate> candidates;
  for(int dy=-1;dy<=1;++dy)for(int dx=first;dx<=last;++dx){
   int col=int(grid&15)+dx,row=int(grid>>4)+dy;if((!dx&&!dy)||col<0||col>=int(extent)||row<0||row>=16)continue;
   int ox=-view.left+dx*rw,oy=16-top+dy*rh;
   bool horizontal=ox<int(width)&&ox+rw>0,visible=horizontal&&oy<144&&oy+rh>16;
   if(!horizontal&&std::abs(dx)>1)continue;
   int distance=std::abs(dx*rw+rw/2-world.focus_x)+std::abs(dy*rh+rh/2-world.focus_y);
   int id=neighbor(s.ram.data(),dx,dy);if(id<0)continue;
   candidates.push_back({unsigned(id),dx,dy,distance+(visible?0:512),visible});
  }
  std::sort(candidates.begin(),candidates.end(),[](const Candidate& a,const Candidate& z){return a.priority!=z.priority?a.priority<z.priority:a.id<z.id;});
  if(candidates.size()>world.rooms.size())candidates.resize(world.rooms.size());
#if GBCLLR_ORACLE_SHARED_OWNER
  // A single ready neighbor cannot reuse an alignment. Only prepare this
  // ephemeral source when at least two visible overworld rooms will tick.
  if(overworld(s)) {
   unsigned ready=0;
   for(const auto& room:world.rooms)if(room.valid==1&&room.ram[b.group-0xc000]==group&&
       room.ram[b.room-0xc000]!=active&&
       (room.ram[b.room-0xc000]&15)<extent) {
    unsigned p=position(room);int dx=int(p&15)-int(grid&15),dy=int(p>>4)-int(grid>>4);
    int ox=-view.left+dx*rw,oy=16-top+dy*rh;
    if(ox<int(width)&&ox+rw>0&&oy<144&&oy+rh>16&&++ready==2){share_owner=true;break;}
   }
  }
#endif
  bool initialized=false;
  for(const auto& candidate:candidates) {
   unsigned id=candidate.id,slot=world.rooms.size();
   for(unsigned i=0;i<world.rooms.size();++i)if(world.rooms[i].valid&&world.rooms[i].ram[b.room-0xc000]==id){slot=i;break;}
   if(slot==world.rooms.size())for(unsigned i=0;i<world.rooms.size();++i)if(!world.rooms[i].valid){slot=i;break;}
   if(slot==world.rooms.size())for(unsigned i=0;i<world.rooms.size();++i){
    unsigned old=world.rooms[i].ram[b.room-0xc000];
    if(!has_items(world.rooms[i])&&std::none_of(candidates.begin(),candidates.end(),[&](const Candidate& c){return c.id==old;})){
     slot=i;std::memset(&world.rooms[i],0,sizeof(State));world.retry[i]=0;break;
    }
   }
   if(slot==world.rooms.size())continue;
   auto& room=world.rooms[slot];
   if(room.valid==1){if(candidate.visible||has_items(room))tick(owner,room);continue;}
   if(initialized||world.retry[slot]>world.frame)continue;initialized=true;
   if(!initialize(owner,room,group,id)) {
    room.valid=2;room.ram[b.group-0xc000]=uint8_t(group);room.ram[b.room-0xc000]=uint8_t(id);
    world.retry[slot]=world.frame+120;++world.failures;
   }
  }
  for(auto& room:world.rooms)if(room.valid==1&&has_items(room)&&std::none_of(candidates.begin(),candidates.end(),[&](const auto& c){return c.id==room.ram[b.room-0xc000];}))tick(owner,room);
  reconcile_items(live);
 }
 unsigned body_lcdc(const State& s)const {
  // wram.s/bank0.s: wGfxRegs3 is the published game-body register set,
  // installed by the LCD interrupt after the HUD. Line0 uses a different map.
  return (s.ram[0x491]&128)?s.ram[0x491]:s.lcdc;
 }
 unsigned pixel(const State& s,unsigned tile,unsigned attr,int x,int y,bool obj)const {
  if(attr&32)x=7-x;if(attr&64)y=7-y;
  int base=obj||(body_lcdc(s)&16)?int(tile)*16:0x1000+int8_t(tile)*16;
  unsigned at=unsigned(base)+((attr&8)?0x2000:0)+unsigned(y)*2,bit=7-x;
  return ((s.vram[at]>>bit)&1)|(((s.vram[at+1]>>bit)&1)<<1);
 }
 // Native entry opens an incoming shutter before Link can enter it. Preview
 // that same opening while the adjoining outgoing edge is already passable.
 // Only pixels change here; the real room/controller/collision stays native.
 unsigned doorway(ModContext& c,const State& room,unsigned col,unsigned row,unsigned tile)const {
  if(&room==&s||overworld(room)||(col&&row&&col<unsigned(room.room_width/16-1)&&row<unsigned(room.room_height/16-1)))return tile;
  unsigned table=8*0x4000+(variant?0x474e:0x4818)-0x4000;
  if(table+32>c.rom_size)return tile;
  for(unsigned n=0;n<16;++n)if(tile==c.rom[table+n*2+1]) {
   unsigned direction=n&3;
   if((direction==0&&row!=0)||(direction==1&&col!=unsigned(room.room_width/16-1))||
      (direction==2&&row!=unsigned(room.room_height/16-1))||(direction==3&&col!=0))return tile;
   int id=neighbor(room,direction);if(id<0)return tile;
   const auto* source=unsigned(id)==ram(b.room)?&s:find(unsigned(id));
   if(!source||!same_extent(room,*source)||!same_map(room,*source))return tile;
   unsigned opposite=direction==0?(source->room_height/16-1)*16+col:
     direction==2?col:row*16+(direction==3?source->room_width/16-1:0);
   if(!source->ram[0xe00+opposite])return c.rom[table+n*2];
   return tile;
  }return tile;
 }
 void terrain(ModContext& c,const State& s,unsigned x,unsigned y,int wx,int wy) {
  auto at=size_t(y)*width+x;
  if(wx<0||wx>=s.room_width||wy<0||wy>=s.room_height)return;
  unsigned raw=s.ram[0xf00+unsigned(wy/16)*16+unsigned(wx/16)];
  unsigned metatile=doorway(c,s,unsigned(wx/16),unsigned(wy/16),raw);
  unsigned tile,attr;int px=wx,py=wy;
  if(metatile==raw&&wx>=s.camera_x&&wx<s.camera_x+160&&wy>=s.camera_y&&wy<s.camera_y+128) {
   // The actual source camera publishes this window in the native VRAM
   // ring, including direct8x8writers absent from the CF00metatile layout.
   px=(wx+s.ram[0xd09])&255;py=(wy+s.ram[0xd08])&255;
   unsigned map=(body_lcdc(s)&8)?0x1c00:0x1800,cell=map+unsigned(py/8)*32+unsigned(px/8);
   tile=s.vram[cell];attr=s.vram[cell+0x2000];
  }else {
   unsigned sub=((wy&8)?2:0)+((wx&8)?1:0),map=0x3000+metatile*8;
   tile=s.ram[map+sub];attr=s.ram[map+4+sub];
  }
  unsigned p=pixel(s,tile,attr,px&7,py&7,false);
  pixels[at]=(fade_palette?fade_palette->bg_rgb:s.bg.data())[(attr&7)*4+p];opaque[at]=uint8_t(p);priority[at]=uint8_t(attr>>7);
 }
 void sprite(const State& s,int ox,int oy,int sx,int sy,unsigned tile,unsigned attr) {
  int height=(body_lcdc(s)&4)?16:8,center=int(width-160)/2;
  if(height==16)tile&=254;
  for(int dy=0;dy<height;++dy)for(int dx=0;dx<8;++dx) {
   int x=sx+dx,y=sy+dy;
   if(x<0||x>=int(width)||y<16||y>=144||(copy_native_center&&x>=center&&x<center+160))continue;
   int wx=x-ox,wy=y-oy;
   if(copy_native_center&&(wx<0||wx>=s.room_width||wy<0||wy>=s.room_height))continue;
   int py=(attr&64)?height-1-dy:dy;
   unsigned p=pixel(s,tile+unsigned(py/8),attr&~64,dx,py&7,true);auto at=size_t(y)*width+unsigned(x);
   if(p&&!(opaque[at]&&((body_lcdc(s)&1)&&(priority[at]||(attr&128)))))pixels[at]=(fade_palette?fade_palette->obj_rgb:s.obj.data())[(attr&7)*4+p];
  }
 }
 unsigned objects(ModContext& c,const State& s,int ox,int oy,unsigned types=15,bool overlay=false) {
  auto ram=[&](unsigned a){return s.ram[a-0xc000];};
  unsigned count=0;
  for(int p=3;p>=0;--p)for(int page=15;page>=0;--page)for(int type=3;type>=0;--type) {
   unsigned object=0xd000+unsigned(page)*256+unsigned(type)*64;
   if(!(types&(1u<<type)))continue;
   // Link and its primary companion travel with the owner. An outgoing room
   // can still contain their last capture until its next private actor pass.
   if(type==0&&page<2&& &s!=&world.owner)continue;
   bool escort=type==1&&following_slot(s.ram.data(),unsigned(page));
   if(escort&& &s!=&this->s)continue;
   if(type==0&&page>=7) {
    bool owner=&s==&world.owner;
    bool local=world.items[unsigned(page)].room==unsigned(s.ram[b.room-0xc000])+1;
    if(owner?bool(world.items[unsigned(page)].room):!local)continue;
   }
   if(!ram(object)||!(ram(object+0x1a)&128)||(ram(object+0x1a)&3)!=unsigned(p))continue;
   bool hud=type==1&&ram(object+1)==0xe0;
   if(hud!=overlay)continue;
   unsigned pointer=ram(object+0x1e)|(ram(object+0x1f)<<8);
   size_t at=(b.oam_bank+(pointer>>14))*0x4000u+(pointer&0x3fff);
   if(at>=c.rom_size)continue;unsigned n=c.rom[at++];
   if(!n||n>40||at+n*4>c.rom_size)continue;
   int x=ram(object+0x0d),y=ram(object+0x0b);if(s.room_width==160&&x>=240)x-=256;if(y>=240)y-=256;
   if(escort) {
    unsigned lead=primary(s.ram.data());int lx=s.ram[lead+13],ly=s.ram[lead+11];
    if(s.room_width==160&&lx>=240)lx-=256;if(ly>=240)ly-=256;
    x=lx+int8_t(uint8_t(ram(object+13)-s.ram[lead+13]));y=ly+int8_t(uint8_t(ram(object+11)-s.ram[lead+11]));
   }
   if(!escort&&(x< -16||x>s.room_width+16||y< -16||y>s.room_height+16))continue;
   int object_x=ox,object_y=oy;
   if(hud){int right=-256;for(unsigned i=0;i<n;++i)right=std::max(right,int(int8_t(c.rom[at+i*4+1])));
    object_x=int(width)-8-right-x;object_y=16;
   }
   for(int i=int(n)-1;i>=0;--i) {
    const auto* data=c.rom+at+unsigned(i)*4;
    sprite(s,object_x,object_y,object_x+x+int8_t(data[1])-8,
     object_y+y+int8_t(ram(object+0x0f))+int8_t(data[0])-16,
     (ram(object+0x1d)+data[2])&255,ram(object+0x1c)^data[3]);++count;
   }
  }return count;
 }
 struct Camera {int left,top,lo,hi,above,below;bool native_center;};
 Camera camera(bool visibility=true)const {
  int center=int(width-160)/2,rw=s.room_width,rh=s.room_height;
  int left=s.camera_x-center,top=s.camera_y,scene_left=0,scene_right=s.room_width;
  int scene_top=0,scene_bottom=s.room_height;bool native_center=true;
  if(ordinary(s)&&width>160) {
   for(const auto& room:world.rooms)if(room.valid==1&&same_map(s,room)&&(!visibility||!hide_closed_rooms||overworld(s)||connected(room))) {
    int dx=(int(position(room)&15)-int(position(s)&15))*rw;
    int dy=(int(position(room)>>4)-int(position(s)>>4))*rh;
    scene_left=std::min(scene_left,dx);scene_right=std::max(scene_right,dx+rw);
    scene_top=std::min(scene_top,dy);scene_bottom=std::max(scene_bottom,dy+rh);
   }
   if(overworld(s)||scene_right-scene_left>rw||scene_bottom-scene_top>rh) {
    native_center=false;
    left=overworld(s)?world.focus_x-int(width)/2:
     int(width)>=scene_right-scene_left?(scene_left+scene_right-int(width))/2:
      std::clamp(world.focus_x-int(width)/2,scene_left,scene_right-int(width));
    top=std::clamp(world.focus_y-80,scene_top,scene_bottom-128);
   }
  }
  return {left,top,scene_left,scene_right,scene_top,scene_bottom,native_center};
 }
 ModView render(ModContext& c,std::span<const uint32_t> native) {
  ModView out;out.width=width;out.expanded=world.mode&&s.valid&&width>160;
  pixels.assign(size_t(width)*144,0xff000000u);opaque.assign(pixels.size(),0);priority.assign(pixels.size(),0);
  auto view=camera();int center=int(width-160)/2,rw=s.room_width,rh=s.room_height;
  int left=view.left,top=view.top,scene_left=view.lo,scene_right=view.hi,scene_top=view.above,scene_bottom=view.below;
  copy_native_center=!out.expanded||view.native_center;
  fade_palette=world.mode==4?c.ppu:nullptr;
  struct ClearPalette {const ModPpu*& value;~ClearPalette(){value=nullptr;}} clear_palette{fade_palette};
  if(out.expanded) {
   for(unsigned y=16;y<144;++y)for(unsigned x=0;x<width;++x)
    terrain(c,s,x,y,left+int(x),top+int(y)-16);
   if(ordinary(s)) {
    unsigned room=ram(b.room),group=ram(b.group);
    for(const auto& neighbor:world.rooms) {
     if(neighbor.valid!=1||neighbor.ram[b.group-0xc000]!=group||neighbor.ram[b.room-0xc000]==room)continue;
     if(hide_closed_rooms&&!overworld(s)&&!connected(neighbor))continue;
     int dx=(int(position(neighbor)&15)-int(position(s)&15))*rw;
     int dy=(int(position(neighbor)>>4)-int(position(s)>>4))*rh;
     int ox=-left+dx,oy=16-top+dy;
     for(unsigned y=16;y<144;++y)for(unsigned x=0;x<width;++x) {
      if(copy_native_center&&int(x)>=center&&int(x)<center+160)continue;
      int wx=int(x)-ox,wy=int(y)-oy;
      if(wx>=0&&wx<neighbor.room_width&&wy>=0&&wy<neighbor.room_height)terrain(c,neighbor,x,y,wx,wy);
     }
    }
   }
   // A complete world actor owns every pixel of its metasprite. Terrain for
   // all rooms is finished first; the authentic native RGB window never
   // repaints part of an actor in this connected-world camera mode.
   out.sprites=objects(c,s,-left,16-top,copy_native_center?15:14);
   for(const auto& room:world.rooms)if(room.valid==1&&room.ram[b.group-0xc000]==ram(b.group)&&
       room.ram[b.room-0xc000]!=ram(b.room)&&(!hide_closed_rooms||overworld(s)||connected(room))) {
    int dx=(int(position(room)&15)-int(position(s)&15))*rw;
    int dy=(int(position(room)>>4)-int(position(s)>>4))*rh;
    out.sprites+=objects(c,room,-left+dx,16-top+dy,15);
   }
   if(!copy_native_center&&world.owner.valid)out.sprites+=objects(c,world.owner,-left,16-top,1);
   if(!copy_native_center)for(unsigned y=16;y<144;++y)for(unsigned x=0;x<width;++x)
    if(left+int(x)<scene_left||left+int(x)>=scene_right||top+int(y)-16<scene_top||top+int(y)-16>=scene_bottom)pixels[size_t(y)*width+x]=0xff000000u;
   for(unsigned y=0;y<16;++y)std::fill_n(pixels.data()+size_t(y)*width,width,native[y*160]);
  }
  for(unsigned y=0;y<(copy_native_center?144u:16u);++y)std::copy_n(native.data()+y*160,160,pixels.data()+size_t(y)*width+center);
  if(out.expanded&&world.mode==3) {
   // The native textbox consists of priority tiles in BG palette0. Copy only
   // that live UI layer; the expanded field and complete actors stay behind it.
   unsigned lcdc=c.wram[0x491],map=(lcdc&8)?0x1c00:0x1800;
   for(unsigned y=16;y<144;++y)for(unsigned x=0;x<160;++x){
    unsigned bx=(x+c.wram[0x493])&255,by=(y+c.wram[0x492])&255;
    unsigned cell=map+(by/8)*32+bx/8;
    if((c.vram[cell+0x2000]&0x87)==0x80)pixels[size_t(y)*width+unsigned(center)+x]=native[size_t(y)*160+x];
   }
  }
  if(out.expanded&&world.mode==4) {
   auto color=native[16*160];bool uniform=std::all_of(native.begin()+16*160,native.end(),[&](uint32_t p){return p==color;});
   if(uniform)std::fill(pixels.begin()+size_t(16)*width,pixels.end(),color);
  }
  if(out.expanded&&!copy_native_center) {
   std::fill(opaque.begin(),opaque.end(),0);std::fill(priority.begin(),priority.end(),0);
   if(world.owner.valid)out.sprites+=objects(c,world.owner,0,16,2,true);
  }
  out.pixels=pixels;out.camera_x=world.native_x;out.camera_y=world.native_y;out.left=left;
  out.bound_left=scene_left;out.bound_right=scene_right;out.captures=s.captures;
  out.neighbor_rooms=unsigned(std::count_if(world.rooms.begin(),world.rooms.end(),[](const State& r){return r.valid==1;}));
  out.neighbor_ticks=world.ticks;out.neighbor_failures=world.failures;return out;
 }
};
OracleNativeMods::OracleNativeMods(std::string_view rom,const NativeFeature& f,bool cgb):impl_(std::make_unique<Impl>(rom,f,cgb)){}
OracleNativeMods::~OracleNativeMods()=default;
uint8_t OracleNativeMods::read(ModContext& c,uint16_t address,uint8_t value){return impl_->read(c,address,value);}
uint8_t OracleNativeMods::cpu_read(ModContext& c,uint16_t address,uint8_t value,std::span<uint16_t,6> cpu){return impl_->cpu_read(c,address,value,cpu);}
void OracleNativeMods::scanline(ModContext& c,uint8_t line){if(!line){impl_->capture(c);impl_->owner(c);}}
void OracleNativeMods::viewport(unsigned w,unsigned h) {
 int64_t width=impl_->requested;if(width<0)width=h?(uint64_t(w)*144+h/2)/h:256;
 unsigned resolved=unsigned(std::clamp<int64_t>(width,160,max_width));
 if(resolved!=impl_->width)impl_->scratch.reset();impl_->width=resolved;
}
void OracleNativeMods::frame(Machine& machine){impl_->frame(machine);}
void OracleNativeMods::visibility(bool hide){impl_->hide_closed_rooms=hide;}
void OracleNativeMods::preserve_host_presentation(const OracleNativeMods& from)noexcept{impl_->hide_closed_rooms=from.impl_->hide_closed_rooms;}
ModView OracleNativeMods::render(ModContext& c,std::span<const uint32_t> native){return impl_->render(c,native);}
std::vector<uint8_t> OracleNativeMods::snapshot()const {
 static_assert(std::is_trivially_copyable_v<World>);
 std::array<uint32_t,4> header{17,impl_->variant,uint32_t(impl_->requested),impl_->width};
 std::vector<uint8_t> out(sizeof header+sizeof(World));std::memcpy(out.data(),header.data(),sizeof header);
 std::memcpy(out.data()+sizeof header,&impl_->world,sizeof(World));return out;
}
void OracleNativeMods::restore(std::span<const uint8_t> bytes) {
 std::array<uint32_t,4> h{};auto staged=std::make_unique<World>();
 if(bytes.size()!=sizeof h+sizeof(World))throw std::runtime_error("Invalid Oracle view state size");
 std::memcpy(h.data(),bytes.data(),sizeof h);std::memcpy(staged.get(),bytes.data()+sizeof h,sizeof(World));
 if(h[0]!=17||h[1]!=impl_->variant||int32_t(h[2])!=impl_->requested||h[3]<160||h[3]>max_width||staged->active.valid>1||
    staged->handoff_stage!=0||staged->handoff_room>255||staged->handoff_direction>3||
    staged->mode>4||staged->native_x< -240||staged->native_x>240||staged->focus_x< -160||staged->focus_x>255||
    staged->native_y< -176||staged->native_y>176||staged->focus_y< -128||staged->focus_y>255||
    (staged->mode==1&&(staged->focus_x<0||staged->focus_y<0||
     staged->native_x<0||staged->native_x>staged->active.room_width-160||
     staged->native_y<0||staged->native_y>staged->active.room_height-128))||
    (staged->mode&&!staged->active.valid))
   throw std::runtime_error("Invalid Oracle view state/profile");
 auto valid=[](const State& r){return r.valid<=2&&r.authority<=3&&r.enemy_origin_mask<=65535&&r.interaction_origin_mask<=65535&&r.camera_x>=0&&r.camera_x<=80&&r.camera_y>=0&&r.camera_y<=48&&
  (r.valid!=1||(((r.room_width==240&&r.room_height==176)||(r.room_width==160&&r.room_height==128))&&
    r.camera_x<=r.room_width-160&&r.camera_y<=r.room_height-128));};
 if(!valid(staged->active))throw std::runtime_error("Invalid Oracle primary room state");
 if(staged->owner.valid>1||!valid(staged->owner))throw std::runtime_error("Invalid Oracle owner actor state");
 for(const auto& room:staged->rooms)if(!valid(room))throw std::runtime_error("Invalid Oracle neighbor state");
 for(unsigned page=0;page<16;++page) {
  if(staged->items[page].arriving>1||(!staged->items[page].room&&staged->items[page].arriving))throw std::runtime_error("Invalid Oracle projectile arrival");
  if(auto id=staged->items[page].room) {
  if(page<7||id>256||std::none_of(staged->rooms.begin(),staged->rooms.end(),[&](const auto& room){return room.valid==1&&room.ram[impl_->b.room-0xc000]+1==id&&room.ram[0x1000+page*256]&&room.ram[0x1001+page*256]==3;}))
   throw std::runtime_error("Invalid Oracle projectile ownership");
  }
 }
 impl_->world=*staged;impl_->width=h[3];impl_->scratch.reset();
}
}
