// Persistent indexed-surface update pattern adapted from gbarecomp:
// src/gba/gba_high_resolution.cpp, GbaHighResolutionFrame::Surface::update,
// Matthew Stanley (2026), PolyForm Noncommercial 1.0.0. See the retained
// third_party/gbarecomp-renderer/{LICENSE,NOTICE}. GB2bpp layout/guards are local.
#include "indexed_tile_surface.hpp"
#include <algorithm>
#include <array>
#include <cstring>
#include <stdexcept>

namespace gbcllr {
unsigned IndexedTileSurface::address(uint8_t tile,bool unsigned_tiles){return unsigned_tiles?unsigned(tile)*16:unsigned(0x1000+int8_t(tile)*16);}
const char* IndexedTileSurface::implementation(){
#if GBCLLR_PRESENTATION_SAMPLER_HLE
 return "HLE persistent indexed tile-map surface";
#else
 return "LLE direct tile-map sampling";
#endif
}
void IndexedTileSurface::invalidate(){layout_=~uint64_t(0);source_={};}
uint8_t IndexedTileSurface::direct(const Source& s,unsigned cell,unsigned x,unsigned y){
 auto tile=s.map[cell];auto t=s.resolve(s.context,cell,tile,address(tile,s.unsigned_tiles));
 if(!t.planes||t.palette>7||t.flips&~0x60)throw std::runtime_error("invalid indexed tile descriptor");
 unsigned py=y^((t.flips&0x40)?7:0),bit=(7-x)^((t.flips&0x20)?7:0);
 return uint8_t(t.palette*4+((t.planes[py*2]>>bit)&1)+(((t.planes[py*2+1]>>bit)&1)<<1));
}
unsigned IndexedTileSurface::prepare(Source source){
 if(source.characters.size()!=0x1800||source.map.size()!=2048||!source.resolve)throw std::runtime_error("invalid indexed tile surface source");
#if GBCLLR_PRESENTATION_SAMPLER_HLE
 const bool reset=layout_!=source.layout||source_.unsigned_tiles!=source.unsigned_tiles||source_.resolve!=source.resolve||source_.context!=source.context||characters_.size()!=source.characters.size()||map_.size()!=source.map.size();
 source_=source;
 if(!reset&&std::memcmp(characters_.data(),source.characters.data(),characters_.size())==0&&std::memcmp(map_.data(),source.map.data(),map_.size())==0)return 0;
 std::array<bool,384> dirty{};
 for(unsigned tile=0;tile<dirty.size();tile++)dirty[tile]=reset||std::memcmp(characters_.data()+tile*16,source.characters.data()+tile*16,16)!=0;
 map_.resize(source.map.size());characters_.resize(source.characters.size());pixels_.resize(256*512);
 unsigned decoded=0;
 for(unsigned cell=0;cell<source.map.size();cell++){
  auto tile=source.map[cell];
  if(!reset&&map_[cell]==tile&&!dirty[address(tile,source.unsigned_tiles)/16])continue;
  auto t=source.resolve(source.context,cell,tile,address(tile,source.unsigned_tiles));
  if(!t.planes||t.palette>7||t.flips&~0x60)throw std::runtime_error("invalid indexed tile descriptor");
  auto* dst=pixels_.data()+(cell/32)*8*256+(cell%32)*8;
  for(unsigned y=0;y<8;y++)for(unsigned x=0;x<8;x++){
   unsigned py=y^((t.flips&0x40)?7:0),bit=(7-x)^((t.flips&0x20)?7:0);
   dst[y*256+x]=uint8_t(t.palette*4+((t.planes[py*2]>>bit)&1)+(((t.planes[py*2+1]>>bit)&1)<<1));
  }
  ++decoded;
 }
 std::memcpy(characters_.data(),source.characters.data(),characters_.size());std::memcpy(map_.data(),source.map.data(),map_.size());layout_=source.layout;return decoded;
#else
 source_=source;
 return 0;
#endif
}
uint8_t IndexedTileSurface::sample(unsigned cell,unsigned x,unsigned y)const{
 if(cell>=2048||x>=8||y>=8||!source_.resolve)throw std::runtime_error("invalid indexed tile sample");
#if GBCLLR_PRESENTATION_SAMPLER_HLE
 return pixels_[(cell/32*8+y)*256+(cell%32*8+x)];
#else
 return direct(source_,cell,x,y);
#endif
}
std::span<const uint8_t> IndexedTileSurface::span(unsigned cell,unsigned x,unsigned y,unsigned count,std::span<uint8_t,8> scratch)const{
 if(cell>=2048||x>=8||y>=8||!count||count>8-x||!source_.resolve)throw std::runtime_error("invalid indexed tile span");
#if GBCLLR_PRESENTATION_SAMPLER_HLE
 return {pixels_.data()+(cell/32*8+y)*256+(cell%32*8+x),count};
#else
 auto tile=source_.map[cell];auto t=source_.resolve(source_.context,cell,tile,address(tile,source_.unsigned_tiles));
 if(!t.planes||t.palette>7||t.flips&~0x60)throw std::runtime_error("invalid indexed tile descriptor");
 unsigned py=y^((t.flips&0x40)?7:0),lo=t.planes[py*2],hi=t.planes[py*2+1];
 for(unsigned i=0;i<count;i++){unsigned bit=(7-x-i)^((t.flips&0x20)?7:0);scratch[i]=uint8_t(t.palette*4+((lo>>bit)&1)+(((hi>>bit)&1)<<1));}
 return scratch.first(count);
#endif
}
}
