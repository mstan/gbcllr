#include "../src/desktop_mods.hpp"
#include "../src/megaman_titles.hpp"
#include "../src/native_mods.hpp"
#include <cstring>
#include <iostream>
#include <stdexcept>

static void check(bool v,const char* message){if(!v)throw std::runtime_error(message);}
template<class F>static void rejects(F f,const char* message){bool failed=false;try{f();}catch(const std::exception&){failed=true;}check(failed,message);}
int main(int argc,char** argv){try{
 using namespace gbcllr;
 check(argc==2,"native package directory required");
 for(const auto& title:megaman_titles){
  auto root=std::filesystem::path(argv[1]);
  DesktopConfig c;
  c.package=read_mod_package(root/(std::string(title.key)+"-adaptive.gbmod"),title.sha256);
  c.dx_package=read_mod_package(root/(std::string(title.key)+"-dx.gbmod"),title.sha256);
  const auto& f=c.package->feature;
  check(c.package->default_enabled&&f.view_width==-1&&f.weapon_cycle&&f.smooth_gameplay&&f.no_flicker,"requested fresh enhancement defaults missing");
  check(!c.dx_package->default_enabled&&c.dx_package->feature.colorization&&!c.dx_package->author.empty()&&!c.dx_package->source.empty(),"optional DX or author attribution missing");
  std::array features{f};NativeMods adapter(title.sha256,features,false);
  check(adapter.active()&&adapter.completes_frames()&&adapter.uses_instruction_service(),"title was not bound to its native services");
  check(!adapter.ends_cpu_work(0x0711),"another cartridge's wait cleared the CPU budget");
  auto state=adapter.snapshot();adapter.restore(state);check(adapter.snapshot()==state,"native adapter checkpoint did not round-trip");
  auto changed=features;changed[0].no_flicker=false;NativeMods other_profile(title.sha256,changed,false);
  rejects([&]{other_profile.restore(state);},"checkpoint accepted another enhancement profile");
  rejects([&]{NativeMods wrong(std::string(64,'0'),features,false);},"wrong cartridge accepted native features");
  auto mismatched=features;mismatched[0].plugin=megaman_titles[(&title-&megaman_titles[0]+1)%5].adaptive;
  rejects([&]{NativeMods wrong(title.sha256,mismatched,false);},"another title's native package accepted");
  c.adaptive=c.weapon_cycle=c.smooth_gameplay=c.no_flicker=true;
  auto provider=desktop_mod_provider(c);
  check(provider.package_count(provider.ctx)==3&&provider.feature_count(provider.ctx)==6,"solo title mod catalog count incorrect");
  bool seen_weapon=false,seen_smooth=false,seen_flicker=false,seen_dx=false;
  for(int i=0;i<provider.feature_count(provider.ctx);++i){
   RecompLauncherCModFeature feature{};check(provider.feature_get(provider.ctx,i,&feature),"catalog feature missing");
   check(std::strcmp(feature.id,"coop")!=0,"out-of-scope co-op appeared");
   if(!std::strcmp(feature.id,"weapon-cycle"))seen_weapon=feature.enabled;
   if(!std::strcmp(feature.id,"smooth-gameplay"))seen_smooth=feature.enabled;
   if(!std::strcmp(feature.id,"no-flicker"))seen_flicker=feature.enabled;
   if(!std::strcmp(feature.id,"dx-color"))seen_dx=!feature.enabled&&std::strlen(feature.author)>0;
  }
  check(seen_weapon&&seen_smooth&&seen_flicker&&seen_dx,"independent title settings or DX credit missing");
  check(provider.feature_enable(provider.ctx,c.package->id.c_str(),"no-flicker",0)&&!c.no_flicker&&c.smooth_gameplay,"flicker control changed CPU preference");
  check(provider.feature_enable(provider.ctx,c.dx_package->id.c_str(),"dx-color",1)&&c.colorization,"DX feature did not enable independently");
  check(desktop_title(std::string(title.sha256))==title.name,"clean title name missing");
 }
 std::cout<<"Five-title native identity, defaults, independent mods and DX attribution pass\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
