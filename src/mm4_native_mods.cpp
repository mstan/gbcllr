#include "mm4_native_mods.hpp"
namespace gbcllr {
Mm4NativeMods::Mm4NativeMods(std::span<const NativeFeature> features,bool model_cgb)
 :Mm3NativeMods(4,features,model_cgb){}
}
