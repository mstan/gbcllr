#include <gbcllr/machine.hpp>
#include <gbcllr/diagnostics.hpp>
#include <recomp_persistence/atomic_file.h>
#include <gb.h>
#include "identity.h"
#include "native_mods.hpp"
#include "native_sgb_service.hpp"
#include "oracle_native_mods.hpp"
#include <algorithm>
#include <cstring>
#include <cstdlib>
#include <fstream>
#include <set>
#include <map>
#include <stdexcept>
#include <type_traits>
#ifdef _WIN32
#include <windows.h>
#endif

namespace gbcllr {
namespace {
constexpr uint64_t frame_ticks = 140448;
constexpr uint32_t schema = Machine::snapshot_schema;
template<class T> void append(std::vector<uint8_t>& v, const T& t) {
    auto p=reinterpret_cast<const uint8_t*>(&t); v.insert(v.end(),p,p+sizeof(T));
}
void bytes(std::vector<uint8_t>& v,const void* p,size_t n) {
    if(!n)return;
    auto b=static_cast<const uint8_t*>(p);v.insert(v.end(),b,b+n);
}
struct RewindChunkSink {
    void* context;
    void (*write)(void*,const uint8_t*,size_t);
};
template<class T> void append(RewindChunkSink& out,const T& value) {
    out.write(out.context,reinterpret_cast<const uint8_t*>(&value),sizeof value);
}
void bytes(RewindChunkSink& out,const void* p,size_t n) {
    if(n)out.write(out.context,static_cast<const uint8_t*>(p),n);
}
struct Reader {
    std::span<const uint8_t> input;
    void read(void* p,size_t n) {
        if(n>input.size()) throw std::runtime_error("truncated snapshot");
        if(n)std::memcpy(p,input.data(),n);input=input.subspan(n);
    }
    template<class T> T value() {
        if constexpr(std::is_same_v<T,bool>) {
            std::array<uint8_t,sizeof(bool)> raw{},zero{},one{};
            bool no=false,yes=true;std::memcpy(zero.data(),&no,sizeof no);std::memcpy(one.data(),&yes,sizeof yes);
            read(raw.data(),raw.size());
            if(raw==zero)return false;
            if(raw==one)return true;
            throw std::runtime_error("invalid snapshot boolean");
        } else {T t{};read(&t,sizeof t);return t;}
    }
};
}
struct Machine::Impl {
    GB_gameboy_t gb{};
    Options options;
    std::vector<uint8_t> rom;
    std::string identity;
    std::string abi_identity;
    std::string boot_identity;
    std::array<uint8_t,32> plan_identity{};
    struct Hook {uint16_t pc;Machine::RuntimeHook callback;void* user;bool enabled=false;};
    std::map<std::string,Hook,std::less<>> runtime_hooks;
    std::map<std::string,std::vector<uint8_t>,std::less<>> overlays;
    bool applying_plan=false;
    void inherit_plan(const Impl& owner,bool include_hooks=true) {
        plan_identity=owner.plan_identity;overlays=owner.overlays;
        if(include_hooks)runtime_hooks=owner.runtime_hooks;
        std::copy_n(owner.gb.rom,owner.gb.rom_size,gb.rom);
    }
    void extend_plan(std::span<const uint8_t> identity) {
        std::vector<uint8_t> bytes(plan_identity.begin(),plan_identity.end());
        bytes.insert(bytes.end(),identity.begin(),identity.end());auto hash=sha256(bytes);
        for(unsigned i=0;i<32;++i)plan_identity[i]=uint8_t(std::stoul(hash.substr(i*2,2),nullptr,16));
    }
    NativeMods mods;
    // Derived subscriptions are rebuilt at construction, never serialized.
    std::array<uint8_t,8192> instruction_sites{},read_sites{};
    bool filtered_instructions=false,filtered_reads=false;
    std::unique_ptr<NativeSgbService> sgb;
    ModPpu mod_ppu{};
    uint64_t ticks=0, frame_deadline=0;
    uint8_t buttons=0;
    uint16_t instruction_pc=UINT16_MAX;
    std::array<uint32_t,160*144> screen{};
    std::array<uint32_t,160*144> completed_screen{};
    bool completed_valid=false;
    std::vector<int16_t> audio;
    std::set<uint32_t> trace;
    bool tracing=false;
    bool presentation_only=false;
    bool cooperative_active=false,cooperative_boundary=true;
    uint64_t cooperative_tick=0;
    void* input_context=nullptr;
    void (*input_changed)(void*,uint8_t)=nullptr;
    std::string serial;
    uint8_t serial_byte=0;
    unsigned serial_bits=0;
    bool serial_out=true;
    bool serial_pin=false;
    LinkCallbacks link{};
    void* execution_context=nullptr;
    bool (*execution_pause)(void*,uint64_t,unsigned) noexcept=nullptr;
    #include "machine_headroom.inc"
    static void cycle_pause(void* context,uint64_t tick,unsigned phase) {
        auto& m=*static_cast<Impl*>(context);
        m.execution_pause(m.execution_context,tick,phase);
    }
    static Impl& self(GB_gameboy_t* gb) {return *static_cast<Impl*>(GB_get_user_data(gb));}
    Machine* oracle_owner=nullptr; // Bound only while running the actual caller.
    ModContext mod_context() {
        mod_ppu={gb.io_registers[GB_IO_SCX],gb.io_registers[GB_IO_SCY],gb.io_registers[GB_IO_LCDC],
            gb.io_registers[GB_IO_WX],gb.io_registers[GB_IO_WY],gb.background_palettes_data,
            gb.object_palettes_data,gb.background_palettes_rgb,gb.object_palettes_rgb,
            gb.io_registers[GB_IO_BGP],gb.io_registers[GB_IO_OBP0],gb.io_registers[GB_IO_OBP1]};
        return {rom.data(),gb.ram,gb.vram,gb.mbc_ram,gb.oam,gb.hram,rom.size(),gb.mbc_ram_size,
            gb.pc,gb.hl,gb.mbc_rom_bank,instruction_pc,uint8_t(gb.bc),uint8_t(gb.de>>8),gb.mbc_ram_bank,
            gb.cgb_ram_bank,&mod_ppu
#if GBCLLR_OBSERVED_DISPLAY
            ,uint8_t(gb.bc>>8)
#endif
        };
    }
    static uint8_t mod_read(GB_gameboy_t* gb,uint16_t address,uint8_t value) {
        auto& m=self(gb);auto context=m.mod_context();context.e=uint8_t(gb->de);
        if(!gb->in_dma_read&&!gb->hdma_in_progress)context.sp=gb->sp;
        return m.mods.read(context,address,value);
    }
    static uint8_t filtered_mod_read(GB_gameboy_t* gb,uint16_t address,uint8_t value) {
        auto& sites=self(gb).read_sites;
        if(!(sites[address>>3]&(1u<<(address&7))))return value;
        return mod_read(gb,address,value);
    }
    static bool mod_write(GB_gameboy_t* gb,uint16_t address,uint8_t value) {
        auto& m=self(gb);auto context=m.mod_context();context.e=uint8_t(gb->de);context.sp=gb->sp;
        return m.mods.write(context,address,value);
    }
    static uint8_t sgb_read(GB_gameboy_t* gb,uint16_t address,uint8_t value) {
        auto& m=self(gb);
        if(address==0xff00&&!gb->in_dma_read&&!gb->hdma_in_progress)value=m.sgb->read(value);
        return m.mods.active()?mod_read(gb,address,value):value;
    }
    static bool sgb_write(GB_gameboy_t* gb,uint16_t address,uint8_t value) {
        auto& m=self(gb);if(address==0xff00)m.sgb->write(value,gb->vram);
        return m.mods.observes_writes()?mod_write(gb,address,value):true;
    }
    static uint8_t oracle_read(GB_gameboy_t* gb,uint16_t address,uint8_t value) {
        if(gb->in_dma_read||gb->hdma_in_progress||!OracleNativeMods::watches_cpu_read(address))return value;
        auto& m=self(gb);auto context=m.mod_context();context.owner=m.oracle_owner;
        return m.mods.oracle_cpu_read(context,address,value,std::span<uint16_t,6>(gb->registers));
    }
    static void mod_line(GB_gameboy_t* gb,uint8_t line) {
        auto& m=self(gb);
#if GBCLLR_OBSERVED_DISPLAY
        if(line&&(line>144||!m.mods.observes_scanlines()))return;
#else
        if(line&&(line>=144||!m.mods.observes_scanlines()))return;
#endif
        auto context=m.mod_context();m.mods.scanline(context,line);
#if GBCLLR_OBSERVED_DISPLAY
        if(m.mods.tracks_pixel_history())m.bind_mod_pixels();
#endif
    }
#if GBCLLR_OBSERVED_DISPLAY
    static void mod_first_line(GB_gameboy_t* gb,uint8_t) {
        auto& m=self(gb);auto context=m.mod_context();
        if(m.mods.first_line(context,m.screen))m.bind_mod_pixels();
    }
    static void mod_bg_row(GB_gameboy_t* gb,uint16_t map,uint16_t texture,uint8_t tile,uint8_t lower,uint8_t upper,uint8_t attributes) {
        auto& m=self(gb);auto context=m.mod_context();
        m.mods.background_row(context,{map,texture,tile,lower,upper,attributes});
    }
    void bind_mod_pixels() {
        auto sink=mods.pixel_sink(gb.current_line);
        gb.llr_bg_row_callback=sink.output?mod_bg_row:nullptr;
        gb.llr_bg_source_colors=sink.colors;gb.llr_bg_emitted_row=sink.output;
    }
#endif
    static void mod_vblank(GB_gameboy_t* gb,GB_vblank_type_t type) {
        if(type==GB_VBLANK_TYPE_REPEAT||type==GB_VBLANK_TYPE_SKIPPED_FRAME)return;
        // GB_display_vblank replaces the output without rendering pixels in
        // these cases. Its existing callback runs after that native clear.
        bool cleared=!gb->disable_rendering&&(!(gb->io_registers[GB_IO_LCDC]&GB_LCDC_ENABLE)||
           (!GB_is_cgb(gb)&&gb->stopped&&(gb->io_registers[GB_IO_LCDC]&GB_LCDC_ENABLE))||
           gb->frame_skip_state==GB_gameboy_t::GB_FRAMESKIP_LCD_TURNED_ON);
        auto& m=self(gb);
        if(m.mods.active()) {
            auto context=m.mod_context();
            gb->llr_cpu_extra_budget=gb->boot_rom_finished?m.mods.cpu_budget(context,cleared):0u;
        }
#if GBCLLR_OBSERVED_DISPLAY
        if(cleared)m.mods.framebuffer_cleared();
#endif
        if(m.mods.completes_frames()&&!gb->disable_rendering) {
            m.completed_screen=m.screen;m.completed_valid=true;
            auto context=m.mod_context();m.mods.complete_frame(context,cleared,m.sgb&&m.sgb->freezes_frame());
        }
    }
    static void sgb_vblank(GB_gameboy_t* gb,GB_vblank_type_t type) {
        mod_vblank(gb,type);
        if(type==GB_VBLANK_TYPE_REPEAT||type==GB_VBLANK_TYPE_SKIPPED_FRAME||gb->disable_rendering)return;
        auto& m=self(gb);
        if(!m.mods.completes_frames()){m.completed_screen=m.screen;m.completed_valid=true;}
        m.sgb->complete_frame(m.completed_screen);
    }
    static void menu_vblank(GB_gameboy_t* gb,GB_vblank_type_t type) {
        if(type==GB_VBLANK_TYPE_REPEAT||type==GB_VBLANK_TYPE_SKIPPED_FRAME||gb->disable_rendering)return;
        auto& m=self(gb);m.completed_screen=m.screen;m.completed_valid=true;
    }
    static uint32_t rgb(GB_gameboy_t*,uint8_t r,uint8_t g,uint8_t b) {
        return 0xff000000u|uint32_t(r)<<16|uint32_t(g)<<8|b;
    }
    static void sample(GB_gameboy_t* gb,GB_sample_t* s) {
        auto& a=self(gb).audio;
        if(a.size()<48000*4) {a.push_back(s->left);a.push_back(s->right);}
    }
    static void executed(GB_gameboy_t* gb,uint16_t address,uint8_t) {
        auto& m=self(gb);
        if(m.tracing && gb->llr_fetch_offset!=UINT32_MAX) m.trace.insert(gb->llr_fetch_offset);
    }
    static void executed_mod(GB_gameboy_t* gb,uint16_t address,uint8_t opcode) {
        self(gb).instruction_pc=address;executed(gb,address,opcode);
    }
    static void serial_start(GB_gameboy_t* gb,bool bit) {
        auto& m=self(gb);
        m.serial_out=bit;
        m.serial_byte=uint8_t((m.serial_byte<<1)|bit);
        if(++m.serial_bits==8) {m.serial+=char(m.serial_byte);m.serial_bits=0;}
    }
    static uint64_t event_tick(GB_gameboy_t* gb) {
        auto tick=gb->llr_tick_base+gb->cycles_since_run;
        if(gb->llr_in_timers)
            tick+=uint64_t(gb->llr_timer_batch-gb->div_cycles) << !gb->cgb_double_speed;
        return tick;
    }
    static bool serial_end(GB_gameboy_t* gb) {
        auto& m=self(gb);
        return m.link.serial_exchange?m.link.serial_exchange(m.link.context,event_tick(gb),m.serial_pin):true;
    }
    static void serial_rise(GB_gameboy_t* gb,bool bit) {
        auto& m=self(gb);
        if(m.link.serial_rising)m.link.serial_rising(m.link.context,event_tick(gb),bit);
        m.serial_pin=bit;
    }
    static void infrared_changed(GB_gameboy_t* gb,bool on) {
        auto& m=self(gb);
        if(m.link.infrared_output)m.link.infrared_output(m.link.context,event_tick(gb),on);
    }
    Impl(std::span<const uint8_t> image,Options opts):options(std::move(opts)),rom(image.begin(),image.end()),identity(sha256(image)),mods(identity,options.mods,options.model==Model::cgb) {
#if GBCLLR_HLE_ONLY
        if(!options.native)throw std::runtime_error("This build requires native HLE execution");
        if(!options.boot_rom.empty())throw std::runtime_error("This HLE build uses native post-boot initialization; compiled BIOS coverage is unavailable");
#endif
        boot_identity=sha256(options.boot_rom);
#if GBCLLR_MOD_EVENT_FILTERS
        auto instructions=mods.instruction_sites(),reads=mods.read_sites();
        filtered_instructions=!instructions.empty();filtered_reads=!reads.empty();
        for(auto pc:instructions)instruction_sites[pc>>3]|=uint8_t(1u<<(pc&7));
        for(auto address:reads)read_sites[address>>3]|=uint8_t(1u<<(address&7));
#endif
        if(sizeof(gb)!=GB_allocation_size())throw std::runtime_error("C/C++ donor layout mismatch");
        if(rom.size()<0x8000 || (rom.size()&(rom.size()-1))) throw std::runtime_error("invalid ROM size");
        if(options.sgb_service){
            if(rom[0x146]!=3)throw std::runtime_error("This cartridge does not advertise SGB support");
            sgb=std::make_unique<NativeSgbService>(options.model==Model::dmg);
        }
        unsigned program_index=0;
        if(options.native && GBCLLR_PROGRAM_COUNT) {
            bool found=false;
            for(unsigned i=0;i<GBCLLR_PROGRAM_COUNT;++i)if(identity==gbcllr_program_sha256[i]) {
                program_index=i;found=true;break;
            }
            if(!found)throw std::runtime_error("ROM does not match any generated program SHA-256");
        }
        if(!options.boot_rom.empty() && options.boot_rom.size()!=(options.model==Model::dmg?256:2304))
            throw std::runtime_error("boot ROM must be 256 bytes (DMG) or 2304 bytes (CGB)");
        GB_init(&gb,options.model==Model::dmg?GB_MODEL_DMG_B:GB_MODEL_CGB_E);
        GB_set_user_data(&gb,this);
        GB_set_rgb_encode_callback(&gb,rgb);
        GB_set_pixels_output(&gb,screen.data());
        GB_set_sample_rate(&gb,48000);
        GB_apu_set_sample_callback(&gb,sample);
        GB_set_execution_callback(&gb,mods.active()?executed_mod:executed);
        headroom_begin();
        if(mods.completes_frames())GB_set_vblank_callback(&gb,mod_vblank);
        if(mods.active()) {
            GB_set_read_memory_callback(&gb,mods.uses_oracle_cpu_service()?oracle_read:filtered_reads?filtered_mod_read:mod_read);
            if(mods.observes_writes())GB_set_write_memory_callback(&gb,mod_write);
            GB_set_lcd_line_callback(&gb,mod_line);
#if GBCLLR_OBSERVED_DISPLAY
            if(mods.tracks_pixel_history()) {
                GB_set_vblank_callback(&gb,mod_vblank);
                gb.llr_first_line_callback=mod_first_line;
            }
#endif
        }
        if(sgb){GB_set_read_memory_callback(&gb,sgb_read);GB_set_write_memory_callback(&gb,sgb_write);GB_set_vblank_callback(&gb,sgb_vblank);}
        GB_set_serial_transfer_bit_start_callback(&gb,serial_start);
        GB_set_serial_transfer_bit_end_callback(&gb,serial_end);
        gb.llr_serial_rise_callback=serial_rise;
        GB_set_infrared_callback(&gb,infrared_changed);
        GB_set_rtc_mode(&gb,GB_RTC_MODE_ACCURATE);
        GB_set_turbo_mode(&gb,true,true);
        gb.llr_reset_seed=options.seed;
        GB_load_rom_from_buffer(&gb,rom.data(),rom.size());
        GB_reset(&gb);
        gb.last_rtc_second=options.rtc_epoch;
        gb.llr_native_enabled=options.native;
#if GBCLLR_FRAME_PICTURE
        // Only these CGB cartridges have qualified this replacement. Unknown
        // games and hardware/PPU test ROMs retain the actual reference clock.
        gb.llr_frame_picture_enabled=options.model==Model::cgb && !sgb &&
            !options.reference_picture && !mods.has_coop() &&
            (identity=="fdcc3c8c43813cf8731fc037d2a6d191bac75439c34b24ba1c27526e6acdc8a2" ||
             identity=="e37a14a8e61bd64f2226dacc26a7ee75dd90abfcf88c5016eb97b76ecda0ddca");
#if GBCLLR_DMG_FRAME
        // The existing native line-picture owner also serves the exact Gen1
        // cartridge. Its native map compositor retains its instruction, write,
        // line and frame observers; SGB remains a separately qualified profile.
        if(options.model==Model::dmg && !sgb && !options.reference_picture && !mods.active() &&
           identity=="42045a1642f77b568534ab85b9db93c8a33d2b95c38ae84c987f497e8d52f97f")
            gb.llr_frame_picture_enabled=true;
        if(options.model==Model::dmg && !sgb && !options.reference_picture &&
           identity=="5ca7ba01642a3b27b0cc0b5349b52792795b62d3ed977e98a09390659af96b7b")
            gb.llr_frame_picture_enabled=true;
#endif
#endif
#if GBCLLR_HLE_ONLY && GBCLLR_FRAME_PICTURE
        if(!gb.llr_frame_picture_enabled)
            throw std::runtime_error("Native picture coverage is unavailable for the selected cartridge/model/profile");
#endif
        gb.llr_program_index=program_index;
        gb.llr_blocks_enabled=options.native && GBCLLR_NATIVE_BLOCKS;
        if(!options.boot_rom.empty()) GB_load_boot_rom_from_buffer(&gb,options.boot_rom.data(),options.boot_rom.size());
        else {
            // Explicit post-boot entry for early bring-up; hardware boot must be
            // separately qualified using an actual supplied boot ROM.
            gb.boot_rom_finished=true;gb.pc=0x100;gb.sp=0xfffe;
            gb.af=options.model==Model::dmg?0x01b0:0x1180;
            gb.bc=options.model==Model::dmg?0x0013:0;
            gb.de=options.model==Model::dmg?0x00d8:0xff56;
            gb.hl=options.model==Model::dmg?0x014d:0x000d;
            gb.cgb_mode=options.model==Model::cgb && (rom[0x143]&0x80);
            GB_write_memory(&gb,0xff40,0x91);
            GB_write_memory(&gb,0xff47,0xfc);
            GB_write_memory(&gb,0xff48,0xff);GB_write_memory(&gb,0xff49,0xff);
        }
        const std::string abi=std::string(GBCLLR_ABI_ID)+GB_display_state_profile(&gb);
        abi_identity=sha256({reinterpret_cast<const uint8_t*>(abi.data()),abi.size()});
    }
    ~Impl() {GB_free(&gb);}
    std::vector<uint8_t> state(bool include_mods=true,bool simulation=false) const {
        std::vector<uint8_t> v;
        append_state(v,include_mods,simulation);return v;
    }
    template<class Sink> void append_fields(Sink& v,bool include_mods=true,bool simulation=false) const {
#if GBCLLR_FRAME_AUDIO
        GB_apu_frame_finish(const_cast<GB_gameboy_t*>(&gb));
#endif
        // A checkpoint observes the raster at the current guest timestamp,
        // including an ordinary PPU batch whose work is already due.
        if(!completed_valid)GB_display_sync(const_cast<GB_gameboy_t*>(&gb));
        // A partial CPU view is useful for validation/inspection, but cannot
        // be resumed without the paired continuation descriptor.
        append(v,uint32_t(cooperative_active&&!cooperative_boundary?schema+1:schema)); bytes(v,identity.data(),identity.size());
        if(simulation){const char zero_abi[64]{};bytes(v,zero_abi,sizeof zero_abi);}
        else bytes(v,abi_identity.data(),abi_identity.size());
        bytes(v,boot_identity.data(),boot_identity.size());
        bytes(v,plan_identity.data(),plan_identity.size());
        append(v,ticks);append(v,frame_deadline);append(v,buttons);
        const uint32_t core_size=offsetof(GB_gameboy_t,llr_reset_seed);
        // dma_cycles is scratch for the current GB_advance_cycles call: that
        // function overwrites it before its sole consumer, GB_dma_run. Its
        // leftover value differs with batch size even when DMA is inactive.
        // It has no continuation meaning at an instruction boundary.
        append(v,core_size);
        // Normalize between immutable serialized core spans. Unsaved donor
        // services and diagnostic rings never need a full-object stack copy.
        constexpr size_t dma=offsetof(GB_gameboy_t,dma_cycles),mod=offsetof(GB_gameboy_t,dma_cycles_modulo);
        static_assert(dma+sizeof gb.dma_cycles<=mod&&mod+sizeof gb.dma_cycles_modulo<=core_size);
        const auto* raw=reinterpret_cast<const uint8_t*>(&gb);
        bytes(v,raw,dma);append(v,uint16_t(0));
        bytes(v,raw+dma+sizeof gb.dma_cycles,mod-dma-sizeof gb.dma_cycles);
        append(v,GB_is_dma_active(const_cast<GB_gameboy_t*>(&gb))?gb.dma_cycles_modulo:int8_t(0));
        bytes(v,raw+mod+sizeof gb.dma_cycles_modulo,core_size-mod-sizeof gb.dma_cycles_modulo);
        append(v,gb.ram_size);bytes(v,gb.ram,gb.ram_size);
        append(v,gb.vram_size);bytes(v,gb.vram,gb.vram_size);
        append(v,gb.mbc_ram_size);bytes(v,gb.mbc_ram,gb.mbc_ram_size);
        // The donor's save format omits output filter history and some
        // continuation flags. Include pointer-free history for exact PCM replay.
        bytes(v,&gb.apu_output,offsetof(GB_apu_output_t,sample_callback));
        append(v,gb.apu_output.square_sweep_disable_stepping);
        append(v,gb.pending_cycles);
        append(v,gb.wx_just_changed);append(v,gb.tile_sel_glitch);
        append(v,gb.joypad_is_stable);append(v,gb.joyp_accessed);
        append(v,serial_byte);append(v,serial_bits);
        append(v,uint8_t(serial_out));
        append(v,uint8_t(serial_pin));
        // Framebuffer is presentation history needed for exact mid-frame loads.
        bytes(v,screen.data(),sizeof screen);
        const bool completion=(include_mods&&mods.completes_frames())||bool(sgb);append(v,completion);
        if(completion){append(v,completed_valid);bytes(v,completed_screen.data(),sizeof completed_screen);}
        if constexpr(std::is_same_v<Sink,RewindChunkSink>) {
            if(include_mods)mods.checkpoint(v.context,v.write);else append(v,uint32_t(0));
        } else {
            auto native_state=include_mods?mods.snapshot(simulation):std::vector<uint8_t>{};append(v,uint32_t(native_state.size()));
            bytes(v,native_state.data(),native_state.size());
        }
        append(v,options.sgb_service);
        auto sgb_state=sgb?sgb->snapshot():std::vector<uint8_t>{};append(v,uint32_t(sgb_state.size()));bytes(v,sgb_state.data(),sgb_state.size());
        if(include_mods&&std::any_of(options.mods.begin(),options.mods.end(),[](const auto& f){return f.smooth_gameplay||f.colorization;}))append(v,gb.llr_cpu_extra_budget);
    }
    void append_state(std::vector<uint8_t>& v,bool include_mods=true,bool simulation=false,bool sealed=true) const {
        const auto begin=v.size();
        try {
        append_fields(v,include_mods,simulation);
        if(sealed){auto checksum=simulation?std::string(64,'\0'):sha256(std::span<const uint8_t>(v).subspan(begin));bytes(v,checksum.data(),checksum.size());}
        } catch(...) {v.resize(begin);throw;}
    }
    void load(std::span<const uint8_t> data,bool inspect_partial=false,bool sealed=true) {
        if(sealed&&(data.size()<64 || sha256(data.first(data.size()-64))!=
           std::string(reinterpret_cast<const char*>(data.data()+data.size()-64),64))
            )throw std::runtime_error("snapshot checksum mismatch");
        Reader r{sealed?data.first(data.size()-64):data};
        auto format=r.value<uint32_t>();
        if(format==schema+1&&!inspect_partial)throw std::runtime_error("CPU continuation requires a paired checkpoint");
        if(format!=schema&&!(inspect_partial&&format==schema+1))throw std::runtime_error("snapshot schema mismatch");
        char id[64];r.read(id,64);
        if(std::string(id,64)!=identity) throw std::runtime_error("snapshot ROM mismatch");
        r.read(id,64);
        if(std::string(id,64)!=abi_identity)
            throw std::runtime_error("snapshot compiler/platform ABI mismatch");
        r.read(id,64);
        if(std::string(id,64)!=boot_identity) throw std::runtime_error("snapshot boot ROM mismatch");
        std::array<uint8_t,32> saved_plan{};r.read(saved_plan.data(),saved_plan.size());
        if(saved_plan!=plan_identity)throw std::runtime_error("snapshot mod plan mismatch");
        ticks=r.value<uint64_t>();frame_deadline=r.value<uint64_t>();buttons=r.value<uint8_t>();
        auto core_size=r.value<uint32_t>();
        if(core_size!=offsetof(GB_gameboy_t,llr_reset_seed)) throw std::runtime_error("snapshot ABI mismatch");
        auto staged_storage=std::make_unique<GB_gameboy_t>();auto& staged=*staged_storage;r.read(&staged,core_size);
        if(staged.model!=gb.model || staged.ram_size!=gb.ram_size || staged.vram_size!=gb.vram_size || staged.mbc_ram_size!=gb.mbc_ram_size)
            throw std::runtime_error("snapshot machine mismatch");
        if(staged.vblank_half_tick>1)throw std::runtime_error("invalid display clock fraction");
        for(const auto* fifo:{&staged.bg_fifo,&staged.oam_fifo}) {
            if(fifo->read_end>=GB_FIFO_LENGTH||fifo->size>GB_FIFO_LENGTH)
                throw std::runtime_error("invalid snapshot PPU FIFO bounds");
            for(const auto& item:fifo->fifo) {
                const auto* raw=reinterpret_cast<const uint8_t*>(&item);
                if(item.pixel>3||item.palette>7||raw[offsetof(GB_fifo_item_t,bg_priority)]>1)
                    throw std::runtime_error("invalid snapshot PPU FIFO pixel");
            }
        }
        if(staged.n_visible_objs>10||staged.orig_n_visible_objs>10||
           std::any_of(std::begin(staged.visible_objs),std::end(staged.visible_objs),[](auto index){return index>=40;}))
            throw std::runtime_error("invalid snapshot PPU object bounds");
        std::memcpy(&gb,&staged,core_size);
#if GBCLLR_FRAME_AUDIO
        GB_apu_frame_discard(&gb);
#endif
        for(auto [ptr,expected]: {std::pair{gb.ram,gb.ram_size}, {gb.vram,gb.vram_size}, {gb.mbc_ram,gb.mbc_ram_size}}) {
            if(r.value<uint32_t>()!=expected) throw std::runtime_error("snapshot memory size mismatch");
            r.read(ptr,expected);
        }
        r.read(&gb.apu_output,offsetof(GB_apu_output_t,sample_callback));
        gb.apu_output.square_sweep_disable_stepping=r.value<bool>();
        gb.pending_cycles=r.value<unsigned>();
        gb.wx_just_changed=r.value<bool>();gb.tile_sel_glitch=r.value<bool>();
        gb.joypad_is_stable=r.value<bool>();gb.joyp_accessed=r.value<bool>();
        serial_byte=r.value<uint8_t>();serial_bits=r.value<unsigned>();
        auto out=r.value<uint8_t>();
        if(out>1 || serial_bits>7)throw std::runtime_error("invalid serial continuation");
        serial_out=out;
        auto pin=r.value<uint8_t>();if(pin>1)throw std::runtime_error("invalid serial output latch");serial_pin=pin;
        r.read(screen.data(),sizeof screen);
        if(r.value<bool>()!=(mods.completes_frames()||bool(sgb)))throw std::runtime_error("snapshot presentation service mismatch");
        if(mods.completes_frames()||sgb){completed_valid=r.value<bool>();r.read(completed_screen.data(),sizeof completed_screen);}
        auto native_size=r.value<uint32_t>();
        if(native_size>r.input.size())throw std::runtime_error("truncated native feature state");
        mods.restore(r.input.first(native_size),sealed);r.input=r.input.subspan(native_size);
        if(r.value<bool>()!=options.sgb_service)throw std::runtime_error("Snapshot native SGB profile mismatch");
        auto sgb_size=r.value<uint32_t>();if(sgb_size>r.input.size())throw std::runtime_error("Truncated native SGB state");
        if(sgb)sgb->restore(r.input.first(sgb_size));else if(sgb_size)throw std::runtime_error("Unexpected native SGB state");
        r.input=r.input.subspan(sgb_size);
#if GBCLLR_OBSERVED_DISPLAY
        bind_mod_pixels();
#endif
        if(std::any_of(options.mods.begin(),options.mods.end(),[](const auto& f){return f.smooth_gameplay||f.colorization;})){gb.llr_cpu_extra_budget=r.value<uint32_t>();if(gb.llr_cpu_extra_budget>280896)throw std::runtime_error("invalid enhanced CPU budget");}
        if(!r.input.empty()) throw std::runtime_error("snapshot trailing bytes");
        // Refresh derived palette caches without advancing guest time.
        GB_set_rgb_encode_callback(&gb,rgb);
        for(unsigned i=0;i<8;++i) gb.keys[0][i]=(buttons&(1u<<i))!=0;
        audio.clear();serial.clear();
        gb.llr_tick_base=ticks;gb.cycles_since_run=0;
        gb.llr_in_timers=false;
    }
};
Machine::Machine(std::span<const uint8_t> rom,Options o):impl_(std::make_unique<Impl>(rom,std::move(o))) {}
Machine::~Machine(){ if(impl_&&!impl_->headroom_prefix.empty()) {try{headroom_report(impl_->headroom_prefix);}catch(...){}} }
std::unique_ptr<Machine> Machine::presentation_copy() const {
    if(impl_->cooperative_active||impl_->link.serial_exchange||impl_->link.serial_rising)
        throw std::runtime_error("native presentation rooms require an offline instruction boundary");
    auto options=impl_->options;options.mods.clear();
#if GBCLLR_FRAME_PICTURE
    // Clearing native gameplay modules must retain the source's qualified
    // picture profile, including a co-op room's reference-picture fallback.
    options.reference_picture=!impl_->gb.llr_frame_picture_enabled;
#endif
    auto copy=std::make_unique<Machine>(impl_->rom,std::move(options));
    copy->impl_->inherit_plan(*impl_,false);
    std::vector<uint8_t> local;impl_->append_state(local,false,false,false);
    copy->impl_->load(local,false,false);
    copy->impl_->presentation_only=true;
    return copy;
}
void Machine::presentation_refresh(const Machine& owner) {
    // This trusted same-process source already has a verified cartridge and
    // profile. Keep canonical parsing and the external file checksum path.
    if(!impl_->presentation_only||impl_->mods.active()||owner.impl_->cooperative_active||
       owner.impl_->link.serial_exchange||owner.impl_->link.serial_rising||
       impl_->identity!=owner.impl_->identity||impl_->options.model!=owner.impl_->options.model||
       impl_->options.native!=owner.impl_->options.native||impl_->options.boot_rom!=owner.impl_->options.boot_rom||
       impl_->options.sgb_service!=owner.impl_->options.sgb_service)
        throw std::runtime_error("invalid isolated presentation refresh");
    std::vector<uint8_t> local;owner.impl_->append_state(local,false,false,false);
    presentation_refresh(local,false);
}
void Machine::presentation_refresh(std::span<const uint8_t> aligned_owner,bool sealed) {
    if(!impl_->presentation_only||impl_->mods.active()||impl_->cooperative_active||
       impl_->link.serial_exchange||impl_->link.serial_rising)
        throw std::runtime_error("invalid isolated presentation source");
    impl_->load(aligned_owner,false,sealed);
    auto& gb=impl_->gb;
    GB_set_user_data(&gb,impl_.get());GB_set_pixels_output(&gb,impl_->screen.data());
    GB_apu_set_sample_callback(&gb,Impl::sample);GB_set_execution_callback(&gb,Impl::executed);
    GB_set_read_memory_callback(&gb,nullptr);GB_set_write_memory_callback(&gb,nullptr);GB_set_lcd_line_callback(&gb,nullptr);
#if GBCLLR_OBSERVED_DISPLAY
    GB_set_vblank_callback(&gb,nullptr);
    gb.llr_bg_row_callback=nullptr;gb.llr_bg_source_colors=nullptr;gb.llr_bg_emitted_row=nullptr;
    gb.llr_first_line_callback=nullptr;
#endif
    if(impl_->sgb){GB_set_read_memory_callback(&gb,Impl::sgb_read);GB_set_write_memory_callback(&gb,Impl::sgb_write);GB_set_vblank_callback(&gb,Impl::sgb_vblank);}
    // Match fresh-copy host scratch defaults. These are not hardware state;
    // the serialized continuation above supplies all guest timing/DMA state.
    gb.vblank_just_occured=false;gb.disable_oam_corruption=false;gb.in_dma_read=false;
    gb.hdma_in_progress=false;gb.returned_open_bus=false;gb.addr_for_hdma_conflict=0;gb.during_div_write=false;
    gb.battery_dirty=false;
}
ModContext Machine::presentation_context(){return impl_->mod_context();}
void Machine::presentation_menu_frames(std::span<const uint32_t> restored) {
    if(!impl_->presentation_only||impl_->mods.active()||impl_->cooperative_active||impl_->sgb)throw std::runtime_error("invalid private native menu display");
    if(!restored.empty()){if(restored.size()!=160*144)throw std::runtime_error("invalid native menu raster");std::copy(restored.begin(),restored.end(),impl_->completed_screen.begin());impl_->completed_valid=true;}
    GB_set_vblank_callback(&impl_->gb,Impl::menu_vblank);
}
std::span<uint8_t> Machine::mod_wram(){if(impl_->cooperative_active)throw std::runtime_error("private native workspace requires one owning machine");return {impl_->gb.ram,impl_->gb.ram_size};}
std::span<uint8_t> Machine::mod_hram(){if(impl_->cooperative_active)throw std::runtime_error("private native workspace requires one owning machine");return {impl_->gb.hram,sizeof impl_->gb.hram};}
void Machine::presentation_advance_until(const std::bitset<65536>& stops,uint64_t deadline) {
    if(!impl_->presentation_only||impl_->mods.active()||impl_->cooperative_active)
        throw std::runtime_error("data advancement requires a module-free presentation copy");
    do {run_until(impl_->ticks+1);}
    while(impl_->ticks<deadline&&!stops[impl_->gb.pc]);
}
std::array<uint8_t,2> Machine::presentation_banks() const {
    return {impl_->gb.io_registers[GB_IO_SVBK],uint8_t(impl_->gb.cgb_vram_bank)};
}
template<bool SingleInstruction> void Machine::execute_until(uint64_t deadline) {
    if(impl_->cooperative_active)throw std::runtime_error("cannot independently execute an active cable continuation");
    struct OwnerScope {Machine*& p;~OwnerScope(){p=nullptr;}} scope{impl_->oracle_owner};
    if(impl_->mods.uses_oracle_cpu_service())impl_->oracle_owner=this;
    const bool native_coop=impl_->mods.uses_instruction_service();
    const bool all_instructions=!impl_->filtered_instructions||impl_->mods.poll_instructions();
#if GBCLLR_NATIVE_WAIT
    impl_->gb.llr_native_wait_allowed=!SingleInstruction&&!native_coop&&!impl_->execution_pause;
    // The whole operation retains every timed fetch/read observer. Pausing,
    // tracing and per-PC profiling keep their ordinary scheduling boundary.
    impl_->gb.llr_native_poll_allowed=impl_->gb.llr_native_wait_allowed&&!impl_->tracing&&!impl_->sgb&&
        !impl_->mods.uses_oracle_cpu_service()&&impl_->headroom_pc_count.empty();
#endif
    while(impl_->ticks<deadline) {
        if(impl_->applying_plan)throw std::runtime_error("execution during mod activation");
        for(const auto& [id,hook]:impl_->runtime_hooks)
            if(hook.enabled&&hook.pc==impl_->gb.pc)hook.callback(*this,hook.user);
        auto& core=impl_->gb;
        if(core.llr_cpu_extra_budget&&impl_->mods.ends_cpu_work(core.pc))core.llr_cpu_extra_budget=0;
        auto extra_budget=core.llr_cpu_extra_budget;
        const bool physical_poll=extra_budget&&impl_->mods.polls_physical_clock(core.pc);
        if(physical_poll)core.llr_cpu_extra_budget=0;
        impl_->gb.llr_tick_base=impl_->ticks;
        impl_->gb.llr_cycle_budget=impl_->execution_pause?1:
            uint32_t(std::min(deadline-impl_->ticks,uint64_t(UINT32_MAX)));
        auto freeze=impl_->gb.speed_switch_freeze;
        if(native_coop) {
            impl_->gb.llr_blocks_enabled=false;
            if(all_instructions||(impl_->instruction_sites[core.pc>>3]&(1u<<(core.pc&7)))) {
                auto context=impl_->mod_context();
                context.owner=this;
                impl_->mods.coop_instruction(context,std::span<uint16_t,6>(impl_->gb.registers),
                    {impl_->gb.ram,impl_->gb.ram_size},{impl_->gb.hram,sizeof impl_->gb.hram});
            }
        }
        unsigned cycles=GB_run(&impl_->gb);
        if(impl_->gb.llr_native_uncovered) {
            char message[160];std::snprintf(message,sizeof message,
                "Native execution coverage gap: PC=%04x opcode=%02x physical=%08x; no interpreter executed",
                impl_->gb.llr_uncovered_pc,impl_->gb.llr_uncovered_opcode,impl_->gb.llr_fetch_offset);
            throw std::runtime_error(message);
        }
        // GB_run executes one instruction for instruction-service profiles.
        // A new VBlank owns its replacement budget, including a zero budget
        // when the scene ended; never restore the previous frame over it.
        if(physical_poll&&!core.vblank_just_occured&&!core.llr_cpu_extra_budget)
            core.llr_cpu_extra_budget=extra_budget;
        // A zero-display-tick step must consume finite CPU headroom or a
        // shrinking donor speed realignment. A stalled core still fails.
        if(!cycles) {
            if(core.llr_cpu_extra_budget>=extra_budget&&
               !(freeze && impl_->gb.speed_switch_freeze<freeze))
                throw std::runtime_error("CPU made no progress");
            if constexpr(SingleInstruction)break;
            continue;
        }
        impl_->ticks+=cycles;
        if(impl_->execution_pause && impl_->execution_pause(impl_->execution_context,impl_->ticks,1))break;
        if constexpr(SingleInstruction)break;
    }
}
void Machine::run_until(uint64_t deadline) {
    execute_until<false>(deadline);
#if GBCLLR_FRAME_AUDIO
    GB_apu_frame_finish(&impl_->gb);
#endif
}
void Machine::headroom_frame_end() { impl_->headroom_frame(); }
void Machine::debug_step_instruction() {
    auto& enabled=impl_->gb.llr_blocks_enabled;
    struct Restore {bool& value;bool previous;~Restore(){value=previous;}} restore{enabled,enabled};
    enabled=false;
    execute_until<true>(impl_->ticks+1);
}
void Machine::import_overlay(std::string id,std::span<const uint8_t> content) {
    if(id.empty()||id.size()>96||id.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789.-_")!=std::string::npos||
       id=="ram"||id=="cartridge"||content.size()>64*1024*1024||impl_->applying_plan)
        throw std::runtime_error("invalid overlay import");
    std::string binding=id+":"+sha256(content);
    impl_->overlays[std::move(id)]={content.begin(),content.end()};
    impl_->extend_plan({reinterpret_cast<const uint8_t*>(binding.data()),binding.size()});
}
std::span<const uint8_t> Machine::overlay(std::string_view id)const {
    auto i=impl_->overlays.find(id);return i==impl_->overlays.end()?std::span<const uint8_t>{}:std::span<const uint8_t>(i->second);
}
void Machine::register_runtime_hook(std::string id,uint16_t pc,RuntimeHook callback,void* user) {
    if(!callback||id.empty()||impl_->applying_plan||impl_->runtime_hooks.contains(id))throw std::runtime_error("invalid runtime hook registration");
    impl_->runtime_hooks.emplace(std::move(id),Impl::Hook{pc,callback,user});
}
bool Machine::apply_mod_plan(rmod_context* context,std::string_view game,
    std::span<const rmod_patch> patches,std::span<const rmod_hook> hooks,std::string& error) {
    struct Activation {
        Machine& owner;Impl& m;std::map<std::string,Impl::Hook,std::less<>> before;
        static int begin(void* p){auto& a=*static_cast<Activation*>(p);if(a.m.applying_plan||a.m.cooperative_active)return 0;a.m.applying_plan=true;return 1;}
        static int read(void* p,const char* s,uint64_t address,uint8_t* out,size_t n) {
            try{auto& a=*static_cast<Activation*>(p);auto& m=a.m;std::string_view space(s);
                if(space=="ram"){if(!ram_extent(address,n))return 0;auto data=a.owner.debug_read(uint16_t(address),n);std::copy(data.begin(),data.end(),out);return 1;}
                auto data=space=="cartridge"?std::span<const uint8_t>(m.gb.rom,m.gb.rom_size):a.owner.overlay(space);
                if(address>data.size()||n>data.size()-address)return 0;std::copy_n(data.data()+address,n,out);return 1;
            }catch(...){return 0;}
        }
        static int write(void* p,const char* s,uint64_t address,const uint8_t* data,size_t n) {
            try{auto& a=*static_cast<Activation*>(p);auto& m=a.m;std::string_view space(s);
                if(space=="ram"){if(!ram_extent(address,n))return 0;a.owner.debug_write(uint16_t(address),{data,n});return 1;}
                std::span<uint8_t> target;
                if(space=="cartridge")target={m.gb.rom,m.gb.rom_size};
                else {auto i=m.overlays.find(space);if(i==m.overlays.end())return 0;target=i->second;}
                if(address>target.size()||n>target.size()-address)return 0;std::copy_n(data,n,target.data()+address);return 1;
            }catch(...){return 0;}
        }
        static int hook(void* p,const char* id){auto& a=*static_cast<Activation*>(p);auto i=a.m.runtime_hooks.find(id);if(i==a.m.runtime_hooks.end())return 0;i->second.enabled=true;return 1;}
        static bool ram_extent(uint64_t a,size_t n){return (a>=0x8000&&a<=0xfea0&&n<=0xfea0-a)||(a>=0xff80&&a<=0xffff&&n<=0xffff-a);}
        static void finish(void* p,int committed){auto& a=*static_cast<Activation*>(p);if(!committed)a.m.runtime_hooks.swap(a.before);a.m.applying_plan=false;}
    } activation{*this,*impl_,impl_->runtime_hooks};
    std::string game_id(game);rmod_engine engine{&activation,game_id.c_str(),impl_->identity.c_str(),
        Activation::begin,Activation::read,Activation::write,Activation::hook,Activation::finish};
    std::array<uint8_t,32> identity{};
    auto before=snapshot();
    auto result=rmod_apply(context,patches.data(),patches.size(),hooks.data(),hooks.size(),&engine,identity.data());
    if(result<0)throw std::runtime_error("mod rollback failed; execution stopped");
    if(result!=1){error=rmod_error(context);std::string restored;if(!restore(before,restored))throw std::runtime_error("mod rollback state restore failed: "+restored);return false;}
    impl_->extend_plan(identity);error.clear();return true;
}
void Machine::execution_hook(void* context,bool (*pause)(void*,uint64_t,unsigned) noexcept) {
    impl_->execution_context=context;impl_->execution_pause=pause;
    impl_->gb.llr_cycle_context=impl_.get();
    impl_->gb.llr_cycle_callback=pause?Impl::cycle_pause:nullptr;
}
void Machine::cooperative_state(bool active,bool boundary,uint64_t tick) {
    impl_->cooperative_active=active;impl_->cooperative_boundary=boundary;impl_->cooperative_tick=tick;
}
void Machine::input_hook(void* context,void (*changed)(void*,uint8_t)) {
    impl_->input_context=context;impl_->input_changed=changed;
}
std::vector<uint8_t> Machine::continuation_view() const {return impl_->state();}
void Machine::append_state(std::vector<uint8_t>& out,bool continuation) const {
    if(!continuation&&impl_->cooperative_active&&!impl_->cooperative_boundary)
        throw std::runtime_error("CPU continuation requires a paired checkpoint");
    impl_->append_state(out);
}
void Machine::run_frame(uint8_t buttons) {
    run_frame(buttons,0);
}
std::vector<uint8_t> Machine::coop_state()const{return impl_->mods.coop_snapshot();}
void Machine::run_frame(uint8_t buttons,uint8_t partner) {
    run_frame(LogicalInput{buttons},LogicalInput{partner});
}
void Machine::run_frame(LogicalInput player1,LogicalInput player2) {
    if(impl_->cooperative_active)throw std::runtime_error("cannot independently execute an active cable continuation");
    if((player1.virtual_buttons|player2.virtual_buttons)&~3u)throw std::runtime_error("invalid virtual game input");
    auto observation=frame_observation_;observation.player1=player1;observation.player2=player2;observation.generation=restore_generation_;
    if(frame_observer_.frame)frame_observer_.frame(frame_observer_.context,*this,observation,true);
    // Common title/save/shop screens sample the cartridge's one input latch.
    // Per-actor hooks still provide independent gameplay and personal menus.
    uint8_t physical=player1.buttons;
    if(impl_->mods.has_coop())physical=uint8_t(((player1.buttons&15)?player1.buttons:player2.buttons)&15)|uint8_t((player1.buttons|player2.buttons)&0xf0);
    set_buttons(physical);
    impl_->mods.virtual_inputs(player1.virtual_buttons,player2.virtual_buttons);
    impl_->mods.coop_inputs(player1.buttons,player2.buttons);
    // A debugger or connected scheduler may have advanced beyond the previous
    // display deadline. Resume at the next boundary instead of issuing empty
    // frames until the old host counter catches up.
    if(impl_->frame_deadline<impl_->ticks)
        impl_->frame_deadline=(impl_->ticks/frame_ticks)*frame_ticks;
    impl_->frame_deadline+=frame_ticks;run_until(impl_->frame_deadline);
    if(!impl_->presentation_only)impl_->mods.frame(*this);
    impl_->headroom_frame();
    if(frame_observer_.frame)frame_observer_.frame(frame_observer_.context,*this,observation,false);
}
void Machine::set_buttons(uint8_t b) {
    if(impl_->input_changed)impl_->input_changed(impl_->input_context,b);
    impl_->buttons=b;GB_set_key_mask(&impl_->gb,static_cast<GB_key_mask_t>(b));
}
std::vector<uint8_t> Machine::snapshot() const {
    if(impl_->cooperative_active&&!impl_->cooperative_boundary)
        throw std::runtime_error("CPU continuation requires a paired checkpoint");
    return impl_->state();
}
std::vector<uint8_t> Machine::simulation_snapshot() const {
    if(impl_->cooperative_active&&!impl_->cooperative_boundary)
        throw std::runtime_error("CPU continuation requires a paired checkpoint");
    return impl_->state(true,true);
}
bool Machine::restore(std::span<const uint8_t> data,std::string& error) {
    return restore_state(data,error,true);
}
bool Machine::direct_checkpoint_history() const {
    // Keep the same trusted encoder/restore contract while bypassing reverse
    // page comparison and patch assembly for this exact offline DMG title.
    if(impl_->options.model==Model::dmg && !impl_->sgb &&
        impl_->identity=="7a108770a7c1ad592b52d0c46d7ead422d0a20961abaafca3d40086e3f2f588f")return true;
    // The qualified native stock picture keeps bounded typed checkpoints.
    // Enhanced, reference, SGB and unrelated profiles retain reverse pages.
    return impl_->options.model==Model::dmg && !impl_->sgb &&
        impl_->gb.llr_frame_picture_enabled &&
        impl_->identity=="42045a1642f77b568534ab85b9db93c8a33d2b95c38ae84c987f497e8d52f97f";
}
void Machine::checkpoint(RewindState& state) const {
    if(impl_->cooperative_active)throw std::runtime_error("rewind requires an offline machine");
    state.owner_.reset();state.bytes_.clear();
    RewindChunkSink sink{&state.bytes_,[](void* context,const uint8_t* input,size_t n){
        auto& out=*static_cast<std::vector<uint8_t>*>(context);bytes(out,input,n);
    }};
    impl_->append_fields(sink);
    state.owner_=checkpoint_owner_;
}
void Machine::checkpoint_chunks(void* context,void (*write)(void*,const uint8_t*,size_t)) const {
    if(impl_->cooperative_active)throw std::runtime_error("rewind requires an offline machine");
    RewindChunkSink sink{context,write};impl_->append_fields(sink);
}
bool Machine::restore(const RewindState& state,std::string& error) {
    if(state.owner_!=checkpoint_owner_){error="rewind checkpoint belongs to another machine";return false;}
    return restore_state(state.bytes_,error,false);
}
bool Machine::restore_state(std::span<const uint8_t> data,std::string& error,bool sealed) {
    try {
        if(impl_->cooperative_active)throw std::runtime_error("cannot independently restore an active cable continuation");
        // Parse into a separately owned machine; any failure preserves live state.
        auto staged=std::make_unique<Impl>(impl_->rom,impl_->options);
        staged->inherit_plan(*impl_);
        staged->load(data,false,sealed);
        staged->mods.restore_coop_menu(*this);
        staged->mods.preserve_host_presentation(impl_->mods);
        staged->tracing=impl_->tracing;
        staged->presentation_only=impl_->presentation_only;
        staged->trace=impl_->trace;
        staged->gb.llr_headroom=impl_->gb.llr_headroom;
        staged->headroom_ring=std::move(impl_->headroom_ring);
        staged->headroom_pc_count=std::move(impl_->headroom_pc_count);
        staged->headroom_pc_units=std::move(impl_->headroom_pc_units);
        staged->headroom_pc_valid=false;
        staged->gb.llr_native_count=impl_->gb.llr_native_count;
        staged->gb.llr_fallback_count=impl_->gb.llr_fallback_count;
        staged->gb.llr_chained_count=impl_->gb.llr_chained_count;
        staged->link=impl_->link;
        staged->gb.llr_link_timing=staged->link.serial_exchange!=nullptr||staged->link.serial_rising!=nullptr;
        staged->execution_context=impl_->execution_context;
        staged->execution_pause=impl_->execution_pause;
        staged->input_context=impl_->input_context;staged->input_changed=impl_->input_changed;
        staged->gb.llr_cycle_context=staged.get();
        staged->gb.llr_cycle_callback=staged->execution_pause?Impl::cycle_pause:nullptr;
        if(frame_observer_.restoring)frame_observer_.restoring(frame_observer_.context,*this);
        impl_.swap(staged);++restore_generation_;error.clear();return true;
    } catch(const std::exception& e) {error=e.what();return false;}
}
std::string Machine::digest() const {return sha256(snapshot());}
void Machine::link_callbacks(LinkCallbacks callbacks) {
    impl_->link=callbacks;impl_->gb.llr_link_timing=callbacks.serial_exchange!=nullptr||callbacks.serial_rising!=nullptr;
}
SerialPort Machine::serial_port() const {
    auto& g=impl_->gb;auto control=g.io_registers[GB_IO_SC];
    return {bool(control&0x80),bool(control&1),bool(g.io_registers[GB_IO_SB]&0x80),g.serial_count};
}
SerialClock Machine::serial_clock(bool data_in) {
    auto output=serial_clock_edge(true);
    auto input=serial_clock_edge(false,data_in);
    return {input.accepted,output.data_out};
}
SerialClock Machine::serial_clock_edge(bool high,bool data_in) {
    auto state=serial_port();auto& m=*impl_;
    bool accepted=state.active&&!state.internal_clock;
    if(accepted) {if(high)m.serial_pin=state.data_out;else GB_serial_set_data_bit(&m.gb,data_in);}
    return {accepted,m.serial_pin};
}
bool Machine::serial_pin() const {return impl_->serial_pin;}
void Machine::infrared_input(bool on) {GB_set_infrared_input(&impl_->gb,on);}
Stats Machine::stats() const {
    auto& g=impl_->gb;
    uint16_t bank=g.pc<0x4000?g.mbc_rom0_bank:g.pc<0x8000?g.mbc_rom_bank:UINT16_MAX;
    return {impl_->cooperative_active?impl_->cooperative_tick:impl_->ticks,g.llr_native_count,g.llr_fallback_count,g.pc,bank,g.llr_chained_count};
}
std::span<const uint32_t> Machine::pixels() const {
    // Completed native-mod frames already own their observation boundary.
    // A raw raster must materialize only work due at this same timestamp.
    if(!impl_->completed_valid)GB_display_sync(&impl_->gb);
    return impl_->completed_valid?impl_->completed_screen:impl_->screen;
}
void Machine::frame_observer(FrameObserver observer) noexcept {frame_observer_=observer;}
std::string Machine::diagnostic_metadata() const {
    return diagnostic_machine_metadata(rom_identity(),impl_->options);
}
void Machine::mod_viewport(unsigned width,unsigned height) {frame_observation_.width=width;frame_observation_.height=height;impl_->mods.viewport(width,height);}
void Machine::mod_oracle_visibility(bool hide){frame_observation_.hide_closed_rooms=hide;impl_->mods.oracle_visibility(hide);}
void Machine::mod_coop_view(unsigned mode,unsigned actor){frame_observation_.coop_view=mode;frame_observation_.coop_actor=actor;impl_->mods.coop_view(mode,actor);}
void Machine::mod_coop_shared_world(bool enabled){frame_observation_.shared_world=enabled;impl_->mods.coop_shared_world(enabled);}
ModView Machine::mod_view() {if(!impl_->mods.active())return {160,false,pixels()};auto context=impl_->mod_context();return impl_->mods.render(context,pixels());}
ModView Machine::gen2_presentation_view(unsigned width,unsigned height) {auto context=impl_->mod_context();return impl_->mods.render_host_gen2(context,pixels(),width,height);}
ModView Machine::coop_presentation_view(unsigned width,unsigned height,int menu_actor) {auto context=impl_->mod_context();return impl_->mods.render_host_coop(context,pixels(),width,height,menu_actor);}
NativeSgbView Machine::sgb_view() const {if(impl_->sgb)return impl_->sgb->view();NativeSgbView v;v.supported=impl_->rom[0x146]==3;return v;}
std::vector<int16_t> Machine::take_audio() {
#if GBCLLR_FRAME_AUDIO
    GB_apu_frame_finish(&impl_->gb);
#endif
    std::vector<int16_t> a; a.swap(impl_->audio);return a;
}
#include "headroom_report.inc"
std::string Machine::audio_costs() const {
    auto c=GB_get_audio_costs(&impl_->gb);
    std::ostringstream out;
    out<<"{\"audio_service\":\""<<
#if GBCLLR_OPERATION_DMG
       "OPERATION-DMG-PROTOTYPE"
#elif GBCLLR_OPERATION_AUDIO
       "OPERATION-CGB/LLE-DMG"
#else
       (GBCLLR_FRAME_AUDIO?"FRAME-CGB/LLE-DMG":"LLE")
#endif
       <<"\",\"advance_calls\":"<<c.advance_calls<<",\"synthesis_calls\":"<<c.synthesis_calls
       <<",\"samples\":"<<c.samples<<",\"blocks\":"<<c.blocks<<",\"queued_events\":"<<c.queued_events
       <<",\"reads\":"<<c.reads<<",\"writes\":"<<c.writes<<",\"div_events\":"<<c.div_events<<"}";
    return out.str();
}
std::string Machine::wait_costs() const {
    auto c=impl_->gb.llr_wait_costs;
    std::ostringstream out;
    out<<"{\"wait_service\":\""<<(GBCLLR_NATIVE_WAIT?"DEADLINE-CGB":"LLE")
       <<"\",\"calls\":"<<c.calls<<",\"advances\":"<<c.advances
       <<",\"cpu_cycles\":"<<c.cpu_cycles<<",\"batched_cycles\":"<<c.batched_cycles
       <<",\"poll_calls\":"<<c.poll_calls<<",\"poll_iterations\":"<<c.poll_iterations
       <<",\"poll_cpu_cycles\":"<<c.poll_cpu_cycles<<"}";
    return out.str();
}
std::string Machine::picture_costs() const {
    const auto& g=impl_->gb;
    std::ostringstream out;
    out<<"{\"picture_service\":\""<<(g.llr_frame_picture_enabled?(GB_is_cgb(&g)?"FRAME-CGB":"FRAME-DMG"):"LLE")
       <<"\",\"clocks\":"<<g.llr_display_clocks<<",\"services\":"<<g.llr_display_services
       <<",\"events\":"<<g.llr_display_events<<",\"prefixes\":"<<g.llr_display_prefixes<<"}";
    return out.str();
}
std::vector<uint32_t> Machine::take_trace() {std::vector<uint32_t> t(impl_->trace.begin(),impl_->trace.end());impl_->trace.clear();return t;}
void Machine::trace_enabled(bool e) {impl_->tracing=e;}
std::vector<uint8_t> Machine::battery() const {
    if(impl_->presentation_only)throw std::runtime_error("speculative presentation cannot export native saves");
    int n=GB_save_battery_size(&impl_->gb);if(n<=0)return {};
    auto& g=impl_->gb;
    if(g.cartridge_type->has_rtc) {
        if(g.cartridge_type->mbc_type!=GB_cartridge_t::GB_MBC3)throw std::runtime_error("RTC battery format not yet qualified for this mapper");
        // BGB/VBA 64-bit format. Use the simulation's selected RTC epoch,
        // never consult the host clock during serialization or replay.
        std::vector<uint8_t> data(g.mbc_ram,g.mbc_ram+g.mbc_ram_size);
        for(auto* rtc:{&g.rtc_real,&g.rtc_latched})
            for(auto value:rtc->data) {data.push_back(value);data.insert(data.end(),3,0);}
        for(unsigned i=0;i<8;++i)data.push_back(uint8_t(g.last_rtc_second>>(8*i)));
        return data;
    }
    std::vector<uint8_t> data(n);
    if(GB_save_battery_to_buffer(&impl_->gb,data.data(),data.size())) throw std::runtime_error("battery serialization failed");
    return data;
}
bool Machine::load_battery(std::span<const uint8_t> data,std::string& error) {
    if(impl_->cooperative_active) {error="cannot replace battery data in an active cable continuation";return false;}
    auto& g=impl_->gb;
    const size_t ram=g.mbc_ram_size;
    if(!g.cartridge_type->has_battery || data.size()<ram) {error="battery size mismatch";return false;}
    const size_t suffix=data.size()-ram;
    if(g.cartridge_type->has_rtc) {
        if(g.cartridge_type->mbc_type!=GB_cartridge_t::GB_MBC3 || (suffix!=0&&suffix!=44&&suffix!=48)) {
            error="unsupported RTC battery format";return false;
        }
        if(suffix) {
            for(unsigned i=0;i<5;++i) {
                g.rtc_real.data[i]=data[ram+i*4];
                g.rtc_latched.data[i]=data[ram+20+i*4];
            }
        } else g.rtc_real.high|=0x80; // SRAM-only import asks the game to set its clock.
        g.last_rtc_second=impl_->options.rtc_epoch;
    } else if(suffix) {error="battery size mismatch";return false;}
    if(ram)std::memcpy(g.mbc_ram,data.data(),ram);
    error.clear();return true;
}
void Machine::write_frame(const std::filesystem::path& p) const {
    std::ofstream f(p,std::ios::binary);f<<"P6\n160 144\n255\n";
    for(auto pixel:pixels()) {char rgb[]={char(pixel>>16),char(pixel>>8),char(pixel)};f.write(rgb,3);}
    if(!f) throw std::runtime_error("frame write failed");
}
std::array<uint8_t,8> Machine::registers() const {auto& g=impl_->gb;return {uint8_t(g.af>>8),uint8_t(g.af),uint8_t(g.bc>>8),uint8_t(g.bc),uint8_t(g.de>>8),uint8_t(g.de),uint8_t(g.hl>>8),uint8_t(g.hl)};}
std::string Machine::serial_output() const {return impl_->serial;}
std::span<const uint8_t> Machine::cartridge_ram() const {return {impl_->gb.mbc_ram,impl_->gb.mbc_ram_size};}
std::span<uint8_t> Machine::native_gift_ram() {
    if(impl_->cooperative_active||impl_->presentation_only)throw std::runtime_error("native gifts require an offline owning console");
    return {impl_->gb.mbc_ram,impl_->gb.mbc_ram_size};
}
std::string Machine::rom_identity() const {return impl_->identity;}
std::vector<uint8_t> Machine::debug_read(uint16_t address,size_t length) const {
    if(length>65536u-address)throw std::runtime_error("debug read exceeds guest address space");
    auto copy=std::make_unique<Impl>(impl_->rom,impl_->options);
    copy->inherit_plan(*impl_,false);
    copy->load(impl_->state(),true);
    // Debug observations inspect the physical bus without executing a native
    // CPU data service at a coincidentally paused instruction/stack boundary.
    GB_set_read_memory_callback(&copy->gb,nullptr);
    GB_set_write_memory_callback(&copy->gb,nullptr);
    std::vector<uint8_t> result(length);
    for(size_t i=0;i<length;++i)result[i]=GB_safe_read_memory(&copy->gb,uint16_t(address+i));
    return result;
}
void Machine::debug_write(uint16_t address,std::span<const uint8_t> values) {
    if(impl_->cooperative_active)throw std::runtime_error("active cable continuation mutation requires a paired operation");
    if(values.size()>65536u-address)throw std::runtime_error("debug write exceeds guest address space");
    auto& gb=impl_->gb;auto callback=gb.write_memory_callback;
    GB_set_write_memory_callback(&gb,nullptr);
    for(size_t i=0;i<values.size();++i)GB_write_memory(&gb,uint16_t(address+i),values[i]);
    GB_set_write_memory_callback(&gb,callback);
}
Registers Machine::debug_registers() const {
    auto& g=impl_->gb;return {g.af,g.bc,g.de,g.hl,g.sp,g.pc,g.ime};
}
void Machine::debug_registers(Registers r) {
    if(impl_->cooperative_active)throw std::runtime_error("active cable continuation mutation requires a paired operation");
    if(r.ime>1)throw std::runtime_error("IME must be 0 or 1");
    auto& g=impl_->gb;
    g.af=r.af&0xfff0;g.bc=r.bc;g.de=r.de;g.hl=r.hl;g.sp=r.sp;g.pc=r.pc;g.ime=r.ime;
}
void Machine::debug_wake() {
    if(impl_->cooperative_active)throw std::runtime_error("active cable continuation mutation requires a paired operation");
    auto& g=impl_->gb;
    g.halted=false;g.stopped=false;g.just_halted=false;g.halt_bug=false;g.ime_toggle=false;
}
std::vector<uint8_t> read_file(const std::filesystem::path& p) {
    std::ifstream f(p,std::ios::binary|std::ios::ate);
    if(!f)throw std::runtime_error("cannot read "+p.string());
    auto n=f.tellg();if(n<0 || n>64*1024*1024)throw std::runtime_error("invalid file size");
    std::vector<uint8_t> data(static_cast<size_t>(n));f.seekg(0);f.read(reinterpret_cast<char*>(data.data()),data.size());
    if(!f)throw std::runtime_error("file read failed");return data;
}
void write_file_atomic(const std::filesystem::path& p,std::span<const uint8_t> data) {
    auto utf8=p.u8string();
    if(!recomp_atomic_file_store(reinterpret_cast<const char*>(utf8.c_str()),data.data(),data.size()))
        throw std::runtime_error("atomic file write failed");
}
}
