#pragma once
#include "mm3_native_mods.hpp"
namespace gbcllr {
class Mm4NativeMods : public Mm3NativeMods {
public:
 explicit Mm4NativeMods(std::span<const NativeFeature>,bool model_cgb);
};
}
