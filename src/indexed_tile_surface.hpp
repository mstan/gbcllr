#pragma once
#include <cstdint>
#include <span>
#include <vector>

namespace gbcllr {
// Portable presentation-only service. Inputs are already-published GB tile/map
// bytes; callers retain ownership of timing, priorities, palettes and geometry.
// No buffer or source pointer in this object belongs in canonical snapshots.
class IndexedTileSurface {
public:
 struct Tile {const uint8_t* planes;uint8_t palette,flips;};
 using Resolve=Tile (*)(const void*,unsigned cell,uint8_t tile,unsigned address);
 struct Source {
  uint64_t layout; // Revision/qualification rules; change when bindings change.
  std::span<const uint8_t> characters,map;
  bool unsigned_tiles;
  Resolve resolve;
  const void* context;
 };
 // Returns the number of cells decoded this publication. Unchanged input does
 // no decoding; changed source tiles update every referencing cell.
 unsigned prepare(Source);
 // Borrow one tile-row span from the persistent surface. Direct LLE decodes
 // the same span once into caller-owned scratch; no pointer survives prepare.
 std::span<const uint8_t> span(unsigned cell,unsigned x,unsigned y,unsigned count,std::span<uint8_t,8> scratch)const;
 uint8_t sample(unsigned cell,unsigned x,unsigned y)const;
 void invalidate();
 static const char* implementation();
private:
 static unsigned address(uint8_t tile,bool unsigned_tiles);
 static uint8_t direct(const Source&,unsigned cell,unsigned x,unsigned y);
 Source source_{};
 uint64_t layout_=~uint64_t(0);
 std::vector<uint8_t> characters_,map_,pixels_;
};
}
