#include "megaman_native_mods.hpp"
#include "megaman_titles.hpp"
#include "mm1_native_mods.hpp"
#include "mm2_native_mods.hpp"
#include "mm3_native_mods.hpp"
#include "mm4_native_mods.hpp"
#include "mm5_native_mods.hpp"
#include <gbcllr/machine.hpp>
#include <algorithm>
#include <stdexcept>

namespace gbcllr {
struct MegaManNativeMods::Adapter {
    virtual ~Adapter()=default;
    virtual uint8_t read(ModContext&,uint16_t,uint8_t)=0;
    virtual void write(ModContext&,uint16_t,uint8_t)=0;
    virtual void instruction(ModContext&,std::span<uint16_t,6>,std::span<uint8_t>,std::span<uint8_t>)=0;
    virtual std::span<const uint16_t> instruction_sites()const=0;
    virtual std::span<const uint16_t> read_sites()const=0;
    virtual bool poll_instructions()const=0;
    virtual void inputs(uint8_t,uint8_t)=0;
    virtual void complete_frame(ModContext&,bool)=0;
    virtual void scanline(ModContext&,uint8_t)=0;
    virtual uint32_t cpu_budget(const ModContext&,bool)const=0;
    virtual void viewport(unsigned,unsigned)=0;
    virtual ModView render(ModContext&,std::span<const uint32_t>)=0;
    virtual std::vector<uint8_t> snapshot()const=0;
    virtual void restore(std::span<const uint8_t>)=0;
};
template<class T> struct MegaManNativeMods::Model final:Adapter {
    T title;
    Model(std::span<const NativeFeature> f,bool cgb):title(f,cgb){}
    uint8_t read(ModContext& c,uint16_t a,uint8_t v)override{return title.read(c,a,v);}
    void write(ModContext& c,uint16_t a,uint8_t v)override{title.write(c,a,v);}
    void instruction(ModContext& c,std::span<uint16_t,6> cpu,std::span<uint8_t> ram,std::span<uint8_t> hram)override{title.instruction(c,cpu,ram,hram);}
    std::span<const uint16_t> instruction_sites()const override{
        if constexpr(requires{title.instruction_sites();})return title.instruction_sites();
        return {};
    }
    std::span<const uint16_t> read_sites()const override{
        if constexpr(requires{title.read_sites();})return title.read_sites();
        return {};
    }
    bool poll_instructions()const override{
        if constexpr(requires{title.poll_instructions();})return title.poll_instructions();
        return true;
    }
    void inputs(uint8_t a,uint8_t b)override{title.inputs(a,b);}
    void complete_frame(ModContext& c,bool cleared)override{title.complete_frame(c,cleared);}
    void scanline(ModContext& c,uint8_t line)override {
        if constexpr(requires{title.scanline(c,line);})title.scanline(c,line);
    }
    uint32_t cpu_budget(const ModContext& c,bool cleared)const override{return title.cpu_budget(c,cleared);}
    void viewport(unsigned w,unsigned h)override{title.viewport(w,h);}
    ModView render(ModContext& c,std::span<const uint32_t> n)override{return title.render(c,n);}
    std::vector<uint8_t> snapshot()const override{return title.snapshot();}
    void restore(std::span<const uint8_t> s)override{title.restore(s);}
};
MegaManNativeMods::MegaManNativeMods(std::string_view rom,std::span<const NativeFeature> features,bool cgb) {
    const auto* t=megaman_title_for_rom(rom);
    if(!t||features.empty()||features.size()>2)throw std::runtime_error("Mega Man native package requires its exact original cartridge");
    title_=t->key;
    bool adaptive=false,dx=false;
    for(const auto& f:features) {
        if(f.abi!=1||megaman_title_for_plugin(f.plugin)!=t||f.gift||f.expanded_encounters||f.player1_character||
           (f.view_width!=-1&&(f.view_width<160||f.view_width>4096)))
            throw std::runtime_error("Unsupported Mega Man native feature contract");
        if(f.plugin==t->adaptive){if(adaptive)throw std::runtime_error("Duplicate Mega Man enhancement package");adaptive=true;}
        else {if(dx||!f.colorization)throw std::runtime_error("Invalid Mega Man DX package");dx=true;}
    }
    std::vector<std::string> entries;
    for(const auto& f:features)entries.push_back(f.plugin+":"+std::to_string(f.abi)+":"+std::to_string(f.view_width)+":"+
        std::to_string(f.weapon_cycle)+":"+std::to_string(f.smooth_gameplay)+":"+std::to_string(f.no_flicker)+":"+
        std::to_string(f.colorization)+":"+f.asset_input);
    std::sort(entries.begin(),entries.end());
    std::string identity=std::string(t->sha256)+":"+std::to_string(cgb);
    for(const auto& entry:entries)identity+=";"+entry;
    profile_=sha256({reinterpret_cast<const uint8_t*>(identity.data()),identity.size()});
    if(t->key=="mm1")adapter_=std::make_unique<Model<Mm1NativeMods>>(features,cgb);
    else if(t->key=="mm2")adapter_=std::make_unique<Model<Mm2NativeMods>>(features,cgb);
    else if(t->key=="mm3")adapter_=std::make_unique<Model<Mm3NativeMods>>(features,cgb);
    else if(t->key=="mm4")adapter_=std::make_unique<Model<Mm4NativeMods>>(features,cgb);
    else adapter_=std::make_unique<Model<Mm5NativeMods>>(features,cgb);
}
MegaManNativeMods::~MegaManNativeMods()=default;
uint8_t MegaManNativeMods::read(ModContext& c,uint16_t a,uint8_t v){return adapter_->read(c,a,v);}
void MegaManNativeMods::write(ModContext& c,uint16_t a,uint8_t v){adapter_->write(c,a,v);}
void MegaManNativeMods::instruction(ModContext& c,std::span<uint16_t,6> cpu,std::span<uint8_t> ram,std::span<uint8_t> hram){adapter_->instruction(c,cpu,ram,hram);}
std::span<const uint16_t> MegaManNativeMods::instruction_sites()const{return adapter_->instruction_sites();}
std::span<const uint16_t> MegaManNativeMods::read_sites()const{return adapter_->read_sites();}
bool MegaManNativeMods::poll_instructions()const{return adapter_->poll_instructions();}
void MegaManNativeMods::inputs(uint8_t a,uint8_t b){adapter_->inputs(a,b);}
void MegaManNativeMods::complete_frame(ModContext& c,bool cleared){adapter_->complete_frame(c,cleared);}
void MegaManNativeMods::scanline(ModContext& c,uint8_t line){adapter_->scanline(c,line);}
bool MegaManNativeMods::observes_scanlines()const{return true;}
uint32_t MegaManNativeMods::cpu_budget(const ModContext& c,bool cleared)const{return adapter_->cpu_budget(c,cleared);}
bool MegaManNativeMods::ends_cpu_work(uint16_t pc)const {
    if(title_=="mm1")return pc==0x1600||pc==0x1618;
    if(title_=="mm2")return pc==0x01db;
    if(title_=="mm3")return pc==0x1539||pc==0x155d;
    if(title_=="mm4")return pc==0x1849||pc==0x1879;
    return title_=="mm5"&&(pc==0x197e||pc==0x19ae);
}
bool MegaManNativeMods::polls_physical_clock(uint16_t pc)const {
    // Original fixed-bank input routines select the JOYP matrix rows and
    // execute settling reads. On DMG the row switch can take 48 physical
    // clocks; running this sequence entirely from CPU-only headroom makes
    // direction bits leak into A/B/Select/Start. Keep the complete original
    // polling spans physical, including their final row deselection.
    if(title_=="mm1")return pc>=0x157e&&pc<0x15b6;
    if(title_=="mm2")return pc>=0x0371&&pc<0x0399;
    if(title_=="mm5")return pc>=0x1879&&pc<0x18be;
    // Original fixed-bank VRAM writers wait on STAT/LY. CPU-only headroom
    // must let these reads advance the devices whose readiness they inspect.
    if(title_=="mm3")return (pc>=0x14a5&&pc<0x14e3)||pc==0x17c4||pc==0x17ca||pc==0x17f4||pc==0x17fa||pc==0x181f||pc==0x1825;
    if(title_=="mm4")return (pc>=0x1760&&pc<0x17a1)||pc==0x1b44||pc==0x1b4b||pc==0x1b51||pc==0x1b7a||pc==0x1b81||pc==0x1b87||pc==0x1bab||pc==0x1bb2||pc==0x1bb8;
    return false;
}
void MegaManNativeMods::viewport(unsigned w,unsigned h){adapter_->viewport(w,h);}
ModView MegaManNativeMods::render(ModContext& c,std::span<const uint32_t> n){return adapter_->render(c,n);}
std::vector<uint8_t> MegaManNativeMods::snapshot()const {
    std::vector<uint8_t> out(profile_.begin(),profile_.end());
    auto title=adapter_->snapshot();out.insert(out.end(),title.begin(),title.end());return out;
}
void MegaManNativeMods::restore(std::span<const uint8_t> s){
    if(s.size()<profile_.size()||!std::equal(profile_.begin(),profile_.end(),s.begin()))
        throw std::runtime_error("Mega Man checkpoint enhancement profile mismatch");
    adapter_->restore(s.subspan(profile_.size()));
}
}
