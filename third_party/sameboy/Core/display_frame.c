/* Experimental frame-picture service. This is a separate replacement of the
 * dot/FIFO machine, not an alternate pixel body inside that machine. Public
 * palette/output helpers and the existing whole-line raster primitive are
 * shared with LLE. Only the explicit reference fallback enters the old clock.
 */
#include <limits.h>
#define GB_display_run GB_display_reference_run
#define GB_display_state_profile GB_display_reference_state_profile
#include "display.c"
#undef GB_display_run
#undef GB_display_state_profile

const char *GB_display_state_profile(const GB_gameboy_t *gb)
{
    return gb->llr_frame_picture_enabled ? "-frame-picture-v2" : GB_display_reference_state_profile(gb);
}

enum { PICTURE_OFF=GB_FRAME_PICTURE_OFF, FIRST_OAM, FIRST_MODE3, FIRST_PALETTE, FIRST_DRAW,
       LINE_WRITE, LINE_LY, LINE_MODE2, PICTURE_OAM, MODE3_PALETTE, MODE3_DRAW,
       PICTURE_DRAW, HBLANK_MODE, HBLANK_PALETTE, HBLANK_FREE, LINE_END, LINE_NEXT,
       VBLANK_LY, VBLANK_COMPARE, VBLANK_IRQ, VBLANK_REST,
       LAST_LY, LAST_COMPARE, LAST_ZERO, LAST_COMPARE_ZERO, LAST_REST, LAST_END };
_Static_assert(LAST_END == GB_FRAME_PICTURE_LAST, "native picture clock state range");

static void picture_mode(GB_gameboy_t *gb, unsigned mode)
{
    gb->io_registers[GB_IO_STAT] = (gb->io_registers[GB_IO_STAT] & ~3) | mode;
    gb->mode_for_interrupt = mode;
    gb->oam_read_blocked = mode == 2 || mode == 3;
    gb->oam_write_blocked = mode == 2 || mode == 3;
    gb->vram_read_blocked = mode == 3;
    gb->vram_write_blocked = mode == 3;
    GB_STAT_update(gb);
}

static void picture_line(GB_gameboy_t *gb)
{
    gb->lcd_x = 0;
    gb->wx_triggered = false;
    gb->wy_check_scheduled = false;
    wy_check(gb);
    if (gb->lcd_line_callback) gb->lcd_line_callback(gb,gb->current_line);
    gb->oam_write_blocked=!gb->cgb_double_speed;
    gb->accessed_oam_row=0;
    gb->display_state=LINE_WRITE;
    gb->display_cycles-=2*2;
}

static unsigned picture_transfer_length(GB_gameboy_t *gb)
{
    unsigned length = 167 + (gb->io_registers[GB_IO_SCX] & 7);
    if (gb->wy_triggered && (gb->io_registers[GB_IO_LCDC] & GB_LCDC_WIN_ENABLE) &&
        gb->io_registers[GB_IO_WX] < 167) length += 6;
    if (!GB_is_cgb(gb) && !(gb->io_registers[GB_IO_LCDC] & GB_LCDC_OBJ_EN)) return length;
    unsigned previous_tile = UINT_MAX;
    for (unsigned i=gb->n_visible_objs;i--;) {
        const object_t *object = (const object_t*)gb->oam + gb->visible_objs[i];
        if (object->x >= 168) continue;
        unsigned source_x = object->x + gb->io_registers[GB_IO_SCX];
        unsigned phase = source_x & 7, tile = source_x >> 3;
        /* The first object waits for its background fragment. Further objects
         * in the same fragment reuse that completed fetch, even at equal X. */
        length += 6 + (tile != previous_tile && phase < 5 ? 5-phase : 0);
        previous_tile = tile;
    }
    return length;
}

/* Partial prefixes need a separate raster so earlier pixels retain their
 * original inputs. Keep its large temporary allocation off the complete-row
 * path, which can write directly into the caller's picture. */
