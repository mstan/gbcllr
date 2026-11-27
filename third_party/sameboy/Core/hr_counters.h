#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct {
    uint64_t frames, lcd_off_frames, double_speed_frames;
    uint64_t gb_run, cpu_exec, cpu_halt_step, cpu_stopped_step, cpu_irq, cpu_halt_wake_noime, cpu_halt_enter;
    uint64_t irq_bit[5];
    uint64_t adv_calls, adv_split_calls, adv_hist[17], adv_cpu_cycles, adv_dots;
    uint64_t adv_halted_calls, adv_halted_dots, adv_mode_dots[5], adv_mode_calls[5];
    uint64_t apu_run_calls, apu_samples, display_run_calls, div_apu_events;
    uint64_t tima_inc, tima_overflow, serial_master_edges, serial_bits, cycle_callback_calls;
    uint64_t dma_run_active, dma_bytes, hdma_run, mbc_updates, rom_bank_changes;
    uint64_t rd_region[16], wr_region[16], rd_io[256], wr_io[256];
    uint64_t adv_large_cycles;
} GB_headroom_counters_t;
#ifndef GBCLLR_HEADROOM_COUNTERS
#define GBCLLR_HEADROOM_COUNTERS 1
#endif
#if GBCLLR_HEADROOM_COUNTERS
#define HR_COUNT(...) do { __VA_ARGS__; } while (0)
#else
#define HR_COUNT(...) ((void)0)
#endif
#ifdef __cplusplus
}
#endif
