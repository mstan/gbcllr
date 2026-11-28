#define GB_INTERNAL
#define GB_DISABLE_DEBUGGER
#define GB_DISABLE_CHEATS
#define GB_DISABLE_CHEAT_SEARCH
#define GB_DISABLE_REWIND
#define GB_DISABLE_TIMEKEEPING
#include "../third_party/sameboy/Core/gb.h"
#undef internal
#include <array>
#include <algorithm>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

static void check(bool value,const char* text){if(!value)throw std::runtime_error(text);}
struct Picture {
    GB_gameboy_t core{};
    std::array<uint32_t,160*144> pixels{};
    std::array<uint32_t,160> metadata{};
    std::array<uint32_t,8> colors{};
    unsigned starts=0,lines=0,frames=0;
    bool vblank_valid=false;
    explicit Picture(GB_model_t model,bool fast=false) {
        GB_init(&core,model);core.cgb_double_speed=fast;core.cgb_mode=model!=GB_MODEL_DMG_B;
        GB_set_user_data(&core,this);GB_set_pixels_output(&core,pixels.data());
        GB_set_border_mode(&core,GB_BORDER_NEVER);
        GB_set_rgb_encode_callback(&core,[](GB_gameboy_t*,uint8_t r,uint8_t g,uint8_t b){
            return 0xff000000u|(uint32_t(r)<<16)|(uint32_t(g)<<8)|b;
        });
        std::fill_n(core.vram,core.vram_size,0);
        core.llr_frame_picture_enabled=true;core.boot_rom_finished=true;
        core.io_registers[GB_IO_LCDC]=0x91;core.io_registers[GB_IO_BGP]=0xe4;
        core.io_registers[GB_IO_SCX]=core.io_registers[GB_IO_SCY]=0;
        for(unsigned i=0;i<8;++i){core.vram[i*2]=0xff;core.vram[i*2+1]=0;core.vram[16+i*2]=0;core.vram[17+i*2]=0xff;}
        std::fill_n(core.vram+0x1800,32,0);std::fill_n(core.vram+0x1820,32,1);
        core.background_palettes_rgb[0]=0xff000000;core.background_palettes_rgb[1]=0xff112233;
        core.background_palettes_rgb[2]=0xff445566;core.background_palettes_rgb[3]=0xff778899;
        metadata.fill(0xdeadbeef);
        core.llr_first_line_callback=[](GB_gameboy_t* gb,uint8_t line){
            auto& p=*static_cast<Picture*>(GB_get_user_data(gb));check(line==0,"first-line index");++p.starts;p.bind();
        };
        GB_set_lcd_line_callback(&core,[](GB_gameboy_t* gb,uint8_t){
            auto& p=*static_cast<Picture*>(GB_get_user_data(gb));++p.lines;p.bind();
        });
        GB_set_vblank_callback(&core,[](GB_gameboy_t* gb,GB_vblank_type_t type){
            if(type!=GB_VBLANK_TYPE_NORMAL_FRAME)return;
            auto& p=*static_cast<Picture*>(GB_get_user_data(gb));++p.frames;
            p.vblank_valid=gb->current_line==144&&gb->io_registers[GB_IO_LY]==144&&
                (gb->io_registers[GB_IO_STAT]&3)==1&&(gb->io_registers[GB_IO_IF]&1);
        });
    }
    ~Picture(){GB_free(&core);}
    void bind(){
        core.llr_bg_source_colors=colors.data();core.llr_bg_emitted_row=metadata.data();
        core.llr_bg_row_callback=[](GB_gameboy_t* gb,uint16_t,uint16_t,uint8_t tile,uint8_t,uint8_t,uint8_t){
            auto& p=*static_cast<Picture*>(GB_get_user_data(gb));p.colors.fill(tile?0xff000020:0xff000010);
        };
    }
    void tick(){GB_display_run(&core,2,false);check(core.display_state>=GB_FRAME_PICTURE_OFF,"reference picture clock entered");}
    void first_transfer(){
        unsigned n=0;
        while(!(core.current_line==0&&core.mode3_batching_length>=167&&core.display_cycles< -250)&&n++<1000)tick();
        check(n<1000&&starts==1,"native startup/first-line observer");
    }
    void first_line_done(){
        unsigned n=0;while(core.current_line==0&&core.lcd_x<160&&n++<1000)tick();
        check(n<1000&&core.lcd_x==160,"native line did not complete");
    }
};
int main(){try{
    for(auto model:{GB_MODEL_DMG_B,GB_MODEL_CGB_E})for(bool fast:{false,true}) {
        if(model==GB_MODEL_DMG_B&&fast)continue;
        auto p=std::make_unique<Picture>(model,fast);p->first_transfer();
        check(p->core.vram_read_blocked&&p->core.vram_write_blocked,"transfer lost VRAM observation barrier");
        // A live scroll write owns only its elapsed prefix. Future metadata
        // must remain private until the matching native pixels are committed.
        for(unsigned i=0;i<70;++i)p->tick();
        GB_write_memory(&p->core,0xff42,8);unsigned split=p->core.lcd_x;
        check(split>0&&split<160,"scroll write missed a partial native span");
        for(unsigned x=0;x<160;++x)check(p->metadata[x]==(x<split?0xff000010:0xdeadbeef),"private raster leaked/overwrote span metadata");
        p->first_line_done();
        for(unsigned x=0;x<160;++x){
            check(p->pixels[x]==(x<split?0xff112233:0xff445566),"scroll write changed committed native pixels");
            check(p->metadata[x]==(x<split?0xff000010:0xff000020),"scroll write lost old/new native metadata");
        }
        check(p->core.llr_display_services>0&&p->core.llr_display_prefixes>=2,"native owner counters");
    }
    // The complete-row path must publish the same metadata as the private
    // prefix path. No assets, title identities or reference execution enter it.
    auto full=std::make_unique<Picture>(GB_MODEL_DMG_B);full->first_transfer();full->first_line_done();
    check(std::all_of(full->metadata.begin(),full->metadata.end(),[](auto v){return v==0xff000010;}),"complete-row observer missing");
    auto blank=std::make_unique<Picture>(GB_MODEL_DMG_B);blank->core.io_registers[GB_IO_LCDC]&=~GB_LCDC_BG_EN;
    blank->first_transfer();blank->first_line_done();
    check(std::all_of(blank->metadata.begin(),blank->metadata.end(),[](auto v){return v==1;}),"disabled background left undefined metadata");
    auto window=std::make_unique<Picture>(GB_MODEL_CGB_E);
    window->core.io_registers[GB_IO_LCDC]|=GB_LCDC_WIN_ENABLE|GB_LCDC_WIN_MAP;
    window->core.io_registers[GB_IO_WY]=0;window->core.io_registers[GB_IO_WX]=87;
    std::fill_n(window->core.vram+0x1c00,32,1);
    window->first_transfer();window->first_line_done();
    for(unsigned x=0;x<160;++x){
        check(window->pixels[x]==(x<80?0xff112233:0xff445566),"first-row window pixels differ");
        check(window->metadata[x]==(x<80?0xff000010:0xff000020),"first-row window metadata differs");
    }
    auto irq=std::make_unique<Picture>(GB_MODEL_CGB_E);irq->core.io_registers[GB_IO_STAT]|=0x10;
    unsigned ticks=0;while(!irq->frames&&ticks++<72000)irq->tick();
    check(irq->frames==1&&irq->vblank_valid&&irq->starts==1&&irq->lines>=144,"native VBlank/LY/mode/IRQ publication order differs");
    // Independent partial pictures are the primitive used by owned private
    // machines, rewind and paired views. Advancing one must not publish another.
    auto a=std::make_unique<Picture>(GB_MODEL_CGB_E),b=std::make_unique<Picture>(GB_MODEL_CGB_E);
    a->first_transfer();b->first_transfer();auto before=b->metadata;
    for(unsigned n=0;n<80;++n)a->tick();GB_write_memory(&a->core,0xff42,8);
    check(b->metadata==before&&b->core.io_registers[GB_IO_SCY]==0,"private native pictures share state");
    a->first_line_done();b->first_line_done();
    check(std::all_of(b->metadata.begin(),b->metadata.end(),[](auto v){return v==0xff000010;}),"private native picture lost its own inputs");
    std::cout<<"native DMG/CGB startup, spans, metadata, blank rows and private ownership pass\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
