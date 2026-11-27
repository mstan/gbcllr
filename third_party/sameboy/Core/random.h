#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "defs.h"

#ifdef GB_INTERNAL
internal uint8_t GB_random(GB_gameboy_t *gb);
internal uint32_t GB_random32(GB_gameboy_t *gb);
#endif