static noinline bool picture_prefix_copy(GB_gameboy_t *gb, unsigned end)
{
    GB_gameboy_t render = *gb;
    uint32_t scratch[WIDTH * LINES];
    render.screen = scratch;
    render.border_mode = GB_BORDER_NEVER;
    render.n_visible_objs = render.orig_n_visible_objs;
#if GBCLLR_OBSERVED_DISPLAY
    /* Private row evaluation must publish only the committed span. Earlier
     * palette/window inputs have already produced their own metadata. */
    uint32_t observed[WIDTH];
    bool observe = gb->llr_bg_emitted_row && gb->llr_bg_source_colors && gb->llr_bg_row_callback;
    render.llr_bg_emitted_row = observed;
    render_line(&render, observe);
    if (observe) memcpy(gb->llr_bg_emitted_row + gb->lcd_x, observed + gb->lcd_x,
                        (end - gb->lcd_x) * sizeof(uint32_t));
#else
    render_line(&render);
#endif
    uint32_t *destination = gb->screen + gb->current_line * WIDTH;
    if (gb->border_mode == GB_BORDER_ALWAYS)
        destination = gb->screen + gb->current_line * BORDERED_WIDTH +
            (BORDERED_WIDTH-WIDTH)/2 + BORDERED_WIDTH*(BORDERED_HEIGHT-LINES)/2;
    memcpy(destination+gb->lcd_x,scratch+gb->current_line*WIDTH+gb->lcd_x,(end-gb->lcd_x)*sizeof(uint32_t));
    return render.window_y != gb->window_y;
}

/* Writes are observation boundaries: materialize only the elapsed prefix,
 * retaining its old inputs. STAT/LY reads merely query the event schedule. */
static void picture_prefix(GB_gameboy_t *gb, unsigned end)
{
    if (end <= gb->lcd_x || gb->current_line >= 144 || !gb->screen || gb->disable_rendering) return;
    gb->llr_display_prefixes++;
    if (end > 160) end = 160;
    bool window;
    if (!GB_is_cgb(gb) && gb->lcd_x == 0 && end == 160) {
        /* render_line owns only these raster working bytes. The picture
         * service owns their persistent phase, just as with a private raster. */
        uint8_t objects = gb->n_visible_objs;
        uint8_t glitch = gb->data_for_sel_glitch;
        uint8_t window_y = gb->window_y;
        uint8_t fetcher = gb->fetcher_state;
        uint8_t tile0 = gb->current_tile_data[0], tile1 = gb->current_tile_data[1];
        gb->n_visible_objs = gb->orig_n_visible_objs;
#if GBCLLR_OBSERVED_DISPLAY
        render_line(gb, gb->llr_bg_emitted_row && gb->llr_bg_source_colors && gb->llr_bg_row_callback);
#else
        render_line(gb);
#endif
        window = gb->window_y != window_y;
        gb->n_visible_objs = objects;
        gb->data_for_sel_glitch = glitch;
        gb->window_y = window_y;
        gb->fetcher_state = fetcher;
        gb->current_tile_data[0] = tile0;
        gb->current_tile_data[1] = tile1;
    }
    else {
        window = picture_prefix_copy(gb,end);
    }
    if (window) gb->wx_triggered = true;
    gb->lcd_x = end;
    if (end == 160 && gb->wx_triggered) gb->window_y++;
}

void GB_frame_picture_before_write(GB_gameboy_t *gb, uint8_t reg)
{
    if (!gb->llr_frame_picture_enabled || gb->display_state != PICTURE_DRAW) return;
    switch (reg) {
        case GB_IO_LCDC: case GB_IO_SCX: case GB_IO_SCY: case GB_IO_WX: case GB_IO_WY:
        case GB_IO_BGP: case GB_IO_OBP0: case GB_IO_OBP1: case GB_IO_BGPD: case GB_IO_OBPD: break;
        default: return;
    }
    unsigned elapsed = gb->mode3_batching_length + gb->display_cycles / 2;
    unsigned start = 12 + (gb->io_registers[GB_IO_SCX] & 7);
    picture_prefix(gb,elapsed > start ? elapsed-start : 0);
}

