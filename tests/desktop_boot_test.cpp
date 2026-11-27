// Shared boot/model contract and bounded actual-ROM callback regression.
#include "../src/desktop_mods.hpp"
#include <cstring>
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>

using namespace gbcllr;
static void check(bool v,const char* message){if(!v)throw std::runtime_error(message);}
template<class F> static void rejects(F f,const char* message){
 bool rejected=false;try{f();}catch(const std::exception&){rejected=true;}check(rejected,message);
}
constexpr const char* dmg="cf053eccb4ccafff9e67339d4e78e98dce7d1ed59be819d2a1ba2232c6fce1c7";
constexpr const char* cgb="b4f2e416a35eef52cba161b159c7c8523a92594facb924b3ede0d722867c50c7";
static DesktopConfig selected(const std::filesystem::path& mods,const char* name,const char* identity){
 DesktopConfig c;c.package=read_mod_package(mods/name,identity);return c;
}
int main(int argc,char** argv){try{
 {
  auto profile=std::filesystem::temp_directory_path()/("gbcllr-profile-contract-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  std::filesystem::create_directories(profile);
  std::ofstream(profile/"desktop.cfg")<<"gbcllr-desktop 1\nvolume 75\n";
  DesktopConfig old;old.profile=profile;old.load();check(old.diagnostics,"old profile did not enable normal diagnostics");
  old.diagnostics=false;old.persist();DesktopConfig opted_out;opted_out.profile=profile;opted_out.load();
  check(!opted_out.diagnostics,"explicit diagnostics opt-out not retained");
  std::filesystem::remove(profile/"desktop.cfg");std::filesystem::remove(profile);
 }
 check(argc==2||argc==8,"usage: desktop-boot-test MODS [RED GOLD Xtreme2 DMG_BOOT CGB_BOOT NEW_PROFILE]");
 auto mods=std::filesystem::path(argv[1]);
 constexpr const char* yellow="8cbaa499397e4f1a679c992ea9382a2dd7942ab398b48c19829c2d9529de47bf";
 auto colored=selected(mods,"yellow-adaptive.gbmod",yellow);DesktopBootContext colored_context{colored,{}};
 check(desktop_boot_model(colored_context,yellow,0x80,cgb)==Model::cgb&&desktop_boot_model(colored_context,yellow,0x80,dmg)==Model::dmg,"Yellow authentic CGB or explicit DMG rejected");
 check(desktop_boot_model(colored_context,yellow,0x80,{})==Model::cgb,"Yellow fresh default is not authentic CGB");
 colored.sgb_supported=true;auto sgp=desktop_mod_provider(colored);RecompLauncherCModOption sgo{};
 check(sgp.feature_option_get(sgp.ctx,desktop_sgb_package,desktop_sgb_feature,0,&sgo)&&!std::strcmp(sgo.default_value,"1")&&colored.sgb_colors,"SGB color catalog default differs from config");
 check(sgp.feature_option_get(sgp.ctx,desktop_sgb_package,desktop_sgb_feature,1,&sgo)&&!std::strcmp(sgo.default_value,"0")&&!colored.sgb_border,"SGB border catalog default differs from config");
 colored.sgb_requested=true;check(desktop_boot_model(colored_context,yellow,0x80,dmg)==Model::dmg&&desktop_boot_model(colored_context,yellow,0x80,{})==Model::dmg,"SGB next start is not explicit DMG");
 rejects([&]{desktop_boot_model(colored_context,yellow,0x80,cgb);},"SGB session accepted incompatible CGB boot");colored.sgb_requested=false;
 check(desktop_boot_model(colored_context,yellow,0x80,cgb)==Model::cgb,"native CGB choice not restored after SGB");
 constexpr const char* red="5ca7ba01642a3b27b0cc0b5349b52792795b62d3ed977e98a09390659af96b7b";
 constexpr const char* gold="fb0016d27b1e5374e1ec9fcad60e6628d8646103b5313ca683417f52b97e7e4e";
 constexpr const char* xtreme="e37a14a8e61bd64f2226dacc26a7ee75dd90abfcf88c5016eb97b76ecda0ddca";
 auto y=selected(mods,"red-adaptive.gbmod",red);DesktopBootContext yc{y,{}};
 check(desktop_boot_model(yc,red,0x80,dmg)==Model::dmg,"Gen1 DMG rejected");
 rejects([&]{desktop_boot_model(yc,red,0x80,cgb);},"Red native CGB falsely accepted");
 check(desktop_boot_model(yc,red,0x80,{})==Model::dmg,"no-boot native Red default incompatible");
 auto yp=desktop_mod_provider(y);
 check(yp.feature_enable(yp.ctx,y.package->id.c_str(),"adaptive-view",0),"disable rejected");
 check(desktop_boot_model(yc,red,0x80,cgb)==Model::cgb&&desktop_boot_model(yc,red,0x80,dmg)==Model::dmg,"stock dual models narrowed");
 check(desktop_boot_model(yc,red,0x80,{})==Model::cgb,"stock header default changed");
 yc.explicit_model=Model::cgb;
 rejects([&]{desktop_boot_model(yc,red,0x80,dmg);},"explicit model/boot mismatch accepted");
 check(desktop_boot_model(yc,red,0x80,cgb)==Model::cgb,"explicit stock CGB rejected");
 auto g=selected(mods,"gold-adaptive.gbmod",gold);DesktopBootContext gc{g,{}};
 rejects([&]{desktop_boot_model(gc,gold,0x80,dmg);},"Gold native DMG falsely accepted");
 check(desktop_boot_model(gc,gold,0x80,cgb)==Model::cgb,"Gen2 CGB rejected");
 g.sgb_requested=true;g.sgb_supported=true;auto sgb_gen2_provider=desktop_mod_provider(g);RecompLauncherCModFeature gf{};
 check(desktop_boot_model(gc,gold,0x80,dmg)==Model::dmg&&sgb_gen2_provider.feature_get(sgb_gen2_provider.ctx,0,&gf)&&std::strstr(gf.status,"Original SGB view"),"Gen2 SGB profile promised unsupported widescreen");g.sgb_requested=false;
 auto gp=desktop_mod_provider(g);
 check(gp.feature_enable(gp.ctx,g.package->id.c_str(),"adaptive-view",0),"Gen2 disable rejected");
 check(desktop_boot_model(gc,gold,0x80,dmg)==Model::dmg,"stock Gold DMG rejected");
 check(gp.feature_enable(gp.ctx,g.package->id.c_str(),"native-gift",1),"gift selection rejected");
 rejects([&]{desktop_boot_model(gc,gold,0x80,dmg);},"gift-only native model ignored");
 auto x=selected(mods,"xtreme2-adaptive.gbmod",xtreme);x.adaptive=false;DesktopBootContext xc{x,{}};
 rejects([&]{desktop_boot_model(xc,xtreme,0xc0,dmg);},"CGB-only stock cartridge accepted DMG");
 check(desktop_boot_model(xc,xtreme,0xc0,cgb)==Model::cgb,"CGB-only CGB rejected");
 check(desktop_boot_model(xc,xtreme,0,cgb)==Model::cgb&&desktop_boot_model(xc,xtreme,0,dmg)==Model::dmg,"stock DMG-cart models narrowed");
 auto xp=desktop_mod_provider(x);RecompLauncherCModFeature weapon_feature{};
 RecompLauncherCModFeature smooth_feature{};
 check(xp.feature_get(xp.ctx,3,&smooth_feature)&&!std::strcmp(smooth_feature.id,"smooth-gameplay")&&!smooth_feature.enabled&&!x.smooth_gameplay,"Smooth gameplay must be elective");
 check(xp.feature_enable(xp.ctx,x.package->id.c_str(),"smooth-gameplay",1)&&x.smooth_gameplay,"Smooth gameplay selection rejected");
 check(!yp.feature_enable(yp.ctx,y.package->id.c_str(),"smooth-gameplay",1),"Unrelated cartridge exposed enhanced CPU timing");
 check(xp.feature_get(xp.ctx,1,&weapon_feature)&&!std::strcmp(weapon_feature.id,"weapon-cycle")&&!weapon_feature.enabled&&!x.weapon_cycle,"Quick weapon switching must default off");
 check(xp.feature_enable(xp.ctx,x.package->id.c_str(),"weapon-cycle",1)&&x.weapon_cycle,"Native weapon feature selection rejected");
 rejects([&]{desktop_boot_model(xc,xtreme,0,dmg);},"Weapon-only native feature lost its model contract");
 check(desktop_boot_model(xc,xtreme,0,cgb)==Model::cgb,"Weapon-only native feature requires widescreen");
 check(!yp.feature_enable(yp.ctx,y.package->id.c_str(),"weapon-cycle",1),"Unrelated cartridge exposed Xtreme2 weapon switching");
 check(xp.feature_enable(xp.ctx,x.package->id.c_str(),"weapon-cycle",0)&&!x.weapon_cycle,"Native weapon feature disable rejected");
 RecompLauncherCModFeature coop_feature{},aspect_feature{};
 check(xp.feature_get(xp.ctx,2,&coop_feature)&&!std::strcmp(coop_feature.id,"coop")&&!coop_feature.enabled,"Co-op must be elective alongside the bundled widescreen package");
 check(xp.feature_enable(xp.ctx,x.package->id.c_str(),"coop",1)&&x.coop_enabled&&!x.adaptive,"Enabling co-op replaced the player's aspect preference");
 check(xp.feature_get(xp.ctx,0,&aspect_feature)&&!std::strcmp(aspect_feature.id,"adaptive-view")&&!aspect_feature.enabled,"Co-op consumed the adaptive-view feature");
 check(xp.feature_set_option(xp.ctx,x.package->id.c_str(),"coop","shared-world","0")&&!x.coop_shared,"Independent camera policy rejected");
 check(xp.feature_set_option(xp.ctx,x.package->id.c_str(),"coop","coop-view","2")&&x.coop_view==2&&!x.coop_shared,"Local view changed the shared gameplay policy");
 check(xp.commit_netplay(xp.ctx,nullptr)&&x.session_mods,"Network commit discarded the co-op module");
 check(!yp.feature_enable(yp.ctx,y.package->id.c_str(),"coop",1),"Unrelated title exposed Xtreme2 co-op");
 {
  // Old eight-entry profiles retain every user binding and seed only the
  // new host actions. Both players' ten-entry edits then round-trip.
  auto path=std::filesystem::current_path()/"desktop-bindings-contract";
  check(!std::filesystem::exists(path),"Disposable binding fixture already exists");std::filesystem::create_directory(path);
  const std::string legacy="keys 1 2 3 4 5 6 7 8\npartner_keys 11 12 13 14 15 16 17 18\n";
  write_file_atomic(path/"desktop.cfg",{reinterpret_cast<const uint8_t*>(legacy.data()),legacy.size()});
  DesktopConfig loaded,defaults;loaded.profile=path;loaded.load();
  for(unsigned i=0;i<8;++i)check(loaded.keys[i]==int(i+1)&&loaded.partner_keys[i]==int(i+11),"Legacy player binding was replaced");
  check(loaded.keys[8]==defaults.keys[8]&&loaded.keys[9]==defaults.keys[9]&&loaded.partner_keys[8]==defaults.partner_keys[8]&&loaded.partner_keys[9]==defaults.partner_keys[9],"Legacy profile did not seed separate virtual controls");
  loaded.keys[8]=30;loaded.keys[9]=31;loaded.partner_keys[8]=32;loaded.partner_keys[9]=33;loaded.package=x.package;loaded.weapon_cycle=true;loaded.smooth_gameplay=true;loaded.coop_enabled=true;loaded.coop_first=1;loaded.coop_view=2;loaded.coop_shared=false;loaded.persist();
  DesktopConfig again;again.profile=path;again.load();
  check(again.keys==loaded.keys&&again.partner_keys==loaded.partner_keys&&again.pads==loaded.pads&&again.partner_pads==loaded.partner_pads&&again.weapon_cycle,"Virtual/player bindings or optional feature did not persist");
  check(again.coop_enabled&&again.coop_enabled_saved&&again.coop_first==1&&again.coop_view==2&&!again.coop_shared,"Separate co-op preferences did not persist");
  check(again.smooth_gameplay&&again.mod_smooth_saved,"Smooth gameplay preference did not persist");
  check(std::filesystem::remove(path/"desktop.cfg")&&std::filesystem::remove(path),"Disposable binding fixture cleanup failed");
 }
 rejects([&]{desktop_boot_model(xc,xtreme,0xc0,"unqualified");},"unknown boot accepted");
 if(argc==8){
  // Actual files exercise the exact contextual callback wired into GameInfo.
  const char* boots[]={argv[5],argv[6]};RecompLauncherCBiosVerify verified{};
  auto verify=[&](DesktopBootContext& context,const char* rom,const char* boot,bool expected){
   check(desktop_verify_boot_for_rom(&context,boot,rom,&verified)==1,"callback failed");
   if(bool(verified.ok)!=expected)throw std::runtime_error(std::string("callback result differs: ")+verified.detail);
  };
  yc.explicit_model.reset();y.adaptive=true;
  verify(yc,argv[2],boots[0],true);check(!y.color,"verified DMG color state stale");
  verify(yc,argv[2],boots[1],false);verify(yc,argv[2],"",false);
  check(yp.feature_enable(yp.ctx,y.package->id.c_str(),"adaptive-view",0),"actual disable rejected");
  verify(yc,argv[2],boots[1],true);check(y.color,"verified CGB color state stale");
  check(yp.feature_enable(yp.ctx,y.package->id.c_str(),"adaptive-view",1),"actual enable rejected");
  verify(yc,argv[2],boots[1],false);
  g.adaptive=true;g.gift_enabled=false;verify(gc,argv[3],boots[0],false);verify(gc,argv[3],boots[1],true);
  g.adaptive=false;verify(gc,argv[3],boots[0],true);
  verify(xc,argv[4],boots[0],false);verify(xc,argv[4],boots[1],true);
  auto profile=std::filesystem::path(argv[7]);check(!std::filesystem::exists(profile),"owned test requires new private profile");
  std::filesystem::path rom=argv[2],boot=boots[1],save;DesktopConfig fresh;Options options;
  fresh.package_override=mods/"red-adaptive.gbmod";
  rejects([&]{desktop_prepare(fresh,rom,boot,save,profile,false,false,false,options);},"final guard accepted wrong native boot");
  check(!std::filesystem::exists(save),"failed boot wrote native save");
  fresh=DesktopConfig{};fresh.package_override=mods/"red-adaptive.gbmod";save.clear();boot.clear();options={};
  check(desktop_prepare(fresh,rom,boot,save,profile,false,false,false,options)&&options.model==Model::dmg&&options.mods.size()==1,"skip-launcher no-boot Gen1 default failed");
  fresh.adaptive=false;fresh.view_width=336;fresh.persist();auto before=read_file(profile/"desktop.cfg");
  DesktopConfig stock;stock.package_override=mods/"red-adaptive.gbmod";options={};boot=boots[1];
  check(desktop_prepare(stock,rom,boot,save,profile,false,false,false,options)&&options.model==Model::cgb&&options.mods.empty()&&stock.view_width==336,"saved stock model/choices failed");
  check(read_file(profile/"desktop.cfg")==before&&!std::filesystem::exists(save),"preparation changed settings/native save");
  // The real main loop reuses both objects; SGB must not overwrite original
  // CLI --model cgb intent with its temporary DMG simulation profile.
  DesktopConfig loop;loop.package_override=mods/"red-adaptive.gbmod";Options reused;reused.model=Model::cgb;boot=boots[1];
  check(desktop_prepare(loop,rom,boot,save,profile,false,false,true,reused)&&reused.model==Model::cgb,"explicit native loop setup failed");
  loop.sgb_requested=true;loop.sgb_boot=boots[0];loop.persist();reused.mods.clear();
  check(desktop_prepare(loop,rom,boot,save,profile,false,false,true,reused)&&reused.model==Model::dmg&&reused.sgb_service&&boot==std::filesystem::path(boots[0])&&loop.boot==boots[1],"same-object SGB loop lost native boot");
  loop.sgb_requested=false;loop.persist();reused.mods.clear();
  check(desktop_prepare(loop,rom,boot,save,profile,false,false,true,reused)&&reused.model==Model::cgb&&!reused.sgb_service&&boot==std::filesystem::path(boots[1]),"same-object SGB/native loop lost original explicit CGB model");
  check(!std::filesystem::exists(save),"profile round trip wrote native save");
 }
 std::cout<<"Boot/model, enabled-feature changes, stock-only and explicit-model contracts passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
