#pragma once
#include <gbcllr/mods.hpp>
#include <memory>
namespace gbcllr {
struct ModContext;
class Mm5NativeMods {
public:
 explicit Mm5NativeMods(std::span<const NativeFeature>,bool model_cgb);
 ~Mm5NativeMods();
 uint8_t read(ModContext&,uint16_t,uint8_t);
 void write(ModContext&,uint16_t,uint8_t);
 void instruction(ModContext&,std::span<uint16_t,6>,std::span<uint8_t>,std::span<uint8_t>);
 static std::span<const uint16_t> instruction_sites();
 static std::span<const uint16_t> read_sites();
 bool poll_instructions()const;
 void inputs(uint8_t,uint8_t);
 void complete_frame(ModContext&,bool);
 void scanline(ModContext&,uint8_t);
 uint32_t cpu_budget(const ModContext&,bool)const;
 void viewport(unsigned,unsigned);
 ModView render(ModContext&,std::span<const uint32_t>);
 std::vector<uint8_t> snapshot()const;
 void restore(std::span<const uint8_t>);
private:
 struct Impl;
 std::unique_ptr<Impl> impl_;
};
}