void GB_display_run(GB_gameboy_t *gb, unsigned cycles, bool force)
{
    if (!gb->llr_frame_picture_enabled) {
        GB_display_reference_run(gb,cycles,force);
        return;
    }
    gb->llr_display_services++;
    unsigned elapsed = cycles + gb->vblank_half_tick;
    gb->cycles_since_vblank_callback += elapsed / 2;
    gb->vblank_half_tick = elapsed & 1;
    gb->frame_parity_ticks += cycles;
    gb->wy_check_modulo += cycles;
    gb->frame_repeat_countdown = cycles < gb->frame_repeat_countdown ? gb->frame_repeat_countdown-cycles : 0;
    gb->display_cycles += cycles;
    if (!(gb->io_registers[GB_IO_LCDC] & GB_LCDC_ENABLE)) {
        if (gb->display_state != PICTURE_OFF) {
            gb->display_state = PICTURE_OFF;
            gb->display_cycles = gb->cycles_since_vblank_callback*2 + gb->vblank_half_tick - LCDC_PERIOD*2;
        }
        while (gb->display_cycles > 0) {
            GB_display_vblank(gb,GB_VBLANK_TYPE_LCD_OFF);
            gb->display_cycles -= LCDC_PERIOD*2;
        }
        return;
    }
    if (gb->display_state < PICTURE_OFF || gb->display_state > LAST_END || gb->display_state==PICTURE_OFF) {
        gb->current_line = 0;
#if GBCLLR_OBSERVED_DISPLAY
        /* Startup bypasses the normal scanline callback. Publish its source
         * before the first row without executing the old dot/FIFO clock. */
        if (gb->llr_first_line_callback) gb->llr_first_line_callback(gb, 0);
#endif
        gb->window_y = -1;
        gb->wy_triggered = false;
        gb->ly_for_comparison=0;gb->io_registers[GB_IO_STAT]&=~3;gb->mode_for_interrupt=-1;
        gb->oam_read_blocked=gb->oam_write_blocked=gb->vram_read_blocked=gb->vram_write_blocked=false;
        gb->cgb_palettes_blocked=false;gb->cycles_for_line=76;GB_STAT_update(gb);
        // DMG LCD startup has its original one-dot delay before first OAM.
        gb->display_state=FIRST_OAM;gb->display_cycles-=(76+!GB_is_cgb(gb))*2;
    }
    if (gb->wy_check_scheduled) {gb->wy_check_scheduled=false;wy_check(gb);}
    while (gb->display_cycles > 0) {
        gb->llr_display_events++;
        switch (gb->display_state) {
            case FIRST_OAM:
                gb->oam_write_blocked=true;gb->cycles_for_line+=2;GB_STAT_update(gb);
                gb->display_state=FIRST_MODE3;gb->display_cycles-=2*2;break;
            case FIRST_MODE3:
                /* Startup has no normal line-mode2 transition. The first
                 * native transfer still observes WY=0 before its window. */
                wy_check(gb);
                gb->n_visible_objs=gb->orig_n_visible_objs=0;gb->cycles_for_line+=8;
                gb->io_registers[GB_IO_STAT]=(gb->io_registers[GB_IO_STAT]&~3)|3;
                gb->mode_for_interrupt=3;gb->oam_write_blocked=gb->oam_read_blocked=true;
                gb->vram_write_blocked=gb->vram_read_blocked=gb->cgb_double_speed;
                gb->cycles_for_line+=2;gb->display_state=FIRST_PALETTE;gb->display_cycles-=2*2;break;
            case FIRST_PALETTE:
                gb->cgb_palettes_blocked=true;gb->cycles_for_line+=3;
                gb->display_state=FIRST_DRAW;gb->display_cycles-=3*2;break;
            case FIRST_DRAW:
                gb->vram_write_blocked=gb->vram_read_blocked=true;
                goto begin_picture_draw;
            case LINE_WRITE:
                gb->oam_write_blocked=true;gb->display_state=LINE_LY;gb->display_cycles-=2;break;
            case LINE_LY:
                gb->io_registers[GB_IO_LY]=gb->current_line;
                gb->oam_read_blocked=!gb->cgb_double_speed||gb->model>=GB_MODEL_CGB_D;
                gb->ly_for_comparison=gb->current_line?(uint16_t)-1:0;
                if(gb->current_line){gb->mode_for_interrupt=2;gb->io_registers[GB_IO_STAT]&=~3;}
                GB_STAT_update(gb);gb->display_state=LINE_MODE2;gb->display_cycles-=2;break;
            case LINE_MODE2:
                gb->oam_read_blocked=gb->oam_write_blocked=true;
                gb->io_registers[GB_IO_STAT]=(gb->io_registers[GB_IO_STAT]&~3)|2;
                gb->mode_for_interrupt=2;gb->ly_for_comparison=gb->current_line;wy_check(gb);GB_STAT_update(gb);
                gb->mode_for_interrupt=-1;GB_STAT_update(gb);
                gb->n_visible_objs=gb->orig_n_visible_objs=0;
                gb->display_state=PICTURE_OAM;gb->display_cycles-=80*2;break;
            case PICTURE_OAM:
                gb->n_visible_objs=0;
                for (unsigned i=0;i<40;i++) add_object_from_index(gb,i);
                gb->orig_n_visible_objs=gb->n_visible_objs;
                gb->accessed_oam_row=-1;
                gb->cycles_for_line=84;
                gb->io_registers[GB_IO_STAT]=(gb->io_registers[GB_IO_STAT]&~3)|3;gb->mode_for_interrupt=3;
                gb->oam_read_blocked=gb->oam_write_blocked=gb->vram_read_blocked=gb->vram_write_blocked=true;
                gb->cgb_palettes_blocked=false;GB_STAT_update(gb);
                gb->cycles_for_line+=3;gb->display_state=MODE3_PALETTE;gb->display_cycles-=3*2;break;
            case MODE3_PALETTE:
                gb->cgb_palettes_blocked=true;gb->cycles_for_line+=2;
                gb->display_state=MODE3_DRAW;gb->display_cycles-=2*2;break;
            case MODE3_DRAW:
begin_picture_draw:
                gb->lcd_x=0;gb->wx_triggered=false;
                gb->mode3_batching_length=picture_transfer_length(gb);
                gb->cycles_for_line+=gb->mode3_batching_length;
                gb->display_state=PICTURE_DRAW;
                gb->display_cycles-=gb->mode3_batching_length*2;
                break;
            case PICTURE_DRAW:
                picture_prefix(gb,160);
                gb->n_visible_objs=0;
                if(!gb->cgb_double_speed){gb->io_registers[GB_IO_STAT]&=~3;gb->mode_for_interrupt=0;
                    gb->oam_read_blocked=gb->model>=GB_MODEL_CGB_D;gb->oam_write_blocked=false;
                    gb->vram_read_blocked=gb->vram_write_blocked=false;}
                gb->cycles_for_line++;gb->display_state=HBLANK_MODE;gb->display_cycles-=2;break;
            case HBLANK_MODE:
                picture_mode(gb,0);gb->cycles_for_line+=2;
                gb->display_state=HBLANK_PALETTE;gb->display_cycles-=2*2;break;
            case HBLANK_PALETTE:
                gb->cgb_palettes_blocked=!gb->cgb_double_speed;
                if(gb->hdma_on_hblank&&!gb->halted&&!gb->stopped)gb->hdma_on=true;
                gb->cycles_for_line+=2;gb->display_state=HBLANK_FREE;gb->display_cycles-=2*2;break;
            case HBLANK_FREE:
                gb->cgb_palettes_blocked=false;
                gb->display_cycles-=(456-gb->cycles_for_line-2)*2;
                gb->cycles_for_line=0;gb->display_state=LINE_END;break;
            case LINE_END:
                gb->display_state=LINE_NEXT;gb->display_cycles-=2*2;break;
            case LINE_NEXT:
                if(gb->current_line!=143)gb->mode_for_interrupt=2;
                gb->current_lcd_line++;gb->current_line++;
                if(gb->current_line<144){picture_line(gb);break;}
                goto begin_vblank_line;
            case VBLANK_REST:
                gb->current_line++;
                if(gb->current_line==153){
                    gb->ly_for_comparison=-1;GB_STAT_update(gb);
                    gb->display_state=LAST_LY;gb->display_cycles-=2*2;break;
                }
begin_vblank_line:
                gb->ly_for_comparison=-1;
                if(gb->lcd_line_callback)gb->lcd_line_callback(gb,gb->current_line);
                GB_STAT_update(gb);gb->display_state=VBLANK_LY;gb->display_cycles-=2*2;break;
            case VBLANK_LY:
                gb->io_registers[GB_IO_LY]=gb->current_line;
                if(gb->current_line==144&&!gb->stat_interrupt_line&&(gb->io_registers[GB_IO_STAT]&0x20))gb->io_registers[GB_IO_IF]|=2;
                gb->display_state=VBLANK_COMPARE;gb->display_cycles-=2*2;break;
            case VBLANK_COMPARE:
                gb->ly_for_comparison=gb->current_line;GB_STAT_update(gb);
                gb->display_state=VBLANK_IRQ;gb->display_cycles-=2;break;
            case VBLANK_IRQ:
                if(gb->current_line==144){
                    gb->io_registers[GB_IO_STAT]=(gb->io_registers[GB_IO_STAT]&~3)|1;gb->io_registers[GB_IO_IF]|=1;
                    if(!gb->stat_interrupt_line&&(gb->io_registers[GB_IO_STAT]&0x20))gb->io_registers[GB_IO_IF]|=2;
                    gb->mode_for_interrupt=1;GB_STAT_update(gb);
                    if(gb->frame_skip_state!=GB_FRAMESKIP_LCD_TURNED_ON)update_frame_parity(gb);
                    GB_display_vblank(gb,GB_VBLANK_TYPE_NORMAL_FRAME);
                    gb->frame_skip_state=GB_FRAMESKIP_FIRST_FRAME_RENDERED;
                }
                gb->frame_repeat_countdown=144*456*2+(gb->model>GB_MODEL_CGB_E?5982:3640);
                if((unsigned)gb->display_cycles<gb->frame_repeat_countdown)gb->frame_repeat_countdown-=gb->display_cycles;
                else gb->frame_repeat_countdown=0;
                gb->display_state=VBLANK_REST;gb->display_cycles-=451*2;break;
            case LAST_LY:
                gb->io_registers[GB_IO_LY]=153;gb->display_state=LAST_COMPARE;
                gb->display_cycles-=(gb->model>GB_MODEL_CGB_C?2:4)*2;break;
            case LAST_COMPARE:
                if(gb->model<=GB_MODEL_CGB_C&&!gb->cgb_double_speed)gb->io_registers[GB_IO_LY]=0;
                gb->ly_for_comparison=153;GB_STAT_update(gb);gb->display_state=LAST_ZERO;
                gb->display_cycles-=(gb->model>GB_MODEL_CGB_C?4:2)*2;break;
            case LAST_ZERO:
                gb->io_registers[GB_IO_LY]=0;gb->ly_for_comparison=(gb->model>GB_MODEL_CGB_C||gb->cgb_double_speed)?153:(uint16_t)-1;
                GB_STAT_update(gb);gb->display_state=LAST_COMPARE_ZERO;gb->display_cycles-=4*2;break;
            case LAST_COMPARE_ZERO:
                gb->ly_for_comparison=0;GB_STAT_update(gb);gb->display_state=LAST_REST;gb->display_cycles-=12*2;break;
            case LAST_REST:
                gb->display_state=LAST_END;gb->display_cycles-=432*2;break;
            case LAST_END:
                gb->current_line=0;gb->window_y=-1;gb->wy_triggered=false;gb->current_lcd_line=0;
                if(gb->icd_vreset_callback)gb->icd_vreset_callback(gb);
                picture_line(gb);break;
        }
    }
}
