#pragma once
#include <gbcllr/netplay.hpp>
#include <memory>

struct DesktopConfig;
struct DesktopNetLobby;
struct DesktopNetStart {
    std::array<std::vector<uint8_t>,2> images;
    std::array<gbcllr::Options,2> machines;
    gbcllr::NetOptions options;
    std::filesystem::path local_save;
    std::string original_save_sha;
    bool write_progress=true;
};
void desktop_net_save(const DesktopNetStart&,const gbcllr::NetSession&);
#ifdef RECOMP_LAUNCHER
#include "recomp_launcher.h"
void desktop_net_lobby(DesktopConfig&,RecompLauncherCGameInfo&);
void desktop_net_launch(DesktopConfig&,const RecompLauncherCNetplayLaunch&,
    const gbcllr::Options&,const std::filesystem::path& save);
#endif
