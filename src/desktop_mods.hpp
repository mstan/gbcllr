#pragma once
#include "desktop_ui.hpp"
#include "recomp_launcher.h"
RecompLauncherCModProvider desktop_mod_provider(DesktopConfig&);

inline constexpr const char* desktop_presentation_package="gbcllr.presentation";
inline constexpr const char* desktop_presentation_feature="display-colors";
int desktop_verify_boot_for_rom(void*,const char*,const char*,RecompLauncherCBiosVerify*);

inline constexpr const char* desktop_sgb_package="gbcllr.sgb-service";
inline constexpr const char* desktop_sgb_feature="cartridge-sgb";
