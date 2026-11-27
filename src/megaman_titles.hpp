#pragma once
#include <array>
#include <string_view>

namespace gbcllr {
struct MegaManTitle {
    std::string_view key,name,sha256,adaptive,dx;
};
inline constexpr std::array<MegaManTitle,5> megaman_titles{{
    {"mm1","Mega Man: Dr. Wily's Revenge","33d16365318411f063edd9100c79458aabb7ea71bb1873e30f38b93814f0ec6e","gbcllr.mm1.adaptive-view","gbcllr.mm1.dx"},
    {"mm2","Mega Man II","be3fc7ab2d7eb79a9e2538b090bdecc2694740f3beda933f34cb1e4059b10a11","gbcllr.mm2.adaptive-view","gbcllr.mm2.dx"},
    {"mm3","Mega Man III","7344a36fcfc8151098238529218762e44c5a1546fab7e7fd5d32927e06cbf5a8","gbcllr.mm3.adaptive-view","gbcllr.mm3.dx"},
    {"mm4","Mega Man IV","4d980ca46a83cd127312dbc445ed1a83e97ed7a8026dce6fdd22a01b1895781b","gbcllr.mm4.adaptive-view","gbcllr.mm4.dx"},
    {"mm5","Mega Man V","7a108770a7c1ad592b52d0c46d7ead422d0a20961abaafca3d40086e3f2f588f","gbcllr.mm5.adaptive-view","gbcllr.mm5.dx"}
}};
inline constexpr const MegaManTitle* megaman_title_for_rom(std::string_view sha) {
    for(const auto& t:megaman_titles)if(t.sha256==sha)return &t;
    return nullptr;
}
inline constexpr const MegaManTitle* megaman_title_for_plugin(std::string_view plugin) {
    for(const auto& t:megaman_titles)if(t.adaptive==plugin||t.dx==plugin)return &t;
    return nullptr;
}
}
