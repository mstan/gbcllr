#pragma once
#include <gbcllr/cable.hpp>
#include <functional>

struct RNetSession;
struct RNetSignal;
namespace gbcllr {
enum class NetTopology { cable, xtreme2_coop };
struct NetOptions {
    unsigned local_slot=0,delay=3,prediction=6,snapshot_depth=64;
    // Session-slot to ordered console/logical-player mapping, fixed for the match. This
    // preserves launcher seat moves without changing the host's wire role.
    std::array<unsigned,2> machine_slots{0,1};
    uint32_t session_id=1;
    CableSchedule schedule=CableSchedule::common_instruction;
    std::string bind="127.0.0.1:7777",peer="127.0.0.1:7778";
    // Optional owner-supplied transport handover, invoked once after profile
    // setup and before any guest execution. Zero transfers the established
    // transport to recomp-net; a nonzero result refuses startup. A captured
    // lease must release resources if startup fails or is abandoned.
    // Empty preserves the ordinary bind/peer UDP path.
    std::function<int(RNetSession*)> start_transport;
    // Optional signaling for a transport still connecting. It must not
    // advance a guest or change the fixed simulation profile.
    std::function<void(RNetSession*)> pump_transport;
    std::function<void(const RNetSignal&)> signal_transport;
    std::vector<uint8_t> initial_pair;
    // Local, validated native starts. Both peers supply the same ordered pair;
    // bytes are fingerprinted, never transferred or persisted by the session.
    // Mutually exclusive with initial_pair.
    std::array<std::vector<uint8_t>,2> initial_batteries;
    std::function<uint8_t(uint32_t wire_tick)> sample;
    std::function<void(const std::string&)> log;
    // Owner-thread presentation only. Remote/replayed PCM is discarded. The
    // sample span is valid only for this call; never mutate the machine.
    std::function<void(uint32_t frame,const Machine&,std::span<const int16_t>)> present;
    // Local live frames only; view pixels are valid for this call. Raster
    // dimensions/cache never enter guest state or the common profile.
    std::function<void(uint32_t frame,const ModView&)> present_view;
    std::function<void()> presentation_reset;
    NetTopology topology=NetTopology::cable;
    unsigned coop_width=384;
    bool coop_shared=true;
    // Prepared local, current-build one-world checkpoint. Never transferred.
    // Empty uses cold boot and the host campaign SRAM startup barrier.
    std::vector<uint8_t> initial_machine;
    // Only wire host0 may supply a personal campaign. Guests start transiently
    // from its SRAM transfer; they never supply or export their own campaign.
    std::vector<uint8_t> initial_host_battery;
    std::function<LogicalInput(uint32_t wire_tick)> sample_logical;
};
struct NetProgress {
    uint32_t frames=0; // State before this frame, after digest-agreed input.
    std::vector<uint8_t> battery;
};
struct NetStats {
    uint32_t tick=0,confirmed=0,episodes=0,predicted=0,profile=0,transcript=0,module_profile=0;
    uint64_t replayed=0;
};
// One session per process/thread. Cable mode simulates a complete console
// pair. Logical co-op simulates one Machine; its host supplies startup SRAM.
// Rollback checkpoints remain local and only logical inputs advance play.
class NetSession {
public:
    NetSession(std::array<std::vector<uint8_t>,2> roms,
               std::array<Options,2> machines,NetOptions options);
    NetSession(std::vector<uint8_t> rom,Options machine,NetOptions options);
    ~NetSession();
    NetSession(const NetSession&)=delete;
    NetSession& operator=(const NetSession&)=delete;
    // Pumps once; true means one live/replayed guest frame ran. The caller
    // retains presentation/event pacing when false and during replay.
    bool poll();
    void wait(unsigned milliseconds=8);
    bool ready() const;
    bool replaying() const;
    unsigned local_console() const noexcept; // Assigned player in one-world mode; not save authority.
    void presentation_viewport(unsigned width,unsigned height);
    void presentation_coop_view(unsigned mode); // Local only; gameplay policy stays fixed.
    void frame_observer(FrameObserver); // One-world actual executions, including replay.
    void hold_at(uint32_t tick); // Every peer must request the same future tick.
    bool settled() const;
    void quiesce();
    bool drained() const;
    NetStats stats() const;
    std::string diagnostics() const;
    const CablePair& pair() const;
    const Machine& machine() const; // One shared co-op world; no cable console.
    // Available only at an agreed settled hold, outside speculative execution.
    // The caller chooses when/where to persist the local seat's native save.
    std::vector<uint8_t> confirmed_battery() const;
    std::vector<uint8_t> confirmed_snapshot() const;
    // Read the most recent digest-agreed local native progress without
    // stopping the live match. Restores an isolated current-build checkpoint;
    // never reads speculative SRAM or changes the running pair. Refuses before
    // a positive confirmed watermark, during replay, and on a refused session.
    NetProgress confirmed_progress() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
