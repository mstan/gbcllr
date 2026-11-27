#pragma once
#include <gbcllr/mods.hpp>
#include <memory>
namespace gbcllr {
struct ModContext;
class MegaManNativeMods {
public:
    MegaManNativeMods(std::string_view,std::span<const NativeFeature>,bool);
    ~MegaManNativeMods();
    uint8_t read(ModContext&,uint16_t,uint8_t);
    void write(ModContext&,uint16_t,uint8_t);
    void instruction(ModContext&,std::span<uint16_t,6>,std::span<uint8_t>,std::span<uint8_t>);
    std::span<const uint16_t> instruction_sites()const;
    std::span<const uint16_t> read_sites()const;
    bool poll_instructions()const;
    void inputs(uint8_t,uint8_t);
    void complete_frame(ModContext&,bool);
    void scanline(ModContext&,uint8_t);
    bool observes_scanlines()const;
    uint32_t cpu_budget(const ModContext&,bool)const;
    bool ends_cpu_work(uint16_t pc)const;
    bool polls_physical_clock(uint16_t pc)const;
    void viewport(unsigned,unsigned);
    ModView render(ModContext&,std::span<const uint32_t>);
    std::vector<uint8_t> snapshot()const;
    void restore(std::span<const uint8_t>);
private:
    struct Adapter;
    template<class T> struct Model;
    std::unique_ptr<Adapter> adapter_;
    std::string profile_;
    std::string title_;
};
}
