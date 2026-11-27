#pragma once
#include <array>
#include <bitset>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <vector>
#include <gbcllr/mods.hpp>
#include <gbcllr/input.hpp>

namespace gbcllr {
class OracleNativeMods;
class Gen2NativeMods;
class Xtreme2Coop;
struct ModContext;
enum class Model { dmg, cgb };
struct Options {
    Model model = Model::dmg;
    bool native = true;
    bool sgb_service = false; // Preboot native protocol profile; display preferences are separate.
    // Fixed before execution. Connected sessions retain reference picture
    // timing until their native picture profile is separately qualified.
    bool reference_picture = false;
    uint64_t seed = 1;
    uint64_t rtc_epoch = 946684800;
    std::vector<uint8_t> boot_rom;
    std::vector<NativeFeature> mods;
};
struct Stats {
    uint64_t cycles, instructions_native, instructions_fallback;
    uint16_t pc, rom_bank;
    uint64_t instructions_chained;
};
struct Registers { uint16_t af, bc, de, hl, sp, pc; uint8_t ime; };
struct SerialPort {
    bool active, internal_clock, data_out;
    uint8_t shifted_bits;
};
struct SerialClock { bool accepted, data_out; };
struct FrameObservation {
    LogicalInput player1,player2;
    uint64_t generation=0; // Host restore generation; never serialized.
    unsigned width=160,height=144,coop_view=0,coop_actor=0;
    bool shared_world=true,hide_closed_rooms=false;
};
// Host diagnostics only. Called around actual executions, including rollback.
// No throwing, execution or mutation from callbacks. Binding survives restore.
struct FrameObserver {
    void* context=nullptr;
    void (*frame)(void*,const class Machine&,const FrameObservation&,bool before) noexcept=nullptr;
    void (*restoring)(void*,const class Machine&) noexcept=nullptr;
};
// Owner-thread callbacks at actual hardware events. No reentrant execution,
// restore or mutation of this machine. Return the sampled peer data bit at an
// internal-clock edge. An unattached input reads high. Host bindings are kept
// across restore, never serialized. The context must outlive the binding.
struct LinkCallbacks {
    void* context = nullptr;
    bool (*serial_exchange)(void*, uint64_t tick, bool data_out) noexcept = nullptr;
    void (*infrared_output)(void*, uint64_t tick, bool on) noexcept = nullptr;
    // Before the internal rising edge updates SO; a cable may suspend here
    // to order both consoles' edges before any following device/CPU action.
    void (*serial_rising)(void*, uint64_t tick, bool data_out) noexcept = nullptr;
};
// One owner/thread per instance. No process-global simulation state.
class Machine {
public:
    static constexpr uint32_t snapshot_schema=12;
    static constexpr uint32_t partial_snapshot_schema=snapshot_schema+1;
    Machine(std::span<const uint8_t> rom, Options options = {});
    ~Machine();
    Machine(const Machine&) = delete;
    Machine& operator=(const Machine&) = delete;
    // Time is in 8 MHz master ticks. Stops at the first instruction boundary
    // at or after the absolute deadline, even when LCD is disabled.
    void run_until(uint64_t deadline);
    void run_frame(uint8_t buttons = 0);
    // Two logical actors in one machine; no second emulated controller port.
    void run_frame(uint8_t player1,uint8_t player2);
    void run_frame(LogicalInput player1,LogicalInput player2={});
    std::vector<uint8_t> coop_state()const; // Opaque current-build diagnostics, empty without co-op.
    void set_buttons(uint8_t buttons); // Right Left Up Down A B Select Start
    std::vector<uint8_t> snapshot() const;
    // Same-process rewind storage. Bytes are private and cannot be imported
    // from a file; reuse the allocation when replacing an old ring entry.
    class RewindState {
        friend class Machine;
        friend class RewindHistory;
        friend class Xtreme2Coop;
        std::shared_ptr<const char> owner_;
        std::vector<uint8_t> bytes_;
    };
    void checkpoint(RewindState&) const;
    bool restore(const RewindState&,std::string& error);
      // Comparison only: local ABI/checksum envelopes, including isolated
      // native menu children, are zeroed. Never use this as a restore input.
      std::vector<uint8_t> simulation_snapshot() const;
    bool restore(std::span<const uint8_t> bytes, std::string& error);
    std::string digest() const;
    Stats stats() const;
    std::string audio_costs() const;
    std::string headroom_costs() const;
    void headroom_report(const std::string& prefix) const; // Cumulative host cost counters; never simulation state.
    std::string wait_costs() const;
    std::string picture_costs() const;
    std::span<const uint32_t> pixels() const;
    // Pure host viewport input. A simulation-affecting view module must be
    // selected before boot; its effective width/state are in snapshots.
    void mod_viewport(unsigned width,unsigned height);
    void mod_oracle_visibility(bool hide_closed_rooms); // Host-only room reveal policy.
    void mod_coop_view(unsigned mode,unsigned actor=0); // Host-only unified/peer/split presentation.
    void mod_coop_shared_world(bool enabled); // Common gameplay policy, sampled before simulation.
    ModView mod_view();
    NativeSgbView sgb_view() const;
    // Gen2 host presentation only: dimensions and raster caches are excluded
    // from machine/continuation state. The original full snapshot is retained.
    ModView gen2_presentation_view(unsigned width,unsigned height);
    // Co-op host presentation only. Keep the agreed gameplay viewport and
    // shared-world policy fixed while a local window changes dimensions.
      ModView coop_presentation_view(unsigned width,unsigned height,int menu_actor=-1);
    std::vector<int16_t> take_audio();
    std::vector<uint32_t> take_trace();
    void trace_enabled(bool enabled);
    std::vector<uint8_t> battery() const;
    bool load_battery(std::span<const uint8_t> bytes, std::string& error);
    void write_frame(const std::filesystem::path&) const;
    std::array<uint8_t, 8> registers() const;
    std::string serial_output() const;
    std::span<const uint8_t> cartridge_ram() const;
    std::string rom_identity() const;
    std::string diagnostic_metadata() const; // Effective settings; no paths or source bytes.
    void frame_observer(FrameObserver observer) noexcept;
    uint64_t restore_generation() const noexcept {return restore_generation_;}
    void link_callbacks(LinkCallbacks callbacks);
    SerialPort serial_port() const;
    // Paused external-clock edge, at this machine's current time. The future
    // connected scheduler must align the peer before applying an edge.
    SerialClock serial_clock(bool data_in);
    // Edge-level cable contract. An active external receiver latches SO on
    // high and shifts SI on low. Internal receivers ignore external edges.
    SerialClock serial_clock_edge(bool high,bool data_in=false);
    bool serial_pin() const; // Latched SO, retained after transfer completion.
    void infrared_input(bool on);
    // Paused, offline QA interface. Reads operate on an isolated copy so even
    // donor IO synchronization cannot perturb the observed live machine.
    std::vector<uint8_t> debug_read(uint16_t address, size_t length) const;
    void debug_write(uint16_t address, std::span<const uint8_t> values);
    Registers debug_registers() const;
    void debug_registers(Registers registers);
    void debug_wake(); // Explicit QA bypass of HALT/STOP and pending CPU entry flags.
    // One CPU instruction (or halted hardware quantum), including an enhanced
    // instruction which consumes CPU headroom without advancing physical time.
    void debug_step_instruction();
private:
    friend class RewindHistory;
    friend class CablePair;
    void headroom_frame_end();
    friend class OracleNativeMods;
    friend class Gen2NativeMods;
    friend class Xtreme2Coop;
    // Owner-thread internal rewind sink. Spans are consumed before return;
    // external files still use the sealed archive and its validation path.
    void checkpoint_chunks(void*,void (*)(void*,const uint8_t*,size_t)) const;
    bool direct_checkpoint_history() const;
    std::span<uint8_t> mod_wram();
    std::span<uint8_t> mod_hram();
    std::span<uint8_t> native_gift_ram();
    // Native presentation services may execute isolated original-game rooms.
    // Copies have no modules/link callbacks and cannot export native saves.
    std::unique_ptr<Machine> presentation_copy() const;
    void presentation_refresh(const Machine& owner);
    void presentation_refresh(std::span<const uint8_t> aligned_owner,bool sealed=true);
    void presentation_menu_frames(std::span<const uint32_t> restored = {});
    void presentation_advance_until(const std::bitset<65536>& stops,uint64_t deadline);
    std::array<uint8_t,2> presentation_banks() const;
    ModContext presentation_context();
    void cooperative_state(bool active,bool boundary,uint64_t tick);
    void input_hook(void*,void (*)(void*,uint8_t));
    std::vector<uint8_t> continuation_view() const;
    // Append the unchanged bounded codec to a containing paired archive.
    // Only its owning cable may request a partial CPU continuation view.
    void append_state(std::vector<uint8_t>&,bool continuation) const;
    // Internal cooperative execution hook. Only CablePair owns this binding;
    // public snapshots are still taken at complete instruction boundaries.
    void execution_hook(void*, bool (*)(void*, uint64_t, unsigned) noexcept);
    struct Impl;
    bool restore_state(std::span<const uint8_t>,std::string&,bool sealed);
    template<bool SingleInstruction> void execute_until(uint64_t deadline);
    std::unique_ptr<Impl> impl_;
    std::shared_ptr<const char> checkpoint_owner_=std::make_shared<const char>(0);
    FrameObserver frame_observer_;
    FrameObservation frame_observation_;
    uint64_t restore_generation_=0;
};
std::string sha256(std::span<const uint8_t>);
std::vector<uint8_t> read_file(const std::filesystem::path&);
void write_file_atomic(const std::filesystem::path&, std::span<const uint8_t>);
}
