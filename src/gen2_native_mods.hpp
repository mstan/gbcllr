#pragma once
#include <gbcllr/mods.hpp>
#include <memory>
namespace gbcllr {
struct ModContext;
class Machine;
class Gen2NativeMods {
public:
 Gen2NativeMods(std::string_view,const NativeFeature&,bool);
 ~Gen2NativeMods();
 uint8_t read(ModContext&,uint16_t,uint8_t);
 void scanline(ModContext&,uint8_t);
 void complete_frame(ModContext&,bool cleared);
 void frame(Machine&);
 void viewport(unsigned,unsigned);
 void preserve_host_presentation(const Gen2NativeMods&) noexcept;
 ModView render(ModContext&,std::span<const uint32_t>);
 // Pure host output dimensions. Unlike viewport(), this never changes the
 // serialized module or continuation-anchor state.
 ModView render_host(ModContext&,std::span<const uint32_t>,unsigned,unsigned);
 static bool supports(std::string_view);
 static bool linked_pair(std::string_view,std::string_view);
 static bool linked_profile(std::span<const NativeFeature>,bool cgb);
 std::vector<uint8_t> snapshot()const;
 void checkpoint(void*,void (*)(void*,const uint8_t*,size_t))const;
 void restore(std::span<const uint8_t>);
private:
 struct Impl;
 std::unique_ptr<Impl> impl_;
};
}
