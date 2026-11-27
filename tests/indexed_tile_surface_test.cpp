#include "../src/indexed_tile_surface.hpp"
#include <array>
#include <iostream>
#include <stdexcept>
#include <string>
namespace {
void check(bool v,const char* message){if(!v)throw std::runtime_error(message);}
struct Fixture {
 std::array<uint8_t,0x1800> characters{};
 std::array<uint8_t,2048> map{};
 uint8_t palette=3,flips=0;
 mutable unsigned resolutions=0;
 static gbcllr::IndexedTileSurface::Tile resolve(const void* context,unsigned,uint8_t,unsigned address){auto& f=*static_cast<const Fixture*>(context);++f.resolutions;return {f.characters.data()+address,f.palette,f.flips};}
};
}
int main(){try{
 using gbcllr::IndexedTileSurface;Fixture f;f.characters[0]=0x80;f.characters[1]=0x40;IndexedTileSurface surface;
 IndexedTileSurface::Source source{1,f.characters,f.map,true,Fixture::resolve,&f};
 bool cached=std::string(IndexedTileSurface::implementation()).starts_with("HLE");
 auto decoded=surface.prepare(source);check(decoded==(cached?2048u:0u),"initial surface publication differs");
 check(surface.sample(0,0,0)==13&&surface.sample(0,1,0)==14&&surface.sample(0,2,0)==12,"native2bpp/palette decoding differs");
 std::array<uint8_t,8> scratch{};auto before_span=f.resolutions;auto span=surface.span(0,0,0,8,scratch);
 check(span[0]==13&&span[1]==14&&span[7]==12&&f.resolutions-before_span==(cached?0u:1u),"tile span repeated source resolution or changed native colors");
 auto calls=f.resolutions;check(surface.prepare(source)==0&&f.resolutions==calls,"unchanged source repeated decoding");
 f.map[1]=1;f.characters[16]=0xff;decoded=surface.prepare(source);check(decoded==(cached?1u:0u)&&surface.sample(1,7,0)==13,"changed map/source tile was not updated");
 f.characters[0]=0;decoded=surface.prepare(source);check(decoded==(cached?2047u:0u)&&surface.sample(0,0,0)==12&&surface.sample(0,1,0)==14,"changed tile did not update all referencing cells");
 f.flips=0x20;source.layout++;surface.prepare(source);check(surface.sample(0,6,0)==14,"flipped source orientation differs");
 f.flips=0;source.layout++;source.unsigned_tiles=false;f.characters[0x1000]=0x80;surface.prepare(source);check(surface.sample(0,0,0)==13,"signed tile selection reused unsigned cache");
 surface.invalidate();bool rejected=false;try{surface.sample(0,0,0);}catch(const std::runtime_error&){rejected=true;}check(rejected,"invalidated surface remained published");
 check(surface.prepare(source)==(cached?2048u:0u)&&surface.sample(0,0,0)==13,"invalidated source did not rebuild");
 rejected=false;try{surface.sample(2048,0,0);}catch(const std::runtime_error&){rejected=true;}check(rejected,"out-of-range sample accepted");
 auto bad=source;bad.map=std::span(f.map).first(2047);rejected=false;try{surface.prepare(bad);}catch(const std::runtime_error&){rejected=true;}check(rejected,"malformed source dimensions accepted");
 rejected=false;try{surface.span(0,7,0,2,scratch);}catch(const std::runtime_error&){rejected=true;}check(rejected,"span crossed a tile boundary");
 std::cout<<IndexedTileSurface::implementation()<<": source/map changes, persistent reuse, signed addressing, flips and invalidation passed\n";return 0;
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
