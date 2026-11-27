/* Build-selected timestamped audio service.
 *
 * The CPU produces register/DIV events and PCM deadlines. Synthesis consumes
 * their whole interval on observation or frame completion. Channel-edge and
 * band-limited mixing arithmetic are shared with the reference implementation;
 * ordinary CPU/device advances never enter that implementation.
 */
#define GB_apu_run GB_apu_reference_run
#define GB_apu_read GB_apu_reference_read
#define GB_apu_write GB_apu_reference_write
#define GB_apu_div_event GB_apu_reference_div_event
#define GB_apu_div_secondary_event GB_apu_reference_div_secondary_event
#define GB_apu_delayed_envelope_tick GB_apu_reference_delayed_envelope_tick
#include "apu.c"
#undef GB_apu_run
#undef GB_apu_read
#undef GB_apu_write
#undef GB_apu_div_event
#undef GB_apu_div_secondary_event
#undef GB_apu_delayed_envelope_tick

enum { FRAME_CLOCK, FRAME_WRITE, FRAME_DIV, FRAME_DIV_SECONDARY, FRAME_ENVELOPE, FRAME_FINE };
typedef struct {
    uint32_t sample_cycles;
    uint16_t cycles, address_bus, div_counter, pc;
    uint8_t type, reg, value, context;
} AudioEvent;
typedef struct {
    unsigned count;
    unsigned cycles_since_render;
    bool initialized;
    bool replaying;
#if GBCLLR_AUDIO_OPERATION_DMG
    bool pcm_reset_pending;
#endif
#if GBCLLR_AUDIO_OPERATION
    unsigned capacity, fine_cycles;
    bool powered, wave_enabled, wave_pulsed, wave_active, wave_length_enabled;
    uint16_t wave_length;
    uint8_t div_phase, skip_div;
    AudioEvent *events;
#else
    AudioEvent events[2048];
#endif
} FrameAudio;

static bool frame_model_supported(GB_gameboy_t *gb)
{
#if GBCLLR_AUDIO_OPERATION_DMG
    return GB_is_cgb(gb) || gb->model == GB_MODEL_DMG_B;
#else
    return GB_is_cgb(gb);
#endif
}

static void operation_span_run(GB_gameboy_t *gb)
{
#if GBCLLR_AUDIO_OPERATION_DMG
    /* DMG active wave clocking also establishes the last machine quantum's
     * channel-edge flags. Consume the unobserved bulk analytically, then
     * materialize that final quantum for register/read boundary semantics. */
    if (!GB_is_cgb(gb) && gb->apu.wave_channel.enable && gb->apu.wave_channel.pulsed &&
        gb->apu.apu_cycles > 2) {
        const uint32_t tail_samples = gb->apu_output.sample_rate * 4;
        gb->apu.apu_cycles -= 2;
        gb->apu_output.sample_cycles -= tail_samples;
        GB_apu_reference_run(gb, true);
        gb->apu.apu_cycles += 2;
        gb->apu_output.sample_cycles += tail_samples;
    }
#endif
    GB_apu_reference_run(gb, true);
}

#if GBCLLR_AUDIO_OPERATION
static void operation_control_sync(GB_gameboy_t *gb, FrameAudio *frame)
{
    frame->powered = gb->apu.global_enable;
    frame->wave_enabled = gb->apu.wave_channel.enable;
    frame->wave_pulsed = gb->apu.wave_channel.pulsed;
    frame->wave_active = gb->apu.is_active[GB_WAVE];
    frame->wave_length_enabled = gb->apu.wave_channel.length_enabled;
    frame->wave_length = gb->apu.wave_channel.pulse_length;
    frame->div_phase = gb->apu.div_divider;
    frame->skip_div = gb->apu.skip_div_event;
    if (gb->apu.square_sweep_calculate_countdown || gb->apu.channel_1_restart_hold ||
        gb->apu.square_sweep_calculate_countdown_reload_timer || gb->apu.wave_channel.bugged_read_countdown)
        frame->fine_cycles = 32;
}

/* The producer needs only wave activity to select externally relevant bus
 * observations. Length/DIV transitions are a native control operation; no
 * oscillator, mixer, filter or sample synthesis runs here. */
