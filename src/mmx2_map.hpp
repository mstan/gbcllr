// Adapted from Megaman Xtreme 2 e4649e231cc239a0f220f8afe0ae996916e0c6cf.
/* ROM map decoding from the verified 00:0AB1/0B4D consumers. */
#define MMX2_WRAM_BANK_SIZE 0x1000u
#define MMX2_VRAM_BANK_SIZE 0x2000u

/* Faithful host clone of func_0ab1's expanded-map lookup. Coordinates are
 * 16-pixel metatiles. Bank 5 contains a DA00 page table and the selected
 * D000-DFFF map pages. */
static int mmx2_rom_byte(ModContext *ctx, int bank, uint16_t addr,
                         uint8_t *out);

static int mmx2_bg_meta_id(ModContext *ctx, int bank, uint16_t mx,
                           uint16_t my, uint8_t *out_id) {
    uint8_t xl = (uint8_t)mx;
    uint8_t xh = (uint8_t)(mx >> 8);
    uint8_t yl = (uint8_t)my;
    uint8_t yh = (uint8_t)(my >> 8);
    uint8_t page_index = (uint8_t)((yl & 0xF0u) + ((xl & 0xF0u) >> 4) +
                                   ((yh & 0xF0u) >> 4) + xh);
    uint8_t offset = (uint8_t)(((yl & 0x0Fu) << 4) + (xl & 0x0Fu));
    uint8_t page = ctx->wram[5u * MMX2_WRAM_BANK_SIZE + 0x0A00u + page_index];
    if (page >= 0x40u && page <= 0x7Fu) {
        return mmx2_rom_byte(ctx, bank,
                             (uint16_t)(((uint16_t)page << 8) | offset), out_id);
    } else if (page >= 0xC0u && page <= 0xCFu) {
        *out_id = ctx->wram[((uint32_t)(page - 0xC0u) << 8) + offset];
    } else if (page >= 0xD0u && page <= 0xDFu) {
        *out_id = ctx->wram[5u * MMX2_WRAM_BANK_SIZE +
                            ((uint32_t)(page - 0xD0u) << 8) + offset];
    } else {
        return 0;
    }
    return 1;
}

static int mmx2_rom_byte(ModContext *ctx, int bank, uint16_t addr,
                         uint8_t *out) {
    uint32_t off;
    if (bank <= 0 || addr < 0x4000 || addr >= 0x8000) return 0;
    off = (uint32_t)bank * 0x4000u + (uint32_t)(addr - 0x4000u);
    if (off >= ctx->rom_size) return 0;
    *out = ctx->rom[off];
    return 1;
}

static int mmx2_bg_block(ModContext *ctx, int bank, uint16_t mx,
                         uint16_t my, uint8_t tile[4], uint8_t attr[4]) {
    uint8_t id;
    if (!mmx2_bg_meta_id(ctx, bank, mx, my, &id)) return 0;
    for (int i = 0; i < 4; i++) {
        if (!mmx2_rom_byte(ctx, bank, (uint16_t)(0x4000 + i * 0x100 + id),
                           &tile[i]) ||
            !mmx2_rom_byte(ctx, bank, (uint16_t)(0x4400 + i * 0x100 + id),
                           &attr[i])) return 0;
    }
    return 1;
}
