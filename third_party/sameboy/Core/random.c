/* gbcllr: deterministic instance-owned RNG, saved with core_state. */
#include "gb.h"
uint8_t GB_random(GB_gameboy_t *gb) {
    gb->llr_rng = gb->llr_rng * UINT64_C(0x27BB2EE687B0B0FD) + UINT64_C(0xB504F32D);
    return gb->llr_rng >> 56;
}
uint32_t GB_random32(GB_gameboy_t *gb) {
    GB_random(gb);
    return gb->llr_rng >> 32;
}
