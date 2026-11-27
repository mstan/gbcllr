#include "gen1_native_mods.hpp"
#include "native_mods.hpp"
#include <algorithm>
#include <array>
#include <cstring>
#include <cstdlib>
#include <stdexcept>
#include <type_traits>
#if GBCLLR_GEN1_EXTENDED_ASSETS
#include "gen1_extended_data.hpp"
#endif
namespace gbcllr {
namespace {
constexpr std::string_view identities[]={"5ca7ba01642a3b27b0cc0b5349b52792795b62d3ed977e98a09390659af96b7b","2a951313c2640e8c2cb21f25d1db019ae6245d9c7121f754fa61afd7bee6452d","8cbaa499397e4f1a679c992ea9382a2dd7942ab398b48c19829c2d9529de47bf"};
// Red/Blue ownbd77 symbols agree. Yellow own2dcc bindings/table format differ.
// Source contracts: pret/pokeredfbcf7d0e and pret/pokeyellowb452f2ab,
// home/overworld.asm,engine/gfx/sprite_oam.asm,data/sprites/facings.asm.
struct Bindings {uint16_t map,anchor,meta_y,meta_x,tileset,height,width,battle,bg_anchor,bank,blocks,border,caller,flags;};
constexpr Bindings rb{0xd35e,0xd35f,0xd363,0xd364,0xd367,0xd368,0xd369,0xd057,0xd526,0xd52b,0xd52c,0xd3ad,0x4b0f,0xcfcb};
constexpr Bindings yellow{0xd35d,0xd35e,0xd362,0xd363,0xd366,0xd367,0xd368,0xd056,0xd525,0xd52a,0xd52b,0xd3ac,0x499b,0xcfca};
struct DexCount {unsigned bank;uint16_t site,base;};
// Original main/save menu,Oak/poster/aide/rating/designer and Dex count callers.
constexpr DexCount dex_counts[]={{1,0x5e43,0xd2f7},{7,0x5250,0xd2f7},{7,0x6968,0xd2f7},
 {16,0x4143,0xd2f7},{17,0x4176,0xd2f7},{18,0x47b3,0xd2f7},{22,0x5044,0xd2f7},
 {16,0x412f,0xd30a},{17,0x4169,0xd30a}};
uint8_t ram(const ModContext& c,unsigned a){return c.wram[a-0xc000];}
unsigned word(const ModContext& c,unsigned a){return ram(c,a)|(unsigned(ram(c,a+1))<<8);}
uint8_t rom(const ModContext& c,unsigned bank,unsigned a){return a>=0x4000&&a<0x8000&&bank<c.rom_size/0x4000?c.rom[bank*0x4000+a-0x4000]:0;}
int floor_div(int a,int b){return a>=0?a/b:-((-a+b-1)/b);}
struct Sprite {int x,y;uint8_t tile,attr;};
struct State {uint32_t valid,captures,callers;int camera_x,camera_y,count;uint16_t blocks_address,map_width,map_height,stride;uint8_t blocks_bank,border,lcdc,scx,scy;std::array<uint8_t,1300> blocks;std::array<uint8_t,0x2000> vram;std::array<uint32_t,4> bg;std::array<uint32_t,8> obj;std::array<Sprite,160> sprites;std::array<std::array<uint32_t,12>,144> row_palettes;};
}
struct Gen1NativeMods::Impl {
 unsigned variant=0;const Bindings& b;int requested=-1;State s{},presented{};bool has_presented=false;bool extended=false,encounters=false,color=false;
 std::array<int,2> host_size{256,144}; // Derived presentation geometry, never simulation state.
 uint32_t decoder_species=0,decoder_base=0;
 std::array<uint8_t,64> dex_flags{}; // Two National Dex bitfields; lower151 stay in original WRAM.
 std::vector<uint32_t> pixels;std::vector<uint8_t> opaque;
 Impl(std::string_view identity,const NativeFeature& f,bool cgb):b(identity==identities[2]?yellow:rb){
  auto it=std::find(std::begin(identities),std::end(identities),identity);
  extended=f.plugin=="gbcllr.gen1.extended";
  encounters=f.expanded_encounters;
  bool profile=f.plugin=="gbcllr.gen1.adaptive-view"&&f.asset_input.empty();
#if GBCLLR_GEN1_EXTENDED_ASSETS
  if(extended)profile=it!=std::end(identities)&&it!=std::begin(identities)+2&&f.asset_input==gen1_extended_data::asset_identity;
#endif
  if(it==std::end(identities)||(cgb&&it!=std::begin(identities)+2)||!profile||f.abi!=1||f.colorization||(encounters&&!extended)||(f.view_width!=-1&&(f.view_width<160||f.view_width>4096)))throw std::runtime_error("Unqualified Gen1 ROM/model/native profile");
  color=cgb;
  variant=unsigned(it-std::begin(identities));requested=f.view_width;if(requested>=0)host_size[0]=requested;std::memset(&s,0,sizeof s);
 }
 static bool valid_dex_flags(std::span<const uint8_t,64> flags){
  for(unsigned base:{0u,32u}){
   if(std::any_of(flags.begin()+base,flags.begin()+base+18,[](uint8_t v){return v!=0;})||
      (flags[base+18]&0x7f)||(flags[base+31]&0xf8))return false;
  }
  return true;
 }
 uint8_t dex_value(unsigned base,unsigned address,uint8_t value)const{
  unsigned index=address-base,at=(base==0xd30a?32:0)+index;
  if(index<18)return value;
  return index==18?uint8_t((value&0x7f)|dex_flags[at]):dex_flags[at];
 }
 int dex_byte(const ModContext& c,uint16_t address)const{
  // Exact stock bank3 FlagAction's saved original HL, index and shifted mask.
  // Neighboring bitfields and original151 species retain their original bus.
  if(!extended||!c.wram||c.rom_bank!=3||c.sp<0xc000||c.sp>0xdffa||
     c.c<151||c.c>250||c.d!=(1u<<(c.c&7))||c.pc!=c.instruction_pc+1)return -1;
  unsigned base=word(c,c.sp+4),index=c.c/8;
  if((base!=0xd2f7&&base!=0xd30a)||c.hl!=base+index||address!=c.hl)return -1;
  return int((base==0xd30a?32:0)+index);
 }
#if GBCLLR_GEN1_EXTENDED_ASSETS
 static constexpr size_t dex_save=0x1f00; // Bank0 BF00..BF4B; HOF endsB858, banks1..3 unchanged.
 static constexpr std::array<uint8_t,8> dex_magic{'G','B','C','D','E','X','0','1'};
 static uint32_t dex_checksum(std::span<const uint8_t,64> flags){uint32_t h=2166136261u;for(auto v:flags)h=(h^v)*16777619u;return h;}
 void load_dex(const ModContext& c){
  std::array<uint8_t,64> next{};
  if(!c.eram||c.eram_size!=0x8000)throw std::runtime_error("Wrong native Extended save allocation");
  const auto* slot=c.eram+dex_save;
  if(std::equal(dex_magic.begin(),dex_magic.end(),slot)){
   std::copy_n(slot+8,64,next.begin());uint32_t saved=0;
   for(unsigned i=0;i<4;++i)saved|=uint32_t(slot[72+i])<<(8*i);
   if(!valid_dex_flags(next)||saved!=dex_checksum(next))throw std::runtime_error("Corrupt native Extended Pokedex save");
  }
  dex_flags=next;
 }
 void save_dex(ModContext& c,uint8_t value){
  if(!c.eram||c.eram_size!=0x8000||c.ram_bank!=1)return;
  uint8_t sum=0;for(unsigned a=0x598;a<0x1523;++a)sum=uint8_t(sum+c.eram[0x2000+a]);
  if(uint8_t(~sum)!=value)return;
  auto* slot=const_cast<uint8_t*>(c.eram)+dex_save;
  std::copy(dex_magic.begin(),dex_magic.end(),slot);std::copy(dex_flags.begin(),dex_flags.end(),slot+8);
  auto hash=dex_checksum(dex_flags);for(unsigned i=0;i<4;++i)slot[72+i]=uint8_t(hash>>(8*i));
 }
#endif
 void extended_return(ModContext& c,uint16_t address){
#if GBCLLR_GEN1_EXTENDED_ASSETS
  // Complete only the data result of these exact original home functions.
  // Every original instruction, stack read, bank change and register restore
  // still executes. No donor opcode or donor ROM pointer is executed.
  if(!extended||!c.wram||!c.rom||address<0xc000||address>=0xe000||
     c.pc!=uint16_t(c.instruction_pc+1)||c.instruction_pc>=c.rom_size||
     (c.instruction_pc<0x4000?c.rom[c.instruction_pc]:rom(c,c.rom_bank,c.instruction_pc))!=0xc9)return;
  if(c.sp!=UINT16_MAX&&c.rom_bank==11&&c.instruction_pc==0x7e7c&&address<=0xdffa&&
     word(c,address)==0x3e8d&&ram(c,address+3)==15&&word(c,address+4)==0x711e&&
     c.eram&&c.eram_size==0x8000&&!c.ram_bank&&ram(c,0xd0a3)==48&&ram(c,0xd0a4)==48){
   // Exact original LoadMonBackPic -> Predef ScaleSpriteByTwo return. Its
   // stock32px scaler has completed with all CPU/bus/IRQ/clock effects intact.
   // Supply native48px centered planes before the original merge/VRAM caller;
   // trainer backs and all original species retain the complete stock path.
   unsigned species=ram(c,0xcf91);
   if(species==ram(c,0xcfd9))for(const auto& mon:gen1_extended_data::mons)if(mon.species==species){
    auto* output=const_cast<uint8_t*>(c.eram);
    for(unsigned i=0;i<392;++i){output[i]=mon.back.expected[i*2];output[392+i]=mon.back.expected[i*2+1];}
    return;
   }
  }
  if(c.instruction_pc==0x2519){decoder_species=decoder_base=0;return;}
  if(c.instruction_pc==0x00bd&&c.rom_bank==14&&c.sp!=UINT16_MAX&&address<=0xdffe&&
     word(c,address)==0x6e57&&!c.c&&c.d==0xd0&&c.e==0xd4){
   // EvolutionAfterBattle's original direct BaseStats copy (not GetMonHeader).
   // Preserve the original copy/return and provide its native data result.
   for(const auto& mon:gen1_extended_data::mons)if(mon.species==ram(c,0xd0b5)&&c.hl==0x43de + mon.dex*28){
    std::copy(mon.header.begin(),mon.header.end(),const_cast<uint8_t*>(c.wram)+0x10b8);return;
   }
  }
  if(c.instruction_pc==0x00bd&&c.rom_bank==14&&address<=0xdffa&&!c.c&&c.hram&&c.hl>=0x4000&&((c.hl-0x4000)%6)==0){
   // All eight original move-data consumers use the same six-byte codec.
   // Match the immutable caller continuation, original bank and complete DE
   // output extent; source overlap with BaseStats must never be sufficient.
   bool far=word(c,address)==0x00ae;unsigned caller=word(c,address+(far?4:0)),bank=far?ram(c,address+3):c.rom_bank,target=0;
   if(far){
    if(bank==15&&(caller==0x6aee||caller==0x633b))target=c.hram[0x73]?0xcfcc:0xcfd2;
    else if((bank==1&&caller==0x6ea7)||(bank==14&&caller==0x7042))target=0xcee9;
    else if(bank==3&&(caller==0x7491||caller==0x76dc||caller==0x66b7))target=0xcd6d;
   }else if(bank==14&&caller==0x5897)target=0xcfcc;
   unsigned id=(c.hl-0x4000)/6;
   if(target&&((unsigned(c.d)<<8)|c.e)==target+6)for(const auto& move:gen1_extended_data::moves)if(move.id==id){std::copy(move.data.begin(),move.data.end(),const_cast<uint8_t*>(c.wram)+target-0xc000);return;}
  }
  bool header=c.instruction_pc==0x15b3,name=c.instruction_pc==0x2fce;
  if(!header&&!name)return;
  unsigned species=ram(c,header?0xd0b5:0xd11e);
  auto it=std::find_if(gen1_extended_data::mons.begin(),gen1_extended_data::mons.end(),[&](const auto& mon){return mon.species==species;});
  if(it==gen1_extended_data::mons.end())return;
  auto* output=const_cast<uint8_t*>(c.wram);
  if(header){
   std::copy(it->header.begin(),it->header.end(),output+0x10b8);
  }else std::copy(it->name.begin(),it->name.end(),output+0xd6d);
#else
  (void)c;(void)address;
#endif
 }
 uint8_t extended_data(const ModContext& c,uint16_t address,uint8_t value) {
#if GBCLLR_GEN1_EXTENDED_ASSETS
  if(!extended||!c.wram)return value;
  if(c.sp!=UINT16_MAX){
   // Native data providers at qualified stock CPU callers. The cartridge
   // backing bytes are const and stay untouched; no guest patch/code runs.
   // InitOpponent has a nonzero original wCurOpponent. The successful random
   // wild path has zero, so it can distinguish added IDs200+ from trainers
   // without a new selector,flag or snapshot field. The original SUB/branch
   // still executes; its unused temporary result is replaced by InitWildBattle.
   if(c.rom_bank==15&&c.instruction_pc==0x6f53&&c.pc==0x6f55&&address==0x6f54&&
      value==200&&!ram(c,0xd059)&&ram(c,0xcfd8)>=200)
    for(const auto& mon:gen1_extended_data::mons)if(mon.species==ram(c,0xcfd8))return 255;
   if(c.rom_bank==28&&c.instruction_pc==0x58ff&&c.pc==0x5900){
    unsigned dex=ram(c,0xd11e);
    for(const auto& mon:gen1_extended_data::mons)if(mon.dex==dex&&address==0x590d+(dex-1)/2)
     return dex&1?uint8_t((value&15)|(mon.icon<<4)):uint8_t((value&0xf0)|mon.icon);
   }
   if(encounters&&c.rom_bank==4&&c.instruction_pc==0x78ec&&c.pc==0x78ed&&c.hl==address&&
      c.hram&&!(c.hram[0x53]&1)){
    unsigned map=ram(c,0xd35e);
    if(map<gen1_extended_data::habitat_maps.size())for(unsigned water=0;water<2;++water){
     unsigned index=gen1_extended_data::habitat_maps[map][water],base=water?0xd8a5:0xd888;
     if(index>=gen1_extended_data::habitats.size()||(address!=base+17&&address!=base+19))continue;
     const auto& habitat=gen1_extended_data::habitats[index];unsigned species=habitat.species[(address-base-17)/2];
     // Reject stale/foreign RAM tables (including the original shore glitch).
     // The original species remains possible on the other existing RNG parity.
     if(!species||ram(c,base-1)!=rom(c,3,habitat.address-1))continue;
     bool exact=true;for(unsigned i=0;i<20;++i)if(ram(c,base+i)!=rom(c,3,habitat.address+i)){exact=false;break;}
     if(exact)return uint8_t(species);
    }
   }
   if(encounters&&c.rom_bank==3&&c.instruction_pc==0x69f6&&c.pc==0x69f7&&c.hl==address){
    unsigned species=ram(c,0xd11e);
    // Original area search asks about one species. Its rare slot may contain
    // either original or added species; expose the added result only for that
    // exact query, retaining every original species' ordinary area results.
    for(const auto& habitat:gen1_extended_data::habitats)for(unsigned i=0;i<2;++i)
     if(species&&habitat.species[i]==species&&address==habitat.address+17+2*i)return uint8_t(species);
   }
   if(c.rom_bank==14){
    bool pointer=(c.instruction_pc==0x6d5d||c.instruction_pc==0x6d5e||
                  c.instruction_pc==0x6f70||c.instruction_pc==0x6f71||
                  c.instruction_pc==0x6fcc||c.instruction_pc==0x6fcd);
    bool stone=c.instruction_pc==0x00b5&&c.d==0xcd&&c.e>=0x6d&&c.e<0x6f&&
               c.sp>=0xc000&&c.sp<=0xdff8&&word(c,c.sp)==0x00ae&&
               ram(c,c.sp+3)==4&&word(c,c.sp+4)==0x6dcb;
    if((pointer||stone)&&c.pc==c.instruction_pc+1&&address>=0x705c&&address<0x7258){
     unsigned species=(address-0x705c)/2+1;
     for(const auto& mon:gen1_extended_data::mons)if(mon.species==species){unsigned p=0x4000+mon.evo_offset;return uint8_t((address&1)?p>>8:p);}
    }
    bool evolution=c.instruction_pc>=0x6d71&&c.instruction_pc<0x6ed9;
    bool learning=c.instruction_pc>=0x6f73&&c.instruction_pc<0x704a;
    uint8_t opcode=rom(c,14,c.instruction_pc);
    bool data=(evolution||learning)&&(opcode==0x2a||opcode==0x7e||opcode==0xbe);
    bool stone_data=c.instruction_pc==0x00b5&&c.d==0xcd&&c.e>=0x6d&&c.e<0x7a&&
                    c.sp>=0xc000&&c.sp<=0xdff8&&word(c,c.sp)==0x00ae&&
                    ram(c,c.sp+3)==4&&word(c,c.sp+4)==0x6ddc;
    if((data||stone_data)&&c.pc==c.instruction_pc+1&&address>=0x4000&&address-0x4000<gen1_extended_data::evolution_bytes.size())
     return gen1_extended_data::evolution_bytes[address-0x4000];
   }
   if(c.rom_bank==14&&(c.instruction_pc==0x13e8||c.instruction_pc==0x13ea||c.instruction_pc==0x13ee)&&
      c.pc==c.instruction_pc+1&&address>=0x5446){
    unsigned offset=address-0x5446,species=offset/3+1;
    for(const auto& mon:gen1_extended_data::mons)if(mon.species==species)return mon.cry[offset%3];
   }
   if(c.rom_bank==9&&c.pc==c.instruction_pc+1){
    for(unsigned n=0;n<2;++n){unsigned type=n?27:9,pointer=0x7dae + 2*type,window=0x7fe0+n*16;
     if(c.instruction_pc==0x7da7&&address==pointer)return uint8_t(window);
     if(c.instruction_pc==0x7da9&&address==pointer+1)return uint8_t(window>>8);
     if(c.instruction_pc==0x1956&&address>=window&&address<window+16)return gen1_extended_data::type_names[n][address-window];
    }
   }
  }
  if(c.sp!=UINT16_MAX){
   if(c.rom_bank==3&&c.instruction_pc==0x7850&&c.pc==0x7852&&address==0x7851)dex_flags.fill(0); // InitPlayerData2, New Game.
   if(c.rom_bank==28&&c.instruction_pc==0x76f4&&c.pc==0x76f5&&address==0x76f5)load_dex(c); // Successful native party/Dex load only.
   if(c.instruction_pc==0x768f||c.instruction_pc==0x7695||c.instruction_pc==0x769d){int i=dex_byte(c,address);if(i>=0)return dex_flags[size_t(i)];}
   for(const auto& count:dex_counts)if(c.rom_bank==count.bank){
    // Run the unchanged original CountSetBits over the full virtual32-byte
    // field. Original WRAM and adjacent arrays remain physically unchanged.
    if(c.instruction_pc==count.site+3&&c.pc==count.site+5&&address==count.site+4&&c.hl==count.base&&value==19)return 32;
    if(c.instruction_pc==0x2b81&&c.pc==0x2b82&&c.sp>=0xc000&&c.sp<=0xdffe&&
       word(c,c.sp)==count.site+8&&address>=count.base&&address-count.base<32&&c.hl==address+1)return dex_value(count.base,address,value);
   }
   // Preserve the donor's NUM_POKEMON formulas at original consumers:
   // final rating covers0..251; the diploma still discounts one rare mon.
   if(c.rom_bank==17&&c.instruction_pc==0x4186&&c.pc==0x4187&&address==0x41fe&&value==152)return 252;
   if(c.rom_bank==18&&c.instruction_pc==0x47be&&c.pc==0x47c0&&address==0x47bf&&value==150)return 250;
   if(c.rom_bank==16){
    // The original reverse scan starts at bit256 and decrements an8-bit
    // count before each bit;257 wraps to1, then reaches actual max251.
    if(c.instruction_pc==0x417b&&c.pc==0x417d&&address==0x417c&&value==0x1c)return 0x29;
    if(c.instruction_pc==0x417e&&c.pc==0x4180&&address==0x417f&&value==153)return 1;
    if(c.instruction_pc==0x4180&&c.pc==0x4181&&address>=0xd30a&&address<0xd32a&&c.hl==address-1)return dex_value(0xd30a,address,value);
    unsigned species=ram(c,0xcf91);
    for(const auto& mon:gen1_extended_data::mons)if(mon.species==species){
     unsigned table=0x447e + (species-1)*2;
     // Two owned categories have eleven glyphs. Keep their full data and
     // native right border by using the available adjacent column8.
     if(c.instruction_pc==0x436d&&c.pc==0x436f&&address==0x436e&&
        std::find(gen1_extended_data::dex_bytes.begin()+mon.dex_offset,
                  gen1_extended_data::dex_bytes.begin()+mon.dex_offset+11,uint8_t(0x50))==
                  gen1_extended_data::dex_bytes.begin()+mon.dex_offset+11)return 0xf8;
     if(c.instruction_pc==0x436a&&c.pc==0x436b&&address==table)return 0;
     if(c.instruction_pc==0x436c&&c.pc==0x436d&&address==table+1)return 0x60;
     bool reader=c.instruction_pc==0x1956||c.instruction_pc==0x3c88||c.instruction_pc==0x43c3||
                 c.instruction_pc==0x43e7||c.instruction_pc==0x43ec||c.instruction_pc==0x1b55;
     if(reader&&c.pc==c.instruction_pc+1&&address>=0x6000&&address-0x6000<mon.dex_size)
      return gen1_extended_data::dex_bytes[mon.dex_offset+address-0x6000];
     break;
    }
   }
  }
  if(c.rom_bank==15){
   bool player=address==0xcfd2&&(c.instruction_pc==0x7b8c||c.instruction_pc==0x7baf);
   bool enemy=address==0xcfcc&&(c.instruction_pc==0x7b91||c.instruction_pc==0x7bb4);
   if((player||enemy)&&c.pc==c.instruction_pc+3)for(const auto& move:gen1_extended_data::moves)if(move.id==value)return move.animation;
   bool type_reader=c.instruction_pc==0x63fa||c.instruction_pc==0x6402||c.instruction_pc==0x6414||c.instruction_pc==0x645b||c.instruction_pc==0x6462||c.instruction_pc==0x646f;
   if(type_reader&&c.pc==c.instruction_pc+1&&address>=0x6474&&address-0x6474<gen1_extended_data::type_effects.size())return gen1_extended_data::type_effects[address-0x6474];
  }
  if(address==0x3772&&c.instruction_pc==0x3771&&c.pc==0x3773&&value==196){
   unsigned id=ram(c,0xd0b5),type=ram(c,0xd0b6);
   if(type==1){for(const auto& mon:gen1_extended_data::mons)if(mon.species==id)return 255;}
   if(type==2&&id>=166&&id<=226)return 255;
  }
  if(c.rom_bank==44&&ram(c,0xd0b6)==2&&address>=0x4000&&address-0x4000<gen1_extended_data::move_names.size()&&
     ((c.instruction_pc==0x37b8&&c.pc==0x37b9)||(c.instruction_pc==0x00b5&&c.pc==0x00b6&&c.d==0xcd)))return gen1_extended_data::move_names[address-0x4000];
  if(c.instruction_pc==0x162f&&c.pc==0x1630&&(address==0xd0c4||address==0xd0c6)){
   decoder_species=decoder_base=0;unsigned species=ram(c,0xcf91);
   for(const auto& mon:gen1_extended_data::mons)if(mon.species==species&&value==(address==0xd0c4?0x40:0x60)){decoder_species=species;decoder_base=unsigned(value)<<8;break;}
  }
  if(address==0x13a4&&c.instruction_pc==0x13a3&&c.pc==0x13a5&&value==152){
   unsigned species=ram(c,0xcf91);for(const auto& mon:gen1_extended_data::mons)if(mon.species==species)return 252;
  }
  if(c.rom_bank==16&&((c.instruction_pc==0x501d&&c.pc==0x501e)||(c.instruction_pc==0x5005&&c.pc==0x5006))&&address>=0x5024&&address<0x5024+254){
   unsigned species=address-0x5024+1;
   for(const auto& mon:gen1_extended_data::mons)if(mon.species==species)return mon.dex;
  }
  if(decoder_species&&c.instruction_pc==0x2693&&c.pc==0x2694&&address>=decoder_base&&address<decoder_base+0x2000){
   unsigned species=ram(c,0xcf91);
   for(const auto& mon:gen1_extended_data::mons)if(mon.species==species&&species==decoder_species){
    const auto& picture=decoder_base==0x6000?mon.back:mon.front;unsigned offset=address-decoder_base;
    if(offset<picture.size&&picture.offset+offset<gen1_extended_data::sprite_bytes.size())return gen1_extended_data::sprite_bytes[picture.offset+offset];
    return 0; // Fail closed outside this owned bounded compressed record.
   }
  }
#else
  (void)c;(void)address;
#endif
  return value;
 }
 unsigned world_tile(ModContext& c,int x,int y,const State* source=nullptr)const {
  const auto& s=source?*source:this->s;
  int bx=floor_div(x,32)+3,by=floor_div(y,32)+3;
  unsigned block=bx>=0&&bx<s.stride&&by>=0&&by<s.map_height+6?s.blocks[size_t(by)*s.stride+bx]:s.border;
  return rom(c,s.blocks_bank,s.blocks_address+block*16+((y&31)/8)*4+(x&31)/8);
 }
 unsigned actor_packet(ModContext& c,std::array<Sprite,9>& packet,unsigned face,unsigned graphic,int x,int y,unsigned grass,bool restored)const {
  if(face>=32||(variant==2&&face>=16))return 0;
  unsigned table=0x4000+face*(variant==2?2:4);
  unsigned pattern=rom(c,1,table)|(unsigned(rom(c,1,table+1))<<8);
  unsigned layout=variant==2?pattern:(rom(c,1,table+2)|(unsigned(rom(c,1,table+3))<<8));
  unsigned tile_base=graphic==11?124:graphic*12,count=variant==2?rom(c,1,layout++):4;
  if(count>packet.size())return 0;
  for(unsigned n=0;n<count;++n){int dy=int8_t(rom(c,1,layout++)),dx=int8_t(rom(c,1,layout++));unsigned tile=variant==2?rom(c,1,layout++):rom(c,1,pattern++),attr=rom(c,1,layout++);
   if(attr&2)attr|=grass&128;
   if(variant==2&&tile_base+tile>=128)tile+=c.hram?c.hram[0x7c]:0;
   packet[n].x=x+dx;packet[n].y=y+dy;packet[n].tile=uint8_t(tile_base+tile);packet[n].attr=uint8_t((attr&0xf0)|(restored?1:0));
  }
  return count;
 }
 bool published_actor(ModContext& c,std::array<Sprite,9>& packet,unsigned count,unsigned graphic,int x,int y,unsigned grass,uint64_t& used)const {
  if(!c.oam||!count||count>40)return false;
  unsigned best=40;int best_score=33;std::array<Sprite,9> selected{};
  unsigned faces=graphic<10?16:variant==2?1:32,begin=graphic<10?0:variant==2?0:16;
  for(unsigned face=begin;face<faces;++face){std::array<Sprite,9> proposed{};
   if(actor_packet(c,proposed,face,graphic,x,y,grass,false)!=count)continue;
   for(unsigned at=0;at+count<=40;++at){uint64_t mask=((uint64_t(1)<<count)-1)<<at;if(used&mask)continue;
    int dx=int8_t(int(c.oam[at*4+1])-8-proposed[0].x),dy=int8_t(int(c.oam[at*4])-16-proposed[0].y);
    int score=std::abs(dx)+std::abs(dy);if(score>=best_score||std::abs(dx)>16||std::abs(dy)>16)continue;
    bool same=true,visible=false;
    for(unsigned n=0;n<count;++n){const auto* o=c.oam+(at+n)*4;const auto& sp=proposed[n];
     unsigned attr=color?((o[3]&0xe0)|((o[3]&4)?16:0)):o[3]&0xf0;
     same&=o[2]==sp.tile&&((attr^sp.attr)&0x70)==0&&(!color||!(o[3]&8))&&uint8_t(sp.x+dx+8)==o[1]&&uint8_t(sp.y+dy+16)==o[0];
     visible|=o[0]>0&&o[0]<160&&o[1]>0&&o[1]<168;
    }
    if(!same||!visible)continue;
    best=at;best_score=score;
    for(unsigned n=0;n<count;++n){selected[n]=proposed[n];selected[n].x+=dx;selected[n].y+=dy;unsigned attr=c.oam[(at+n)*4+3];selected[n].attr=uint8_t(color?((attr&0xe0)|((attr&4)?16:0)):attr&0xf0);}
   }
  }
  if(best!=40){used|=((uint64_t(1)<<count)-1)<<best;packet=selected;}
  return best!=40;
 }
 void capture_palettes(ModContext& c,State& state)const {
  for(int i=0;i<4;++i){state.bg[i]=c.ppu->bg_rgb[color?i:(c.ppu->bgp>>(i*2))&3];state.obj[i]=c.ppu->obj_rgb[color?i:(c.ppu->obp0>>(i*2))&3];state.obj[4+i]=c.ppu->obj_rgb[color?16+i:4+((c.ppu->obp1>>(i*2))&3)];}
 }
 bool visible_tiles_match(ModContext& c,const State& state)const {
  unsigned map=(state.lcdc&8)?0x1c00:0x1800;
  for(int y=0;y<18;++y)for(int x=0;x<20;++x)if(c.vram[map+(((c.ppu->scy/8+y)&31)*32)+((c.ppu->scx/8+x)&31)]!=world_tile(c,floor_div(state.camera_x,8)*8+x*8,floor_div(state.camera_y,8)*8+y*8,&state))return false;
  return true;
 }
 void capture_world(ModContext& c){
  s.valid=0;if(!c.wram||!c.vram||!c.rom||!c.ppu)return;
  if(ram(c,b.battle)||!(c.ppu->lcdc&0x80)||(c.ppu->lcdc&4)||((c.ppu->lcdc&32)&&c.ppu->wx<167&&c.ppu->wy<144))return;
  // LoadMapData/LoadCurrentMapView use the actual width plus three blocks
  // on each side. Reject dimensions that cannot fit the original 1300-byte
  // cache or the game's eight-bit hMapStride; menus retain the LLE fallback.
  unsigned map_width=ram(c,b.width),map_height=ram(c,b.height),stride=map_width+6;
  if(!map_width||!map_height||stride>255||stride*(map_height+6)>s.blocks.size())return;
  unsigned anchor=word(c,b.anchor),bg=word(c,b.bg_anchor),bank=ram(c,b.bank),blocks=word(c,b.blocks);
  if(anchor<0xc6e8||anchor>=0xc6e8+stride*(map_height+6)||bg<0x9800||bg>=0xa000||bank>=c.rom_size/0x4000||blocks<0x4000||blocks>=0x8000)return;
  auto cell=anchor-0xc6e8;s.camera_x=(int(cell%stride)-3)*32+ram(c,b.meta_x)*16+int8_t(c.ppu->scx-((bg&31)*8));s.camera_y=(int(cell/stride)-3)*32+ram(c,b.meta_y)*16+int8_t(c.ppu->scy-(((bg>>5)&31)*8));
  s.map_width=uint16_t(map_width);s.map_height=uint16_t(map_height);s.stride=uint16_t(stride);
  s.blocks_bank=uint8_t(bank);s.blocks_address=uint16_t(blocks);s.border=ram(c,b.border);s.lcdc=c.ppu->lcdc;
  std::copy_n(c.wram+0x6e8,1300,s.blocks.begin());std::copy_n(c.vram,0x2000,s.vram.begin());
  // Facility's real table begins at77FF. Its actual block IDs fit; an
  // arbitrary4KiB allowance would incorrectly reject that supported tileset.
  unsigned block_count=(0x8000-blocks)/16,cells=stride*(map_height+6);
  if(!block_count||s.border>=block_count||std::any_of(s.blocks.begin(),s.blocks.begin()+cells,[&](uint8_t block){return block>=block_count;}))return;
  unsigned map=(s.lcdc&8)?0x1c00:0x1800;
  // Yellow's original whole-field CGB attributes select palette/bank0.
  if(color&&std::any_of(c.vram+map+0x2000,c.vram+map+0x2400,[](uint8_t attr){return attr!=0;}))return;
  // Its native palette producer already converted BGP/OBP. OBP1 maps to
  // CGB OBJ palette4; applying DMG remapping again changes authentic colors.
  capture_palettes(c,s);
  if(!visible_tiles_match(c,s))return;
  s.count=0;uint64_t used_oam=0;
  for(unsigned slot=0;slot<16;++slot){unsigned a=0xc100+slot*16,a2=a+0x100,base=ram(c,a2+14);if(!ram(c,a)||!base)continue;
   // Yellow slot15 is dispatched to SpawnPikachu, not UpdateNPCSprite.
   // Its stale hidden fields cannot use the regular NPC clipping contract.
   // Follower enhancement is unqualified; its native center stays intact.
   if(variant==2&&slot==15)continue;
   // Match IsObjectHidden before interpreting FF as viewport clipping.
   bool hidden=false;unsigned list=variant==2?0xd5cd:0xd5ce,flags=variant==2?0xd5a5:0xd5a6;
   if(slot)for(unsigned n=0;n<16;++n){unsigned id=ram(c,list+n*2);if(id==255)break;if(id==slot){unsigned flag=ram(c,list+n*2+1);hidden=(ram(c,flags+flag/8)&(1u<<(flag&7)))!=0;break;}}
   if(hidden)continue;
   // Gen1's player map fields in C2 are unused: NPC map coordinates are
   // padded by four. InitializeSpriteScreenPosition subtracts wX/YCoord.
   unsigned image=ram(c,a+2);int expected_x=(int(ram(c,a2+5))-ram(c,b.map+4))*16,expected_y=(int(ram(c,a2+4))-ram(c,b.map+3))*16-4;
   int x=expected_x+int8_t(ram(c,a+6)-expected_x),y=expected_y+int8_t(ram(c,a+4)-expected_y);
   // Player C2 map coordinates are unused. Its own screen coordinates keep
   // the native player in the authoritative center instead of duplicating
   // it far into a large margin when the current map coordinate grows.
   if(!slot){x=int(ram(c,a+6));y=int(ram(c,a+4));}
   bool restored=image==255;
   if(restored){
    // Availability freezes offscreen actors before initializing/updating their
    // byte-sized screen coordinates. Project their own padded map coordinates
    // and remaining native step instead of unwrapping stale $F0/$00 sentinels.
    // No actor activation, AI, collision, script or guest coordinates change.
    unsigned remaining=(ram(c,a+1)&0x7f)==3?ram(c,a2):0;
    x=(int(ram(c,a2+5))-4)*16-s.camera_x-int8_t(ram(c,a+5))*int(remaining);
    y=(int(ram(c,a2+4))-4)*16-s.camera_y-4-int8_t(ram(c,a+3))*int(remaining);
    image=(base-1)*16+ram(c,a+9)+ram(c,a+8);
   }
   unsigned face=image<160?image&15:variant==2?0:(image&15)+16;
   std::array<Sprite,9> packet{};unsigned graphic=(image>>4)&15,grass=ram(c,a2+7);
   unsigned count=actor_packet(c,packet,face,graphic,x,y,grass,false);
   bool published=published_actor(c,packet,count,graphic,x,y,grass,used_oam);
   // Returning to the native rectangle initializes RAM before the next OAM
   // upload. Keep an unhidden boundary actor's complete body visible through
   // that publication gap, just as when its image is still clipped to FF.
   if(slot&&!published&&(restored||c.oam)) {
    bool boundary=false;
    for(unsigned n=0;n<count;++n)boundary|=(packet[n].x<8&&packet[n].x+8>0)||(packet[n].x<160&&packet[n].x+8>152)||(packet[n].y<8&&packet[n].y+8>0)||(packet[n].y<144&&packet[n].y+8>136);
    if(boundary)for(unsigned n=0;n<count;++n)packet[n].attr|=1;
   }
   for(unsigned n=0;n<count&&s.count<160;++n)s.sprites[s.count++]=packet[n];
  }
  s.valid=1;++s.captures;
 }
 void capture(ModContext& c){
  capture_world(c);
  // Connected-map headers are staged before the original palette transfer
  // and block-cache update. Keep a previous source only when every visible
  // hardware tile still proves that source is what the PPU is displaying.
  if(!s.valid&&has_presented&&presented.valid&&c.wram&&c.vram&&c.ppu&&c.rom&&
     !ram(c,b.battle)&&(c.ppu->lcdc&0x80)&&!(c.ppu->lcdc&4)&&
     !((c.ppu->lcdc&32)&&c.ppu->wx<167&&c.ppu->wy<144)) {
   unsigned map=(c.ppu->lcdc&8)?0x1c00:0x1800;
   bool attributes=!color||std::all_of(c.vram+map+0x2000,c.vram+map+0x2400,[](uint8_t attr){return attr==0;});
   if(attributes) {
    State previous=presented;int dx=int8_t(c.ppu->scx-previous.scx),dy=int8_t(c.ppu->scy-previous.scy);
    previous.camera_x+=dx;previous.camera_y+=dy;previous.lcdc=c.ppu->lcdc;
    if(visible_tiles_match(c,previous)) {
     for(int n=0;n<previous.count;++n){previous.sprites[n].x-=dx;previous.sprites[n].y-=dy;}
     std::copy_n(c.vram,0x2000,previous.vram.begin());capture_palettes(c,previous);
     previous.callers=s.callers;previous.captures=s.captures+1;s=previous;
    }
   }
  }
  if(s.valid){s.scx=c.ppu->scx;s.scy=c.ppu->scy;
   if(color)for(auto& row:s.row_palettes){std::copy(s.bg.begin(),s.bg.end(),row.begin());std::copy(s.obj.begin(),s.obj.end(),row.begin()+4);}
  }
 }
 void capture_row(ModContext& c,unsigned line){
  if(!color||!s.valid||line>=144||!c.ppu)return;
  for(unsigned i=0;i<4;++i){s.row_palettes[line][i]=c.ppu->bg_rgb[i];s.row_palettes[line][4+i]=c.ppu->obj_rgb[i];s.row_palettes[line][8+i]=c.ppu->obj_rgb[16+i];}
 }
 uint32_t pixel_palette(const State& state,int line,unsigned entry)const {
  return color?state.row_palettes[std::clamp(line,0,143)][entry]:entry<4?state.bg[entry]:state.obj[entry-4];
 }
 ModView render(ModContext& c,std::span<const uint32_t> native){
  const auto& s=has_presented?presented:this->s;
  const auto [width,height]=host_size;
  ModView v;v.width=unsigned(width);v.height=unsigned(height);v.expanded=s.valid&&(width>160||height>144);int center=(width-160)/2,top=(height-144)/2;pixels.assign(size_t(width)*height,0xff000000);opaque.assign(pixels.size(),0);
  // Map loaders deliberately invalidate the world while its tilemap changes.
  // A completed full-screen fade still fills Fit's entire canvas: black side
  // margins around a uniform native fade would expose the old rectangle.
  if(!v.expanded&&!native.empty()&&std::all_of(native.begin(),native.end(),[&](uint32_t p){return p==native.front();}))std::fill(pixels.begin(),pixels.end(),native.front());
  if(v.expanded){for(int y=0;y<height;++y)for(int x=0;x<width;++x){unsigned tile=world_tile(c,s.camera_x+x-center,s.camera_y+y-top,&s);int address=(s.lcdc&16)?int(tile)*16:0x1000+int8_t(tile)*16;unsigned row=unsigned(address+((s.camera_y+y-top)&7)*2),bit=7-((s.camera_x+x-center)&7),color=((s.vram[row]>>bit)&1)|(((s.vram[row+1]>>bit)&1)<<1);pixels[size_t(y)*width+x]=pixel_palette(s,y-top,color);opaque[size_t(y)*width+x]=uint8_t(color);}
   for(int n=s.count-1;n>=0;--n){const auto& sp=s.sprites[n];for(int y=0;y<8;++y)for(int x=0;x<8;++x){int dx=sp.x+x+center,dy=sp.y+y+top;if(dx<0||dx>=width||dy<0||dy>=height||(dx>=center&&dx<center+160&&dy>=top&&dy<top+144))continue;unsigned row=unsigned(sp.tile)*16+((sp.attr&64)?7-y:y)*2,bit=(sp.attr&32)?x:7-x,color=((s.vram[row]>>bit)&1)|(((s.vram[row+1]>>bit)&1)<<1);if(color&&(!(sp.attr&128)||!opaque[size_t(dy)*width+dx]))pixels[size_t(dy)*width+dx]=pixel_palette(s,dy-top,4+((sp.attr&16)?4:0)+color);}}
  }
  for(unsigned y=0;y<144;++y)std::copy_n(native.data()+y*160,160,pixels.data()+size_t(y+top)*width+center);
  // Availability culls on map cells before the actor's body leaves the native
  // rectangle. Continue that same unhidden actor across an expanded seam.
  // Original160 remains the untouched PPU presentation.
  if(v.expanded)for(int n=s.count-1;n>=0;--n){const auto& sp=s.sprites[n];if(!(sp.attr&1))continue;
   for(int y=0;y<8;++y)for(int x=0;x<8;++x){int dx=sp.x+x+center,dy=sp.y+y+top;
    if(dx<center||dx>=center+160||dy<top||dy>=top+144)continue;
    unsigned row=unsigned(sp.tile)*16+((sp.attr&64)?7-y:y)*2,bit=(sp.attr&32)?x:7-x;
    unsigned color=((s.vram[row]>>bit)&1)|(((s.vram[row+1]>>bit)&1)<<1);
    if(color&&(!(sp.attr&128)||!opaque[size_t(dy)*width+dx]))pixels[size_t(dy)*width+dx]=pixel_palette(s,dy-top,4+((sp.attr&16)?4:0)+color);
   }
  }
  v.pixels=pixels;v.camera_x=s.camera_x;v.camera_y=s.camera_y;v.left=s.camera_x-center;v.bound_right=s.map_width*32;v.sprites=unsigned(s.count);v.captures=s.captures;v.spawn_reads=s.callers;return v;
 }
};
Gen1NativeMods::Gen1NativeMods(std::string_view id,const NativeFeature& f,bool cgb):impl_(std::make_unique<Impl>(id,f,cgb)){}
Gen1NativeMods::~Gen1NativeMods()=default;
uint8_t Gen1NativeMods::read(ModContext& c,uint16_t address,uint8_t value){if(impl_->extended&&c.sp==UINT16_MAX)return value;if(c.rom_bank==1&&c.instruction_pc==impl_->b.caller&&address==impl_->b.flags)++impl_->s.callers;impl_->extended_return(c,address);return impl_->extended_data(c,address,value);}
bool Gen1NativeMods::observes_writes()const{return impl_->extended;}
bool Gen1NativeMods::write(ModContext& c,uint16_t address,uint8_t value){
#if GBCLLR_GEN1_EXTENDED_ASSETS
 if(impl_->extended){
  if(c.instruction_pc==0x7692||c.instruction_pc==0x769a){int i=impl_->dex_byte(c,address);if(i>=0){impl_->dex_flags[size_t(i)]=value;return false;}}
  // Only the completed native main/party+Dex checksum writes commit live flags.
  // Cancelled menus, host battery exports, PC-box saves and speculative copies do not.
  if(c.rom_bank==28&&(c.instruction_pc==0x77d7||c.instruction_pc==0x783d)&&
     c.pc==c.instruction_pc+3&&address==0xb523)impl_->save_dex(c,value);
 }
#endif
 return true;
}
void Gen1NativeMods::scanline(ModContext& c,uint8_t line){if(!line)impl_->capture(c);else impl_->capture_row(c,line);}
void Gen1NativeMods::complete_frame(ModContext&,bool cleared,bool frozen){
 // SGB MASK1 holds the last live image while the cartridge replaces VRAM.
 // Keep its matching world/actors too; this source is already serialized and
 // independent of the host viewport, so a resize/load can reproduce margins.
 if(!frozen||!impl_->has_presented){impl_->presented=cleared?State{}:impl_->s;impl_->has_presented=true;}
}
void Gen1NativeMods::viewport(unsigned w,unsigned h){
 if(impl_->requested>=0)impl_->host_size={impl_->requested,144};
 else if(!h)impl_->host_size={256,144};
 else if(!w)impl_->host_size={160,144};
 else if(uint64_t(w)*144>=uint64_t(h)*160)impl_->host_size={int(std::clamp<uint64_t>((uint64_t(w)*144+h/2)/h,160,4096)),144};
 else impl_->host_size={160,int(std::clamp<uint64_t>((uint64_t(h)*160+w/2)/w,144,4096))};
}
ModView Gen1NativeMods::render(ModContext& c,std::span<const uint32_t> native){return impl_->render(c,native);}
void Gen1NativeMods::preserve_host_presentation(const Gen1NativeMods& from) noexcept {impl_->host_size=from.impl_->host_size;}
std::vector<uint8_t> Gen1NativeMods::snapshot()const {static_assert(std::is_trivially_copyable_v<State>);std::array<uint32_t,4> h{7,impl_->variant,uint32_t(impl_->requested),uint32_t(impl_->encounters)|(impl_->color?2u:0u)};std::vector<uint8_t> out(sizeof h+2*sizeof(State)+sizeof(uint32_t));std::memcpy(out.data(),h.data(),sizeof h);std::memcpy(out.data()+sizeof h,&impl_->s,sizeof(State));std::memcpy(out.data()+sizeof h+sizeof(State),&impl_->presented,sizeof(State));uint32_t valid=impl_->has_presented;std::memcpy(out.data()+sizeof h+2*sizeof(State),&valid,sizeof valid);
#if GBCLLR_GEN1_EXTENDED_ASSETS
 if(impl_->extended){out.insert(out.begin(),std::begin(gen1_extended_data::content_identity),std::end(gen1_extended_data::content_identity)-1);std::array<uint32_t,2> decoder{impl_->decoder_species,impl_->decoder_base};const auto* p=reinterpret_cast<const uint8_t*>(decoder.data());out.insert(out.begin(),p,p+sizeof decoder);out.insert(out.begin(),impl_->dex_flags.begin(),impl_->dex_flags.end());}
#endif
 return out;}
void Gen1NativeMods::restore(std::span<const uint8_t> bytes){
 uint32_t decoder_species=0,decoder_base=0;
 std::array<uint8_t,64> dex_flags{};
#if GBCLLR_GEN1_EXTENDED_ASSETS
 if(impl_->extended){constexpr size_t n=sizeof(gen1_extended_data::content_identity)-1;std::array<uint32_t,2> decoder{};if(bytes.size()<dex_flags.size()+sizeof decoder+n)throw std::runtime_error("Truncated Gen1 Extended state");std::copy_n(bytes.begin(),dex_flags.size(),dex_flags.begin());bytes=bytes.subspan(dex_flags.size());if(!Impl::valid_dex_flags(dex_flags))throw std::runtime_error("Invalid Gen1 Extended Dex state");std::memcpy(decoder.data(),bytes.data(),sizeof decoder);bytes=bytes.subspan(sizeof decoder);decoder_species=decoder[0];decoder_base=decoder[1];
  bool valid=decoder_species==0?decoder_base==0:(decoder_base==0x4000||decoder_base==0x6000)&&std::any_of(gen1_extended_data::mons.begin(),gen1_extended_data::mons.end(),[&](const auto& mon){return mon.species==decoder_species;});
  if(!valid||!std::equal(bytes.begin(),bytes.begin()+n,std::begin(gen1_extended_data::content_identity)))throw std::runtime_error("Wrong Gen1 Extended decoder/data state");bytes=bytes.subspan(n);}
#endif
 std::array<uint32_t,4> h{};State states[2]{};uint32_t presented=0;if(bytes.size()!=sizeof h+sizeof states+sizeof presented)throw std::runtime_error("Invalid Gen1 native state size");std::memcpy(h.data(),bytes.data(),sizeof h);std::memcpy(states,bytes.data()+sizeof h,sizeof states);std::memcpy(&presented,bytes.data()+sizeof h+sizeof states,sizeof presented);
 if(h[0]!=7||h[3]!=(uint32_t(impl_->encounters)|(impl_->color?2u:0u))||h[1]!=impl_->variant||int32_t(h[2])!=impl_->requested||presented>1)throw std::runtime_error("Invalid Gen1 native state/profile");
 for(const auto& s:states){
  if(s.valid>1||s.count<0||s.count>160||s.camera_x< -4096||s.camera_x>8192||s.camera_y< -4096||s.camera_y>8192||(s.valid&&(!s.map_width||!s.map_height||s.stride!=s.map_width+6||s.stride>255||unsigned(s.stride)*(s.map_height+6)>s.blocks.size()||s.blocks_bank>=64||s.blocks_address<0x4000||s.blocks_address>=0x8000)))throw std::runtime_error("Invalid Gen1 native state/profile");
  if(s.valid){unsigned block_count=(0x8000-s.blocks_address)/16,cells=unsigned(s.stride)*(s.map_height+6);if(!block_count||s.border>=block_count||std::any_of(s.blocks.begin(),s.blocks.begin()+cells,[&](uint8_t block){return block>=block_count;}))throw std::runtime_error("Invalid Gen1 block table state");}
  for(const auto& sp:s.sprites)if(sp.x< -4096||sp.x>8192||sp.y< -4096||sp.y>8192)throw std::runtime_error("Invalid Gen1 sprite state");
 }
 impl_->s=states[0];impl_->presented=states[1];impl_->has_presented=presented;impl_->decoder_species=decoder_species;impl_->decoder_base=decoder_base;impl_->dex_flags=dex_flags;
}
}
