#pragma once
#include <gbcllr/mods.hpp>
#include <memory>
namespace gbcllr {
struct ModContext;
class Gen1NativeMods {
public:
 Gen1NativeMods(std::string_view,const NativeFeature&,bool);
 ~Gen1NativeMods();
 uint8_t read(ModContext&,uint16_t,uint8_t);
 bool observes_writes()const;
 bool write(ModContext&,uint16_t,uint8_t);
 void scanline(ModContext&,uint8_t);
 void complete_frame(ModContext&,bool cleared,bool frozen=false);
 void viewport(unsigned,unsigned);
 void preserve_host_presentation(const Gen1NativeMods&) noexcept;
 ModView render(ModContext&,std::span<const uint32_t>);
 std::vector<uint8_t> snapshot()const;
 void restore(std::span<const uint8_t>);
private:
 struct Impl;
 std::unique_ptr<Impl> impl_;
};
}