static void operation_control_write(GB_gameboy_t *gb, FrameAudio *frame, uint8_t reg, uint8_t value)
{
    if (reg == GB_IO_NR52) {
        bool powered = value & 0x80;
        if (powered != frame->powered) {
            unsigned old_length = frame->wave_length;
            frame->powered = powered;
            frame->wave_enabled = frame->wave_pulsed = frame->wave_active = frame->wave_length_enabled = false;
            frame->wave_length = 0;
#if GBCLLR_AUDIO_OPERATION_DMG
            if (powered && !GB_is_cgb(gb)) frame->wave_length = old_length;
#else
            (void)old_length;
#endif
            frame->div_phase = 0;
            frame->skip_div = GB_SKIP_DIV_EVENT_INACTIVE;
            if (powered && (gb->div_counter & (gb->cgb_double_speed ? 0x2000 : 0x1000))) {
                frame->div_phase = 1;
                frame->skip_div = GB_SKIP_DIV_EVENT_SKIP;
            }
            gb->apu.pending_envelope_tick = false;
        }
        return;
    }
    if (!frame->powered) {
#if GBCLLR_AUDIO_OPERATION_DMG
        if (!GB_is_cgb(gb) && reg == GB_IO_NR31) frame->wave_length = 0x100-value;
#endif
        return;
    }
    if (reg == GB_IO_NR30) {
        frame->wave_enabled = value & 0x80;
        if (!frame->wave_enabled) frame->wave_pulsed = frame->wave_active = false;
    }
    else if (reg == GB_IO_NR31) frame->wave_length = 0x100-value;
    else if (reg == GB_IO_NR34) {
        if (value & 0x80) {
            frame->wave_pulsed = true;
            if (frame->wave_enabled) frame->wave_active = true;
            if (!frame->wave_length) {
                frame->wave_length = 0x100;
                frame->wave_length_enabled = false;
            }
        }
        if ((value & 0x40) && !frame->wave_length_enabled && (frame->div_phase & 1) && frame->wave_length) {
            if (!--frame->wave_length) {
                if (value & 0x80) frame->wave_length = 0xFF;
                else frame->wave_active = false;
            }
        }
        frame->wave_length_enabled = value & 0x40;
    }
}

static bool operation_control_div(FrameAudio *frame)
{
    if (!frame->powered) return false;
    if (frame->skip_div == GB_SKIP_DIV_EVENT_SKIP) {
        frame->skip_div = GB_SKIP_DIV_EVENT_SKIPPED;
        return false;
    }
    if (frame->skip_div == GB_SKIP_DIV_EVENT_SKIPPED) frame->skip_div = GB_SKIP_DIV_EVENT_INACTIVE;
    else frame->div_phase++;
    if ((frame->div_phase & 1) && frame->wave_length_enabled && frame->wave_length && !--frame->wave_length)
        frame->wave_active = false;
    return true;
}
#endif

static FrameAudio *frame_audio(GB_gameboy_t *gb)
{
    if (unlikely(!gb->llr_audio_frame)) {
        gb->llr_audio_frame = calloc(1, sizeof(FrameAudio));
        if (!gb->llr_audio_frame) abort();
    }
    FrameAudio *frame = gb->llr_audio_frame;
    if (unlikely(!frame->initialized)) {
        frame->cycles_since_render = gb->apu_output.cycles_since_render;
#if GBCLLR_AUDIO_OPERATION
        operation_control_sync(gb, frame);
#endif
        frame->initialized = true;
    }
    return frame;
}

