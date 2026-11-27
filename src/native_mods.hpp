#pragma once
#include <gbcllr/mods.hpp>
#include <array>
#include <memory>
#include <span>

namespace gbcllr {
class Machine;
// Owner-thread, non-reentrant native module context. Reads inspect physical
// backing storage only: they never perform another guest bus transaction.
struct ModPpu {
    uint8_t scx,scy,lcdc,wx,wy;
    const uint8_t *bg_palette_ram,*obj_palette_ram;
    const uint32_t *bg_rgb,*obj_rgb;
    uint8_t bgp=0xe4,obp0=0xe4,obp1=0xe4;
};
struct ModContext {
    const uint8_t *rom,*wram,*vram,*eram,*oam,*hram;
    size_t rom_size,eram_size;
    uint16_t pc,hl,rom_bank,instruction_pc;
    uint8_t c,d,ram_bank,wram_bank;
    ModPpu* ppu;
#if GBCLLR_OBSERVED_DISPLAY
    uint8_t b=0; // Native fixed-scene producer anchor; absent from ordinary builds.
#endif
    uint8_t e=0; // Transient low DE byte, populated for actual CPU reads only.
    Machine* owner=nullptr; // Transient native caller, never serialized.
    uint16_t sp=UINT16_MAX; // CPU bus service only; never a serialized host pointer.
};
struct ModBackgroundRow {
    uint16_t map,texture;
    uint8_t tile,lower,upper,attributes;
};
struct ModPixelSink {
    const uint32_t* colors=nullptr;
    uint32_t* output=nullptr; //160 presentation texels, owned by the module.
};
class Sml2NativeMods;
class Gen2NativeMods;
class Gen1NativeMods;
class OracleNativeMods;
class Xtreme2Coop;
class Xtreme2Controls;
class MegaManNativeMods;
class NativeMods {
    friend struct RewindChildFixture;
public:
    NativeMods(std::string_view rom,std::span<const NativeFeature> features,bool model_cgb=true);
    ~NativeMods();
    bool active() const;
    bool completes_frames() const;
    void complete_frame(ModContext&,bool cleared,bool freeze_gen1=false);
    uint32_t cpu_budget(const ModContext&,bool cleared)const;
    bool ends_cpu_work(uint16_t pc)const;
    bool polls_physical_clock(uint16_t pc)const;
    bool observes_scanlines() const;
    bool tracks_pixel_history() const;
    bool first_line(ModContext&,std::span<const uint32_t> native);
    void framebuffer_cleared();
    void background_row(ModContext&,const ModBackgroundRow&);
    ModPixelSink pixel_sink(uint8_t line);
    bool uses_oracle_cpu_service() const;
    bool observes_writes() const;
    bool write(ModContext&,uint16_t address,uint8_t value);
    uint8_t read(ModContext&,uint16_t address,uint8_t value);
    // Transient AF/BC/DE/HL/SP/PC words for an exact native Oracle caller.
    // The service never retains this architectural array or any host pointer.
    uint8_t oracle_cpu_read(ModContext&,uint16_t,uint8_t,std::span<uint16_t,6>);
    void scanline(ModContext&,uint8_t line);
    void viewport(unsigned width,unsigned height);
    void oracle_visibility(bool hide_closed_rooms);
    void coop_view(unsigned mode,unsigned actor);
    void coop_shared_world(bool enabled);
    // Retain derived host choices after validated same-profile staged restore.
    void preserve_host_presentation(const NativeMods&) noexcept;
    void frame(Machine&);
    bool has_coop()const;
    bool uses_instruction_service()const;
    // Empty sites retain the ordinary observer. Listed sites allow the host
    // to construct a complete native context only when an operation needs it.
    std::span<const uint16_t> instruction_sites()const;
    std::span<const uint16_t> read_sites()const;
    bool poll_instructions()const;
    void virtual_inputs(uint8_t,uint8_t);
    void coop_inputs(uint8_t,uint8_t);
    void coop_instruction(ModContext&,std::span<uint16_t,6>,std::span<uint8_t>,std::span<uint8_t>);
    std::vector<uint8_t> coop_snapshot()const;
    void restore_coop_menu(Machine&);
    ModView render(ModContext&,std::span<const uint32_t> native);
    ModView render_host_gen2(ModContext&,std::span<const uint32_t>,unsigned,unsigned);
    ModView render_host_coop(ModContext&,std::span<const uint32_t>,unsigned,unsigned,int menu_actor=-1);
    std::vector<uint8_t> snapshot(bool simulation=false,bool sealed_children=true) const;
    // Internal rewind: emit the existing size-prefixed snapshot from typed
    // live spans, without allocating an intermediate module archive.
    void checkpoint(void*,void (*)(void*,const uint8_t*,size_t)) const;
    void restore(std::span<const uint8_t> bytes,bool sealed_children=true);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    std::unique_ptr<Sml2NativeMods> sml2_;
    std::unique_ptr<Gen2NativeMods> gen2_;
    std::unique_ptr<Gen1NativeMods> gen1_;
    std::unique_ptr<OracleNativeMods> oracle_;
    std::unique_ptr<Xtreme2Coop> coop_;
    std::unique_ptr<Xtreme2Controls> controls_;
    std::unique_ptr<MegaManNativeMods> megaman_;
    bool observe_scanlines_=false;
    unsigned coop_view_=0,coop_actor_=0; // Local presentation, excluded from snapshots.
    int coop_menu_actor_=-1; // Temporary local network menu selection only.
};
}
