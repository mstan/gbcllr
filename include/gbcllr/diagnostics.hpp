#pragma once
#include <gbcllr/machine.hpp>
#include <memory>
#include <string_view>

namespace gbcllr {
// Local-only, current-build recordings. Never reads cartridge/boot files,
// profile files or battery exports. The caller supplies sanitized metadata.
class Diagnostics {
public:
    struct Limits {
        size_t queue_bytes=16*1024*1024,history_bytes=64*1024*1024;
        unsigned segment_frames=600,history_segments=360,report_segments=6;
    };
    Diagnostics(std::filesystem::path folder,std::string metadata,bool recording=true);
    Diagnostics(std::filesystem::path folder,std::string metadata,bool recording,Limits);
    ~Diagnostics(); // Drains accepted writes; use before closing host services.
    Diagnostics(const Diagnostics&)=delete;
    Diagnostics& operator=(const Diagnostics&)=delete;
    FrameObserver observer() noexcept;
    void log(std::string_view) noexcept;
    void finish(const Machine&) noexcept;
    // Queues a self-contained, store-only ZIP. Empty state is permitted for
    // a session that failed before startup. Screenshot is logical ARGB pixels.
    std::filesystem::path report(std::vector<uint8_t> state,
        std::span<const uint32_t> pixels,unsigned width,unsigned height,
        std::string context={}) noexcept;
    std::string status() const;
    bool recording() const noexcept;
    const std::filesystem::path& folder() const noexcept;
    void drain(); // Tests/normal shutdown only; never used during play.
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
std::string diagnostics_build();
std::string diagnostic_machine_metadata(std::string_view rom_identity,const Options&);
// A report segment needs the exact current build and owned source inputs.
// Restores only a machine checkpoint; never exports or changes native saves.
bool replay_diagnostic_segment(Machine&,std::span<const uint8_t>,std::string& error);
// Best-effort startup failure log, including errors before desktop creation.
std::filesystem::path diagnostic_failure(std::string_view) noexcept;
}
