#pragma once
#include <gbcllr/machine.hpp>

namespace gbcllr {
enum class CableSchedule { common_instruction, cycle_continuations };
struct CableExecutionStats {
    uint64_t continuation_yields=0,idle_yields_elided=0;
    uint64_t wire_peer_bits=0,native_exchanges=0,exchange_handoffs_elided=0;
};
// One owner thread; both consoles execute locally. The default schedule stops
// at common instruction boundaries. Cycle continuations preserve in-flight
// CPU work through bounded replay checkpoints; individual state/mutation
// operations are guarded while those continuations are active. No battery
// file is written by either schedule.
class CablePair {
public:
    CablePair(std::span<const uint8_t> left, Options left_options,
              std::span<const uint8_t> right, Options right_options,
              CableSchedule schedule = CableSchedule::common_instruction);
    ~CablePair();
    CablePair(const CablePair&)=delete;
    CablePair& operator=(const CablePair&)=delete;
    void run_until(uint64_t deadline);
    void run_frame(uint8_t left_buttons=0,uint8_t right_buttons=0);
    // Paused access only. Do not independently execute or bind hardware ports;
    // run_until rejects consoles advanced away from the common checkpoint.
    Machine& machine(unsigned index);
    const Machine& machine(unsigned index) const;
    std::vector<uint8_t> snapshot() const;
    bool restore(std::span<const uint8_t>,std::string& error);
    uint64_t ticks() const;
    const char* continuation_backend() const noexcept; // Actual host backend, for logs/profile identity.
    CableExecutionStats execution_stats() const noexcept; // Host-only, excluded from checkpoints.
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
