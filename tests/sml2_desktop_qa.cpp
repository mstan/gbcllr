// Bounded real-cartridge QA of the actual packaged provider, without a window.
#include "../src/desktop_mods.hpp"
#include <iostream>
#include <cstring>
#include <stdexcept>
namespace gbcllr {void execute_commands(Machine&,const std::filesystem::path&,bool*);}
static void check(bool v,const char* m){if(!v)throw std::runtime_error(m);}
int main(int argc,char** argv){try{
 using namespace gbcllr;
 check(argc==5,"usage: sml2-desktop-qa ROM BOOT NEW_PROFILE COMMANDS");
 std::filesystem::path rom=argv[1],boot=argv[2],profile=argv[3],save;
 check(!std::filesystem::exists(profile),"QA requires a new private profile");
 DesktopConfig c;Options options;
 check(desktop_prepare(c,rom,boot,save,profile,false,false,false,options),"desktop preparation failed");
 check(c.package&&c.adaptive&&c.view_width==-1&&c.colorization,"fresh packaged Fit/color defaults failed");
 check(c.fullscreen==0&&c.volume==100&&c.audio,"fresh interactive defaults differ");
 check(options.model==Model::dmg&&options.mods.size()==1&&options.mods[0].colorization,"desktop did not select stock DMG color module");
 auto p=desktop_mod_provider(c);RecompLauncherCModFeature f{};RecompLauncherCModOption o{};
 check(p.feature_get(p.ctx,0,&f)&&f.option_count==2,"SML2 provider feature/options missing");
 check(p.feature_option_get(p.ctx,c.package->id.c_str(),f.id,0,&o)&&!std::strcmp(o.value,"fit"),"fresh provider aspect differs");
 check(p.feature_option_get(p.ctx,c.package->id.c_str(),f.id,1,&o)&&!std::strcmp(o.value,"1"),"fresh provider color differs");
 options.boot_rom=read_file(boot);Machine game(read_file(rom),options);bool rewound=false;
 execute_commands(game,argv[4],&rewound);
 check(game.debug_read(0xff9b,1)[0]==4,"actual default gameplay did not enter a stage");
 game.mod_viewport(1280,720);auto view=game.mod_view();check(view.width==256&&view.expanded,"actual packaged Fit gameplay failed");
 bool colored=false;for(auto px:view.pixels)colored|=((px>>16)&255)!=((px>>8)&255)||((px>>8)&255)!=(px&255);
 check(colored,"actual packaged gameplay is not colored");
 const char* id=c.package->id.c_str();
 check(p.feature_set_option(p.ctx,id,f.id,"aspect","336")&&p.feature_set_option(p.ctx,id,f.id,"dx-color","0")&&p.feature_enable(p.ctx,id,f.id,0),"provider did not accept explicit choices");
 check(p.commit(p.ctx,rom.string().c_str()),"provider did not persist explicit choices");
 check(p.install_archive(p.ctx,c.bundled_package.string().c_str()),"qualified SML2 package install failed");
 DesktopConfig reloaded;Options next;save.clear();
 check(desktop_prepare(reloaded,rom,boot,save,profile,false,false,false,next),"saved desktop preparation failed");
 check(!reloaded.adaptive&&!reloaded.colorization&&reloaded.view_width==336&&next.mods.empty(),"package update overwrote explicit saved choices");
 check(reloaded.package->feature.asset_input==c.package->feature.asset_input,"installed package lost presentation identity");
 check(!std::filesystem::exists(save),"QA wrote a native battery");
 std::cout<<"Packaged fresh Fit/color, actual gameplay, provider install and saved choices passed; no native battery written\n";
 return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
