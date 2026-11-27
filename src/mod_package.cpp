#include <gbcllr/mods.hpp>
#include "megaman_titles.hpp"
#include <fstream>
#include <iomanip>
#include <set>
#include <sstream>
#include <stdexcept>

namespace gbcllr {
NativePackage read_mod_package(const std::filesystem::path& path,std::string_view rom) {
    auto size=std::filesystem::file_size(path);
    if(size>16384)throw std::runtime_error("native package exceeds 16 KiB manifest limit");
    std::ifstream file(path,std::ios::binary);char buffer[16385]{};
    file.read(buffer,sizeof buffer);auto read=file.gcount();
    if(read>16384)throw std::runtime_error("native package exceeds manifest limit");
    std::istringstream input(std::string(buffer,size_t(read)));std::string line;
    if(!std::getline(input,line))throw std::runtime_error("empty native package");
    if(line.ends_with('\r'))line.pop_back();
    if(line!="gbcllr-native-package 1")throw std::runtime_error("unsupported native package format");
    NativePackage package;std::set<std::string> seen;
    while(std::getline(input,line)) {
        std::istringstream row(line);std::string key;
        if(!(row>>key)||key.starts_with('#'))continue;
        if(!seen.insert(key).second)throw std::runtime_error("duplicate native package field: "+key);
        if(key=="id")row>>std::quoted(package.id);
        else if(key=="version")row>>std::quoted(package.version);
        else if(key=="author")row>>std::quoted(package.author);
        else if(key=="description")row>>std::quoted(package.description);
        else if(key=="source")row>>std::quoted(package.source);
        else if(key=="target")row>>std::quoted(package.target);
        else if(key=="plugin")row>>std::quoted(package.feature.plugin)>>package.feature.abi;
        else if(key=="view_width")row>>package.feature.view_width;
        else if(key=="asset_input")row>>std::quoted(package.feature.asset_input);
        else if(key=="gift")row>>package.feature.gift;
        else if(key=="player1_character")row>>package.feature.player1_character;
        else if(key=="no_flicker") {
            int enabled=-1;row>>enabled;
            if(enabled!=0&&enabled!=1)throw std::runtime_error("invalid no flicker option");
            package.feature.no_flicker=enabled!=0;
        }
        else if(key=="smooth_gameplay") {
            int enabled=-1;row>>enabled;
            if(enabled!=0&&enabled!=1)throw std::runtime_error("invalid smooth gameplay option");
            package.feature.smooth_gameplay=enabled!=0;
        }
        else if(key=="weapon_cycle") {
            int enabled=-1;row>>enabled;
            if(enabled!=0&&enabled!=1)throw std::runtime_error("invalid quick weapon selection option");
            package.feature.weapon_cycle=enabled!=0;
        }
        else if(key=="expanded_encounters") {
            int enabled=-1;row>>enabled;
            if(enabled!=0&&enabled!=1)throw std::runtime_error("invalid expanded encounter option");
            package.feature.expanded_encounters=enabled!=0;
        } else if(key=="colorization") {
            int enabled=-1;row>>enabled;
            if(enabled!=0&&enabled!=1)throw std::runtime_error("invalid native colorization option");
            package.feature.colorization=enabled!=0;
        } else if(key=="default_enabled") {
            int enabled=-1;row>>enabled;
            if(enabled!=0&&enabled!=1)throw std::runtime_error("invalid native package default");
            package.default_enabled=enabled!=0;
        } else throw std::runtime_error("unknown native package field: "+key);
        if(row.fail())throw std::runtime_error("invalid native package field: "+key);
        row>>std::ws;if(!row.eof())throw std::runtime_error("trailing native package field data: "+key);
    }
    auto identifier=[](const std::string& s){return !s.empty()&&s.size()<=96&&s.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789.-_")==std::string::npos;};
    if(!identifier(package.id)||!identifier(package.feature.plugin)||package.version.empty()||package.version.size()>31||package.target.size()!=64||package.target!=rom)
        throw std::runtime_error("native package identity or ROM target mismatch");
    if(package.author.size()>255||package.description.size()>2048||package.source.size()>1024)
        throw std::runtime_error("native package attribution exceeds limit");
    if(!package.feature.asset_input.empty()&&(package.feature.asset_input.size()!=64||package.feature.asset_input.find_first_not_of("0123456789abcdef")!=std::string::npos))
        throw std::runtime_error("invalid native asset input identity");
    if(!seen.contains("plugin")||!seen.contains("view_width")||!seen.contains("default_enabled")||package.feature.abi!=1||
       package.feature.player1_character>1||(seen.contains("player1_character")&&package.feature.plugin!="gbcllr.xtreme2.coop")||
       package.feature.gift>74||(package.feature.gift&&package.feature.plugin!="gbcllr.gen2.adaptive-view")||
       (package.feature.expanded_encounters&&package.feature.plugin!="gbcllr.gen1.extended")||
       (package.feature.weapon_cycle&&!megaman_title_for_plugin(package.feature.plugin)&&package.feature.plugin!="gbcllr.xtreme2.adaptive-view"&&package.feature.plugin!="gbcllr.xtreme2.coop")||
       (package.feature.smooth_gameplay&&!megaman_title_for_plugin(package.feature.plugin)&&package.feature.plugin!="gbcllr.xtreme2.adaptive-view"&&package.feature.plugin!="gbcllr.xtreme2.coop")||
       (package.feature.no_flicker&&!megaman_title_for_plugin(package.feature.plugin))||
       (package.feature.view_width!=-1&&(package.feature.view_width<160||package.feature.view_width>4096)))
        throw std::runtime_error("unsupported native package feature contract");
    return package;
}
}
