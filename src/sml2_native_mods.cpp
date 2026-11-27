// World-map/actor compositor adapted from the SML2 donor feeb8943e07b1adf.
// All executing read sites below are byte-qualified against the stock UE v1.2.
#include "sml2_native_mods.hpp"
#include "native_mods.hpp"
#include "sml2_color.hpp"
#include "indexed_tile_surface.hpp"
#include <algorithm>
#include <array>
#include <climits>
#include <cmath>
#include <cstring>
#include <stdexcept>
#include <type_traits>
namespace gbcllr {
struct Sml2NativeMods::Impl {
 static constexpr int max_width=4096,max_sprites=1024;
 static constexpr unsigned animation_address[8]={0x8e0,0x8f0,0x9e0,0x9f0,0xce0,0xcf0,0xde0,0xdf0};
 struct Sprite {int x,y;uint8_t tile,attr,palette,screen_space=0;};
 struct Raster {uint8_t scx,scy,lcdc,bgp;};
 struct ActorPose {
  int x=0,y=0;
  uint16_t origin=0;
  uint8_t valid=0,alive=0,kind=0,frame=0,alternate=0,flip=0,stage=0;
  uint8_t tiles[32]{};
 };
 struct State {
  int valid=0,cam_x=0,cam_y=0,left=0,top=0,bound_left=0,bound_right=0,map_x=0,map_y=0;
  int frame=0,count=0,build_count=0,owner_count=0,owner_build_count=0,previous_camera=0,previous_valid=0;
  int scan_edge[2]{},scan_frame[2]{-1,-1},scan_valid[2]{};
  unsigned captures=0,rescues=0,scans=0,scan_reads=0;
  unsigned fireball_slot=0;
  uint8_t animation_source[128]{},animation_color[128]{},animation_ready=0;
  uint8_t vram[0x2000]{},map[0x3000]{},blockdef[0x200]{},box[48]{},oam[160]{};
  uint32_t bg[32]{},obj[32]{};
  uint8_t lcdc=0,wx=0,wy=0,scx=0,scy=0,stage=0,power=0,color_scene=0,fixed_scene=0;
  uint8_t cloud_scroll[3]{};
  uint8_t file_publication=0,title_to_file=0; //1:title palettes;2:after the actual startup clear.
  Sprite sprite[max_sprites]{},build[max_sprites]{},owner[max_sprites]{},owner_build[max_sprites]{};
  Raster raster[144]{};
  ActorPose actors[16]{};
  // Publication history:0 unpublished,1 unknown BG,2 object,RGBA known.
  uint32_t fifo_color[8]{},emitted_color[160*144]{};
 } s{};
 struct SurfaceContext {Impl* owner;bool hud,unsigned_tiles;};
 IndexedTileSurface fixed_surface[4];
 SurfaceContext surface_context[4]{{this,false,false},{this,false,true},{this,true,false},{this,true,true}};
 bool fixed_surface_ready=false;
 Impl(){std::memset(&s,0,sizeof s);s.scan_frame[0]=s.scan_frame[1]=-1;}
 int requested=-1,width=256,render_left=0;
 bool colorization=false;
 std::vector<uint32_t> pixels;
 std::vector<uint8_t> opaque;
 static uint8_t peek(const ModContext& c,unsigned a) {
  if(a>=0xa000&&a<0xc000){auto off=size_t(c.ram_bank)*0x2000+a-0xa000;return off<c.eram_size?c.eram[off]:0;}
  if(a>=0xc000&&a<0xd000)return c.wram[a-0xc000];
  if(a>=0xd000&&a<0xe000)return c.wram[size_t(c.wram_bank)*0x1000+a-0xd000];
  if(a>=0xff80&&a<0xffff)return c.hram[a-0xff80];
  return 0;
 }
 static int word(const ModContext& c,unsigned a){return peek(c,a)|(peek(c,a+1)<<8);}
 static uint8_t rom(const ModContext& c,unsigned a,unsigned bank=3) {
  auto off=size_t(bank)*0x4000+a-0x4000;return a>=0x4000&&a<0x8000&&off<c.rom_size?c.rom[off]:0;
 }
 uint8_t tile(int x,int y)const {
  if(x<0||x>=4096||y<0||y>=768)return 0xff;
  auto block=s.map[(y>>4)*256+(x>>4)];
  if(block>127)return 0xff;
  return s.blockdef[block*4+((y&8)?2:0)+((x&8)?1:0)];
 }
 unsigned graphics_power()const{return s.power?1:0;}
 bool colored_emission(const ModContext& c)const {
  // Sprite producers belong to the current loader, even while the framebuffer
  // and its qualified title source are still waiting for a new publication.
  return s.color_scene&&!(s.fixed_scene&&sml2_color::fixed_modes[s.fixed_scene-1]==1&&peek(c,0xff9b)!=1);
 }
 static int paid_course(unsigned scene){
  if(!sml2_color::paid_bonus_available||scene>=sml2_color::fixed_count||sml2_color::fixed_modes[scene]!=12)return -1;
  int sub=sml2_color::fixed_submodes[scene];return sub>=9&&sub<=12?12-sub:-1;
 }
 static bool paid_panel_cell(unsigned cell){return cell>=0x8a&&cell<=0x18f&&(cell&31)>=10&&(cell&31)<16;}
 bool paid_background(unsigned scene)const {
  int course=paid_course(scene);if(course<0)return false;
  //6C4F..6C73 can finish the same write with one2x2 selection highlight.
  // Recover its published native cells, never the next live A7AA request.
  int highlight=-1;
  for(int row=0;row<8;row++){
   auto p=s.vram+0x188c+row*32;
   if(p[0]==0xd4&&p[1]==0xd5&&p[32]==0xd8&&p[33]==0xd9){if(highlight>=0)return false;highlight=row;}
  }
  for(unsigned frame=0;frame<30;frame++){
   auto record=sml2_color::paid_bonus_tiles+course*228+frame*6;std::array<uint8_t,54> expected;std::copy_n(record,54,expected.begin());
   if(highlight>=0){expected[highlight*6+2]=0xd4;expected[highlight*6+3]=0xd5;expected[(highlight+1)*6+2]=0xd8;expected[(highlight+1)*6+3]=0xd9;}
   bool match=true;
   for(unsigned row=0;row<9;row++)if(std::memcmp(s.vram+0x188a+row*32,expected.data()+row*6,6)){match=false;break;}
   if(match)return true;
  }
  return false;
 }
 static uint8_t fixed_paint(unsigned scene,unsigned cell,uint8_t source){
  // A qualified native producer overwrites this entire rectangle, including
  // blanks. Never resurrect the checkpoint's preceding prize beneath it.
  if(paid_course(scene)>=0&&paid_panel_cell(cell))return source;
  unsigned at=scene*2048+cell;return source==sml2_color::fixed_old_map[at]?sml2_color::fixed_color_map[at]:source;
 }
 uint8_t color_tile(int x,int y,uint8_t source)const {
  if(!s.color_scene||!s.valid||x<0||x>=4096||y<0||y>=768)return source;
  unsigned cell=(y>>4)*256+(x>>4);auto block=s.map[cell];if(block>127)return source;
  int t=sml2_color::block_tile(s.stage,graphics_power(),cell,block,((y&8)?2:0)+((x&8)?1:0));
  return t<0?source:uint8_t(t);
 }
 int view_left()const {
  int span=s.bound_right-s.bound_left;
  int v=span<=width?s.bound_left-(width-span)/2:std::clamp(s.left-(width-160)/2,s.bound_left,s.bound_right-width);
  return std::clamp(v,s.left+160-width,s.left);
 }
 void bounds() {
  int col=(s.cam_x>>8)&15,row=(s.cam_y>>8)&3,lo=col,hi=col;
  if(row<3){while(lo>0&&!(s.box[row*16+lo]&2)&&!(s.box[row*16+lo-1]&1))--lo;
   while(hi<15&&!(s.box[row*16+hi]&1)&&!(s.box[row*16+hi+1]&2))++hi;}
  s.bound_left=lo*256;s.bound_right=(hi+1)*256;
 }
 void own(Sprite sp){if(s.owner_build_count<max_sprites)s.owner_build[s.owner_build_count++]=sp;}
 void presentation(unsigned table,unsigned frame,int ox,int oy,int flip,bool screen_space=false,unsigned palette_offset=0,unsigned priority=0) {
  unsigned q=sml2_color::presentation(table,frame),start=sml2_color::presentation_offset[q],n=sml2_color::presentation_length[q];
  for(unsigned i=0;i<n&&s.build_count<max_sprites;i++){
   auto p=sml2_color::pieces[start+i];int dx=(flip&0x20)?int8_t(uint8_t(~p.x-7)):p.x,dy=(flip&0x40)?int8_t(uint8_t(~p.y-7)):p.y;
   auto attr=uint8_t(((p.attr+palette_offset)^flip)|priority);s.build[s.build_count++]={ox+dx,oy+dy,p.tile,attr,uint8_t(attr&7),uint8_t(screen_space)};++s.captures;
  }
 }
 void capture(const ModContext& c) {
  if(peek(c,0xffe2)==0x80)return;
  int ax=(peek(c,0xffd0)<<8)|peek(c,0xffd1),ay=(peek(c,0xffd3)<<8)|peek(c,0xffd4);
  int flip=peek(c,0xffdc)^peek(c,0xffdd)^peek(c,0xffde);unsigned frame=peek(c,0xffdb),alternate=peek(c,0xaf06)!=0;
  unsigned entry=(alternate?0x4f11:0x40b1)+frame*2,ptr=rom(c,entry)|(rom(c,entry+1)<<8);
  std::array<Sprite,64> original;unsigned original_count=0;bool enemy_atlas=true;
  for(int n=0;n<64&&ptr>=0x4000&&ptr+3<0x8000;n++,ptr+=4){
   auto y=rom(c,ptr),x=rom(c,ptr+1);if(y==0x80)break;
   int dy=(flip&0x40)?int8_t(uint8_t(~y-7)):int8_t(y),dx=(flip&0x20)?int8_t(uint8_t(~x-7)):int8_t(x);
   Sprite sp{ax+dx,ay+dy,rom(c,ptr+2),uint8_t(rom(c,ptr+3)^flip),0};
   original[original_count++]=sp;enemy_atlas&=sp.tile>=0x80;
  }
  // AD00..AEFF are the original sixteen live enemy records. Their native
  // kind and level-object origin identify a slot across culling and reuse.
  // A complete metasprite is captured in one call, independently of the
  // hardware OAM upload that may still contain an older or clipped body.
  if(s.color_scene&&!s.fixed_scene&&original_count&&enemy_atlas)for(unsigned slot=0;slot<16;++slot) {
   unsigned at=0xad00+slot*32,kind=peek(c,at+5);
   if(!kind||kind!=peek(c,0xffd5)||((peek(c,at)<<8)|peek(c,at+1))!=ax||((peek(c,at+3)<<8)|peek(c,at+4))!=ay)continue;
   auto& pose=s.actors[slot];unsigned origin=(peek(c,at+6)<<8)|peek(c,at+7);
   if(!pose.valid||pose.kind!=kind||pose.origin!=origin||pose.stage!=peek(c,0xa269))pose={};
   pose.valid=pose.alive=1;pose.kind=uint8_t(kind);pose.origin=uint16_t(origin);pose.stage=peek(c,0xa269);
   pose.x=ax;pose.y=ay;pose.frame=uint8_t(frame);pose.alternate=uint8_t(alternate);pose.flip=uint8_t(flip);
   for(unsigned n=0;n<original_count;++n)pose.tiles[original[n].tile/8]|=uint8_t(1u<<(original[n].tile&7));
   ++s.captures;return;
  }
  for(unsigned n=0;n<original_count;++n) {
   if(s.color_scene)own(original[n]);else if(s.build_count<max_sprites){s.build[s.build_count++]=original[n];++s.captures;}
  }
  if(s.color_scene){
   // Original16:768B alternates the Star's mono palette bit. DX uses
   // the same native phase to alternate its graphics bank instead.
   if(peek(c,0xffd5)==20)flip=(flip&~0x10)^((flip&0x10)?8:0);
   unsigned table=(s.stage==15||s.stage==11)?2:s.stage==13?4:0;presentation(table+alternate,frame,ax,ay,flip);
  }
 }
 bool fireball(const ModContext& c,int idx,int sx,int sy,int& x,int& y)const {
  if(idx<0xb0||idx>0xb7)return false;
  for(unsigned base:{0xa880u,0xa890u})if(peek(c,base)){
   int wx=word(c,base+3)-word(c,0xffca)+80,wy=word(c,base+1)-word(c,0xffc8)+70;
   if((wx&255)==sx&&(wy&255)==sy){x=wx;y=wy;return true;}
  }
  return false;
 }
 void capture_emitter(const ModContext& c) {
  int sx=peek(c,0xffc5),sy=peek(c,0xffc4),idx=peek(c,0xffc6),pal=peek(c,0xffc7)!=0;
  // Original1:52D6..52DE sets OBJ-behind-BG when FFBB is zero. Pipe
  // entry/exit changes this after selecting the same ordinary player pose.
  unsigned priority=peek(c,0xffbb)?0:0x80;
  // The screen coordinates target the next OAM upload. Defer their world
  // projection until line0 has latched the matching uploaded scroll.
  int ox=(sx>=0xd0?sx-256:sx)-8,oy=(sy>=0xd0?sy-256:sy)-16;
  // Extended fireballs can be a whole 256-pixel page from Mario. Their
  // live slots retain full coordinates; keep those before the line0 projection.
  int fx,fy;if(fireball(c,idx,sx,sy,fx,fy)){ox=fx-8;oy=fy-16;}
  unsigned entry=0x4000+idx*2,ptr=rom(c,entry,1)|(rom(c,entry+1,1)<<8);
  for(int n=0;n<64&&ptr>=0x4000&&ptr+3<0x8000;n++,ptr+=4){
   auto y=rom(c,ptr,1);if(y==0x80)break;
   Sprite sp{ox+int8_t(rom(c,ptr+1,1)),oy+int8_t(y),rom(c,ptr+2,1),uint8_t(rom(c,ptr+3,1)|(pal?0x10:0)|priority),0,1};
   if(s.color_scene)own(sp);else if(s.build_count<max_sprites){s.build[s.build_count++]=sp;++s.captures;}
  }
  // DX2C:5DC2 adds this phase to each emitted attribute when the
  // native player producer selects its Star palette (FFC7).
  if(s.color_scene)presentation(6,idx,ox,oy,0,true,pal?((peek(c,0xa272)>>1)&3):0,priority);
 }
#if GBCLLR_OBSERVED_DISPLAY
 void capture_bonus(const ModContext& c,unsigned entry) {
  int sub=peek(c,0xa68b);if(!colorization||peek(c,0xff9b)!=12||sub<9||sub>14||
     entry<0x51b1||(entry-0x51b1)%2)return;
  unsigned frame=(entry-0x51b1)/2;if(frame>=sml2_color::bonus_frame_count)return;
  // Original18:508B selects51B1;50A2 reads the chosen pointer before
  // the native producer emits it. B/C are its live OAM anchors. The
  // following5213 table is BG commands and must never be read as sprites.
  if(rom(c,0x508b,24)!=0x21||rom(c,0x508c,24)!=0xb1||rom(c,0x508d,24)!=0x51||
     rom(c,0x50a2,24)!=0x2a||rom(c,0x50a3,24)!=0x66||rom(c,0x50a4,24)!=0x6f)return;
  unsigned ptr=rom(c,entry,24)|(rom(c,entry+1,24)<<8),n=0,start=ptr;
  for(;n<40&&ptr>=0x4000&&ptr<=0x7ffc;ptr+=4,n++){
   unsigned tile=rom(c,ptr+2,24);if(tile==255)break;
   auto pattern=sml2_color::fixed_original(32+sub-9,tile*16);
   if(!pattern||std::memcmp(pattern,c.vram+tile*16,16))return;
  }
  if(n==40||ptr<0x4000||ptr>0x7ffc||peek(c,0xa782)+n>40)return;
  unsigned q=sml2_color::bonus_presentation_index[frame],count=sml2_color::presentation_length[q];
  if(s.owner_build_count+int(n)>max_sprites||s.build_count+int(count)>max_sprites)return;
  for(unsigned i=0;i<n;i++,start+=4)own({int(uint8_t(c.b+rom(c,start+1,24)))-8,
   int(uint8_t(c.c+rom(c,start,24)))-16,rom(c,start+2,24),rom(c,start+3,24),0,1});
  unsigned first=sml2_color::presentation_offset[q];
  for(unsigned i=0;i<count;i++){
   auto p=sml2_color::pieces[first+i];s.build[s.build_count++]={int(uint8_t(c.b+p.x))-8,
    int(uint8_t(c.c+p.y))-16,p.tile,p.attr,uint8_t(p.attr&7),1};++s.captures;
  }
 }
#endif
 static uint32_t rgb555(const uint8_t* p){unsigned v=p[0]|(p[1]<<8);auto channel=[](unsigned x){x&=31;return (x<<3)|(x>>2);};return 0xff000000u|(channel(v)<<16)|(channel(v>>5)<<8)|channel(v>>10);}
 static int ending_source_phase(int phase){
  // The natural handler snapshots have identical original/DX maps,
  // textures and palettes for these shared presentation states.
  if(phase<=4)return 0;if(phase==5)return 6;if(phase==11)return 10;
  if(phase==13||phase==14)return 12;if(phase==16)return 17;return phase;
 }
 void capture_map_marker(const ModContext& c) {
  // DX36:7A3D adds this fifth piece before the original position update.
  // Original Mario uses character0; DX's optional Luigi selector is absent.
  if(peek(c,0xa106)==0x7f)return;
  bool known=false;for(unsigned q=0x4b07;q<0x4be7;q+=8){bool match=true;for(unsigned i=0;i<4;i++)if(rom(c,q+i*2,15)!=peek(c,0xa102+i*4)||rom(c,q+i*2+1,15)!=peek(c,0xa103+i*4))match=false;if(match){known=true;break;}}
  if(!known||s.build_count==max_sprites)return;
  int dx=(peek(c,0xa107)&0xf0)==0x20?(peek(c,0xa102)==0x0a?4:5):2;
  s.build[s.build_count++]={int(peek(c,0xa101))-8+dx,int(peek(c,0xa100))-16,0xf4,0x09,1,1};++s.captures;
 }
 void snapshot(const ModContext& c,int source_mode=-1) {
  if(s.frame==INT_MAX){s.frame=0;s.scan_frame[0]=s.scan_frame[1]=-1;}else ++s.frame;
  s.lcdc=c.ppu->lcdc;s.wx=c.ppu->wx;s.wy=c.ppu->wy;s.scx=c.ppu->scx;s.scy=c.ppu->scy;s.stage=peek(c,0xa269);s.power=peek(c,0xa216);
  s.cam_x=word(c,0xffca);s.cam_y=word(c,0xffc8);
  s.left=s.cam_x-80;s.top=s.cam_y-72;
  s.left+=int8_t(uint8_t(c.ppu->scx-s.left));s.top+=int8_t(uint8_t(c.ppu->scy-s.top));
  std::memcpy(s.vram,c.vram,sizeof s.vram);std::memcpy(s.oam,c.oam,sizeof s.oam);
  for(int i=0;i<4;i++){s.bg[i]=c.ppu->bg_rgb[(c.ppu->bgp>>(i*2))&3];s.obj[i]=c.ppu->obj_rgb[(c.ppu->obp0>>(i*2))&3];s.obj[4+i]=c.ppu->obj_rgb[4+((c.ppu->obp1>>(i*2))&3)];}
  int mode=source_mode<0?peek(c,0xff9b):source_mode;bool field=mode==4||mode==8||mode==9;
  s.color_scene=colorization&&s.stage<sml2_color::stage_count&&s.power<=3&&(s.power!=3||sml2_color::fire_available)&&field&&!(peek(c,0xa28b)&0xf0)&&(s.lcdc&0x80);
  s.fixed_scene=0;
  // Native mode25 starts title cleanup before the title framebuffer changes.
  // Its still-rendered title must pass the same PPU/map/texture qualification.
  if(colorization)for(unsigned scene=0;scene<sml2_color::fixed_count;scene++)if((mode==sml2_color::fixed_modes[scene]||(mode==25&&sml2_color::fixed_modes[scene]==1))&&((s.lcdc^sml2_color::fixed_old_ppu[scene*16])&(sml2_color::fixed_phases[scene]>=0?0xef:0xff))==0&&
     (c.ppu->bgp==sml2_color::fixed_old_ppu[scene*16+7]||(sml2_color::fixed_submodes[scene]==7&&peek(c,0xa797)<=2&&c.ppu->bgp==0x1e))){
   if(sml2_color::fixed_phases[scene]>=0&&ending_source_phase(peek(c,0xa2dd))!=sml2_color::fixed_phases[scene])continue;
   if(sml2_color::fixed_bosses[scene]&&(s.stage!=sml2_color::fixed_stages[scene]||s.power>3||peek(c,0xafc0)+1!=sml2_color::fixed_bosses[scene]))continue;
   if(sml2_color::fixed_submodes[scene]>=0&&peek(c,0xa68b)!=sml2_color::fixed_submodes[scene])continue;
   bool paid=paid_course(scene)>=0;if(paid&&!paid_background(scene))continue;
   if(scene==2){
    if(peek(c,0xa68b)||c.ram_bank||c.eram_size<0x2000)continue;
    // Main overworld: the original loader streams its live64x64 tile map
    // from B000; A680/A682 are its full scroll, not the gameplay camera.
    s.map_x=word(c,0xa680);s.map_y=word(c,0xa682);
    s.map_x+=int8_t(uint8_t(s.scx-s.map_x));s.map_y+=int8_t(uint8_t(s.scy-s.map_y));
    std::memcpy(s.map,c.eram+0x1000,0x1000);
   }
   // Credits replace the lower map rows with live letters. Recognize the
   // unchanged castle above the original LY111 tile-data split instead.
   int rows=sml2_color::fixed_phases[scene]>=18?12:18;
   int hit=0;for(int y=0;y<rows;y++)for(int x=0;x<20;x++){
    unsigned cell=((s.lcdc&8)?1024:0)+((s.scy+y*8)&255)/8*32+((s.scx+x*8)&255)/8;
    auto expected=scene==2?s.map[(((s.map_y+y*8)&511)/8)*64+((s.map_x+x*8)&511)/8]:paid&&paid_panel_cell(cell)?s.vram[0x1800+cell]:sml2_color::fixed_old_map[scene*2048+cell];
    if(s.vram[0x1800+cell]==expected)++hit;
   }
   if(hit*100>=rows*20*95){s.fixed_scene=uint8_t(scene+1);s.color_scene=1;break;}
  }
  if(s.fixed_scene==1)s.title_to_file=1;
  else if(mode!=1&&mode!=25&&mode!=26)s.title_to_file=0;
  if(s.color_scene)for(unsigned i=0;i<32;i++){
   auto bg=s.fixed_scene?sml2_color::fixed_bg_palettes+(s.fixed_scene-1)*64:sml2_color::bg_palettes+s.stage*64;
   // Original0F:602E and DX29:6062/6083/60A6 share the actual A797
   // transition. DX uses26:6A00 while1/2, and26:69C0 after it clears;
   // BGP alone is insufficient because its inversion alternates separately.
   if(s.fixed_scene&&sml2_color::fixed_submodes[s.fixed_scene-1]==7&&sml2_color::castle_flash_available&&peek(c,0xa797)>0&&peek(c,0xa797)<=2)bg=sml2_color::castle_flash_palette;
   auto obj=s.fixed_scene?sml2_color::fixed_obj_palettes+(s.fixed_scene-1)*64:sml2_color::obj_palettes+s.stage*64;
   s.bg[i]=rgb555(bg+i*2);s.obj[i]=rgb555(obj+i*2);
  }
  if(s.fixed_scene&&sml2_color::fixed_submodes[s.fixed_scene-1]==7)for(unsigned i=0;i<3;i++)s.cloud_scroll[i]=peek(c,0xa78a+i);
  auto project=[&](Sprite& sp){if(sp.screen_space){sp.x+=s.left;sp.y+=s.top;sp.screen_space=0;}};
  for(int i=0;i<s.build_count;i++)project(s.build[i]);
  for(int i=0;i<s.owner_build_count;i++)project(s.owner_build[i]);
  if(mode!=8||s.build_count||s.owner_build_count) {
   s.count=s.build_count;std::copy_n(s.build,s.count,s.sprite);s.build_count=0;
   s.owner_count=s.owner_build_count;std::copy_n(s.owner_build,s.owner_count,s.owner);s.owner_build_count=0;
  }
  for(unsigned slot=0;slot<16;++slot) {
   auto& pose=s.actors[slot];unsigned at=0xad00+slot*32;
   pose.alive=uint8_t(pose.valid&&pose.stage==s.stage&&peek(c,at+5)==pose.kind&&((peek(c,at+6)<<8)|peek(c,at+7))==pose.origin);
   if(pose.alive){pose.x=(peek(c,at)<<8)|peek(c,at+1);pose.y=(peek(c,at+3)<<8)|peek(c,at+4);}
  }
  s.valid=field&&c.ram_bank==0&&c.eram_size>=0x2000&&!(peek(c,0xa28b)&0xf0)&&
   s.lcdc==0xe3&&s.wy==136&&s.wx==7&&s.cam_x>=80&&s.cam_x<4096&&s.cam_y>=72&&s.cam_y<768;
  if(s.valid){
   std::memcpy(s.map,c.eram+0x1000,0x1000);std::memcpy(s.map+0x1000,c.wram,0x2000);
   for(unsigned i=0;i<sizeof s.blockdef;i++)s.blockdef[i]=peek(c,0xa600+i);
   for(unsigned i=0;i<sizeof s.box;i++)s.box[i]=peek(c,0xa960+i);
   int total=0,hit=0;
   // A tile-aligned160px strip uses20 columns. Its21st tile is visible
   // only with fine scroll; probing it at world4096 falsely drops Fit.
   int columns=20+((s.scx&7)!=0);
   for(int y=0;y<17;y++)for(int x=0;x<columns;x++){
    int wx=s.left+x*8,wy=s.top+y*8;if(wx<0||wy<0||wx>=4096||wy>=768){s.valid=0;continue;}
    auto id=s.map[(wy>>4)*256+(wx>>4)];if(id>127){s.valid=0;continue;}
    auto t=tile(wx,wy),hw=s.vram[0x1800+((c.ppu->scy+y*8)&255)/8*32+((c.ppu->scx+x*8)&255)/8];
    ++total;if(t==hw)++hit;
    else { // Direct writers may use a different tile with identical pixels.
     int a=0x1000+int8_t(t)*16,b=0x1000+int8_t(hw)*16;
     if(std::memcmp(s.vram+a,s.vram+b,16)==0)++hit;
    }
   }
   if(!total||hit*100<total*95)s.valid=0;
   bounds();
  }
  if(!s.valid||!s.previous_valid||std::abs(s.cam_x-s.previous_camera)>8){s.scan_valid[0]=s.scan_valid[1]=0;s.scan_frame[0]=s.scan_frame[1]=-1;}
  s.previous_valid=s.valid;s.previous_camera=s.cam_x;
  if(!s.valid&&!s.color_scene){s.count=0;s.owner_count=0;}
 }
 uint8_t read(ModContext& c,uint16_t a,uint8_t value) {
  if(colorization&&c.rom_bank==15&&a==0xa844&&c.instruction_pc==0x49b8&&c.pc==0x49bb&&rom(c,0x49b8,15)==0xfa&&rom(c,0x49b9,15)==0x44&&rom(c,0x49ba,15)==0xa8&&peek(c,0xff9b)==12&&peek(c,0xa68b)<=8)capture_map_marker(c);
  if(colorization&&c.rom_bank==15){
   // Observe the original main-map initializer and its actual paired-row
   // rotations. A689 alone does not identify the buffers after re-entry.
   if(a==0x88e0&&c.instruction_pc==0x67eb&&c.pc==0x67ec&&rom(c,0x67eb,15)==0x2a&&c.rom_size>=0x35800){
    bool original=true,continuing=s.animation_ready;for(unsigned i=0;i<8;i++){if(std::memcmp(c.vram+animation_address[i],c.rom+0x34000+animation_address[i],16)!=0)original=false;if(std::memcmp(c.vram+animation_address[i],s.animation_source+i*16,16)!=0)continuing=false;}
    s.animation_ready=original||continuing;
    if(original){for(unsigned i=0;i<8;i++)std::copy_n(c.rom+0x34000+animation_address[i],16,s.animation_source+i*16);std::copy_n(sml2_color::world_animation_color,128,s.animation_color);}
   }
   if(s.animation_ready&&a>=0xa710&&a<0xa780&&(a&0x10)&&c.pc==c.instruction_pc+1){
    constexpr unsigned right[]={0x6876,0x6896,0x68b6,0x68d6,0x68f8,0x6918},left[]={0x6939,0x6959,0x6979,0x6999,0x69bb,0x69db};
    bool r=std::find(std::begin(right),std::end(right),c.instruction_pc)!=std::end(right),l=std::find(std::begin(left),std::end(left),c.instruction_pc)!=std::end(left);
    unsigned pair=(a-0xa700)/32,row=a&15,at=pair*32+row;
    if((r||l)&&c.hl==0x8000+animation_address[pair*2+1]+row&&rom(c,c.instruction_pc,15)==0x0a&&rom(c,c.instruction_pc+1,15)==0xcb&&rom(c,c.instruction_pc+2,15)==(r?0x3f:0x27)){
     for(auto pixels:{s.animation_source,s.animation_color}){unsigned x=pixels[at],y=pixels[at+16];pixels[at]=uint8_t(r?(x>>1)|((y&1)<<7):(x<<1)|(y>>7));pixels[at+16]=uint8_t(r?(y>>1)|((x&1)<<7):(y<<1)|(x>>7));}
    }
   }
  }
  // The native OAM cull stores hidden screen coordinates in the actor.
  // Fire collision must instead read that actor's live world position.
  // Keep the original compare/vulnerability/defeat path, and exclude only
  // modulo-256 false hits that the extended projectile range makes possible.
  unsigned off=(a&31),site=off==18?0x300e:off==9?0x302e:off==10?0x3033:0;
  if(s.valid&&width>160&&a>=0xad00&&a<0xaf00&&site&&c.instruction_pc==site&&c.pc==site+1&&
     c.rom_size>site&&c.rom[site]==(off==9?0x2a:0x7e)&&s.fireball_slot&&peek(c,s.fireball_slot)&&
     peek(c,s.fireball_slot+3)==peek(c,0xa25f)&&peek(c,s.fireball_slot+1)==peek(c,0xa25d)&&peek(c,s.fireball_slot+5)==peek(c,0xa212)){
   unsigned base=a-off;int camx=word(c,0xffca),camy=word(c,0xffc8);
   int ax=(peek(c,base)<<8)|peek(c,base+1),ay=(peek(c,base+3)<<8)|peek(c,base+4);
   ax=ax-camx+88+int8_t(uint8_t(camx-80-peek(c,0xa2b1)));
   ay=ay-camy+88+int8_t(uint8_t(camy-72-peek(c,0xa2b0)));
   if(off==9)return uint8_t(ay);if(off==10)return uint8_t(ax);
   int px=word(c,s.fireball_slot+3)-camx+80,py=word(c,s.fireball_slot+1)-camy+70;
   int x0=ax+int8_t(peek(c,base+21))-6,x1=ax+int8_t(peek(c,base+22))+6;
   int y0=ay+int8_t(peek(c,base+19))-6,y1=ay+int8_t(peek(c,base+20))+6;
   bool full=px>=x0&&px<x1&&py>=y0&&py<y1;
   bool wrapped=uint8_t(px-x0)<uint8_t(x1-x0)&&uint8_t(py-y0)<uint8_t(y1-y0);
   if(wrapped&&!full)return 0;
  }
  // UE v1.2 ROM0:3281 `cp $c0`, horizontal boundary only. Reconstruct
  // its A from the exact scratch/camera operands. The next JR consumes Z;
  // A is reloaded immediately afterwards, so the compare carry is dead.
  if(a==0x3282&&c.instruction_pc==0x3281&&c.pc==0x3283&&value==0xc0&&
     c.rom_size>0x3282&&c.rom[0x3281]==0xfe&&c.rom[0x3282]==0xc0&&
     (c.hl==0xa885||c.hl==0xa895)&&peek(c,c.hl-5)){
   s.fireball_slot=c.hl-5;
   int x=word(c,c.hl-2),rel=x-word(c,0xffca)+80;
   auto compared=uint8_t((peek(c,0xa25f)-peek(c,0xffca)+80)&0xf0);
   if(!s.valid||width<=160)return rel< -64||rel>207?compared:value;
   int left=view_left();return x<=left-32||x>=left+width+32?compared:uint8_t(compared^0x10);
  }
#if GBCLLR_OBSERVED_DISPLAY
  if(c.rom_bank==24&&c.instruction_pc==0x50a2&&c.pc==0x50a3)capture_bonus(c,a);
#endif
  if(colored_emission(c)||(s.valid&&width>160)) {
   if(c.rom_bank==3&&a==0xffe2&&c.instruction_pc==0x4019&&c.pc==0x401b)capture(c);
   // Game Over's paired records retain native geometry/tile IDs. Its
   // emitter can already be building the next OAM upload while the plaque
   // slides; color the actual uploaded OAM instead of that future pose.
   if(c.rom_bank==1&&a==0xffc5&&(c.pc==0x52ba||c.pc==0x5e5d)&&c.instruction_pc==c.pc-2&&peek(c,0xff9b)!=7)capture_emitter(c);
  }
  // An aliased hardware copy would reappear inside the native strip. Only
  // these two emitter Y reads receive hidden OAM Y; the guest Y boundary
  // and the full-coordinate compositor capture remain untouched.
  if(s.valid&&width>160&&c.rom_bank==1&&a==0xffc4&&
     (c.instruction_pc==0x52b5||c.instruction_pc==0x5e58)&&c.pc==c.instruction_pc+2&&
     rom(c,c.instruction_pc,1)==0xf0&&rom(c,c.instruction_pc+1,1)==0xc4){
   int x,y;if(fireball(c,peek(c,0xffc6),peek(c,0xffc5),value,x,y)&&(x<0||x>255))return 0xf0;
  }
  if(!s.valid||width<=160)return value;
  // Every override names the executing instruction and its completed operands.
  if(c.rom_bank==2&&c.instruction_pc==0x3caa&&c.pc==0x3cab){
   int half=0;if(a>=0xaf0a&&a<=0xaf0d)half=96;if(a>=0xaf1a&&a<=0xaf1d)half=160;
   if(half){int left=view_left(),extra_left=s.left-left,extra_right=left+width-(s.left+160);
    int upper=std::min(65535,s.cam_x+half+extra_right),lower=std::max(0,s.cam_x-half-extra_left);++s.rescues;
    switch(a&15){case 10:return uint8_t(upper>>8);case 11:return uint8_t(upper);case 12:return uint8_t(lower>>8);case 13:return uint8_t(lower);}
   }
  }
  if(c.rom_bank==3&&a==0xa2b1&&c.instruction_pc==0x409b&&c.pc==0x409e){
   int ax=(peek(c,0xffd0)<<8)|peek(c,0xffd1),rel=ax-s.left;
   if(rel< -8||rel>=176)return uint8_t(peek(c,0xffd1)+8-0xb8);
  }
  if(c.rom_bank==2&&a>=0xaf12&&a<=0xaf15){
   int side=a>=0xaf14,high=(a&1)==0;
   unsigned pc=side?(high?0x40a9:0x40af):(high?0x408a:0x4090);
   if(c.instruction_pc!=pc||c.pc!=pc+3)return value;
   if(high&&s.scan_frame[side]!=s.frame){
    int original=std::clamp(s.cam_x+(side?-112:112),0,4095),left=view_left();
    int extra=side?s.left-left:left+width-(s.left+160),target=std::clamp(original+(side?-extra:extra),0,4095);
    if(!s.scan_valid[side]){s.scan_edge[side]=original;s.scan_valid[side]=1;}
    else s.scan_edge[side]+=std::clamp(target-s.scan_edge[side],-8,8);s.scan_frame[side]=s.frame;++s.scans;
   }
   if(s.scan_frame[side]!=s.frame)return value;++s.scan_reads;
   return uint8_t(high?s.scan_edge[side]>>8:s.scan_edge[side]);
  }
  return value;
 }
 void draw_sprite(Sprite sp) {
  int h=(s.lcdc&4)?16:8;if(h==16)sp.tile&=0xfe;
  for(int y=0;y<h;y++)for(int x=0;x<8;x++){
   int sx=sp.x-render_left+x,sy=sp.y-s.top+y;
   if(sx<0||sx>=width||sy<0||sy>=144)continue;
   if(s.valid&&(sp.x+x<s.bound_left||sp.x+x>=s.bound_right))continue;
   if(!s.valid&&(sp.x+x<s.left||sp.x+x>=s.left+160))continue;
   int py=(sp.attr&0x40)?h-1-y:y,px=(sp.attr&0x20)?x:7-x,a=sp.tile*16+py*2;
   const uint8_t* pattern=s.fixed_scene?sml2_color::fixed_texture(s.fixed_scene-1,(sp.attr&8)!=0,sp.tile*16):nullptr;
   // Boss rooms retain their own enemy atlas. Small Mario's bank1 tiles
   // use the same qualified first64 player tiles as the ordinary stage.
   if(boss_scene()&&s.power==0&&(sp.attr&8)&&sp.tile<0x40)pattern=sml2_color::texture(s.stage,0,1,sp.tile*16);
   if(!s.fixed_scene&&s.color_scene&&s.power==3)pattern=sml2_color::fire_texture((sp.attr&8)!=0,sp.tile*16);
   if(!s.fixed_scene&&s.color_scene&&!pattern)pattern=sml2_color::texture(s.stage,graphics_power(),(sp.attr&8)!=0,sp.tile*16);
   uint8_t lo=pattern?pattern[py*2]:s.vram[a],hi=pattern?pattern[py*2+1]:s.vram[a+1];
   int color=((lo>>px)&1)|(((hi>>px)&1)<<1),off=sy*width+sx;
   if(color&&!(opaque[off]&0x80)&&(!(sp.attr&0x80)||!opaque[off]))pixels[off]=s.obj[(s.color_scene?sp.palette*4:((sp.attr&0x10)?4:0))+color];
  }
 }
 uint8_t attribute(uint8_t t)const{return s.color_scene&&!s.fixed_scene?sml2_color::bg_attributes[s.stage*256+t]:0;}
 bool boss_scene()const{return s.fixed_scene&&sml2_color::fixed_bosses[s.fixed_scene-1]!=0;}
 bool finale_scene()const{return s.fixed_scene&&sml2_color::fixed_phases[s.fixed_scene-1]>=0;}
 bool draw_lead_player(const ModContext& c) {
  if(!finale_scene()||sml2_color::fixed_phases[s.fixed_scene-1]!=0||s.power<1||s.power>3||
     peek(c,0xa2dd)>1||(s.lcdc&4)||s.count||s.owner_count)return false;
  // The lead keeps the original player producer, not the Small-only actor
  // set used to capture this room. Match its complete actual OAM group to
  // the original ROM1 table, then use that same frame's already-owned DX
  // producer. This retains the native pose/anchor and its extra color pieces.
  const uint8_t* actual[40];unsigned count=0;
  for(unsigned i=0;i<160;i+=4)if(s.oam[i]){
   auto e=s.oam+i;if(e[0]>=160||e[1]<1||e[1]>167)return false;
   auto pattern=sml2_color::fixed_original(s.fixed_scene-1,e[2]*16);
   if(!pattern||std::memcmp(pattern,s.vram+e[2]*16,16))return false;
   actual[count++]=e;
  }
  if(!count)return false;
  unsigned chosen=0;int origin_x=0,origin_y=0;bool found=false;
  for(unsigned frame=0;frame<256;frame++){
   unsigned ptr=rom(c,0x4000+frame*2,1)|(rom(c,0x4001+frame*2,1)<<8);
   if(ptr<0x4000||ptr+3>=0x8000||rom(c,ptr,1)==0x80)continue;
   int x=int(actual[0][1])-int8_t(rom(c,ptr+1,1)),y=int(actual[0][0])-int8_t(rom(c,ptr,1));
   unsigned n=0;for(;n<count&&ptr>=0x4000&&ptr+3<0x8000;ptr+=4,n++){
    if(rom(c,ptr,1)==0x80||uint8_t(y+int8_t(rom(c,ptr,1)))!=actual[n][0]||
       uint8_t(x+int8_t(rom(c,ptr+1,1)))!=actual[n][1]||rom(c,ptr+2,1)!=actual[n][2]||rom(c,ptr+3,1)!=actual[n][3])break;
   }
   if(n!=count||ptr<0x4000||ptr>=0x8000||rom(c,ptr,1)!=0x80)continue;
   unsigned sequence=sml2_color::presentation(6,frame);
   if(!sml2_color::presentation_length[sequence])continue;
   if(found&&(sequence!=chosen||x!=origin_x||y!=origin_y))return false;
   chosen=sequence;origin_x=x;origin_y=y;found=true;
  }
  if(!found)return false;
  unsigned first=sml2_color::presentation_offset[chosen],size=sml2_color::presentation_length[chosen];
  for(unsigned i=size;i--;){auto p=sml2_color::pieces[first+i];
   draw_sprite({s.left+origin_x-8+p.x,s.top+origin_y-16+p.y,p.tile,p.attr,uint8_t(p.attr&7),0});
  }
  return true;
 }
 unsigned line_lcdc(int y)const{return finale_scene()?s.raster[y].lcdc:s.lcdc;}
 int background_scroll_y(int y)const{return finale_scene()?s.raster[y].scy:s.scy;}
 int background_scroll_x(int y)const {
  // Original15:67F2 and DX36:7BBD use the same three cloud bands;
  // the castle body scroll is zero, independent of line0's cloud SCX.
  if(s.fixed_scene&&sml2_color::fixed_submodes[s.fixed_scene-1]==7)return y<15?s.cloud_scroll[0]:y<23?s.cloud_scroll[1]:y<31?s.cloud_scroll[2]:0;
  return finale_scene()?s.raster[y].scx:s.scx;
 }
 static IndexedTileSurface::Tile fixed_surface_tile(const void* context,unsigned cell,uint8_t source,unsigned address){
  auto& view=*static_cast<const SurfaceContext*>(context);auto& self=*view.owner;auto& s=self.s;unsigned scene=s.fixed_scene-1,at=scene*2048+cell;
  auto attr=sml2_color::fixed_attributes[at],paint=fixed_paint(scene,cell,source);
  auto original=sml2_color::fixed_original(scene,address);const uint8_t* pattern=nullptr;
  if(view.hud||(original&&std::memcmp(original,s.vram+address,16)==0)){
   unsigned target=view.unsigned_tiles?unsigned(paint)*16:unsigned(0x1000+int8_t(paint)*16);
   pattern=sml2_color::fixed_texture(scene,(attr&8)!=0,target);
  }
  return {pattern?pattern:s.vram+address,uint8_t(attr&7),uint8_t(pattern?(attr&0x60):0)};
 }
 void window_position(int& x,int& y)const {
  x=s.wx;y=s.wy;
  if(s.fixed_scene){unsigned at=(s.fixed_scene-1)*16;x=boss_scene()?sml2_color::fixed_color_ppu[at+11]:x+int(sml2_color::fixed_color_ppu[at+11])-sml2_color::fixed_old_ppu[at+11];y+=int(sml2_color::fixed_color_ppu[at+10])-sml2_color::fixed_old_ppu[at+10];}
 }
 void prepare_fixed_surfaces(int window_x,int window_y){
  fixed_surface_ready=s.color_scene&&s.fixed_scene&&s.fixed_scene!=3;
  if(!fixed_surface_ready)return;
  unsigned used=0;bool window=(s.lcdc&32)&&window_y<144&&window_x<167;
  for(int y=0;y<144;y++){
   unsigned lcdc=line_lcdc(y),style=(lcdc&0x10)?1:0;used|=1u<<style;
   if(window&&y>=window_y&&(lcdc&32))used|=1u<<(style+2);
  }
  for(unsigned i=0;i<4;i++)if(used&(1u<<i))fixed_surface[i].prepare({uint64_t(s.fixed_scene)*4+i,std::span(s.vram,size_t(0x1800)),std::span(s.vram+0x1800,size_t(2048)),bool(i&1),fixed_surface_tile,&surface_context[i]});
 }
 // Tile-span consumer adapted from gbarecomp92b26b28 regularBG: select map,
 // source row and attributes once, then emit color/coverage in one span.
 // Retained donor license/notice: third_party/gbarecomp-renderer/.
 void emit_fixed_span(unsigned style,unsigned lcdc,unsigned cell,unsigned px,unsigned py,int x,int y,unsigned count){
  std::array<uint8_t,8> direct;
  auto indices=fixed_surface[style].span(cell,px,py,count,direct);
  unsigned attr=sml2_color::fixed_attributes[(s.fixed_scene-1)*2048+cell],priority=((lcdc&1)&&(attr&0x80))?0x80:0;
  auto* rgb=pixels.data()+y*width+x;auto* coverage=opaque.data()+y*width+x;
  for(unsigned i=0;i<count;i++){auto index=indices[i],color=uint8_t(index&3);rgb[i]=s.bg[index];coverage[i]=uint8_t(color|(color?priority:0));}
 }
 void fixed_background_row(int y,int offset,unsigned lcdc,int scroll_x,int scroll_y){
  unsigned style=(lcdc&0x10)?1:0,sy=unsigned(scroll_y+y)&255;
  int x=std::max(0,offset),end=std::min(width,offset+160);unsigned sx=unsigned(scroll_x+x-offset)&255;
  while(x<end){unsigned count=std::min(unsigned(end-x),8-(sx&7)),cell=((lcdc&8)?1024:0)+(sy>>3)*32+(sx>>3);
   emit_fixed_span(style,lcdc,cell,sx&7,sy&7,x,y,count);x+=int(count);sx=(sx+count)&255;
  }
 }
 void fixed_window_row(int y,int offset,unsigned lcdc,int origin,int window_y,bool expanded){
  bool spread=expanded&&s.wy==136&&s.wx==7;
  int x=spread?0:std::max(0,offset+origin),end=expanded?width:std::min(width,offset+160);
  unsigned style=((lcdc&0x10)?1:0)+2,sy=unsigned(y-window_y)&255;
  while(x<end){
   int src=x-offset-origin,limit=end;
   if(spread){if(x<120){src=x;limit=std::min(end,120);}else if(x>=width-40)src=160-(width-x);else{src=32+(x&7);limit=width-40;}}
   unsigned sx=unsigned(src)&255,count=std::min(unsigned(limit-x),8-(sx&7)),cell=((lcdc&64)?1024:0)+(sy>>3)*32+(sx>>3);
   emit_fixed_span(style,lcdc,cell,sx&7,sy&7,x,y,count);x+=int(count);
  }
 }
 uint32_t background_pixel(uint8_t t,uint8_t source,int px,int py,unsigned palette,uint8_t& color,bool hud=false,int fixed_attr=-1,int lcdc=-1)const {
  if(lcdc<0)lcdc=s.lcdc;
  int a=(lcdc&0x10)?source*16:0x1000+int8_t(source)*16;
  const uint8_t* pattern=nullptr;
  if(s.color_scene&&!s.fixed_scene&&s.stage==0&&!hud&&source>=0x20&&source<=0x23){
   // DX's water planes invert the live monochrome wave into color2; its
   // leading row is solid. Use the cart's current animation, not a frozen
   // reference atlas phase (the two ROMs need not advance at the same pace).
   unsigned raw=((s.vram[a+py*2]>>(7-px))&1)|(((s.vram[a+py*2+1]>>(7-px))&1)<<1);
   color=uint8_t(py==0||raw==0?2:0);return s.bg[(attribute(source)&7)*4+color];
  }
  if(s.color_scene){
   auto original=s.fixed_scene?sml2_color::fixed_original(s.fixed_scene-1,a):sml2_color::original(s.stage,graphics_power(),a);
   // Texture changes continue to show live guest pixels. Never freeze a
   // newly animated/mutated tile on a formerly recognized presentation tile.
   int target=(lcdc&0x10)?t*16:0x1000+int8_t(t)*16;
   int animated=-1;if(s.fixed_scene==3&&!hud)for(unsigned i=0;i<8;i++)if(a==int(animation_address[i])&&target==a)animated=int(i);
   if(animated>=0){if(s.animation_ready&&std::memcmp(s.animation_source+animated*16,s.vram+a,16)==0)pattern=s.animation_color+animated*16;}
   else if(hud||(original&&std::memcmp(original,s.vram+a,16)==0))pattern=s.fixed_scene?sml2_color::fixed_texture(s.fixed_scene-1,(fixed_attr&8)!=0,target):sml2_color::texture(s.stage,graphics_power(),hud?1:((attribute(t)&8)!=0),target);
  }
  int dx=px,dy=py;if(pattern&&(!hud||s.fixed_scene)){auto attr=s.fixed_scene?fixed_attr:attribute(t);if(attr&0x20)dx=7-dx;if(attr&0x40)dy=7-dy;}
  uint8_t lo=pattern?pattern[dy*2]:s.vram[a+py*2],hi=pattern?pattern[dy*2+1]:s.vram[a+py*2+1];
  color=uint8_t(((lo>>(7-dx))&1)|(((hi>>(7-dx))&1)<<1));return s.bg[palette*4+color];
 }
 void credits_text(const ModContext& c,std::span<const uint32_t> native,int offset) {
  if(!finale_scene()||sml2_color::fixed_phases[s.fixed_scene-1]<18)return;
  // A fully qualified emitted layer already carries its exact geometry and
  // colors; the older shade proof remains the portable incomplete-layer path.
  if(std::all_of(std::begin(s.emitted_color)+112*160,std::end(s.emitted_color),[](uint32_t pixel){return (pixel>>24)==255;}))return;
  constexpr int first=112;
  auto overlaps=[&](int x,int y,int h){return x<160&&x+8>0&&y<144&&y+h>first;};
  int height=(s.lcdc&4)?16:8;
  for(auto data:{static_cast<const uint8_t*>(s.oam),c.oam})for(unsigned i=0;i<160;i+=4)if(overlaps(int(data[i+1])-8,int(data[i])-16,height))return;
  for(auto list:{std::span(s.sprite,size_t(s.count)),std::span(s.owner,size_t(s.owner_count))})for(auto sp:list)if(overlaps(sp.x-s.left,sp.y-s.top,height))return;
  uint32_t mapping[4]{};bool used[4]{};
  // The staff loop switches to unsigned font data after LY111. Prove a
  // unique shade-to-DX-color mapping across every tile-map column of the
  // complete lower layer, including columns reached by midline SCX writes.
  // Mixed portrait palettes, changed live textures and objects fall back.
  for(int y=first;y<144;y++){
   auto line=s.raster[y];if(!(line.lcdc&0x10)||(line.lcdc&0x20))return;int yy=(y+line.scy)&255;
   for(unsigned col=0;col<32;col++){
    unsigned cell=((line.lcdc&8)?1024:0)+(yy>>3)*32+col,at=(s.fixed_scene-1)*2048+cell;
    auto t=s.vram[0x1800+cell],paint=t;if(t==sml2_color::fixed_old_map[at])paint=sml2_color::fixed_color_map[at];auto attr=sml2_color::fixed_attributes[at];
    auto original=sml2_color::fixed_original(s.fixed_scene-1,t*16),texture=sml2_color::fixed_texture(s.fixed_scene-1,(attr&8)!=0,paint*16);
    if(!original||!texture||std::memcmp(original,s.vram+t*16,16)!=0)return;
    int py=(yy&7)^((attr&0x40)?7:0);
    for(int x=0;x<8;x++){
     int bit=7-x,dx=bit^((attr&0x20)?7:0);
     unsigned raw=((original[(yy&7)*2]>>bit)&1)|(((original[(yy&7)*2+1]>>bit)&1)<<1),shade=(line.bgp>>(raw*2))&3;
     unsigned color=((texture[py*2]>>dx)&1)|(((texture[py*2+1]>>dx)&1)<<1);auto rgb=s.bg[(attr&7)*4+color];
     if(used[shade]&&mapping[shade]!=rgb)return;used[shade]=true;mapping[shade]=rgb;
    }
   }
  }
  std::array<uint32_t,160*(144-first)> layer;
  for(unsigned i=0;i<layer.size();i++){
   auto source=native[first*160+i];bool found=false;
   for(unsigned shade=0;shade<4;shade++)if(used[shade]&&source==c.ppu->bg_rgb[shade]){
    if(found&&layer[i]!=mapping[shade])return;layer[i]=mapping[shade];found=true;
   }
   if(!found)return;
  }
  for(int y=first;y<144;y++)std::copy_n(layer.data()+(y-first)*160,160,pixels.data()+y*width+offset);
 }
 ModView render(const ModContext& c,std::span<const uint32_t> native) {
  fixed_surface_ready=false;
  ModView out;out.width=width;out.expanded=s.valid&&width>160;pixels.assign(size_t(width)*144,0xff000000u);
  render_left=out.expanded?view_left():s.left-(width-160)/2;
  int offset=s.left-render_left;unsigned cached_sprites=0;
  int window_x,window_y;window_position(window_x,window_y);
  // The phase5 loader clears the window after the final door frame was
  // latched. Preserve its actual LCD-off blank, rather than recoloring that
  // stale room. Earlier lead phases deliberately keep their DX room colors.
  bool loader_blank=s.color_scene&&s.fixed_scene&&sml2_color::fixed_phases[s.fixed_scene-1]==0&&
   peek(c,0xff9b)==19&&peek(c,0xa2dd)==5&&(s.lcdc&32)&&!(c.ppu->lcdc&32)&&
   std::all_of(native.begin(),native.end(),[&](uint32_t pixel){return pixel==c.ppu->bg_rgb[4];});
  bool title_blank=s.color_scene&&s.fixed_scene&&sml2_color::fixed_modes[s.fixed_scene-1]==1&&!(c.ppu->lcdc&0x80)&&
   std::all_of(native.begin(),native.end(),[&](uint32_t pixel){return pixel==c.ppu->bg_rgb[4];});
  bool title_departed=s.fixed_scene&&sml2_color::fixed_modes[s.fixed_scene-1]==1&&peek(c,0xff9b)!=1&&peek(c,0xff9b)!=25;
  if((!out.expanded&&!s.color_scene)||loader_blank||title_blank||title_departed){for(int y=0;y<144;y++)std::copy_n(native.data()+y*160,160,pixels.data()+y*width+offset);}
  else {
   opaque.assign(pixels.size(),0);
   prepare_fixed_surfaces(window_x,window_y);
   for(int y=0;y<144;y++){
    unsigned lcdc=line_lcdc(y);int scroll_y=background_scroll_y(y),scroll_x=background_scroll_x(y);
    if(fixed_surface_ready)fixed_background_row(y,offset,lcdc,scroll_x,scroll_y);
    auto background_range=[&](int first,int last){for(int x=first;x<last;x++){
    int wx=render_left+x,wy=s.top+y,screen_x=scroll_x+wx-s.left;bool hardware=s.color_scene&&wx>=s.left&&wx<s.left+160;
    if(!hardware&&(!out.expanded||wx<s.bound_left||wx>=s.bound_right||wy<0||wy>=768))continue;
    uint8_t t;
    if(hardware)t=s.vram[((lcdc&8)?0x1c00:0x1800)+((scroll_y+y)&255)/8*32+((screen_x&255)/8)];else t=tile(wx,wy);
    auto paint=t;
    if(s.color_scene&&!s.fixed_scene){auto expected=tile(wx,wy);int a=0x1000+int8_t(t)*16,b=0x1000+int8_t(expected)*16;
     // Keep actual hardware writers authoritative when they diverge from the
     // live block map; replace only the corresponding DX block presentation.
     if(!hardware||t==expected||std::memcmp(s.vram+a,s.vram+b,16)==0)paint=color_tile(wx,wy,t);
    }
    int attr=attribute(paint);
    if(s.fixed_scene){unsigned cell=((lcdc&8)?1024:0)+((scroll_y+y)&255)/8*32+((screen_x&255)/8);
     unsigned at=(s.fixed_scene-1)*2048+cell;attr=sml2_color::fixed_attributes[at];
     if(s.fixed_scene==3){unsigned world=((s.map_y+y)&511)/8*64+((s.map_x+wx-s.left)&511)/8;
      if(t==s.map[world]&&t==sml2_color::world_old_map[world])paint=sml2_color::world_color_map[world];attr=sml2_color::world_attributes[paint];
     }else paint=fixed_paint(s.fixed_scene-1,cell,t);
    }
    uint8_t color=0;pixels[y*width+x]=background_pixel(paint,t,hardware?screen_x&7:wx&7,hardware?(scroll_y+y)&7:wy&7,attr&7,color,false,attr,int(lcdc));opaque[y*width+x]=color;
    if(s.fixed_scene&&(lcdc&1)&&(attr&0x80)&&color)opaque[y*width+x]|=0x80;
   }};
   if(fixed_surface_ready){if(out.expanded){background_range(0,std::clamp(offset,0,width));background_range(std::clamp(offset+160,0,width),width);}}
   else background_range(0,width);
   }
   auto entities=[&]{if(draw_lead_player(c))return;
   if(s.color_scene&&!s.fixed_scene)for(int slot=15;slot>=0;--slot) {
    const auto& pose=s.actors[slot];if(!pose.alive)continue;
    unsigned table=(s.stage==15||s.stage==11)?2:s.stage==13?4:0;
    unsigned q=sml2_color::presentation(table+pose.alternate,pose.frame),first=sml2_color::presentation_offset[q],n=sml2_color::presentation_length[q];
    for(unsigned i=n;i--;) {
     auto p=sml2_color::pieces[first+i];int dx=(pose.flip&0x20)?int8_t(uint8_t(~p.x-7)):p.x,dy=(pose.flip&0x40)?int8_t(uint8_t(~p.y-7)):p.y;
     auto attr=uint8_t(p.attr^pose.flip);draw_sprite({pose.x+dx,pose.y+dy,p.tile,attr,uint8_t(attr&7),0});++cached_sprites;
    }
   }
   if(out.expanded||s.color_scene)for(int i=s.count-1;i>=0;i--)draw_sprite(s.sprite[i]);
   for(int i=39;i>=0;i--){auto e=s.oam+i*4;if(!e[0]||e[0]>=160||e[1]<1||e[1]>167)continue;
    Sprite sp{s.left+e[1]-8,s.top+e[0]-16,e[2],e[3],0};
    bool owned=false;
    if(s.color_scene)for(int n=s.owner_count-1;n>=0;n--){auto other=s.owner[n];
     if(other.x==sp.x&&other.y==sp.y&&other.tile==sp.tile&&((other.attr^sp.attr)&0x60)==0){owned=true;break;}}
    if(owned)continue;
    if(s.color_scene&&!s.fixed_scene&&sp.tile>=0x80) {
     for(const auto& pose:s.actors)if(pose.valid&&pose.stage==s.stage&&(pose.tiles[sp.tile/8]&(1u<<(sp.tile&7)))&&
        std::abs(int(int8_t(uint8_t(sp.x-pose.x))))<48&&std::abs(int(int8_t(uint8_t(sp.y-pose.y))))<48){owned=true;break;}
     if(owned)continue;
    }
     if(s.fixed_scene){auto attr=sml2_color::fixed_object_attributes[(s.fixed_scene-1)*256+sp.tile];
      if(s.fixed_scene==3&&i>=13)attr=sml2_color::world_object_attributes[sp.tile];
      if(sml2_color::fixed_submodes[s.fixed_scene-1]==7&&i>=4&&sml2_color::castle_objects_available){auto key=((sp.attr>>4)&1)|((sp.attr>>6)&2);auto actual=sml2_color::castle_object_attributes[key*256+sp.tile];if(actual!=255)attr=actual;}
      // Map counter glyphs $6f='x', $70..$79='0'..'9' share DX palette1.
      if(s.fixed_scene==3&&sp.tile>=0x6f&&sp.tile<=0x79)attr=1;
      if(attr!=255){sp.attr=uint8_t((sp.attr&0x60)|attr);sp.palette=attr&7;}
     }
    draw_sprite(sp);
   }};
   if(!s.fixed_scene||boss_scene())entities();
   if(!s.color_scene)for(int y=0;y<144;y++)std::copy_n(native.data()+y*160,160,pixels.data()+y*width+offset);
   if((s.lcdc&32)&&window_y<144&&window_x<167){
    if(s.color_scene){
     int origin=window_x-7;
     for(int y=std::max(0,window_y);y<144;y++){
      unsigned lcdc=line_lcdc(y);if(!(lcdc&32))continue;
      if(fixed_surface_ready){fixed_window_row(y,offset,lcdc,origin,window_y,out.expanded);continue;}
      unsigned map=(lcdc&64)?0x1c00:0x1800;
      for(int x=0;x<width;x++){
      int screen_x=x-offset;if(!out.expanded&&(screen_x<origin||screen_x>=160))continue;
      int src=out.expanded&&s.wy==136&&s.wx==7?(x<120?x:x>=width-40?160-(width-x):32+(x&7)):screen_x-origin;
      if(src<0)continue;unsigned cell=detail::sml2_window_address(map,src,y-window_y);auto t=s.vram[cell],paint=t;uint8_t color=0,attr;
      if(s.fixed_scene){unsigned at=(s.fixed_scene-1)*2048+cell-0x1800;attr=sml2_color::fixed_attributes[at];if(t==sml2_color::fixed_old_map[at])paint=sml2_color::fixed_color_map[at];}
      else attr=sml2_color::hud_attributes[s.stage*32+((src>>3)&31)];
      pixels[y*width+x]=background_pixel(paint,t,src&7,(y-window_y)&7,attr&7,color,true,attr,int(lcdc));
      if(s.fixed_scene)opaque[y*width+x]=color|(((lcdc&1)&&(attr&0x80)&&color)?0x80:0);
     }}
    }else if(out.expanded&&s.wx==7)for(int y=s.wy;y<144;y++){
     for(int x=0;x<width;x++)pixels[y*width+x]=native[y*160+32];
     for(int x=0;x<160;x++)pixels[y*width+(x<120?x:width-160+x)]=native[y*160+x];
    }
   }
   if(s.fixed_scene&&!boss_scene())entities();
   if(s.color_scene){
    credits_text(c,native,offset);
    if(s.file_publication)for(int y=0;y<144;y++)for(int x=0;x<160;x++){
     auto color=s.emitted_color[y*160+x];
     if((color>>24)==255)pixels[y*width+offset+x]=color;
     else if(color!=2)pixels[y*width+offset+x]=native[y*160+x];
    }
    else if(finale_scene()&&sml2_color::fixed_phases[s.fixed_scene-1]>=18)
     for(int y=112;y<144;y++)for(int x=0;x<160;x++)if(auto color=s.emitted_color[y*160+x];(color>>24)==255)pixels[y*width+offset+x]=color;
   }
  }
  out.pixels=pixels;out.camera_x=s.cam_x;out.camera_y=s.cam_y;out.left=render_left;out.bound_left=s.bound_left;out.bound_right=s.bound_right;
  out.sprites=s.count+cached_sprites;out.captures=s.captures;out.activation_adjustments=s.rescues;out.spawn_scans=s.scans;out.spawn_reads=s.scan_reads;return out;
 }

};
Sml2NativeMods::Sml2NativeMods(std::span<const NativeFeature> f,bool cgb):impl_(std::make_unique<Impl>()) {
 if(cgb)throw std::runtime_error("stock SML2 adaptive adapter requires DMG hardware");
 if(f.size()!=1||f[0].plugin!="gbcllr.sml2.adaptive-view"||f[0].abi!=1)throw std::runtime_error("unsupported SML2 native feature set");
 if(!f[0].asset_input.empty()&&f[0].asset_input!=sml2_color::source_identity)throw std::runtime_error("SML2 presentation input identity mismatch");
 if(f[0].colorization&&(!sml2_color::available||!sml2_color::fire_available||!sml2_color::world_animation_available||!sml2_color::castle_flash_available||!sml2_color::castle_objects_available||!sml2_color::ending_extra_available||sml2_color::fixed_count!=39||f[0].asset_input!=sml2_color::source_identity))throw std::runtime_error("SML2 color requires the qualified asset-enabled build, all stage/map/Fire/boss/bonus/finale sources and pinned asset input");
 int width=f[0].view_width;if(width!=-1&&(width<160||width>4096))throw std::runtime_error("invalid SML2 adaptive width");
 impl_->requested=width;impl_->width=width<0?256:width;impl_->colorization=f[0].colorization;
}
Sml2NativeMods::~Sml2NativeMods()=default;
uint8_t Sml2NativeMods::read(ModContext& c,uint16_t a,uint8_t v){return impl_->read(c,a,v);}
void Sml2NativeMods::scanline(ModContext& c,uint8_t line){
 if(!line){
  bool previous=observes_pixels();impl_->snapshot(c);
  if(impl_->s.file_publication&&impl_->s.fixed_scene!=2)impl_->s.file_publication=0;
  std::fill(std::begin(impl_->s.raster),std::end(impl_->s.raster),Impl::Raster{c.ppu->scx,c.ppu->scy,c.ppu->lcdc,c.ppu->bgp});
  // Like the authentic framebuffer, provenance persists until those pixels
  // are actually replaced. A new staff interval starts with unknown history.
  if(!previous||!observes_pixels())framebuffer_cleared();
 }
 if(line==144&&impl_->s.file_publication==2&&std::all_of(std::begin(impl_->s.emitted_color),std::end(impl_->s.emitted_color),[](auto color){return color!=0;}))impl_->s.file_publication=0;
 if(line<144&&impl_->finale_scene())impl_->s.raster[line]={c.ppu->scx,c.ppu->scy,c.ppu->lcdc,c.ppu->bgp};
}
bool Sml2NativeMods::observes_pixels()const{return impl_->s.file_publication||(impl_->finale_scene()&&sml2_color::fixed_phases[impl_->s.fixed_scene-1]>=18);}
bool Sml2NativeMods::first_line(ModContext& c,std::span<const uint32_t> native){
 auto& s=impl_->s;
 if(!impl_->colorization||sml2_color::fixed_count<2||!s.title_to_file||
    (Impl::peek(c,0xff9b)!=25&&Impl::peek(c,0xff9b)!=sml2_color::fixed_modes[1])||c.ppu->lcdc!=sml2_color::fixed_old_ppu[16]||
    !std::all_of(native.begin(),native.end(),[&](auto pixel){return pixel==c.ppu->bg_rgb[4];}))return false;
 // The original enables the file LCD while cleanup mode25 is still current;
 // mode26 is published after drawing has started. Qualify the actual file
 // map/PPU/textures here without changing the guest's mode or timing.
 impl_->snapshot(c,sml2_color::fixed_modes[1]);if(s.fixed_scene!=2)return false;
 // The actual DX first-file pixels retain its preceding title palettes.
 // F14 already owns both exact palettes. The native startup clear separates
 // this interval from the normal file-palette publication.
 for(unsigned i=0;i<32;i++){s.bg[i]=Impl::rgb555(sml2_color::fixed_bg_palettes+i*2);s.obj[i]=Impl::rgb555(sml2_color::fixed_obj_palettes+i*2);}
 std::fill(std::begin(s.emitted_color),std::end(s.emitted_color),0);s.file_publication=1;s.title_to_file=0;return true;
}
void Sml2NativeMods::framebuffer_cleared(){if(impl_->s.file_publication==1)impl_->s.file_publication=2;std::fill(std::begin(impl_->s.emitted_color),std::end(impl_->s.emitted_color),0);}
void Sml2NativeMods::background_row(ModContext& c,const ModBackgroundRow& row){
 auto& s=impl_->s;std::fill(std::begin(s.fifo_color),std::end(s.fifo_color),0);
 if(!observes_pixels()||row.map<0x1800||row.map>=0x2000||row.attributes)return;
 bool file=s.file_publication;unsigned base=row.texture&~15;
 if(file){if(base>=0x1800||(c.ppu->lcdc&0x20))return;}
 else if(row.texture>=0x1000||row.texture/16!=row.tile||!(c.ppu->lcdc&0x10)||(c.ppu->lcdc&0x20))return;
 unsigned scene=s.fixed_scene-1,at=scene*2048+row.map-0x1800;
 auto attr=sml2_color::fixed_attributes[at],paint=row.tile;
 if(row.tile==sml2_color::fixed_old_map[at])paint=sml2_color::fixed_color_map[at];
 unsigned address=file&&!(sml2_color::fixed_color_ppu[scene*16]&0x10)?unsigned(0x1000+int8_t(paint)*16):unsigned(paint)*16;
 auto original=sml2_color::fixed_original(scene,base),texture=sml2_color::fixed_texture(scene,(attr&8)!=0,address);
 unsigned py=(row.texture&15)/2;
 if(!original||!texture||original[py*2]!=row.lower||original[py*2+1]!=row.upper||std::memcmp(original,c.vram+base,16))return;
 unsigned dy=py^((attr&0x40)?7:0);
 for(unsigned i=0;i<8;i++){
  unsigned bit=(7-i)^((attr&0x20)?7:0),color=((texture[dy*2]>>bit)&1)|(((texture[dy*2+1]>>bit)&1)<<1);
  s.fifo_color[i]=s.bg[(attr&7)*4+color];
 }
}
ModPixelSink Sml2NativeMods::pixel_sink(uint8_t line){return line<144&&observes_pixels()&&(impl_->s.file_publication||line>=112)?ModPixelSink{impl_->s.fifo_color,impl_->s.emitted_color+line*160}:ModPixelSink{};}
void Sml2NativeMods::viewport(unsigned width,unsigned height){int64_t v=impl_->requested;if(v<0)v=height?(uint64_t(width)*144+height/2)/height:256;impl_->width=int(std::clamp<int64_t>(v,160,4096));}
ModView Sml2NativeMods::render(ModContext& c,std::span<const uint32_t> native){return impl_->render(c,native);}
std::vector<uint8_t> Sml2NativeMods::snapshot()const {
 static_assert(std::is_trivially_copyable_v<Impl::State>);std::vector<uint8_t> bytes(80+sizeof(Impl::State));
 uint32_t h[]={11,uint32_t(impl_->requested),uint32_t(impl_->width),uint32_t(impl_->colorization)};std::memcpy(bytes.data(),h,16);if(impl_->colorization)std::memcpy(bytes.data()+16,sml2_color::data_identity.data(),64);else std::memset(bytes.data()+16,'0',64);std::memcpy(bytes.data()+80,&impl_->s,sizeof impl_->s);return bytes;
}
void Sml2NativeMods::restore(std::span<const uint8_t> bytes) {
 if(bytes.size()!=80+sizeof(Impl::State))throw std::runtime_error("invalid SML2 native state size");
 uint32_t h[4];std::memcpy(h,bytes.data(),16);if(h[0]!=11||int32_t(h[1])!=impl_->requested||h[2]<160||h[2]>4096||h[3]!=unsigned(impl_->colorization))throw std::runtime_error("SML2 native state profile mismatch");
 auto asset=std::string_view(reinterpret_cast<const char*>(bytes.data()+16),64);
 if((impl_->colorization&&asset!=sml2_color::data_identity)||(!impl_->colorization&&asset.find_first_not_of('0')!=std::string_view::npos))throw std::runtime_error("SML2 snapshot presentation assets mismatch");
 Impl::State s{};std::memcpy(&s,bytes.data()+80,sizeof s);
 for(const auto& pose:s.actors)if(pose.valid>1||pose.alive>1||(pose.alive&&!pose.valid)||pose.alternate>1||pose.x<0||pose.x>65535||pose.y<0||pose.y>65535)throw std::runtime_error("invalid SML2 actor pose");
 if(s.animation_ready>1||(s.animation_ready&&!impl_->colorization))throw std::runtime_error("invalid SML2 animation state");
 for(auto color:s.fifo_color)if(color&&((color>>24)!=255||!impl_->colorization))throw std::runtime_error("invalid SML2 FIFO color");
 for(auto color:s.emitted_color)if(color&&((color>2&&(color>>24)!=255)||!impl_->colorization))throw std::runtime_error("invalid SML2 emitted color");
 if(s.file_publication>2||(s.file_publication&&(!impl_->colorization||s.fixed_scene!=2)))throw std::runtime_error("invalid SML2 file publication");
 if(s.title_to_file>1||(s.title_to_file&&!impl_->colorization))throw std::runtime_error("invalid SML2 title publication");
 if(s.fixed_scene>sml2_color::fixed_count||(s.fixed_scene&&!s.color_scene))throw std::runtime_error("invalid SML2 fixed scene");
 if(s.fixed_scene&&sml2_color::fixed_bosses[s.fixed_scene-1]&&(s.stage!=sml2_color::fixed_stages[s.fixed_scene-1]||s.power>3))throw std::runtime_error("invalid SML2 boss presentation state");
 if(s.fireball_slot&&s.fireball_slot!=0xa880&&s.fireball_slot!=0xa890)throw std::runtime_error("invalid SML2 projectile cursor");
 if(s.color_scene>1||(s.color_scene&&!impl_->colorization)||(s.color_scene&&!s.fixed_scene&&(s.stage>=sml2_color::stage_count||s.power>3||(s.power==3&&!sml2_color::fire_available)))||s.valid<0||s.valid>1||s.previous_valid<0||s.previous_valid>1||s.frame<0||s.count<0||s.count>Impl::max_sprites||s.build_count<0||s.build_count>Impl::max_sprites||s.owner_count<0||s.owner_count>Impl::max_sprites||s.owner_build_count<0||s.owner_build_count>Impl::max_sprites)throw std::runtime_error("invalid SML2 native state");
 for(int n:{s.cam_x,s.cam_y,s.left,s.top,s.bound_left,s.bound_right,s.previous_camera,s.map_x,s.map_y})if(n< -131072||n>131072)throw std::runtime_error("invalid SML2 native coordinates");
 if(s.valid&&(s.bound_left<0||s.bound_right>4096||s.bound_right<=s.bound_left))throw std::runtime_error("invalid SML2 native bounds");
 for(int i=0;i<2;i++)if(s.scan_edge[i]<0||s.scan_edge[i]>4095||s.scan_frame[i]< -1||s.scan_frame[i]>s.frame||s.scan_valid[i]<0||s.scan_valid[i]>1)throw std::runtime_error("invalid SML2 native scanner state");
 for(auto list:{std::span(s.sprite),std::span(s.build),std::span(s.owner),std::span(s.owner_build)})for(auto sp:list)if(sp.x< -131072||sp.x>131072||sp.y< -131072||sp.y>131072||sp.palette>7||sp.screen_space>1)throw std::runtime_error("invalid SML2 native sprite");
 impl_->s=s;impl_->width=int(h[2]);for(auto& surface:impl_->fixed_surface)surface.invalidate();impl_->fixed_surface_ready=false;
}
}
