#include "gb.h"

/* One guest HALT operation. The CPU contributes no work between its entry
 * and an enabled interrupt; only device deadlines and the host horizon can
 * end an interval. LLE and unsupported profiles keep their original path. */
#if GBCLLR_NATIVE_WAIT && GBCLLR_FRAME_AUDIO
static unsigned halt_interval(GB_gameboy_t *gb)
{
    if (gb->tima_reload_state != GB_TIMA_RUNNING) return 4;
    unsigned scale = gb->cgb_double_speed ? 1 : 2;
    unsigned remaining = gb->llr_cycle_budget - gb->cycles_since_run;
    unsigned limit = remaining / scale;
    /* GB_advance_cycles stores its doubled single-speed quantum in a byte. */
    if (limit > 124) limit = 124;
    if (gb->display_cycles >= 0 || gb->wy_check_scheduled || gb->delayed_glitch_hblank_interrupt) return 4;
    uint64_t picture = (uint64_t)-(int64_t)gb->display_cycles / scale;
    if (picture < limit) limit = picture;
    if (limit <= 7) return 4;

    /* A DIV edge may alter a short APU sweep. Timer overflow and JOYP/serial
     * observers use their unchanged quantum instead of speculative batching. */
    unsigned bit = gb->cgb_double_speed ? 0x2000 : 0x1000;
    unsigned div = bit - (gb->div_counter & (bit - 1));
    div = div > 3 ? div - 3 : 0;
    if (div < limit) limit = div;
    if (gb->io_registers[GB_IO_TAC] & 4) {
        static const unsigned periods[] = {1024, 16, 64, 256};
        unsigned period = periods[gb->io_registers[GB_IO_TAC] & 3];
        unsigned timer = period - (gb->div_counter & (period - 1));
#if GBCLLR_ELAPSED_TIMERS
        /* Counter increments are arithmetic; the next CPU-visible event is
         * overflow, followed by the unchanged four-clock reload delay. */
        timer += (255 - gb->io_registers[GB_IO_TIMA]) * period;
#endif
        timer = timer > 3 ? timer - 3 : 0;
        if (timer < limit) limit = timer;
    }
    unsigned audio = GB_apu_frame_wait_limit(gb);
    if (audio < limit) limit = audio;
    limit &= ~3u;
    return limit >= 4 ? limit : 4;
}
#endif

bool GB_native_halt_wait(GB_gameboy_t *gb)
{
#if GBCLLR_NATIVE_WAIT && GBCLLR_FRAME_AUDIO
    if (!gb->halted || gb->stopped || !GB_is_cgb(gb) || !gb->llr_native_wait_allowed ||
        gb->llr_cycle_callback || gb->llr_cpu_extra_budget || gb->update_input_hint_callback ||
        gb->speed_switch_countdown || gb->speed_switch_freeze || gb->speed_switch_halt_countdown ||
        (gb->io_registers[GB_IO_SC] & 0x80))
        return false;
    if (gb->cycles_since_run >= gb->llr_cycle_budget) return false;
    if (gb->interrupt_enable & gb->io_registers[GB_IO_IF] & 0x1F) return false;
    gb->llr_wait_costs.calls++;
    // Only CPU writes or host input setters introduce these phases. The CPU
    // is halted and host input callbacks use the reference path; device
    // clocks only decrement the timers. Their shared doubled-clock horizon
    // is therefore fixed until this operation returns to its owner.
    unsigned input_remaining = gb->joyp_switching_delay;
    for (unsigned key = 0; key < GB_KEY_MAX; key++) {
        if (gb->key_bounce_timing[key] > input_remaining) input_remaining = gb->key_bounce_timing[key];
    }
    uint64_t input_deadline = (uint64_t)gb->cycles_since_run + input_remaining;
    for (;;) {
        // Own only the rare reference phases. Input cannot restart without
        // an excluded CPU/host observer; OAM DMA cannot start without a CPU
        // write. Do not repeat their eligibility work in the steady loop.
        while (gb->cycles_since_run < input_deadline || gb->ime_toggle ||
               gb->tima_reload_state != GB_TIMA_RUNNING || gb->hdma_on || GB_is_dma_active(gb)) {
            if (gb->interrupt_enable & gb->io_registers[GB_IO_IF] & 0x1F) return false;
            GB_advance_cycles(gb, 4);
            gb->just_halted = false;
            gb->llr_wait_costs.advances++;
            gb->llr_wait_costs.cpu_cycles += 4;
            if (gb->ime_toggle) {gb->ime = !gb->ime; gb->ime_toggle = false;}
            if (!(gb->io_registers[GB_IO_IF] & 0x10) &&
                (gb->io_registers[GB_IO_JOYP] & 0x30) != 0x30) gb->joyp_accessed = true;
            if (gb->vblank_just_occured || gb->cycles_since_run >= gb->llr_cycle_budget) return true;
        }
        // Retain the accepted deadline loop and shared CPU wake quantum.
        // A PPU deadline can activate already-armed HBlank DMA; return to
        // reference phases at that real transition before another jump.
        while (!(gb->interrupt_enable & gb->io_registers[GB_IO_IF] & 0x1F)) {
            unsigned interval = halt_interval(gb);
            unsigned on = gb->rumble_strength & 3;
            GB_advance_cycles(gb, interval);
            unsigned skipped = interval / 4 - 1;
            gb->rumble_on_cycles += skipped * on;
            gb->rumble_off_cycles += skipped * (on ^ 3);
            gb->just_halted = false;
            gb->llr_wait_costs.advances++;
            gb->llr_wait_costs.cpu_cycles += interval;
            if (interval > 4) gb->llr_wait_costs.batched_cycles += interval;
            if (gb->vblank_just_occured || gb->cycles_since_run >= gb->llr_cycle_budget) return true;
            if (unlikely(gb->hdma_on)) break;
        }
        if (!gb->hdma_on) return false;
    }
#endif
    return false;
}
