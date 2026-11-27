#include "desktop_mods.hpp"
#include "native_mods.hpp"
#include "megaman_titles.hpp"
#include "gen2_gifts.hpp"
#include <charconv>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <sstream>
#include <iomanip>

namespace {
constexpr const char* presentation_package=desktop_presentation_package;
constexpr const char* presentation_feature=desktop_presentation_feature;
bool presentation_identity(const char* p,const char* f){return p&&f&&!std::strcmp(p,presentation_package)&&!std::strcmp(f,presentation_feature);}
bool sgb_identity(DesktopConfig& c,const char* p,const char* f){return c.sgb_supported&&p&&f&&!std::strcmp(p,desktop_sgb_package)&&!std::strcmp(f,desktop_sgb_feature);}
constexpr const char* feature="adaptive-view";
constexpr int widths[]={-1,256,336,512,160};
constexpr const char* labels[]={"Fit to screen","16:9","21:9","32:9","Original"};
DesktopConfig& config(void* p){return *static_cast<DesktopConfig*>(p);}
bool identity(DesktopConfig& c,const char* p,const char* f){return c.package&&p&&f&&c.package->id==p&&std::strcmp(f,feature)==0;}
bool gift_identity(DesktopConfig& c,const char* p,const char* f){return c.package&&c.package->feature.plugin=="gbcllr.gen2.adaptive-view"&&p&&f&&c.package->id==p&&!std::strcmp(f,"native-gift");}
template<size_t N> void copy(char(&dst)[N],const std::string& s){std::snprintf(dst,N,"%s",s.c_str());}
std::string value(int n){return n<0?"fit":std::to_string(n);}
bool sml2(DesktopConfig& c){return c.package&&c.package->feature.plugin=="gbcllr.sml2.adaptive-view";}
bool gen2(DesktopConfig& c){return c.package&&c.package->feature.plugin=="gbcllr.gen2.adaptive-view";}
bool extended(DesktopConfig& c){return c.package&&c.package->feature.plugin=="gbcllr.gen1.extended";}
bool extended_identity(DesktopConfig& c,const char* p,const char* f,const char* id){return extended(c)&&p&&f&&c.package->id==p&&!std::strcmp(f,id);}
bool oracle(DesktopConfig& c){return c.package&&c.package->feature.plugin=="gbcllr.oracle.adaptive-view";}
bool coop(DesktopConfig& c){return c.package&&c.package->feature.plugin=="gbcllr.xtreme2.coop";}
bool xtreme2(DesktopConfig& c){return coop(c)||(c.package&&c.package->feature.plugin=="gbcllr.xtreme2.adaptive-view");}
bool megaman(DesktopConfig& c){return c.package&&gbcllr::megaman_title_for_plugin(c.package->feature.plugin);}
bool dx_identity(DesktopConfig& c,const char* p,const char* f){return c.dx_package&&p&&f&&c.dx_package->id==p&&!std::strcmp(f,"dx-color");}
bool weapon_identity(DesktopConfig& c,const char* p,const char* f){return (xtreme2(c)||megaman(c))&&p&&f&&c.package->id==p&&!std::strcmp(f,"weapon-cycle");}
bool coop_identity(DesktopConfig& c,const char* p,const char* f){return xtreme2(c)&&p&&f&&c.package->id==p&&!std::strcmp(f,"coop");}
const char* author(DesktopConfig& c){return megaman(c)?"gbcllr contributors":oracle(c)?"gbcllr and Oracle Recomp contributors":(gen2(c)||extended(c)||(c.package&&c.package->feature.plugin=="gbcllr.gen1.adaptive-view"))?"gbcllr contributors":sml2(c)?"gbcllr and Super Mario Land 2 Recomp contributors":"gbcllr and Mega Man Xtreme 2 Recomp contributors";}
const char* dx_author(DesktopConfig& c){return c.dx_package->author.empty()?"See bundled DX credits":c.dx_package->author.c_str();}
bool custom(DesktopConfig& c){for(auto n:widths)if(n==c.view_width)return false;return true;}
int game_packages(DesktopConfig& c){return int(bool(c.package))+int(bool(c.dx_package));}
int game_features(void* p){auto& c=config(p);return (c.package?(megaman(c)?4:xtreme2(c)?4:extended(c)?3:gen2(c)?2:1):0)+int(bool(c.dx_package));}
int count(void* p){return game_packages(config(p))+1+config(p).sgb_supported;}
int feature_count(void* p){return game_features(p)+1+config(p).sgb_supported;}
int package_get(void* p,int i,RecompLauncherCModPackage* out) {
 auto& c=config(p);if(!out||i<0||i>=count(p))return 0;*out={};
 if(i==game_packages(c)+1){
  copy(out->id,desktop_sgb_package);copy(out->version,"1");copy(out->name,"Cartridge Super Game Boy");copy(out->author,"gbcllr contributors");
  copy(out->description,"Enable a DMG/SGB session on the next start. Save in game before quitting. The cartridge supplies its own regional colors and border; display switches then apply live.");
  copy(out->status,c.sgb_current?"Active in current session":"Requires next start");out->enabled=c.sgb_requested;return 1;
 }
 if(i==game_packages(c)){
  copy(out->id,presentation_package);copy(out->version,"1");copy(out->name,"Display colors and filters");copy(out->author,"gbcllr contributors");
  copy(out->description,"Live host palettes, color correction and screen filters. Native preserves authentic game colors. SGB choices are palette-only tints.");
  copy(out->status,"Built in; applies live in Mods");out->enabled=c.presentation.enabled;return 1;
 }
 if(c.dx_package&&i==1){
  copy(out->id,c.dx_package->id);copy(out->version,c.dx_package->version);copy(out->name,"DX color");copy(out->author,dx_author(c));
  const bool installed=std::filesystem::exists(c.profile/"native-dx-package.gbmod");
  copy(out->description,c.dx_package->description);copy(out->status,installed?"Installed native mod; applies on next start":"Bundled native mod; applies on next start");out->enabled=c.colorization;out->removable=installed;return 1;
 }
 copy(out->id,c.package->id);copy(out->version,c.package->version);copy(out->name,megaman(c)||xtreme2(c)||gen2(c)||extended(c)?"Native enhancements":"Adaptive widescreen");
 copy(out->author,author(c));
 copy(out->description,extended(c)?"Choose widescreen,Extended content and optional expanded encounters.":oracle(c)?"Reveal more of large rooms and their actors. Other scenes retain the original view.":(c.package&&c.package->feature.plugin=="gbcllr.gen1.adaptive-view")?"Expand the qualified Pallet field and its native actors. Other scenes retain the original view.":gen2(c)?"Expand the qualified Cherrygrove field and its native objects. Other scenes retain the original view.":"Expand the world, enemy range and HUD to your window.");
 bool installed=std::filesystem::exists(c.profile/"native-package.gbmod");
 copy(out->status,installed?"Installed package":"Bundled package");out->removable=installed;out->enabled=c.adaptive||c.gift_enabled||(extended(c)&&c.extended_content)||((xtreme2(c)||megaman(c))&&(c.weapon_cycle||c.smooth_gameplay||c.no_flicker||c.coop_enabled));return 1;
}
int feature_get(void* p,int i,RecompLauncherCModFeature* out) {
 auto& c=config(p);if(!out||i<0||i>=feature_count(p))return 0;*out={};
 if(i==game_features(p)+1){
  copy(out->id,desktop_sgb_feature);copy(out->package_id,desktop_sgb_package);copy(out->package_version,"1");copy(out->package_name,"Cartridge Super Game Boy");
  copy(out->name,"SGB session (next start)");copy(out->group,"Super Game Boy");copy(out->author,"gbcllr contributors");
  copy(out->description,"Next start uses a DMG/SGB compatibility session with your native save. Save in game before quitting; changing this never restarts current play. Colors and cartridge border apply live after startup. Offline only.");
  copy(out->status,c.sgb_current?(c.sgb_requested?"Active; next start enabled":"Active; next start returns to native"):c.sgb_requested?"Selected for next start":"Native session");
  out->enabled=c.sgb_requested;out->option_count=2;out->channel=RECOMP_MOD_CHANNEL_EXPERIMENTAL;return 1;
 }
 if(i==game_features(p)){
  copy(out->id,presentation_feature);copy(out->package_id,presentation_package);copy(out->package_version,"1");copy(out->package_name,"Display colors and filters");
  copy(out->name,"Display colors and filters");copy(out->group,"Presentation");copy(out->author,"gbcllr contributors");
  copy(out->description,"Change palettes, color correction and filters live from the in-game Mods menu. Applies to gameplay, battles and menus. SGB palettes do not enable SGB hardware or borders.");
  copy(out->status,c.presentation.enabled?"Enabled":"Disabled");out->enabled=c.presentation.enabled;out->option_count=int(gbcllr::presentation_options().size());out->quick=1;return 1;
 }
 if(megaman(c)&&i==4&&c.dx_package){
  copy(out->id,"dx-color");copy(out->package_id,c.dx_package->id);copy(out->package_version,c.dx_package->version);copy(out->package_name,"DX color");
  copy(out->name,"DX color");copy(out->group,"Presentation");copy(out->author,dx_author(c));copy(out->description,c.dx_package->description);
  copy(out->status,c.colorization?"Enabled on next start":"Disabled");out->enabled=c.colorization;return 1;
 }
 if(megaman(c)&&i==3){
  copy(out->id,"no-flicker");copy(out->package_id,c.package->id);copy(out->package_version,c.package->version);copy(out->package_name,"Native enhancements");
  copy(out->name,"No sprite flicker");copy(out->group,"Presentation");copy(out->author,author(c));
  copy(out->description,"Remove hardware and alternating-frame sprite flicker; damage and invincibility flashes remain. Widescreen and DX always use full sprite drawing. Turning this off restores original limits at native width with DX off. Applies on next start.");
  copy(out->status,c.no_flicker?"Enabled on next start":"Original limits at native width, DX off");out->enabled=c.no_flicker;return 1;
 }
 if(extended(c)&&i){
  copy(out->id,i==1?"extended-content":"expanded-encounters");copy(out->package_id,c.package->id);copy(out->package_version,c.package->version);
  copy(out->package_name,"Native enhancements");copy(out->name,i==1?"Extended content":"Expanded encounters");copy(out->group,"Optional content");copy(out->author,author(c));
  copy(out->description,i==1?"Add the Extended Pokemon and moves. Uses a separate desktop save,starting from your current game.":"Find additional Pokemon in rare wild encounters. Original species remain available.");
  out->enabled=c.extended_content&&(i==1||c.expanded_encounters);copy(out->status,out->enabled?"Enabled":i==2&&!c.extended_content?"Requires Extended content":"Disabled");out->channel=RECOMP_MOD_CHANNEL_EXPERIMENTAL;return 1;
 }
 if(i==1&&(xtreme2(c)||megaman(c))){
  copy(out->id,"weapon-cycle");copy(out->package_id,c.package->id);copy(out->package_version,c.package->version);
  copy(out->package_name,"Native enhancements");copy(out->name,"Quick weapon switching");copy(out->group,"Gameplay");copy(out->author,author(c));
  copy(out->description,megaman(c)?"L/R selects previous/next acquired equipment, including empty weapons. Holding does not repeat. Map Virtual L/R in Controller settings. Applies on next start.":"Virtual L/R cycles X's acquired weapons. Zero keeps the original technique controls. Change takes effect on the next start; map Virtual L/R in Controller settings.");
  out->enabled=c.weapon_cycle;copy(out->status,c.weapon_cycle?"Enabled on next start":"Disabled");out->channel=RECOMP_MOD_CHANNEL_EXPERIMENTAL;return 1;
 }
 if((i==3&&xtreme2(c))||(i==2&&megaman(c))){
  copy(out->id,"smooth-gameplay");copy(out->package_id,c.package->id);copy(out->package_version,c.package->version);
  copy(out->package_name,"Native enhancements");copy(out->name,"Smooth gameplay");copy(out->group,"Gameplay");copy(out->author,author(c));
  copy(out->description,megaman(c)?"Give game logic extra CPU time to remove slowdown while display and audio keep normal speed. DX includes its authored CPU timing even with this setting off. Applies on next start.":"Remove sprite-limit flicker and give game logic extra CPU time while keeping normal display and audio speed. Applies on the next start; the host selects it for co-op sessions.");
  out->enabled=c.smooth_gameplay;copy(out->status,c.smooth_gameplay?"Enabled on next start":"Disabled");out->channel=RECOMP_MOD_CHANNEL_EXPERIMENTAL;return 1;
 }
 if(i==2&&xtreme2(c)){
  copy(out->id,"coop");copy(out->package_id,c.package->id);copy(out->package_version,c.package->version);
  copy(out->package_name,"Native enhancements");copy(out->name,"X + Zero co-op");copy(out->group,"Gameplay");copy(out->author,author(c));
  copy(out->description,"Play together as X and Zero with original graphics and personal menus. Only the host campaign is saved. Takes effect on the next start.");
  out->enabled=c.coop_enabled;copy(out->status,c.coop_enabled?"Enabled on next start":"Disabled");out->option_count=3;out->channel=RECOMP_MOD_CHANNEL_EXPERIMENTAL;return 1;
 }
 if(i==1){copy(out->id,"native-gift");copy(out->package_id,c.package->id);copy(out->package_version,c.package->version);
  copy(out->package_name,"Native enhancements");copy(out->name,"Queue a gift");copy(out->group,"Optional gifts");copy(out->author,author(c));
  copy(out->description,"One gift on this launch after Carrie's unlock. Collect items upstairs in a Pokémon Center. Decorations appear after Save and Continue. Existing pending gifts are preserved. Offline only.");
  copy(out->status,c.gift_enabled?"Queued for next launch":"Not queued");out->enabled=c.gift_enabled;out->option_count=1;out->channel=RECOMP_MOD_CHANNEL_EXPERIMENTAL;return 1;}
 copy(out->id,feature);copy(out->package_id,c.package->id);copy(out->package_version,c.package->version);
 copy(out->package_name,"Adaptive widescreen");copy(out->name,"Adaptive widescreen");copy(out->group,"Presentation");
 copy(out->author,author(c));
 bool sgb_gen2=gen2(c)&&(c.sgb_requested||c.sgb_current);
 copy(out->description,sgb_gen2?"DMG/SGB sessions use the original SGB view. Your widescreen choice is kept for native Color sessions.":"Fit follows the window. Unsupported scenes retain the original view.");
 copy(out->status,sgb_gen2?"Original SGB view; Color choice kept":c.adaptive?"Enabled":"Disabled");out->enabled=c.adaptive;out->option_count=(sml2(c)||oracle(c))?2:1;out->channel=RECOMP_MOD_CHANNEL_EXPERIMENTAL;out->quick=1;return 1;
}
int option_get(void* p,const char* package,const char* f,int i,RecompLauncherCModOption* out) {
 auto& c=config(p);if(!out||i<0)return 0;*out={};
 if(sgb_identity(c,package,f)){
  if(i>1)return 0;copy(out->id,i?"border":"colors");copy(out->label,i?"Cartridge border":"Cartridge colors");
  copy(out->description,i?"Original 256x224 SGB layout. Toggle live in an active SGB session once the cartridge has sent its border.":"Actual game-supplied SGB palettes and regions. Toggle live after the DMG/SGB session has started.");
  copy(out->value,(i?c.sgb_border:c.sgb_colors)?"1":"0");copy(out->default_value,i?"0":"1");out->type=RECOMP_MOD_OPTION_CHOICE;out->choice_count=2;return 1;
 }
 if(presentation_identity(package,f)){
  auto options=gbcllr::presentation_options();if(size_t(i)>=options.size())return 0;const auto& o=options[i];
  copy(out->id,o.key);copy(out->label,o.label);copy(out->description,o.description);copy(out->value,std::to_string(gbcllr::presentation_value(c.presentation,c.linear,o.key)));
  copy(out->default_value,"0");out->type=RECOMP_MOD_OPTION_CHOICE;out->choice_count=int(o.choices.size());return 1;
 }
 if(coop_identity(c,package,f)){
  if(i>2)return 0;
  copy(out->id,i==0?"player1-character":i==1?"coop-view":"shared-world");
  copy(out->label,i==0?"Player 1 character":i==1?"Co-op view":"Keep players together");
  copy(out->description,i==0?"Player 2 receives the counterpart. Takes effect on the next start.":i==1?"Local independent and split views allow free separation. Network camera rules are settled at session start.":"Catch up a partner who leaves the unified view. Independent and split play disable distance catch-up. Network games use the host's starting choice.");
  copy(out->value,std::to_string(i==0?c.coop_first:i==1?c.coop_view:unsigned(c.coop_shared)));copy(out->default_value,i==2?"1":"0");out->type=RECOMP_MOD_OPTION_CHOICE;out->choice_count=i==1?3:2;return 1;
 }
 if(i>1)return 0;
 if(gift_identity(c,package,f)){if(i)return 0;copy(out->id,"gift");copy(out->label,"Gift");copy(out->description,"Choose an original gift item or bedroom decoration.");copy(out->value,std::to_string(c.gift_choice));copy(out->default_value,"1");out->type=RECOMP_MOD_OPTION_CHOICE;out->choice_count=int(gbcllr::gen2_gifts.size());return 1;}
 if(!identity(c,package,f))return 0;
 if(i==1){
  if(oracle(c)){copy(out->id,"hide-closed-rooms");copy(out->label,"Hide closed rooms");copy(out->description,"Hide adjacent dungeon rooms until their doors open.");copy(out->value,c.hide_closed_rooms?"1":"0");copy(out->default_value,"0");out->type=RECOMP_MOD_OPTION_CHOICE;out->choice_count=2;return 1;}
  if(!sml2(c))return 0;copy(out->id,"dx-color");copy(out->label,"DX color");copy(out->description,"Use DX character art and color.");copy(out->value,c.colorization?"1":"0");copy(out->default_value,"1");out->type=RECOMP_MOD_OPTION_CHOICE;out->choice_count=2;return 1;
 }
 copy(out->id,"aspect");copy(out->label,"Aspect ratio");copy(out->description,"Fit to screen adapts when you resize the window.");
 copy(out->value,value(c.view_width));copy(out->default_value,"fit");out->type=RECOMP_MOD_OPTION_CHOICE;out->choice_count=5+custom(c);return 1;
}
int choice_get(void* p,const char* package,const char* f,const char* option,int i,RecompLauncherCModChoice* out) {
 auto& c=config(p);if(!out||!option||i<0)return 0;*out={};
 if(sgb_identity(c,package,f)){
  if((std::strcmp(option,"colors")&&std::strcmp(option,"border"))||i>1)return 0;copy(out->value,i?"1":"0");copy(out->label,i?"On":"Off");return 1;
 }
 if(presentation_identity(package,f)){
  for(const auto& o:gbcllr::presentation_options())if(!std::strcmp(option,o.key)&&size_t(i)<o.choices.size()){copy(out->value,std::to_string(i));copy(out->label,o.choices[i]);return 1;}return 0;
 }
 if(gift_identity(c,package,f)){if(std::strcmp(option,"gift")||size_t(i)>=gbcllr::gen2_gifts.size())return 0;const auto& gift=gbcllr::gen2_gifts[size_t(i)];copy(out->value,std::to_string(i+1));copy(out->label,std::string(gift.item?"Item: ":"Decoration: ")+std::string(gift.name));return 1;}
 if(coop_identity(c,package,f)){
  if(!std::strcmp(option,"coop-view")&&i<3){copy(out->value,std::to_string(i));copy(out->label,i==0?"Unified":i==1?"Independent":"Split screen");return 1;}
  if(!std::strcmp(option,"player1-character")&&i<2){copy(out->value,i?"1":"0");copy(out->label,i?"Zero":"X");return 1;}
  if(!std::strcmp(option,"shared-world")&&i<2){copy(out->value,i?"1":"0");copy(out->label,i?"On":"Off");return 1;}return 0;
 }
 if(!identity(c,package,f))return 0;
 if(!std::strcmp(option,"hide-closed-rooms")){if(!oracle(c)||i>=2)return 0;copy(out->value,i?"1":"0");copy(out->label,i?"On":"Off");return 1;}
 if(!std::strcmp(option,"dx-color")){if(!sml2(c)||i>=2)return 0;copy(out->value,i?"1":"0");copy(out->label,i?"On":"Off");return 1;}
 if(std::strcmp(option,"aspect")||i>=5+custom(c))return 0;
 copy(out->value,value(i<5?widths[i]:c.view_width));copy(out->label,i<5?labels[i]:"Custom ("+std::to_string(c.view_width)+")");return 1;
}
int enable(void* p,const char* package,const char* f,int enabled) {
 auto& c=config(p);
 if(sgb_identity(c,package,f)){c.sgb_requested=enabled!=0;return 1;}
 if(presentation_identity(package,f)){c.presentation.enabled=enabled!=0;return 1;}
 if(dx_identity(c,package,f)){c.colorization=enabled!=0;c.mod_color_saved=true;return 1;}
 if(megaman(c)&&package&&f&&c.package->id==package&&!std::strcmp(f,"no-flicker")){c.no_flicker=enabled!=0;c.mod_no_flicker_saved=true;return 1;}
 if((xtreme2(c)||megaman(c))&&package&&f&&c.package->id==package&&!std::strcmp(f,"smooth-gameplay")){c.smooth_gameplay=enabled!=0;c.mod_smooth_saved=true;return 1;}
 if(weapon_identity(c,package,f)){c.weapon_cycle=enabled!=0;c.mod_weapon_cycle_saved=true;return 1;}
 if(coop_identity(c,package,f)){c.coop_enabled=enabled!=0;c.coop_enabled_saved=true;return 1;}
 if(extended_identity(c,package,f,"extended-content")){c.extended_content=enabled!=0;return 1;}
 if(extended_identity(c,package,f,"expanded-encounters")){c.expanded_encounters=enabled!=0;if(enabled)c.extended_content=true;return 1;}
 if(gift_identity(c,package,f)){c.gift_enabled=enabled!=0;return 1;}if(!identity(c,package,f))return 0;c.adaptive=enabled!=0;c.mod_enabled_saved=true;return 1;
}
int option_set(void* p,const char* package,const char* f,const char* option,const char* v) {
 auto& c=config(p);if(!option||!v)return 0;
 if(sgb_identity(c,package,f)){
  if(std::strcmp(v,"0")&&std::strcmp(v,"1"))return 0;
  if(!std::strcmp(option,"colors"))c.sgb_colors=!std::strcmp(v,"1");else if(!std::strcmp(option,"border"))c.sgb_border=!std::strcmp(v,"1");else return 0;return 1;
 }
 if(presentation_identity(package,f)){
  int n=-1;auto end=v+std::strlen(v);auto result=std::from_chars(v,end,n);
  if(result.ec!=std::errc{}||result.ptr!=end||!gbcllr::presentation_set(c.presentation,c.linear,option,n))return 0;
  if(!std::strcmp(option,"filter"))c.presentation_filter_selected=true;return 1;
 }
 if(gift_identity(c,package,f)){if(std::strcmp(option,"gift"))return 0;int choice=0;auto end=v+std::strlen(v);auto result=std::from_chars(v,end,choice);if(result.ec!=std::errc{}||result.ptr!=end||choice<1||size_t(choice)>gbcllr::gen2_gifts.size())return 0;c.gift_choice=choice;return 1;}
 if(coop_identity(c,package,f)){
  if(!std::strcmp(option,"coop-view")&&(!std::strcmp(v,"0")||!std::strcmp(v,"1")||!std::strcmp(v,"2"))){c.coop_view=unsigned(v[0]-'0');return 1;}
  if(std::strcmp(v,"0")&&std::strcmp(v,"1"))return 0;
  if(!std::strcmp(option,"player1-character")){c.coop_first=unsigned(v[0]-'0');c.coop_first_saved=true;return 1;}
  if(!std::strcmp(option,"shared-world")){c.coop_shared=v[0]=='1';return 1;}return 0;
 }
 if(!identity(c,package,f))return 0;
 if(!std::strcmp(option,"hide-closed-rooms")){if(!oracle(c)||(std::strcmp(v,"0")&&std::strcmp(v,"1")))return 0;c.hide_closed_rooms=!std::strcmp(v,"1");return 1;}
 if(!std::strcmp(option,"dx-color")){if(!sml2(c)||(std::strcmp(v,"0")&&std::strcmp(v,"1")))return 0;c.colorization=!std::strcmp(v,"1");c.mod_color_saved=true;return 1;}
 if(std::strcmp(option,"aspect"))return 0;
 int width=-1;
 if(std::strcmp(v,"fit")) {auto end=v+std::strlen(v);auto result=std::from_chars(v,end,width);if(result.ec!=std::errc{}||result.ptr!=end||width<160||width>4096)return 0;}
 c.view_width=width;c.mod_width_saved=true;return 1;
}
int commit(void* p,const char* image) {
 auto& c=config(p);try {
  if(c.package&&(!image||gbcllr::sha256(gbcllr::read_file(image))!=c.package->target))throw std::runtime_error("This package targets another cartridge revision.");
  c.session_mods=true;c.persist();c.mod_error.clear();return 1;
 }catch(const std::exception& e){c.mod_error=e.what();return 0;}
}
int commit_netplay(void* p,const char*) {auto& c=config(p);if(c.sgb_requested){c.mod_error="SGB compatibility sessions are available in offline play only. Disable SGB for the next start before linked play.";return 0;}if(extended(c)&&c.extended_content){c.mod_error="Extended content is not yet available for linked play.";return 0;}if(c.gift_enabled){c.mod_error="Native gifts are available in offline play only. Unqueue the gift before linked play.";return 0;}c.session_mods=xtreme2(c)&&c.coop_enabled;return 1;}
int install(void* p,const char* path) {
 auto& c=config(p);try {
  if(!c.package||!path)throw std::runtime_error("Choose a native package for this cartridge.");
  auto package=gbcllr::read_mod_package(path,c.package->target);
  std::array selected{package.feature};gbcllr::NativeMods verified(package.target,selected,c.color);
  std::ostringstream text;text<<"gbcllr-native-package 1\nid "<<std::quoted(package.id)<<"\nversion "<<std::quoted(package.version)
   <<"\ntarget "<<std::quoted(package.target)<<"\nplugin "<<std::quoted(package.feature.plugin)<<' '<<package.feature.abi
   <<"\nview_width "<<package.feature.view_width<<"\nasset_input "<<std::quoted(package.feature.asset_input)<<"\ncolorization "<<package.feature.colorization<<"\ngift "<<package.feature.gift<<"\nexpanded_encounters "<<package.feature.expanded_encounters<<"\ndefault_enabled "<<package.default_enabled<<'\n';
  if(package.feature.plugin=="gbcllr.xtreme2.coop")text<<"player1_character "<<package.feature.player1_character<<'\n';
  if(gbcllr::megaman_title_for_plugin(package.feature.plugin)||package.feature.plugin=="gbcllr.xtreme2.coop"||package.feature.plugin=="gbcllr.xtreme2.adaptive-view")text<<"weapon_cycle "<<package.feature.weapon_cycle<<"\nsmooth_gameplay "<<package.feature.smooth_gameplay<<'\n';
  if(gbcllr::megaman_title_for_plugin(package.feature.plugin))text<<"no_flicker "<<package.feature.no_flicker<<'\n';
  if(!package.author.empty())text<<"author "<<std::quoted(package.author)<<'\n';
  if(!package.description.empty())text<<"description "<<std::quoted(package.description)<<'\n';
  if(!package.source.empty())text<<"source "<<std::quoted(package.source)<<'\n';
  auto title=gbcllr::megaman_title_for_plugin(package.feature.plugin);
  const bool dx=title&&package.feature.plugin==title->dx;
  auto data=text.str();gbcllr::write_file_atomic(c.profile/(dx?"native-dx-package.gbmod":"native-package.gbmod"),{reinterpret_cast<const uint8_t*>(data.data()),data.size()});
  if(dx)c.dx_package=std::move(package);else c.package=std::move(package);c.mod_error.clear();return 1;
 }catch(const std::exception& e){c.mod_error=e.what();return 0;}
}
int remove(void* p,const char* id,const char* version) {
 auto& c=config(p);try {
  if(c.dx_package&&id&&version&&c.dx_package->id==id&&c.dx_package->version==version){
   const auto* title=gbcllr::megaman_title_for_plugin(c.dx_package->feature.plugin);
   auto fallback=gbcllr::read_mod_package(c.bundled_package.parent_path()/(std::string(title->key)+"-dx.gbmod"),c.dx_package->target);
   if(!std::filesystem::remove(c.profile/"native-dx-package.gbmod"))throw std::runtime_error("Only an installed package can be removed.");
   c.dx_package=std::move(fallback);c.mod_error.clear();return 1;
  }
  if(!c.package||!id||!version||c.package->id!=id||c.package->version!=version)
   throw std::runtime_error("Only an installed package can be removed.");
  auto fallback=gbcllr::read_mod_package(c.bundled_package,c.package->target);
  if(!std::filesystem::remove(c.profile/"native-package.gbmod"))throw std::runtime_error("Only an installed package can be removed.");
  c.package=std::move(fallback);c.mod_error.clear();return 1;
 }catch(const std::exception& e){c.mod_error=e.what();return 0;}
}
const char* error(void* p){return config(p).mod_error.c_str();}
}
int desktop_verify_boot_for_rom(void* p,const char* boot,const char* rom,RecompLauncherCBiosVerify* out) {
 if(!p||!out)return 0;
 *out={};std::string detail;
 try {
  auto& context=*static_cast<DesktopBootContext*>(p);
  auto path=rom&&*rom?std::filesystem::path(rom):std::filesystem::path(context.config.rom);
  if(path.empty())throw std::runtime_error("Choose the cartridge before its boot ROM.");
  auto image=gbcllr::read_file(path);
  if(image.size()<=0x143)throw std::runtime_error("Choose a valid cartridge image.");
  auto model=desktop_boot_model(context,gbcllr::sha256(image),image[0x143],
   boot&&*boot?gbcllr::sha256(gbcllr::read_file(boot)):std::string{});
  if(boot&&*boot) {
   out->ok=1;detail="Boot ROM verified for the cartridge and selected enhancements.";
   context.config.color=model==gbcllr::Model::cgb;
  } else detail=model==gbcllr::Model::cgb?"Choose your Game Boy Color boot ROM.":"Choose your DMG boot ROM.";
 }catch(const std::exception& e){detail=e.what();}
 std::snprintf(out->detail,sizeof out->detail,"%s",detail.c_str());return 1;
}
RecompLauncherCModProvider desktop_mod_provider(DesktopConfig& c) {
 RecompLauncherCModProvider p{};p.ctx=&c;p.package_count=count;p.package_get=package_get;p.feature_count=feature_count;p.feature_get=feature_get;
 p.feature_option_get=option_get;p.feature_choice_get=choice_get;p.feature_enable=enable;p.feature_set_option=option_set;
 p.commit=commit;p.commit_netplay=commit_netplay;p.install_archive=install;p.remove_package=remove;p.last_error=error;
 p.archive_extension=".gbmod";p.archive_description="Native enhancement package";return p;
}
