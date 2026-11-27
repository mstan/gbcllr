#include "mm5_native_mods.hpp"
#include "native_mods.hpp"
#include "megaman_pause_canvas.hpp"
#include <algorithm>
#include <array>
#include <cstring>
#include <stdexcept>
#if GBCLLR_MM5_DX_ASSETS
#include "mm5_dx_data.hpp"
#endif
namespace gbcllr {
namespace {
uint8_t peek(const ModContext& c,unsigned a,unsigned bank=~0u){
 if(a<0x4000)return a<c.rom_size?c.rom[a]:0;
 if(a<0x8000){auto p=(bank==~0u?c.rom_bank:bank)*0x4000u+(a&0x3fff);return p<c.rom_size?c.rom[p]:0;}
 if(a>=0xc000&&a<0xe000)return c.wram[a-0xc000];
 if(a>=0xff80&&a<0xffff)return c.hram[a-0xff80];
 return 0;
}
unsigned word(const ModContext& c,unsigned a,unsigned bank=~0u){return peek(c,a,bank)|(unsigned(peek(c,a+1,bank))<<8);}
bool world(const ModContext& c){return c.wram&&c.ppu&&(c.ppu->lcdc&0x80)&&peek(c,0xdf0a)>=1&&peek(c,0xdf0a)<=0x1f&&peek(c,0xdf01)>0&&peek(c,0xdf01)<0x20&&peek(c,0xd88c)&&peek(c,0xd88d)&&peek(c,0xd88d)<=8;}
constexpr std::array<uint32_t,4> gray{0xffffffff,0xffaaaaaa,0xff555555,0xff000000};
}
struct Mm5NativeMods::Impl {
 struct Piece {int16_t x=0,y=0;uint8_t attr=0,tile_index=0,authored=0,reserved=0;std::array<uint8_t,16> tile{};};
 struct Body {int16_t x=0,y=0;uint8_t frame=0,facing=0,attr=0,valid=0;};
 struct Bullet {uint16_t world=0;uint8_t valid=0,reserved=0;};
 struct Frame {
  uint16_t count=0,fixed_count=0;
  std::array<Piece,1024> pieces{};
  std::array<Piece,10> fixed{};
  Body body{};
  std::array<uint8_t,0x2000> ram{},vram{};
  uint8_t lcdc=0,bgp=0xe4,obp0=0xe4,obp1=0xe4,wy=144,wx=7,valid=0;
 };
 struct State {
  uint32_t schema=1,frames=0,captures=0,adjustments=0,width=160;
  uint8_t previous=0,pending=0,selection=0,target=0,world_tick=0,resize_scan=0,menu=0;
  std::array<uint16_t,6> caller{};
  uint16_t building_count=0;
  std::array<Piece,1024> building{};
  Body building_body{};
  Frame queued{},active{},display{};
  std::array<uint8_t,256> direct_resources{};
  std::array<Bullet,12> bullets{};
  std::array<Bullet,4> weapons{};
  std::array<uint8_t,0x2000> menu_ram{},menu_vram{};
  uint8_t menu_lcdc=0,menu_wx=7,menu_wy=144,menu_valid=0;
  uint8_t fixed_scene=0,fixed_valid=0,fixed_build_valid=0,fixed_options=0;
  std::array<uint16_t,16> fixed_palette{};
  std::array<uint32_t,160*144> fixed_build{},fixed_display{};
 } s;
 bool wide=false,no_flicker=false,smooth=false,cycle=false,color=false;
 int requested=160;unsigned width=160;
 std::vector<uint32_t> pixels;
 std::vector<uint32_t> pause_canvas;
 std::vector<uint32_t> framing_background;
 std::vector<uint8_t> background;
 explicit Impl(std::span<const NativeFeature> f){std::memset(&s,0,sizeof s);s.schema=1;for(const auto& v:f){wide|=v.plugin=="gbcllr.mm5.adaptive-view";if(v.plugin=="gbcllr.mm5.adaptive-view")requested=v.view_width;no_flicker|=v.no_flicker;smooth|=v.smooth_gameplay;cycle|=v.weapon_cycle;color|=v.colorization;
  if(v.colorization){
#if GBCLLR_MM5_DX_ASSETS
   if(v.asset_input!=GBCLLR_MM5_DX_ASSET_ID)throw std::runtime_error("Mega Man V DX presentation asset identity differs");
#else
   throw std::runtime_error("Mega Man V DX requires frozen native presentation assets");
#endif
  }
 }width=wide?(requested<0?256:unsigned(requested)):160;s.width=width;}
 void capture_entry(const ModContext& c){
  auto& f=s.active;f.count=f.fixed_count=0;f.body=s.building_body;
  std::copy_n(c.wram,0x2000,f.ram.begin());std::copy_n(c.vram,0x2000,f.vram.begin());f.lcdc=c.ppu->lcdc;f.bgp=c.ppu->bgp;f.obp0=c.ppu->obp0;f.obp1=c.ppu->obp1;f.wx=c.ppu->wx;f.wy=c.ppu->wy;f.valid=1;
  for(unsigned n=0;n<10;++n){const auto* o=c.oam+n*4;if(!o[0]||o[0]>=160)continue;auto& p=f.fixed[f.fixed_count++];p={};p.x=int16_t(int(o[1])-8+int16_t(word(c,0xd711)));p.y=int16_t(int(o[0])-16);p.attr=o[3];p.tile_index=o[2];std::copy_n(c.vram+p.tile_index*16,16,p.tile.begin());}
 }
 int left(const ModContext& c)const {int camera=int16_t(word(c,0xd711)),room=peek(c,0xd88c)*16;if(room<=int(width))return (room-int(width))/2;return std::clamp(camera-(int(width)-160)/2,0,room-int(width));}
 bool extended(const ModContext& c)const {
  // A one-screen arena has no additional world. Its custom controllers may
  // deliberately move an eight-bit staging position to the opposite edge;
  // preserving an earlier world page would change the original boss behavior.
  return wide&&width>160&&s.world_tick==1&&world(c)&&peek(c,0xd88c)>10;
 }
 unsigned collision_actor(const ModContext& c,unsigned stack,unsigned bank)const {
  // These original callers preserve the exact actor base immediately below
  // their return address. Player/platform collision callers are not included.
  unsigned caller=word(c,stack),actor=word(c,stack+2);
  switch((bank<<16)|caller){
   case 0x09415d:case 0x0d49ec:case 0x1246e7:case 0x1546ad:case 0x1d46b9:
   case 0x0d487a:case 0x0d70ca:case 0x124898:case 0x15485e:case 0x1d4888:
   case 0x1d5ffd:case 0x1d69c5:break;
   default:return 0;
  }
  if(actor<0xc600||actor>0xd6c0||(actor&63))return 0;
  // A controller may move its screen byte before its final world writeback.
  // Admit that bounded native motion, but refuse an unrelated stale page.
  int movement=int8_t(peek(c,actor+0x0d)-uint8_t(word(c,actor+0x0a)-word(c,0xd711)));
  if(movement < -32||movement > 32)return 0;
  return actor;
 }
 int projected(const ModContext& c,unsigned actor)const {
  int relative=int16_t(word(c,actor+0x0a)-word(c,0xd711));
  return relative+int8_t(peek(c,actor+0x0d)-uint8_t(relative));
 }
 bool hidden_hurt(const ModContext& c,unsigned actor)const {
  // The native generic producer selects its hurt palette from the decremented
  // timer. DX replaces that palette flash with an offscreen emission at the
  // same timer phase; keep that intentional blink with authored OBJ colors.
  return color&&actor>=0xc600&&actor<=0xd6c0&&!(actor&63)&&
   !(peek(c,actor+0x14)&4)&&(peek(c,actor+0x21)&2);
 }
 static int bullet_index(unsigned token){return token>=0x1b&&token<=0xcb&&!((token-0x1b)&15)?int((token-0x1b)/16):-1;}
 static int weapon_index(unsigned base){return base>=0xc23b&&base<=0xc29b&&(base-0xc23b)%32==0?int((base-0xc23b)/32):-1;}
 bool weak_weapon(const ModContext& c,unsigned base)const{return weapon_index(base)>=0&&!peek(c,0xde9b)&&peek(c,base)&&!(peek(c,base+9)&3);}
 void capture_bullet(const ModContext& c,std::span<uint16_t,6> cpu){
  if(!world(c)||!s.world_tick||s.building_count>=s.building.size())return;int index=bullet_index(cpu[1]&255);if(index<0)return;
  int x=s.bullets[index].valid?int16_t(s.bullets[index].world):int(word(c,0xd711))+int8_t(peek(c,0xff9b));int y=peek(c,0xff9c);if(y>=240)y-=256;
  Piece piece;piece.x=int16_t(x-3);piece.y=int16_t(y-4);piece.attr=peek(c,0xffab);piece.tile_index=uint8_t(cpu[1]>>8);std::copy_n(c.vram+piece.tile_index*16,16,piece.tile.begin());s.building[s.building_count++]=piece;++s.captures;
 }
#if GBCLLR_MM5_DX_ASSETS
 void select_fixed(unsigned scene,unsigned first=0,unsigned count=16){
  if(scene!=s.fixed_scene)s.fixed_build_valid=s.fixed_valid=0;
  s.fixed_scene=uint8_t(scene);
  if(scene<45||(scene>46&&scene!=48))s.fixed_options=0;
  if(scene>=42)std::copy_n(mm5_dx::scene_layouts+scene*16+first,count,s.fixed_palette.begin()+first);
 }
 const uint8_t* resource_texture(const ModContext& c,unsigned resource,unsigned code,unsigned tile)const {
  if(!resource||resource>=std::size(mm5_dx::resource_counts)||code>=mm5_dx::resource_counts[resource])return nullptr;
  unsigned bank=peek(c,0x421f+resource*3,1),descriptor=word(c,0x4220+resource*3,1),packet=word(c,descriptor,bank),count=peek(c,packet,bank),address=word(c,packet+1,bank),at=bank*0x4000+(address&0x3fff)+code*16;
  if(code>=count||at+16>c.rom_size||!std::equal(c.vram+tile*16,c.vram+tile*16+16,c.rom+at))return nullptr;
  return mm5_dx::resource_pixels+mm5_dx::resource_offsets[resource]+code*16;
 }
 const uint8_t* fixed_texture(const ModContext& c,unsigned texture,bool alternate)const {
  if(alternate)return mm5_dx::shared_tiles+texture;
  unsigned scene=s.fixed_scene,entry=mm5_dx::fixed_tiles[scene*384+texture/16];
  if(entry&&std::equal(c.vram+texture,c.vram+texture+16,mm5_dx::fixed_original+(entry-1)*16))return mm5_dx::fixed_authored+(entry-1)*16;
  if(scene>=44&&scene<=48&&texture>=0x1000){unsigned tile=texture/16-256,stage_entry=mm5_dx::stage_tiles[30*128+tile];if(stage_entry&&std::equal(c.vram+texture,c.vram+texture+16,mm5_dx::tile_original+(stage_entry-1)*16))return mm5_dx::tile_authored+(stage_entry-1)*16;}
  if(scene>=32&&texture<0x1000){unsigned tile=texture/16;
   for(int base=int(tile);base>=0;--base){unsigned resource=s.direct_resources[unsigned(base)],code=tile-unsigned(base);if(!resource)continue;if(resource>=146||code>=mm5_dx::resource_counts[resource])break;
    unsigned bank=peek(c,0x421f+resource*3,1),descriptor=word(c,0x4220+resource*3,1),packet=word(c,descriptor,bank),count=peek(c,packet,bank),address=word(c,packet+1,bank),at=bank*0x4000+(address&0x3fff)+code*16;
    if(code<count&&at+16<=c.rom_size&&std::equal(c.vram+texture,c.vram+texture+16,c.rom+at))return mm5_dx::resource_pixels+mm5_dx::resource_offsets[resource]+code*16;break;
   }
  }
  return c.vram+texture;
 }
 void fixed_row(const ModContext& c,unsigned line){
  if(!color||s.fixed_scene<32||s.fixed_scene>=51||(s.world_tick&&world(c))||line>=144||!(c.ppu->lcdc&128))return;
  if(!line)s.fixed_build_valid=1;
  if(!s.fixed_build_valid)return;
  unsigned scene=s.fixed_scene,attributes_scene=scene<42?scene:scene==42?12:scene==43?29:scene==47?33:scene==49?42:scene==50?34:30,fade=mm5_dx::fade_banks[scene==49&&(s.fixed_options&2)?0xff:peek(c,0xde96)];
  auto selector_palette=s.fixed_palette;
  if(scene==50){unsigned selected=peek(c,0xcb00),stage=peek(c,0xdf0a);if(stage&&stage<=11)std::copy_n(mm5_dx::selector_stage_palettes+(stage-1)*2,2,selector_palette.begin()+3);
   if(selected&&selected<=9){unsigned resource=mm5_dx::selector_resources[selected];for(unsigned n=0;n<16;++n)if(auto p=mm5_dx::actor_layouts[resource*16+n])selector_palette[n]=p;selector_palette[15]=uint16_t((selector_palette[15]&0xff00)|mm5_dx::selector_face_lows[selected-1]);}
   if(c.ppu->obp1==0xff){selector_palette[8]=0x5486;for(unsigned n=10;n<=14;++n)selector_palette[n]=0x5486;}else selector_palette[8]=0x53e0;
  }
  // Authored OBJ pointers contain the three opaque colors; transparent
  // color0 has no word in those packets. BG pointers contain all four.
  auto rgb=[&](unsigned raw,unsigned slot){unsigned p=(scene==50?selector_palette[slot&15]:scene>=42?s.fixed_palette[slot&15]:mm5_dx::scene_layouts[scene*16+(slot&15)])+(slot>=8?raw-1:raw)*2;if(fade<0x22||fade>0x2d||p<0x4400||p>=0x6400)return gray[raw&3];unsigned value=mm5_dx::palette_words[(fade-0x22)*0x1000+(p-0x4400)/2];return 0xff000000u|((value&31)*255/31<<16)|(((value>>5)&31)*255/31<<8)|(((value>>10)&31)*255/31);};
  std::array<uint8_t,160> raw_bg{};std::array<const uint8_t*,384> texture_cache{};
  auto image=[&](unsigned texture,bool alternate){if(alternate)return mm5_dx::shared_tiles+texture;auto& result=texture_cache[texture/16];if(!result)result=fixed_texture(c,texture,false);return result;};
  for(unsigned x=0;x<160;++x){bool window=(c.ppu->lcdc&32)&&line>=c.ppu->wy&&c.ppu->wx<=166&&int(x)>=int(c.ppu->wx)-7;
   unsigned sx=window?unsigned(int(x)-int(c.ppu->wx)+7):(x+c.ppu->scx)&255,sy=window?line-c.ppu->wy:(line+c.ppu->scy)&255;
   unsigned map=window?(c.ppu->lcdc&64?0x1c00:0x1800):(c.ppu->lcdc&8?0x1c00:0x1800),cell=(sy/8)*32+sx/8,tile=c.vram[map+cell];
   if(!window&&map==0x1800&&sx<160&&sy<144){unsigned at=scene*360+(sy/8)*20+sx/8;if(tile==mm5_dx::fixed_map_original[at])tile=mm5_dx::fixed_map_authored[at];}
   unsigned attributes=window?6:mm5_dx::bg_attributes[attributes_scene*256+tile];
   if(scene==50&&window)attributes=sy<144&&sx<96?mm5_dx::selector_window_attributes[(sy/8)*12+sx/8]:0;
   if(scene==42&&window)attributes=sy<48&&sx<144?mm5_dx::ending_window_attributes[(sy/8)*18+sx/8]:0;
   if(((scene>=44&&scene<=46)||scene==48)&&!window&&map==0x1800&&(s.fixed_options&1)&&sy>=96&&sy<144)attributes=1;
   if(scene==47&&!window&&map==0x1800&&sy/8==16&&sx/8>=7&&sx/8<13)attributes=7;
   unsigned texture=(c.ppu->lcdc&16)?tile*16:unsigned(0x1000+int8_t(tile)*16);
   const uint8_t* pixels=image(texture,attributes&8);unsigned bit=attributes&32?sx&7:7-(sx&7),row=attributes&64?7-(sy&7):sy&7;
   unsigned raw=(c.ppu->lcdc&1)?((pixels[row*2]>>bit)&1)|(((pixels[row*2+1]>>bit)&1)<<1):0;raw_bg[x]=uint8_t(raw|(attributes&128));s.fixed_build[line*160+x]=rgb(raw,attributes&7);
  }
  if(!(c.ppu->lcdc&2))return;
  // Retain native OAM positions and priority. Title's extra authored lettering
  // is declarative presentation data, never written to the guest OAM buffer.
  std::array<uint8_t,160> objects{};std::copy_n(c.oam,160,objects.begin());unsigned cast_resource=0;std::array<const uint8_t*,40> selector_images{};
  if(scene==35)std::copy_n(mm5_dx::title_objects,44,objects.begin()+16);
  if((scene>=44&&scene<=46)||scene==48){bool custom=objects[0]>0&&objects[0]<160;for(unsigned n=0;n<7;++n)custom&=objects[n*4+2]==0x70+n;
   if(custom){bool flip=objects[3]&32;int x=int(objects[1])+(flip?-17:1),y=int(objects[0])-11;for(unsigned n=0;n<10;++n)objects[n*4]=160;
    for(unsigned n=0;n<8;++n){const auto* p=mm5_dx::credits_objects+n*4;y+=int8_t(p[0]);x+=flip?-int(int8_t(p[1])):int(int8_t(p[1]));objects[n*4]=uint8_t(y+16);objects[n*4+1]=uint8_t(x+8);objects[n*4+2]=p[2];objects[n*4+3]=uint8_t(p[3]^(flip?32:0));}
   }
  }
  if(((scene>=44&&scene<=46)||scene==48)&&peek(c,0xcb80)&&!peek(c,0xcb40)&&!peek(c,0xcb52)&&!peek(c,0xcb5b)&&!peek(c,0xcb64)){
   unsigned card=peek(c,0xcb81);if(card&&card<=10){unsigned index=card-1,count=mm5_dx::credits_cast_original_counts[index];
    if(peek(c,0xcbc0+count*4)==0x80&&std::equal(c.wram+0xbc0,c.wram+0xbc0+count*4,mm5_dx::credits_cast_original+index*120)){
     cast_resource=peek(c,0x56a6+index*3,0x1b);for(unsigned n=10;n<40;++n)objects[n*4]=160;int x=128,y=128;
     for(unsigned n=0;n<mm5_dx::credits_cast_counts[index];++n){const auto* p=mm5_dx::credits_cast_objects+index*120+n*4;y+=int8_t(p[0]);x+=int8_t(p[1]);objects[(n+10)*4]=uint8_t(y+16);objects[(n+10)*4+1]=uint8_t(x+8);objects[(n+10)*4+2]=uint8_t(128+p[2]);objects[(n+10)*4+3]=p[3];}
    }
   }
  }
  if(scene==50){unsigned selected=peek(c,0xcb00);if(selected&&selected<=9){unsigned bank=selected<=5?3:9,resource=mm5_dx::selector_resources[selected],table=word(c,0xc815),pointer=0;
   if(table>=0x4000&&table+peek(c,0xc817)*2<0x7fff)pointer=word(c,table+peek(c,0xc817)*2,bank)+4;
   else pointer=word(c,selected<=5?0x7a99+(selected-1)*2:0x7edc+(selected-6)*2,bank)+4;
   auto key=uint32_t(bank*65536+pointer);auto found=std::lower_bound(std::begin(mm5_dx::list_keys),std::end(mm5_dx::list_keys),key);
   if(found!=std::end(mm5_dx::list_keys)&&*found==key&&s.direct_resources[7]==resource){unsigned index=unsigned(found-std::begin(mm5_dx::list_keys)),count=mm5_dx::list_counts[index];const auto before=objects;
    unsigned source_bank=peek(c,0x421f+resource*3,1),descriptor=word(c,0x4220+resource*3,1),packet=word(c,descriptor,source_bank),native_count=peek(c,packet,source_bank),address=word(c,packet+1,source_bank);
    for(unsigned n=0;n<40;++n){unsigned tile=objects[n*4+2],code=uint8_t(tile-7),at=source_bank*0x4000+(address&0x3fff)+code*16;if(code<native_count&&at+16<=c.rom_size&&std::equal(c.vram+tile*16,c.vram+tile*16+16,c.rom+at))objects[n*4]=160;}
    std::array<unsigned,40> free{};unsigned available=0;for(unsigned n=0;n<40;++n)if(!objects[n*4]||objects[n*4]>=160)free[available++]=n;
    if(available>=count){int x=0x22,y=0x57;for(unsigned n=0;n<count;++n){auto p=mm5_dx::list_data+mm5_dx::list_offsets[index]+n*4;y+=int8_t(p[0]);x+=int8_t(p[1]);unsigned slot=free[n];objects[slot*4]=uint8_t(y+16);objects[slot*4+1]=uint8_t(x+8);objects[slot*4+2]=uint8_t(7+p[2]);objects[slot*4+3]=p[3];if(!(p[3]&8)&&p[2]<mm5_dx::resource_counts[resource])selector_images[slot]=mm5_dx::resource_pixels+mm5_dx::resource_offsets[resource]+p[2]*16;}}
    else objects=before;
   }
  }}
  std::array<unsigned,40> order{};for(unsigned n=0;n<40;++n)order[n]=n;
  std::stable_sort(order.begin(),order.end(),[&](unsigned a,unsigned b){return objects[a*4+1]>objects[b*4+1]||(objects[a*4+1]==objects[b*4+1]&&a>b);});
  unsigned height=c.ppu->lcdc&4?16:8;
  for(auto n:order){const auto* object=objects.data()+n*4;int oy=int(object[0])-16,ox=int(object[1])-8;if(int(line)<oy||int(line)>=oy+int(height))continue;unsigned attributes=object[3],row=unsigned(int(line)-oy);if(attributes&64)row=height-1-row;unsigned tile=(height==16?object[2]&254:object[2])+row/8;row&=7;const uint8_t* pixels=image(tile*16,attributes&8);if(selector_images[n])pixels=selector_images[n];if(cast_resource&&n>=10&&!(attributes&8)&&tile>=128&&tile-128<mm5_dx::resource_counts[cast_resource])pixels=mm5_dx::resource_pixels+mm5_dx::resource_offsets[cast_resource]+(tile-128)*16;if(scene==35&&tile>=16&&tile<26)pixels=mm5_dx::title_obj_tiles+(tile-16)*16;
   for(unsigned x=0;x<8;++x){int dx=ox+int(x);if(dx<0||dx>=160)continue;unsigned bit=attributes&32?x:7-x,raw=((pixels[row*2]>>bit)&1)|(((pixels[row*2+1]>>bit)&1)<<1);if(raw&&(!((attributes|raw_bg[dx])&128)||!(raw_bg[dx]&3)))s.fixed_build[line*160+unsigned(dx)]=rgb(raw,8+(attributes&7));}
  }
 }
 ModView menu_view(std::span<const uint32_t> native){
  if(!s.menu_valid||!(s.menu_lcdc&0x20)||s.menu_wy||s.menu_wx!=7||s.menu_ram[0x11f0])return {160,false,native};
  // The cartridge still owns cursor, equipment, tanks and pause transactions.
  // This is the DX author's NES font, item art, cell geometry and palettes.
  auto ram=[&](unsigned a){return s.menu_ram[a-0xc000];};
  std::array<uint16_t,8> palette{0x4468,uint16_t(0x4400+(ram(0xde9b)%13)*8),uint16_t(0x4400+(ram(0xd1ed)%13)*8),0x4a78,0x4a80,0x4a88,0x4a90,0x4a98};
  unsigned fade=mm5_dx::fade_banks[ram(0xde96)];
  auto rgb=[&](unsigned raw,unsigned pal){unsigned address=palette[pal&7]+raw*2;if(address<0x4400||address>=0x6400||fade<0x22||fade>0x2d)return gray[raw&3];unsigned value=mm5_dx::palette_words[(fade-0x22)*0x1000+(address-0x4400)/2];return 0xff000000u|((value&31)*255/31<<16)|(((value>>5)&31)*255/31<<8)|(((value>>10)&31)*255/31);};
  std::array<uint8_t,360> attributes{};std::copy_n(mm5_dx::menu_attributes,360,attributes.begin());std::fill(attributes.begin()+40,attributes.end(),3);
  unsigned selected=ram(0xd1ed);for(unsigned item=0;item<14;++item){unsigned cell=mm5_dx::menu_cells[item]-0x9c00,x=cell&31,y=cell>>5;bool owned=item<13&&((unsigned(ram(0xde9c))|(unsigned(ram(0xde9d))<<8))&(1u<<item));if(!owned&&item!=selected)continue;auto a=item==selected?mm5_dx::menu_selected_attributes:mm5_dx::menu_normal_attributes;
   for(unsigned dy=0;dy<2;++dy)for(unsigned dx=0;dx<7;++dx)if(x+dx<20&&y+dy<18)attributes[(y+dy)*20+x+dx]=a[dy*7+dx];
  }
  pixels.resize(160*144);
  for(unsigned y=0;y<144;++y)for(unsigned x=0;x<160;++x){
   unsigned tile=s.menu_vram[0x1c00+(y/8)*32+x/8];if(tile==3)tile=0;
   unsigned texture=(s.menu_lcdc&16)?tile*16:unsigned(0x1000+int8_t(tile)*16);
   const uint8_t* image=s.menu_vram.data()+texture;
   if(attributes[(y/8)*20+x/8]&8)image=mm5_dx::shared_tiles+texture;
   else if(texture>=0xe00&&texture<0xff0)image=mm5_dx::menu_glyphs+(texture-0xe00);
   if(texture>=0x1700&&texture<0x1740)image=mm5_dx::menu_icons+std::min(unsigned(ram(0xde9b)),13u)*64+texture-0x1700;
   // The status band keeps the same authored tile planes while the native
   // window moves it from the world into the menu. Original DMG planes have
   // different ink assignments, turning the gauge white and portrait opaque.
   if(y<16&&texture>=0x1740&&texture<0x1800)image=mm5_dx::hud_tiles+texture-0x1740;
   if(y<16&&texture>=0x16e0&&texture<0x1700)for(unsigned n=0;n<12;++n)
    if(std::equal(s.menu_vram.begin()+texture,s.menu_vram.begin()+texture+16,mm5_dx::hud_digit_original+n*16)){image=mm5_dx::hud_digits+n*16;break;}
   unsigned line=(y&7)*2,bit=7-(x&7),raw=((image[line]>>bit)&1)|(((image[line+1]>>bit)&1)<<1);
   pixels[y*160+x]=rgb(raw,attributes[(y/8)*20+x/8]&7);
  }
  for(unsigned item=0;item<14;++item){bool owned=item<13&&((unsigned(ram(0xde9c))|(unsigned(ram(0xde9d))<<8))&(1u<<item));if(!owned&&item!=selected)continue;unsigned cell=mm5_dx::menu_cells[item]-0x9c00,origin_x=(cell&31)*8,origin_y=(cell>>5)*8;for(unsigned y=0;y<16;++y)for(unsigned x=0;x<16;++x){unsigned dx=origin_x+x,dy=origin_y+y;if(dx>=160||dy>=144)continue;unsigned tile=mm5_dx::menu_item_tiles[item*4+(y/8)*2+x/8];const auto* image=mm5_dx::shared_tiles+0x1000+tile*16;unsigned bit=7-(x&7),line=(y&7)*2,raw=((image[line]>>bit)&1)|(((image[line+1]>>bit)&1)<<1);pixels[dy*160+dx]=rgb(raw,item==selected?2:3);}}
  return {160,false,pixels};
 }
#endif
 void capture_list(const ModContext& c,std::span<uint16_t,6> cpu,bool mirrored){
  if(!world(c)||!s.world_tick)return;
  unsigned p=cpu[1],bank=c.rom_bank;if(p<0x4000||p>=0x8000)return;
  int x=peek(c,0xff9b),y=peek(c,0xff9c);unsigned ret=word(c,cpu[4]);
  if(ret==0x29f0||ret==0x29f6){unsigned actor=word(c,cpu[4]+2);if(hidden_hurt(c,actor))return;if(actor>=0xc600&&actor<=0xd6c0&&!(actor&63)){int actual=int16_t(word(c,actor+0x0a)-word(c,0xd711));x=actual+int8_t(x-uint8_t(actual));}}
  bool flip=mirrored&&(peek(c,0xd3ea)&32);if(flip)x-=8;
  const uint8_t* authored=nullptr;unsigned count=128,resource=0,base=peek(c,0xffac);
#if GBCLLR_MM5_DX_ASSETS
  if(color){for(unsigned at=0xc3c6;at<0xc3de&&peek(c,at);at+=2)if(peek(c,at+1)==base){resource=peek(c,at);break;}if(!resource)resource=s.direct_resources[base];
   if(resource<std::size(mm5_dx::resource_counts)&&mm5_dx::resource_counts[resource]){uint32_t key=uint32_t(bank*65536+p);auto found=std::lower_bound(std::begin(mm5_dx::list_keys),std::end(mm5_dx::list_keys),key);if(found!=std::end(mm5_dx::list_keys)&&*found==key){unsigned index=unsigned(found-std::begin(mm5_dx::list_keys));authored=mm5_dx::list_data+mm5_dx::list_offsets[index];count=mm5_dx::list_counts[index];}}
  }
#endif
  auto value=[&](unsigned offset){return authored?authored[offset]:peek(c,p+offset,bank);};
  for(unsigned n=0;n<count&&(authored||p+(n+1)*4<=0x8000);++n){unsigned at=n*4;if(value(at)==0x80)break;y+=int8_t(value(at));int dx=int8_t(value(at+1));x+=flip?-dx:dx;unsigned code=value(at+2),tile=(base+code)&255,attr=value(at+3)^peek(c,0xffab)^(flip?32:0);
   if(s.building_count>=s.building.size())break;Piece piece;piece.x=int16_t(x+word(c,0xd711));piece.y=int16_t(y);piece.attr=uint8_t(attr);piece.tile_index=uint8_t(tile);const uint8_t* pixels=c.vram+tile*16;
#if GBCLLR_MM5_DX_ASSETS
   if(color){if(authored&&(attr&8)){pixels=mm5_dx::shared_tiles+tile*16;piece.authored=1;}
    else if(authored&&code<mm5_dx::resource_counts[resource]){pixels=mm5_dx::resource_pixels+mm5_dx::resource_offsets[resource]+code*16;piece.authored=1;}
    else if(const auto* image=resource_texture(c,resource,code,tile)){pixels=image;piece.authored=1;}}

#endif
   std::copy_n(pixels,16,piece.tile.begin());s.building[s.building_count++]=piece;++s.captures;
  }
 }
 void capture_effect(const ModContext& c,std::span<uint16_t,6> cpu){
  if(!world(c)||!s.world_tick||word(c,cpu[4])!=0x4f1f)return;
  unsigned source=cpu[1];if(source<0x4f2c||source>=0x4f65)return;
  // Original1:4E8D admits X[-32,192), Y[-16,176) before this producer.
  // Its OAM stores wrap negative positions; retain the signed logical origin
  // and every emitted piece before the native allocation can omit it.
  int x=peek(c,0xff9b),y=peek(c,0xff9c);if(x>=224)x-=256;if(y>=240)y-=256;
  for(unsigned n=0;n<16&&source+4<=0x4f65;++n){unsigned dy=peek(c,source++);if(dy==0x80)break;
   y+=int8_t(dy);x+=int8_t(peek(c,source++));unsigned tile=peek(c,source++),attr=peek(c,source++);
   if(s.building_count>=s.building.size())break;auto& p=s.building[s.building_count++];p={};p.x=int16_t(x+word(c,0xd711));p.y=int16_t(y);p.tile_index=uint8_t(tile);p.attr=uint8_t(attr);std::copy_n(c.vram+tile*16,16,p.tile.begin());++s.captures;
  }
 }
 void capture(const ModContext& c,std::span<uint16_t,6> cpu){
  // Original grid and trailing pieces, before the forty-entry allocation can
  // reject them. Positions remain signed world coordinates; no eight-bit X
  // wrapping is carried into the expanded compositor.
  if(!world(c)||!s.world_tick)return;
  unsigned p=cpu[3],bank=c.rom_bank;if(p<0x4000||p>=0x8000)return;
  int ox=int(peek(c,0xff9b)),oy=int(peek(c,0xff9c));
  auto ret=word(c,cpu[4]);
  if(ret==0x29d1){unsigned base=cpu[1];if(hidden_hurt(c,base))return;if(base>=0xc600&&base+0x30<0xd700){int actual=int16_t(word(c,base+0x0a)-word(c,0xd711));ox=actual+int8_t(unsigned(ox-actual));}}
  else {int actual=int16_t(word(c,0xd70d)-word(c,0xd711));ox=actual+int8_t(unsigned(ox-actual));}
  bool flip=peek(c,0xd3ea)&0x20;int dx=int8_t(peek(c,p++,bank)),dy=int8_t(peek(c,p++,bank));unsigned shape=peek(c,p++,bank),cols=shape>>4,rows=shape&15;
  int start=flip?ox-dx-8:ox+dx;unsigned attr=peek(c,0xffab),base=peek(c,0xffac);bool full=peek(c,0xce56)&1;
  unsigned resource=0;
#if GBCLLR_MM5_DX_ASSETS
  if(color){for(unsigned at=0xc3c6;at<0xc3de&&peek(c,at);at+=2)if(peek(c,at+1)==base){resource=peek(c,at);break;}if(!resource)resource=s.direct_resources[base];}
#endif

  auto add=[&](int x,int y,unsigned code){if(code==255||s.building_count>=s.building.size())return;Piece piece;piece.x=int16_t(x+word(c,0xd711));piece.y=int16_t(y);piece.attr=uint8_t(attr^((flip?0x20:0))^((code&0x80)?0x20:0)^((!full&&(code&0x40))?0x40:0));unsigned tile=(base+(code&(full?127:63)))&255;piece.tile_index=uint8_t(tile);const uint8_t* pixels=c.vram+tile*16;
#if GBCLLR_MM5_DX_ASSETS
   if(color)if(const auto* image=resource_texture(c,resource,code&(full?127:63),tile)){pixels=image;piece.authored=1;}
#endif
   std::copy_n(pixels,16,piece.tile.begin());s.building[s.building_count++]=piece;++s.captures;};
  if(!cols||!rows)return;
  for(unsigned y=0;y<rows;++y)for(unsigned x=0;x<cols;++x)add(start+(flip?-int(x*8):int(x*8)),oy+dy+int(y*8),peek(c,p++,bank));
  for(unsigned n=0;n<128;++n){unsigned code=peek(c,p++,bank);if(code==0x80)break;int x=int8_t(code),y=int8_t(peek(c,p++,bank));code=peek(c,p++,bank);add(start+(flip?-x:x),oy+dy+y,code);}
 }
};
Mm5NativeMods::Mm5NativeMods(std::span<const NativeFeature> f,bool):impl_(std::make_unique<Impl>(f)){}
Mm5NativeMods::~Mm5NativeMods()=default;
void Mm5NativeMods::inputs(uint8_t a,uint8_t){auto& s=impl_->s;if(a&~3u)throw std::runtime_error("Invalid Mega Man V shoulder input");if(!impl_->cycle||s.menu||a==3)s.pending=0;else s.pending|=uint8_t(a&~s.previous);s.previous=a;}
void Mm5NativeMods::viewport(unsigned w,unsigned h){if(impl_->wide&&impl_->requested<0&&h){unsigned next=std::clamp(unsigned((uint64_t(w)*144+h-1)/h),160u,4096u);if(next!=impl_->width)impl_->s.resize_scan=2;impl_->s.width=impl_->width=next;}}
void Mm5NativeMods::write(ModContext&,uint16_t address,uint8_t value){if(address==0xff46&&value==0xc0&&impl_->s.queued.valid)impl_->s.active=impl_->s.queued;}
std::span<const uint16_t> Mm5NativeMods::instruction_sites(){
 // All entry sites below, including authored-color paths. Bank checks remain
 // in the actual service. Register new instruction observations here too.
 static constexpr uint16_t sites[]={
  0x04ce,0x04d2,0x0959,0x09ef,0x0f32,0x0f71,0x11f7,0x197e,0x1fe8,
  0x2659,0x28a5,0x2d1b,0x2de8,0x31b9,0x31fb,0x37c5,0x3c46,0x3c6c,
  0x3d11,0x3d3d,0x3d42,0x3db8,0x3df4,0x41aa,0x438e,0x4514,0x457e,
  0x4850,0x48c4,0x48db,0x4979,0x4b5b,0x4c8a,0x4c8d,0x5142,0x5146,
  0x5163,0x51c0,0x5406,0x5418,0x55a2,0x55ea,0x6154,0x6223,0x6329,
  0x6397,0x63a9,0x63bb,0x63d8,0x654c,0x6a5e,0x6ad1,0x6b15,0x6b5d,
  0x6bc5,0x6bfb,0x78d8};
 return sites;
}
std::span<const uint16_t> Mm5NativeMods::read_sites(){
 static constexpr uint16_t sites[]={0xd7c8,0xd7c9,0xd7ca};return sites;
}
bool Mm5NativeMods::poll_instructions()const{
 // Pending shoulder input is the only state polled outside the entry sites.
 // Inputs arrive before execute_until; a pending request keeps the ordinary
 // observer for that entire execution interval, including its cancellation.
 return impl_->s.pending!=0;
}
uint8_t Mm5NativeMods::read(ModContext& c,uint16_t a,uint8_t value){
 auto& i=*impl_;if(!i.extended(c)||c.sp==UINT16_MAX)return value;
 if(a==0xd7ca&&c.instruction_pc==0x26d2&&i.s.resize_scan){unsigned center=peek(c,0xd7c7);unsigned phase=i.s.resize_scan--;if(phase==2&&center<255)return uint8_t(center+1);if(phase==1&&center>0)return uint8_t(center-1);return value;}
 // Only the original coarse activation boundaries observe the larger view.
 // Tile streaming, collision and the hardware camera keep their native values.
 if((a==0xd7c8||a==0xd7c9)&&((c.instruction_pc>=0x2689&&c.instruction_pc<=0x27fd))){int l=i.left(c),edge=a==0xd7c8?l-16:l+int(i.width)+16;return uint8_t(std::clamp(edge/16,0,255));}
 return value;
}
void Mm5NativeMods::instruction(ModContext& c,std::span<uint16_t,6> cpu,std::span<uint8_t> ram,std::span<uint8_t> hram){
 auto& i=*impl_;auto& s=i.s;unsigned pc=cpu[5];
 // Original04CE opens the password screen;04D2 leaves a completed stage.
 // Both retain the former stage number and room dimensions while replacing
 // its graphics. Stop publishing world frames until the native stage loop
 // actually resumes, rather than interpreting those UI tiles as terrain.
 if((c.rom_bank==0x19&&pc==0x41aa)||pc==0x04ce||pc==0x04d2){s.world_tick=0;s.queued.valid=s.active.valid=s.display.valid=0;s.building_count=0;s.building_body={};s.menu=0;s.pending=0;}
 if(i.color){unsigned scene=0;if(c.rom_bank==0x10){if(pc==0x4979)scene=32;if(pc==0x6bfb)scene=35;}if(c.rom_bank==0x14){switch(pc){case 0x6223:scene=33;break;case 0x654c:scene=37;break;case 0x6a5e:scene=38;break;case 0x6ad1:scene=39;break;case 0x6b15:scene=40;break;case 0x6b5d:scene=41;break;default:break;}}if(scene&&scene!=s.fixed_scene){s.fixed_scene=uint8_t(scene);s.fixed_build_valid=s.fixed_valid=0;}
#if GBCLLR_MM5_DX_ASSETS
  if(c.rom_bank==0x19&&pc==0x457e){ram[0x1e9b]=0;i.select_fixed(50);}
  if(c.rom_bank==0x1c){if(pc==0x4c8a)i.select_fixed(42);if(pc==0x4c8d&&s.fixed_scene==42)s.fixed_palette[9]=uint16_t((s.fixed_palette[9]&0xff00)|8);}
  if(c.rom_bank==0x1b){if(pc==0x438e)i.select_fixed(43);if(pc==0x6329)i.select_fixed(49);if(s.fixed_scene==49){if(pc==0x6397){std::copy_n(mm5_dx::absorption_flash_palettes,9,s.fixed_palette.begin());s.fixed_options|=2;}if(pc==0x63bb)s.fixed_palette[8]=mm5_dx::absorption_restore_palette[0];if(pc==0x63a9||pc==0x63d8)s.fixed_options&=uint8_t(~2u);}if(pc==0x51c0)i.select_fixed(45);if(pc==0x55a2)i.select_fixed(46,2,4);if(pc==0x5163)s.fixed_options|=1;if(pc==0x6154)i.select_fixed(47);if(pc==0x5418)i.select_fixed(48);if(pc==0x55ea&&((s.fixed_scene>=44&&s.fixed_scene<=46)||s.fixed_scene==48)){unsigned card=peek(c,0xcb81);if(card&&card<=10)std::copy_n(mm5_dx::credits_cast_palettes+(card-1)*3,3,s.fixed_palette.begin()+10);}}
#endif
 }
 if(c.rom_bank==1&&(pc==0x5142||pc==0x5146)){s.menu=1;s.pending=0;s.world_tick=0;}
 // This service clears the OAM allocator during entry and death/retry too.
 // Scene exits invalidate the world explicitly above; clearing its objects
 // must not revoke a stage-entry frame that0959 has already admitted.
 if(pc==0x37c5){s.building_count=0;s.building_body={};s.pending=0;s.direct_resources.fill(0);s.bullets={};s.weapons={};}
 if(c.rom_bank==6){
  if(pc==0x4514)s.weapons={};
  if(pc==0x48c4||pc==0x48db){unsigned base=pc==0x48c4?0xc200|(cpu[1]&255):cpu[3];int index=Impl::weapon_index(base);if(index>=0)s.weapons[index]={};}
  if(pc==0x4b5b&&(i.wide||i.color||i.no_flicker)&&world(c)){unsigned base=0xc200|(cpu[1]&255);int index=Impl::weapon_index(base);if(i.weak_weapon(c,base)){
   auto& weapon=s.weapons[index];int camera=int(word(c,0xd711)),sample=cpu[2]&255,relative=weapon.valid?int16_t(weapon.world-camera):(sample>=0xe0?sample-256:sample);relative+=int8_t(sample-uint8_t(relative));weapon={uint16_t(camera+relative),1,0};
   // Original4B5B clips X outside[-12,172). Retain its Y branch and every
   // controller timer, extending only this uncharged shot's viewport clip.
   if(i.extended(c)){int x=int16_t(weapon.world),left=std::max(i.left(c),0),right=std::min(i.left(c)+int(i.width),int(peek(c,0xd88c))*16);if(x>=left-12&&x<right+12)cpu[5]=0x4b68;else{cpu[1]|=0x0100;cpu[5]=0x4b78;}++s.adjustments;return;}
  }}
  if(pc==0x4850&&s.world_tick&&world(c)&&s.building_count<s.building.size()){unsigned base=word(c,0xc2bd);int index=Impl::weapon_index(base);if(i.weak_weapon(c,base)&&s.weapons[index].valid){
   // This real producer writes one sprite directly into its allocated OAM
   // slot, bypassing both generic sprite-list services.
   Impl::Piece piece;piece.x=int16_t(s.weapons[index].world-4);piece.y=int16_t(int(peek(c,base+8))-4);piece.attr=peek(c,0xffab);piece.tile_index=0x0d;const uint8_t* pixels=c.vram+0xd0;
#if GBCLLR_MM5_DX_ASSETS
   if(i.color){pixels=mm5_dx::hero_tiles+0xd0;piece.authored=1;}
#endif
   std::copy_n(pixels,16,piece.tile.begin());s.building[s.building_count++]=piece;++s.captures;
  }}
 }
 if(pc==0x3d42||pc==0x3df4)s.bullets={};
 if(pc==0x3db8&&world(c)){int index=Impl::bullet_index(cpu[1]&255);if(index>=0){unsigned camera=word(c,0xd711),sample=cpu[2]&255;int position=int(camera)+(sample>=208?int(sample)-256:int(sample));
   unsigned actor=word(c,0xcc40);bool parent=false;for(unsigned at=cpu[4];at+1<0xffff&&at<cpu[4]+64;at+=2)if(word(c,at)==0x25a8){parent=true;break;}
   if(parent&&actor>=0xc600&&actor<=0xd6c0&&!(actor&63)&&peek(c,actor+0x1b)==c.rom_bank){int offset=int8_t(sample-peek(c,actor+0x0d));if(offset>=-32&&offset<=32)position=int(word(c,actor+0x0a))+offset;}
   s.bullets[index]={uint16_t(position),1,0};
  }}
 if(pc==0x3d3d){int index=Impl::bullet_index(cpu[1]&255);if(index>=0)s.bullets[index].valid=0;}
 if(pc==0x3c46&&world(c)){int index=Impl::bullet_index(cpu[1]&255);if(index>=0&&s.bullets[index].valid){auto& bullet=s.bullets[index];unsigned camera=word(c,0xd711);int relative=int16_t(bullet.world-camera);relative+=int8_t(uint8_t(cpu[2])-uint8_t(relative));bullet.world=uint16_t(int(camera)+relative);
   if(i.extended(c)){int x=int16_t(bullet.world),left=i.left(c);cpu[5]=x>=left-32&&x<left+int(i.width)+32?0x3c50:0x3d3d;++s.adjustments;return;}
  }}
 if(pc==0x3c6c&&i.extended(c)){int index=Impl::bullet_index(cpu[1]&255);if(index>=0&&s.bullets[index].valid){int distance=int16_t(s.bullets[index].world-word(c,0xd70d));if(distance < -128||distance > 128){cpu[5]=0x3cc5;return;}}}
 if(pc==0x11f7&&i.extended(c)){
  // The original weapon service publishes C204/C218 points in the physical
  // camera's coordinate system. Native controller modes/timers are retained.
  // Reject a different world page before the eight-bit rectangle comparison
  // can consume that point or damage an aliased actor. Whole-room attacks use
  // the separate28C0 path and do not enter this point service.
  unsigned cursor=cpu[3],box=0,stack=cpu[4],actor=0;
  if(cursor>=0xc20a&&cursor<=0xc216&&(cursor-0xc20a)%3==0&&word(c,stack)==0x11cc&&word(c,stack+2)==0x28ea){box=0xc204;actor=word(c,stack+4);}
  else if(cursor>=0xc21e&&cursor<=0xc227&&(cursor-0xc21e)%3==0&&word(c,stack)==0x28ea){box=0xc218;actor=word(c,stack+2);}
  if(box&&actor>=0xc600&&actor<=0xd6c0&&!(actor&63)&&peek(c,cursor-1)==peek(c,0xc22a)){
   int camera=int(word(c,0xd711)),actual=camera+i.projected(c,actor),screen=peek(c,actor+0x0d);
   unsigned sample=peek(c,cursor);int point=camera+(sample>=0xe0?int(sample)-256:int(sample)),canonical=point;unsigned base=0xc200+peek(c,cursor-1)-0x0b;int index=Impl::weapon_index(base);
   bool owned=i.weak_weapon(c,base)&&s.weapons[index].valid;
   if(owned)point=int16_t(s.weapons[index].world)+int8_t(sample-peek(c,base+4));
   // Even an actor origin on page0 can have a rectangle crossing256.
   // Known weak-point ownership always admits the full world comparison.
   if(owned||int16_t(actual-camera-screen)||point!=canonical){
    int left=actual+int8_t(peek(c,0xff8a)-screen)-int8_t(peek(c,box+1));
    int right=actual+int8_t(peek(c,0xff8b)-screen)-int8_t(peek(c,box+2));
    if(point<left||point>=right){cpu[3]=uint16_t(cursor+2);cpu[5]=0x11f0;++s.adjustments;return;}
    // True world overlap still uses the original Y comparison, token touch
    // and damage/defeat service even when its eight-bit X interval wraps.
    cpu[3]=uint16_t(cursor+1);cpu[5]=0x11fe;++s.adjustments;return;
   }
  }
 }
 if(pc==0x28a5&&i.extended(c)){unsigned actor=word(c,cpu[4]);if(actor>=0xc600&&actor<=0xd6c0&&!(actor&63)){int distance=int16_t(word(c,actor+0x0a)-word(c,0xd70d));if(distance < -128||distance > 128){cpu[5]=0x28af;return;}}}
 if(pc==0x1fe8&&i.color){unsigned resource=cpu[0]>>8,destination=cpu[2];if(resource&&resource<146&&destination>=0x8000&&destination<0x9000&&!(destination&15))s.direct_resources[(destination-0x8000)/16]=uint8_t(resource);
#if GBCLLR_MM5_DX_ASSETS
  if(s.fixed_scene>=42&&resource<145)for(unsigned slot=0;slot<16;++slot)if(auto pointer=mm5_dx::actor_layouts[resource*16+slot])s.fixed_palette[slot]=pointer;
#endif
 }
 // Weapon art uses the original raw uploader instead of resource1FE8.
 // Salt Water's native packet4:59C5 is exactly resource4B at tile base6A;
 // retain that ownership so its authored cumulative lists/palette can bind.
 if(i.color&&c.rom_bank==4&&pc==0x5406&&cpu[2]==0x86a0){s.direct_resources[0x6a]=peek(c,0xde9b)==5&&cpu[1]==0x0160&&cpu[3]==0x59c8?0x4b:0;}
 if(i.wide&&i.width>160&&world(c)&&peek(c,0xd88c)>10&&pc==0x2d1b){
   // The original controller writes its moved screen byte back to world X.
   // Keep that native movement on the actor's existing sixteen-bit page.
   // The first resumed controller runs before09EF re-arms world_tick. This
   // writeback still belongs to that actor while the pause marker is latched.
   unsigned actor=(cpu[3]&0xff00)|(cpu[1]&255);
   if(actor>=0xc600&&actor<=0xd6c0&&!(actor&63)&&word(c,actor)>=0x4000&&word(c,actor)<0x8000){
    unsigned sample_byte=cpu[2]&255;int relative=int16_t(word(c,actor+0x0a)-word(c,0xd711));
    int moved=relative+int8_t(sample_byte-uint8_t(relative));uint8_t high=uint8_t((moved-int(sample_byte))/256);
    if(high!=uint8_t(cpu[2]>>8)){cpu[2]=uint16_t((unsigned(high)<<8)|sample_byte);++s.adjustments;}
   }
 }
 if(i.extended(c)){
  if(pc==0x0f32&&word(c,cpu[4])==0x0e31){
   if(unsigned actor=i.collision_actor(c,cpu[4]+2,c.rom_bank)){
    int relative=i.projected(c,actor);
    int sample=relative+int8_t(uint8_t(cpu[2])-peek(c,actor+0x0d));
    // The native cache contains the central160 pixels only. Wrapped low
    // bytes from another page must use the original room-table fallback.
    if((sample<0||sample>=160)&&uint8_t(cpu[2])<160){cpu[5]=0x0f50;++s.adjustments;return;}
   }
  }else if(pc==0x0f71&&word(c,cpu[4]+2)==0x0e31){
   if(unsigned actor=i.collision_actor(c,cpu[4]+4,peek(c,cpu[4]+1))){
    unsigned sample_byte=cpu[1]>>8;int relative=i.projected(c,actor);
    int sample=relative+int8_t(sample_byte-peek(c,actor+0x0d));
    uint8_t high=uint8_t((sample-int(sample_byte))/256);
    if(high!=uint8_t(cpu[2]>>8)){cpu[2]=uint16_t((unsigned(high)<<8)|(cpu[2]&255));++s.adjustments;}
   }
  }
 }
 // The existing signed16-bit visibility branch returns carry for a discarded
 // actor. Keep its actual eight-bit draw coordinate, changing only which
 // original branch supplies that result for the expanded world footprint.
 if(pc==0x2659&&i.extended(c)){
  unsigned actor=(cpu[3]&0xff00)|(cpu[1]&255);if(actor>=0xc600&&actor+0x30<0xd700&&!(actor&63)){int x=int(word(c,actor+0x0a)),left=i.left(c);bool visible=x>=left-32&&x<left+int(i.width)+32;cpu[5]=visible?0x266a:0x267a;++s.adjustments;return;}
 }
 // Entry has real room/OAM producers before the first controllable tick.
 // Admit their presentation while retaining physical-clock simulation and
 // excluding stale room descriptors in password/selector/ending screens.
 if(pc==0x0959){s.world_tick=2;s.queued.valid=s.active.valid=0;s.building_count=0;s.queued.count=s.active.count=0;s.fixed_scene=s.fixed_valid=s.fixed_build_valid=s.fixed_options=0;s.weapons={};
#if GBCLLR_MM5_DX_ASSETS
  if(i.color&&peek(c,0xdf0a)==30)i.select_fixed(44);
#endif
 }
 if(pc==0x09ef&&c.rom_bank==1){s.world_tick=1;s.menu=0;
  if(s.selection&&cpu[4]==s.caller[4]){if(s.selection==1){ram[0x1e9b]=s.target;s.selection=2;cpu[4]-=2;hram[cpu[4]-0xff80]=0xef;hram[cpu[4]+1-0xff80]=0x09;cpu[5]=0x043a;return;}std::copy(s.caller.begin(),s.caller.end(),cpu.begin());s.selection=0;s.caller={};return;}
  unsigned fresh=s.pending;s.pending=0;
  if(i.cycle&&!s.selection&&(fresh==1||fresh==2)&&world(c)&&cpu[4]==0xfffe&&peek(c,0xde9e)&&peek(c,0xd702)==1&&!peek(c,0xd81c)&&!peek(c,0xde2c)&&!(peek(c,0xdeaf)&0xfb)){
   unsigned current=peek(c,0xde9b),next=current;if(current<13){unsigned owned=word(c,0xde9c)&0x1fff;do{next=(next+(fresh==1?12:1))%13;}while(next!=current&&!(owned&(1u<<next)));if(next!=current){std::copy(cpu.begin(),cpu.end(),s.caller.begin());s.target=uint8_t(next);s.selection=1;cpu[4]-=2;hram[cpu[4]-0xff80]=0xef;hram[cpu[4]+1-0xff80]=0x09;cpu[5]=0x0446;return;}}}
 }
 if(i.no_flicker||i.wide||i.color){if(pc==0x2de8)i.capture(c,cpu);if(pc==0x31b9)i.capture_list(c,cpu,false);if(pc==0x31fb)i.capture_list(c,cpu,true);if(pc==0x3d11)i.capture_bullet(c,cpu);if(c.rom_bank==1&&pc==0x78d8)i.capture_effect(c,cpu);}
 if(pc==0x6bc5&&c.rom_bank==1&&world(c))s.building_body={int16_t(word(c,0xd711)+peek(c,0xd722)),int16_t(peek(c,0xd725)+2),uint8_t(peek(c,0xff8a)/2),uint8_t(peek(c,0xd703)!=0),peek(c,0xffab),1};
 if(pc==0x197e&&s.world_tick&&!s.menu&&world(c)){auto& frame=s.queued;frame.count=s.building_count;std::copy_n(s.building.begin(),frame.count,frame.pieces.begin());s.building_count=0;frame.fixed_count=0;
  // The original reserves its first ten OAM entries for the player's fixed
  // body producer. They are separate from the general allocated grid lists.
  // Death reuses these hardware slots for the effects captured above and
  // other allocated actors. They no longer belong to the fixed live body.
  bool dead=!peek(c,0xde9e)&&peek(c,0xd702)==3;
  if(!dead)for(unsigned n=0;n<10;++n){auto o=c.wram+n*4;if(!o[0]||o[0]>=160)continue;auto& p=frame.fixed[frame.fixed_count++];p.x=int16_t(int(o[1])-8+word(c,0xd711));p.y=int16_t(int(o[0])-16);p.attr=o[3];p.tile_index=o[2];p.authored=0;std::copy_n(c.vram+o[2]*16,16,p.tile.begin());}
  frame.body=s.building_body;s.building_body={};std::copy_n(c.wram,0x2000,frame.ram.begin());std::copy_n(c.vram,0x2000,frame.vram.begin());frame.lcdc=c.ppu->lcdc;frame.bgp=c.ppu->bgp;frame.obp0=c.ppu->obp0;frame.obp1=c.ppu->obp1;frame.wx=c.ppu->wx;frame.wy=c.ppu->wy;frame.valid=1;}
 if((s.menu||!world(c))&&!s.selection)s.pending=0;
}
void Mm5NativeMods::complete_frame(ModContext& c,bool cleared){auto& s=impl_->s;++s.frames;
 // Room metadata may become ready after line0 during retry. Publish that
 // first completed physical picture instead of inserting a blank Fit frame.
 if(!cleared&&!s.active.valid&&s.world_tick==2&&!s.menu&&world(c))impl_->capture_entry(c);
 if(cleared){s.fixed_valid=s.fixed_build_valid=0;s.display.valid=0;}else if(s.fixed_build_valid){s.fixed_display=s.fixed_build;s.fixed_valid=1;}if(!cleared&&s.active.valid)s.display=s.active;if(impl_->color&&s.menu&&!cleared){std::copy_n(c.wram,0x2000,s.menu_ram.begin());std::copy_n(c.vram,0x2000,s.menu_vram.begin());s.menu_lcdc=c.ppu->lcdc;s.menu_wx=c.ppu->wx;s.menu_wy=c.ppu->wy;s.menu_valid=1;}else if(cleared)s.menu_valid=0;if(cleared||!world(c)){s.queued.valid=s.active.valid=0;s.building_count=0;s.pending=0;}}
void Mm5NativeMods::scanline(ModContext& c,uint8_t line){auto& s=impl_->s;
#if GBCLLR_MM5_DX_ASSETS
 impl_->fixed_row(c,line);
#endif
 if(line)return;
 // Entry uploads a complete room before the first controllable pose commits.
 // Latch that physical picture at line0 so its fade-in is already colored and
 // uses the selected viewport, rather than waiting for the main-loop publish.
 if(s.world_tick==2&&!s.menu&&world(c))impl_->capture_entry(c);
 if(!s.active.valid)return;std::copy_n(c.vram,0x2000,s.active.vram.begin());auto texture=[&](Impl::Piece& p){if(!p.authored)std::copy_n(c.vram+p.tile_index*16,16,p.tile.begin());};for(unsigned n=0;n<s.active.count;++n)texture(s.active.pieces[n]);for(unsigned n=0;n<s.active.fixed_count;++n)texture(s.active.fixed[n]);}
uint32_t Mm5NativeMods::cpu_budget(const ModContext& c,bool cleared)const{return (impl_->smooth||impl_->color)&&!cleared&&!impl_->s.menu&&impl_->s.world_tick==1&&world(c)&&!peek(c,0xde2c)&&!peek(c,0xd81c)?280896:0;}
ModView Mm5NativeMods::render(ModContext& c,std::span<const uint32_t> native){
 auto& i=*impl_;auto& s=i.s.display;
 // Guest room metadata can already describe the next reload while the PPU
 // still owns the preceding complete picture. Use its paired scene data.
 ModPpu owned_ppu=*c.ppu;owned_ppu.lcdc=s.lcdc;ModContext owned=c;owned.wram=s.ram.data();owned.ppu=&owned_ppu;
 bool display_world=s.valid&&world(owned);
 if(i.width==160&&!i.no_flicker&&!i.color)return {160,false,native};
#if GBCLLR_MM5_DX_ASSETS
 if(i.color&&i.s.menu&&i.s.menu_valid&&(i.s.menu_lcdc&32)&&i.s.menu_wy==0&&i.s.menu_wx==7){auto menu=i.menu_view(native);return megaman_pause_canvas(menu,i.width,i.pause_canvas);}
 if(i.color&&(!i.s.world_tick||!display_world)&&!i.s.menu&&i.s.fixed_valid)return {160,false,i.s.fixed_display};
#endif
 if(!display_world||(!i.s.world_tick&&!i.s.menu)||native.size()!=160*144){
  // LCD restart/upload frames have no completed picture. Preserve the
  // selected canvas through that blank interval instead of falling back to
  // the physical monochrome framebuffer (or exposing a partial next frame).
  if(i.color&&(i.s.menu||i.s.world_tick||(!i.s.fixed_scene&&world(c)))){i.pixels.assign(size_t(i.width)*144,0xff000000u);return {i.width,i.width>160,i.pixels};}
  return {160,false,native};
 }
 // Camera, room tables, palettes and tiles belong to the same completed
 // native update. A partially loaded live room must never be mixed with the
 // retained scene while the physical LCD displays that completed frame.
 ModPpu ppu=*c.ppu;ppu.lcdc=s.lcdc;ppu.bgp=s.bgp;ppu.obp0=s.obp0;ppu.obp1=s.obp1;ppu.wx=s.wx;ppu.wy=s.wy;
 ModContext completed=c;completed.wram=s.ram.data();completed.vram=s.vram.data();completed.ppu=&ppu;
 const unsigned width=i.width;int left=i.left(completed),camera=int16_t(word(completed,0xd711)),bank=peek(completed,0xdf01),columns=peek(completed,0xd88c),rows=peek(completed,0xd88d);unsigned table=word(completed,0xd890);
 i.pixels.assign(width*144,0xff000000u);i.background.assign(width*144,0);
 auto color=[&](unsigned raw,unsigned pal){return gray[(pal>>(raw*2))&3];};
#if GBCLLR_MM5_DX_ASSETS
 unsigned scene=peek(completed,0xdf0a);if(scene==4&&!peek(completed,0xde8a))scene=26;if(scene==13&&peek(completed,0xde8a)==12)scene=19;
 std::array<uint16_t,16> palette{};std::copy_n(mm5_dx::scene_layouts+scene*16,16,palette.begin());
 for(unsigned at=0xc3c6;at<0xc3e0;at+=2){unsigned resource=peek(completed,at);if(!resource)break;if(resource>=145)continue;for(unsigned n=0;n<16;++n)if(auto p=mm5_dx::actor_layouts[resource*16+n])palette[n]=p;}
 if(unsigned weapon=peek(completed,0xde9b);weapon&&weapon<13){palette[1]=uint16_t(0x4400+weapon*8);palette[9]=uint16_t(0x5300+weapon*8);}
 unsigned fade=mm5_dx::fade_banks[peek(completed,0xde96)];
 unsigned graphics_stage=peek(completed,0xdf0a);if(graphics_stage==4&&peek(completed,0xde8a))graphics_stage=17;
 const uint8_t* metatiles=nullptr;unsigned metatile_count=0;uint32_t metatile_key=uint32_t(peek(completed,0xdf0a)*65536+table);
 for(unsigned n=0;n<std::size(mm5_dx::metatile_keys);++n)if(mm5_dx::metatile_keys[n]==metatile_key){metatiles=mm5_dx::metatile_data+mm5_dx::metatile_offsets[n];metatile_count=mm5_dx::metatile_counts[n];break;}
 const uint8_t* room_original=nullptr;const uint8_t* room_authored=nullptr;
 uint64_t room_key=(uint64_t(peek(completed,0xdf0a))<<32)|(uint64_t(table)<<16)|word(completed,0xd7ff);
 auto room=std::lower_bound(std::begin(mm5_dx::room_keys),std::end(mm5_dx::room_keys),room_key);
 if(room!=std::end(mm5_dx::room_keys)&&*room==room_key){unsigned index=unsigned(room-std::begin(mm5_dx::room_keys));if(columns==mm5_dx::room_columns[index]&&rows==mm5_dx::room_rows[index]){room_original=mm5_dx::room_original+mm5_dx::room_offsets[index];room_authored=mm5_dx::room_authored+mm5_dx::room_offsets[index];}}
 std::array<const uint8_t*,384> authored_tiles{};
 if(i.color)for(unsigned tile=0;tile<128;++tile){unsigned entry=mm5_dx::stage_tiles[graphics_stage*128+tile];if(entry&&std::equal(s.vram.begin()+0x1000+tile*16,s.vram.begin()+0x1010+tile*16,mm5_dx::tile_original+(entry-1)*16))authored_tiles[256+tile]=mm5_dx::tile_authored+(entry-1)*16;}
 auto dx_color=[&](unsigned raw,unsigned pal){unsigned address=palette[pal&15]+(pal>=8?raw-1:raw)*2;if(address<0x4400||address>=0x6400||fade<0x22||fade>0x2d)return gray[raw&3];unsigned rgb=mm5_dx::palette_words[(fade-0x22)*0x1000+(address-0x4400)/2];return 0xff000000u|((rgb&31)*255/31<<16)|(((rgb>>5)&31)*255/31<<8)|(((rgb>>10)&31)*255/31);};
 std::array<const uint8_t*,2> hud_digits{};
 if(i.color)for(unsigned glyph=0;glyph<2;++glyph)for(unsigned n=0;n<12;++n)if(std::equal(s.vram.begin()+0x16e0+glyph*16,s.vram.begin()+0x16f0+glyph*16,mm5_dx::hud_digit_original+n*16)){hud_digits[glyph]=mm5_dx::hud_digits+n*16;break;}
#endif
 for(unsigned y=0;y<144;++y)for(unsigned x=0;x<width;++x){int world_x=left+int(x),sy=(int(y)+peek(completed,0xd7d2))&255;auto at=y*width+x;
  if(world_x<0||world_x>=columns*16||sy>=rows*16)continue;
  unsigned row=word(completed,0xd7eb+2*(sy/16)),block=peek(completed,row+world_x/16,bank),subtile=((world_x&8)?2:0)+((sy&8)?1:0),tile=peek(completed,table+4*block+subtile,bank);
  // The original dialogue service replaces cached BG cells with its dynamic
  // DC..FF glyph tiles. These are physical-camera cells, not room metatiles;
  // retain them only inside the cache's actual160-pixel ownership interval.
  int physical_x=world_x-camera;
  if(physical_x>=0&&physical_x<160){unsigned map=s.lcdc&8?0x1c00:0x1800,cached=s.vram[map+(sy/8)*32+(unsigned(world_x)&255)/8];if(cached>=0xdc)tile=cached;}
  unsigned texture=(s.lcdc&16)?tile*16:unsigned(0x1000+int8_t(tile)*16),line=(sy&7)*2,bit=7-(world_x&7);const uint8_t* pixels=s.vram.data()+texture;
#if GBCLLR_MM5_DX_ASSETS
  unsigned attributes=0;
  if(i.color){bool unchanged=authored_tiles[texture/16];if(unchanged&&metatiles){unsigned visual_block=block,cell=(sy/16)*unsigned(columns)+unsigned(world_x)/16;if(room_original&&room_original[cell]==block)visual_block=room_authored[cell];if(visual_block<metatile_count)tile=metatiles[visual_block*4+subtile];}attributes=mm5_dx::bg_attributes[scene*256+tile];
   if(unchanged&&tile<128){unsigned entry=mm5_dx::stage_authored_tiles[graphics_stage*128+tile];if(entry)pixels=mm5_dx::tile_authored+(entry-1)*16;}
   else if(authored_tiles[texture/16])pixels=authored_tiles[texture/16];
   if(attributes&8)pixels=mm5_dx::shared_tiles+((s.lcdc&16)?tile*16:unsigned(0x1000+int8_t(tile)*16));
   if(attributes&32)bit=world_x&7;if(attributes&64)line=(7-(sy&7))*2;
  }
#endif
  unsigned raw=((pixels[line]>>bit)&1)|(((pixels[line+1]>>bit)&1)<<1);i.background[at]=uint8_t(raw);i.pixels[at]=color(raw,s.bgp);
#if GBCLLR_MM5_DX_ASSETS
  if(i.color){i.background[at]|=uint8_t(attributes&128);i.pixels[at]=dx_color(raw,attributes&7);}
#endif
 }
 i.framing_background=i.pixels;
 // Complete logical emissions replace hardware OAM; the latter may wrap or
 // omit entries after its native forty-sprite/ten-per-line allocation.
 auto piece=[&](const Impl::Piece& p){for(unsigned y=0;y<8;++y)for(unsigned x=0;x<8;++x){int dx=p.x-left+int(x),dy=p.y+int(y);if(dx<0||dy<0||dx>=int(width)||dy>=144)continue;unsigned tx=p.attr&32?7-x:x,ty=p.attr&64?7-y:y,bit=7-tx,raw=((p.tile[ty*2]>>bit)&1)|(((p.tile[ty*2+1]>>bit)&1)<<1);if(!raw)continue;unsigned at=unsigned(dy)*width+unsigned(dx);if(((p.attr|i.background[at])&128)&&(i.background[at]&3))continue;i.pixels[at]=color(raw,p.attr&16?s.obp1:s.obp0);
#if GBCLLR_MM5_DX_ASSETS
  if(i.color)i.pixels[at]=dx_color(raw,8+(p.attr&7));
#endif
 }};
 for(unsigned n=s.count;n>0;--n)piece(s.pieces[n-1]);
#if GBCLLR_MM5_DX_ASSETS
 if(i.color&&s.body.valid&&s.body.frame<47&&s.fixed_count){int x=s.body.x+(s.body.facing?-7:0),y=s.body.y;std::array<Impl::Piece,10> authored{};unsigned count=mm5_dx::body_counts[s.body.frame];
  for(unsigned n=0;n<count;++n){const auto* p=mm5_dx::body_pieces+s.body.frame*40+n*4;y+=int8_t(p[0]);x+=(s.body.facing?-int(int8_t(p[1])):int(int8_t(p[1])));auto& a=authored[n];a.x=int16_t(x);a.y=int16_t(y);a.attr=uint8_t(p[3]^s.body.attr^(s.body.facing?32:0));unsigned tile=p[2];const uint8_t* pixels=(a.attr&8)?mm5_dx::shared_tiles+tile*16:tile<0x6a?mm5_dx::hero_tiles+tile*16:s.vram.data()+tile*16;std::copy_n(pixels,16,a.tile.begin());}
  for(unsigned n=count;n>0;--n)piece(authored[n-1]);
 }else
#endif
 for(unsigned n=s.fixed_count;n>0;--n)piece(s.fixed[n-1]);
 // The original HUD/window stays in screen space and is centered over the
 // wider world. Its native pixels retain cartridge-specific text and fades.
 if((s.lcdc&32)&&s.wy<144){int origin=(int(width)-160)/2;for(unsigned y=s.wy;y<144;++y)for(unsigned x=0;x<160;++x){int dx=origin+int(x);if(dx>=0&&dx<int(width)){
#if GBCLLR_MM5_DX_ASSETS
   if(i.color){unsigned sx=unsigned(int(x)-int(s.wx)+7);if(int(x)<int(s.wx)-7)continue;unsigned sy=y-s.wy,map=s.lcdc&64?0x1c00:0x1800,tile=s.vram[map+(sy/8)*32+sx/8],texture=s.lcdc&16?tile*16:unsigned(0x1000+int8_t(tile)*16),bit=7-(sx&7),row=(sy&7)*2;const auto* pixels=s.vram.data()+texture;
    if(authored_tiles[texture/16])pixels=authored_tiles[texture/16];
    // DX's original568D/B=0 bar producer writes blank tile0 instead of3.
    // Bind that presentation result without changing the native HUD buffer.
    if(sy<16&&tile==3){i.pixels[y*width+unsigned(dx)]=0xff000000u;continue;}
    if(texture>=0x16e0&&texture<0x1700&&hud_digits[(texture-0x16e0)/16])pixels=hud_digits[(texture-0x16e0)/16];
    if(texture>=0x1700&&texture<0x1740)pixels=mm5_dx::menu_icons+std::min(unsigned(peek(completed,0xde9b)),13u)*64+texture-0x1700;
    if(texture>=0x1740&&texture<0x1800)pixels=mm5_dx::hud_tiles+texture-0x1740;
    unsigned raw=((pixels[row]>>bit)&1)|(((pixels[row+1]>>bit)&1)<<1);i.pixels[y*width+unsigned(dx)]=dx_color(raw,sy<16?mm5_dx::menu_attributes[(sy/8)*20+sx/8]&7:6);}
   else
#endif
   i.pixels[y*width+unsigned(dx)]=native[y*160+x];
  }}}
 ModView view;view.width=width;view.expanded=width>160;view.pixels=i.pixels;view.camera_x=camera;view.left=left;view.bound_right=columns*16;view.sprites=s.count+s.fixed_count;view.captures=i.s.captures;view.activation_adjustments=i.s.adjustments;view.framing_width=std::clamp(unsigned(columns*16),160u,width);view.framing_background=i.framing_background;return view;
}
std::vector<uint8_t> Mm5NativeMods::snapshot()const{std::vector<uint8_t> bytes(sizeof impl_->s);std::memcpy(bytes.data(),&impl_->s,bytes.size());return bytes;}
void Mm5NativeMods::restore(std::span<const uint8_t> bytes){
 if(bytes.size()!=sizeof impl_->s)throw std::runtime_error("Invalid Mega Man V native state size");Impl::State staged;std::memcpy(&staged,bytes.data(),sizeof staged);
 auto valid_frame=[](const Impl::Frame& frame){return frame.valid<=1&&frame.count<=frame.pieces.size()&&frame.fixed_count<=frame.fixed.size()&&frame.body.valid<=1&&frame.body.facing<=1&&(!frame.body.valid||frame.body.frame<47);};
 if(std::any_of(staged.bullets.begin(),staged.bullets.end(),[](const Impl::Bullet& bullet){return bullet.valid>1;}))throw std::runtime_error("Invalid Mega Man V projectile state");
 if(std::any_of(staged.weapons.begin(),staged.weapons.end(),[](const Impl::Bullet& weapon){return weapon.valid>1;}))throw std::runtime_error("Invalid Mega Man V weapon state");
 if(staged.schema!=1||staged.fixed_scene>=51||staged.fixed_valid>1||staged.fixed_build_valid>1||staged.fixed_options>3||staged.width<160||staged.width>4096||staged.building_count>staged.building.size()||staged.selection>2||staged.target>12||staged.previous>3||staged.pending>3||staged.world_tick>2||staged.menu>1||staged.menu_valid>1||staged.resize_scan>2||staged.building_body.valid>1||staged.building_body.facing>1||(staged.building_body.valid&&staged.building_body.frame>=47)||!valid_frame(staged.queued)||!valid_frame(staged.active)||!valid_frame(staged.display))throw std::runtime_error("Invalid Mega Man V native state");impl_->s=staged;impl_->width=staged.width;
}
}