void GB_apu_frame_flush(GB_gameboy_t *gb)
{
    FrameAudio *frame = gb->llr_audio_frame;
    if (!frame || frame->replaying) return;
    if (!frame->count) {
#if GBCLLR_AUDIO_OPERATION_DMG
        if (frame->pcm_reset_pending) {
            gb->apu.pcm_mask[0] = gb->apu.pcm_mask[1] = 0xFF;
            frame->pcm_reset_pending = false;
        }
#endif
        return;
    }
    gb->llr_audio_costs.blocks++;
    uint16_t pending_cycles = gb->apu.apu_cycles;
    uint32_t sample_cycles = gb->apu_output.sample_cycles;
    uint16_t address_bus = gb->address_bus, div_counter = gb->div_counter, pc = gb->pc;
    bool double_speed = gb->cgb_double_speed, stopped = gb->stopped;
    bool div_write = gb->during_div_write, envelope = gb->apu.pending_envelope_tick;
    frame->replaying = true;
#if GBCLLR_AUDIO_OPERATION
    gb->apu.apu_cycles = 0;
#endif
    for (unsigned i = 0; i < frame->count; i++) {
        const AudioEvent *event = &frame->events[i];
#if GBCLLR_AUDIO_OPERATION
        gb->apu.apu_cycles += event->cycles;
#else
        gb->apu.apu_cycles = event->cycles;
#endif
        gb->apu_output.sample_cycles = event->sample_cycles;
        gb->address_bus = event->address_bus;
        gb->div_counter = event->div_counter;
        gb->pc = event->pc;
        gb->cgb_double_speed = event->context & 1;
        gb->stopped = event->context & 2;
        gb->during_div_write = event->context & 4;
#if GBCLLR_AUDIO_OPERATION_DMG
        if (event->context & 8) gb->apu.pcm_mask[0] = gb->apu.pcm_mask[1] = 0xFF;
#endif
        switch (event->type) {
            case FRAME_CLOCK: operation_span_run(gb); break;
            case FRAME_WRITE:
#if GBCLLR_AUDIO_OPERATION_DMG
                if (!GB_is_cgb(gb)) operation_span_run(gb);
#endif
                GB_apu_reference_write(gb, event->reg, event->value); break;
            case FRAME_DIV:
#if GBCLLR_AUDIO_OPERATION_DMG
                if (!GB_is_cgb(gb)) operation_span_run(gb);
#endif
                GB_apu_reference_div_event(gb); break;
            case FRAME_DIV_SECONDARY:
#if GBCLLR_AUDIO_OPERATION_DMG
                if (!GB_is_cgb(gb)) operation_span_run(gb);
#endif
                GB_apu_reference_div_secondary_event(gb); break;
            case FRAME_ENVELOPE:
#if GBCLLR_AUDIO_OPERATION_DMG
                if (!GB_is_cgb(gb) && gb->apu.global_enable) operation_span_run(gb);
#endif
                GB_apu_reference_delayed_envelope_tick(gb); break;
            case FRAME_FINE:
                /* The operation scans the bus timeline but enters synthesis
                 * only when a phase-sensitive channel actually observes it.
                 * Ordinary active channels consume an analytic span instead. */
                if (gb->apu.square_sweep_calculate_countdown || gb->apu.channel_1_restart_hold ||
                    gb->apu.square_sweep_calculate_countdown_reload_timer ||
                    gb->apu.wave_channel.bugged_read_countdown ||
                    (gb->apu.wave_channel.enable && gb->apu.wave_channel.pulsed &&
                     !gb->apu.is_active[GB_WAVE]))
                    operation_span_run(gb);
                break;
        }
    }
    frame->count = 0;
    frame->replaying = false;
#if GBCLLR_AUDIO_OPERATION
    gb->apu.apu_cycles += pending_cycles;
    frame->cycles_since_render = gb->apu_output.cycles_since_render;
    operation_control_sync(gb, frame);
#else
    gb->apu.apu_cycles = pending_cycles;
#endif
    gb->apu_output.sample_cycles = sample_cycles;
    gb->address_bus = address_bus;
    gb->div_counter = div_counter;
    gb->pc = pc;
    gb->cgb_double_speed = double_speed;
    gb->stopped = stopped;
    gb->during_div_write = div_write;
    gb->apu.pending_envelope_tick = envelope;
#if GBCLLR_AUDIO_OPERATION_DMG
    /* GB_advance_cycles resets these latches even when no synthesis is due. */
    if (frame->pcm_reset_pending) {
        gb->apu.pcm_mask[0] = gb->apu.pcm_mask[1] = 0xFF;
        frame->pcm_reset_pending = false;
    }
#endif
}

static void frame_event(GB_gameboy_t *gb, unsigned type, uint8_t reg, uint8_t value)
{
    gb->llr_audio_costs.queued_events++;
    FrameAudio *frame = frame_audio(gb);
#if GBCLLR_AUDIO_OPERATION
    if (unlikely(frame->count == frame->capacity)) {
        unsigned capacity = frame->capacity ? frame->capacity * 2 : 2048;
        AudioEvent *events = realloc(frame->events, capacity * sizeof(*events));
        if (!events) abort();
        frame->events = events;
        frame->capacity = capacity;
    }
#else
    if (unlikely(frame->count == sizeof(frame->events) / sizeof(frame->events[0])))
        GB_apu_frame_flush(gb);
#endif
    AudioEvent *event = &frame->events[frame->count++];
    *event = (AudioEvent){
        .sample_cycles = gb->apu_output.sample_cycles,
        .cycles = gb->apu.apu_cycles,
        .address_bus = gb->address_bus,
        .div_counter = gb->div_counter,
        .pc = gb->pc,
        .type = type, .reg = reg, .value = value,
        .context = gb->cgb_double_speed | (gb->stopped << 1) | (gb->during_div_write << 2),
    };
#if GBCLLR_AUDIO_OPERATION_DMG
    if (frame->pcm_reset_pending) event->context |= 8;
    frame->pcm_reset_pending = false;
#endif
    if (gb->apu_output.sample_rate) frame->cycles_since_render += gb->apu.apu_cycles;
    gb->apu.apu_cycles = 0;
    uint32_t rate = GB_get_clock_rate(gb);
    if (gb->apu_output.sample_cycles >= rate) {
        gb->apu_output.sample_cycles -= rate;
        frame->cycles_since_render = 0;
    }
}

void GB_apu_frame_clock(GB_gameboy_t *gb)
{
    if (!frame_model_supported(gb)) { GB_apu_reference_run(gb, false); return; }
    FrameAudio *frame = frame_audio(gb);
#if GBCLLR_AUDIO_OPERATION_DMG
    frame->pcm_reset_pending = true;
#endif
#if GBCLLR_AUDIO_OPERATION
    bool fine = frame->fine_cycles || (frame->wave_enabled && frame->wave_pulsed && !frame->wave_active);
    unsigned elapsed = gb->apu.apu_cycles;
    frame->fine_cycles = elapsed < frame->fine_cycles ? frame->fine_cycles-elapsed : 0;
#endif
    bool exceptional = gb->apu.square_sweep_calculate_countdown ||
        gb->apu.channel_1_restart_hold || gb->apu.square_sweep_calculate_countdown_reload_timer ||
        (gb->model <= GB_MODEL_CGB_E && (gb->apu.wave_channel.bugged_read_countdown ||
         (gb->apu.wave_channel.enable && gb->apu.wave_channel.pulsed && !gb->apu.is_active[GB_WAVE])));
#if GBCLLR_AUDIO_OPERATION
    exceptional = false; // The block consumer evaluates the live channel state.
#endif
    if (unlikely(gb->apu_output.sample_cycles >= GB_get_clock_rate(gb)) ||
        unlikely(gb->apu.apu_cycles + frame->cycles_since_render >= gb->apu_output.max_cycles_per_sample) ||
        unlikely(gb->apu.apu_cycles >= 0x4000) || unlikely(exceptional)) {
        frame_event(gb, FRAME_CLOCK, 0, 0);
#if !GBCLLR_AUDIO_OPERATION
        if (unlikely(exceptional)) GB_apu_frame_flush(gb);
#endif
    }
#if GBCLLR_AUDIO_OPERATION
    else if (fine) frame_event(gb, FRAME_FINE, 0, 0);
#endif
}

void GB_apu_frame_finish(GB_gameboy_t *gb)
{
    GB_apu_frame_flush(gb);
    /* The reference clocks an active wave at every access. Materialize only
     * that pending tail at a host observation; inactive spans remain deferred.
     * DMG uses the reference below until its span sample math is qualified. */
    if (frame_model_supported(gb) && gb->apu.apu_cycles &&
        (gb->apu.wave_channel.enable && gb->apu.wave_channel.pulsed)) {
        operation_span_run(gb);
        frame_audio(gb)->cycles_since_render = gb->apu_output.cycles_since_render;
    }
}

unsigned GB_apu_frame_wait_limit(GB_gameboy_t *gb)
{
    FrameAudio *frame = frame_audio(gb);
#if GBCLLR_AUDIO_OPERATION
    if (frame->fine_cycles || (frame->wave_enabled && frame->wave_pulsed && !frame->wave_active)) return 4;
#endif
#if !GBCLLR_AUDIO_OPERATION
    if (gb->apu.square_sweep_calculate_countdown || gb->apu.channel_1_restart_hold ||
        gb->apu.square_sweep_calculate_countdown_reload_timer || gb->apu.wave_channel.bugged_read_countdown ||
        (gb->apu.wave_channel.enable && gb->apu.wave_channel.pulsed && !gb->apu.is_active[GB_WAVE])) return 4;
#endif
    unsigned accrued = gb->apu.apu_cycles + frame->cycles_since_render;
    if (accrued >= gb->apu_output.max_cycles_per_sample) return 4;
    unsigned ticks = gb->apu_output.max_cycles_per_sample - accrued;
    unsigned unit = 1u << !gb->cgb_double_speed;
    unsigned limit = ((ticks + unit - 1) / unit) * 4;
    if (gb->apu_output.sample_rate) {
        unsigned rate = GB_get_clock_rate(gb), sample = gb->apu_output.sample_cycles;
        if (sample >= rate) return 4;
        unsigned step = (gb->apu_output.sample_rate << !gb->cgb_double_speed) << 1;
        unsigned sample_limit = ((rate - sample + step - 1) / step) * 4;
        if (sample_limit < limit) limit = sample_limit;
    }
    /* A timer quantum can straddle the caller's current phase by up to 3 CPU
     * clocks. Leave the actual deadline to one unchanged 4-clock advance. */
    return limit > 7 ? (limit - 3) & ~3u : 4;
}

void GB_apu_run(GB_gameboy_t *gb, bool force)
{
    if (!force) { GB_apu_frame_clock(gb); return; }
    GB_apu_frame_flush(gb);
    operation_span_run(gb);
    frame_audio(gb)->cycles_since_render = gb->apu_output.cycles_since_render;
}

uint8_t GB_apu_read(GB_gameboy_t *gb, uint8_t reg)
{
    if (!frame_model_supported(gb)) return GB_apu_reference_read(gb, reg);
    GB_apu_frame_flush(gb);
#if GBCLLR_AUDIO_OPERATION_DMG
    if (!GB_is_cgb(gb)) operation_span_run(gb);
#endif
    uint8_t value = GB_apu_reference_read(gb, reg);
    frame_audio(gb)->cycles_since_render = gb->apu_output.cycles_since_render;
    return value;
}

void GB_apu_write(GB_gameboy_t *gb, uint8_t reg, uint8_t value)
{
    if (!frame_model_supported(gb)) { GB_apu_reference_write(gb, reg, value); return; }
#if GBCLLR_AUDIO_OPERATION
    FrameAudio *frame = frame_audio(gb);
    frame_event(gb, FRAME_WRITE, reg, value);
    frame->fine_cycles = 32;
    operation_control_write(gb, frame, reg, value);
    return;
#endif
    /* Power changes immediately affect STOP and the DIV producer. */
    if (reg == GB_IO_NR52) {
        GB_apu_frame_flush(gb);
        GB_apu_reference_write(gb, reg, value);
        frame_audio(gb)->cycles_since_render = gb->apu_output.cycles_since_render;
        return;
    }
    frame_event(gb, FRAME_WRITE, reg, value);
    GB_apu_frame_flush(gb);
}

void GB_apu_div_event(GB_gameboy_t *gb)
{
    if (!frame_model_supported(gb)) { GB_apu_reference_div_event(gb); return; }
    frame_event(gb, FRAME_DIV, 0, 0);
#if GBCLLR_AUDIO_OPERATION
    frame_audio(gb)->fine_cycles = 32;
    bool envelope_tick = operation_control_div(frame_audio(gb));
#else
    GB_apu_frame_flush(gb);
#endif
    if (
#if GBCLLR_AUDIO_OPERATION
        envelope_tick &&
#else
        gb->apu.global_enable &&
#endif
        gb->cgb_double_speed &&
        (gb->model == GB_MODEL_CGB_D || gb->model == GB_MODEL_CGB_E))
        gb->apu.pending_envelope_tick = true;
}

void GB_apu_div_secondary_event(GB_gameboy_t *gb)
{
    if (!frame_model_supported(gb)) { GB_apu_reference_div_secondary_event(gb); return; }
    frame_event(gb, FRAME_DIV_SECONDARY, 0, 0);
#if GBCLLR_AUDIO_OPERATION
    frame_audio(gb)->fine_cycles = 32;
#else
    GB_apu_frame_flush(gb);
#endif
}

void GB_apu_delayed_envelope_tick(GB_gameboy_t *gb)
{
    if (!frame_model_supported(gb)) { GB_apu_reference_delayed_envelope_tick(gb); return; }
    frame_event(gb, FRAME_ENVELOPE, 0, 0);
#if !GBCLLR_AUDIO_OPERATION
    GB_apu_frame_flush(gb);
#endif
    gb->apu.pending_envelope_tick = false;
}

void GB_apu_frame_discard(GB_gameboy_t *gb)
{
    FrameAudio *frame = gb->llr_audio_frame;
    if (frame) { frame->count = 0; frame->replaying = false; frame->initialized = false; }
}

void GB_apu_frame_free(GB_gameboy_t *gb)
{
#if GBCLLR_AUDIO_OPERATION
    if (gb->llr_audio_frame) free(((FrameAudio*)gb->llr_audio_frame)->events);
#endif
    free(gb->llr_audio_frame);
    gb->llr_audio_frame = NULL;
}
