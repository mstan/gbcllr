// Manual owned-cartridge packaged-provider check; creates no windows/audio.
#include "../src/desktop_mods.hpp"
#include <algorithm>
#include <cstring>
#include <iostream>
#include <stdexcept>
namespace gbcllr {void execute_commands(Machine&,const std::filesystem::path&,bool*);}
static void check(bool v,const char* m){if(!v)throw std::runtime_error(m);}
int main(int argc,char** argv){try{
 using namespace gbcllr;check(argc==6,"usage: oracle-desktop-qa ROM BOOT BATTERY NEW_PROFILE COMMANDS");
 std::filesystem::path rom=argv[1],boot=argv[2],profile=argv[4],save;
 check(!std::filesystem::exists(profile),"QA requires a new private profile");DesktopConfig c;Options o;
 check(desktop_prepare(c,rom,boot,save,profile,false,false,false,o),"desktop preparation failed");
 check(c.package&&c.package->feature.plugin=="gbcllr.oracle.adaptive-view"&&c.adaptive&&c.view_width==-1,"packaged Oracle fresh Fit default failed");
 check(!c.fullscreen&&c.volume==100&&c.audio,"interactive defaults differ");
 check(o.model==Model::cgb&&o.mods.size()==1,"desktop omitted CGB Oracle module");
 auto p=desktop_mod_provider(c);RecompLauncherCModFeature f{};RecompLauncherCModOption option{};
 check(p.feature_get(p.ctx,0,&f)&&f.option_count==2,"Oracle provider feature missing");
 check(p.feature_option_get(p.ctx,c.package->id.c_str(),f.id,0,&option)&&!std::strcmp(option.value,"fit"),"provider Fit differs");
 check(p.feature_option_get(p.ctx,c.package->id.c_str(),f.id,1,&option)&&!std::strcmp(option.id,"hide-closed-rooms")&&!std::strcmp(option.value,"0")&&!std::strcmp(option.default_value,"0"),"closed rooms are not visible by default");
 o.boot_rom=read_file(boot);Machine game(read_file(rom),o);std::string error;
 check(game.load_battery(read_file(argv[3]),error),error.c_str());bool rewound=false;execute_commands(game,argv[5],&rewound);
 check(rewound,"actual room profile did not exercise rewind");
 for(unsigned width:{160u,256u,336u,512u,4096u}){
  game.mod_viewport(width,144);auto view=game.mod_view();check(view.width==width&&view.expanded==(width>160),"actual large-room Fit differs");
  auto native=game.pixels();for(unsigned y=0;y<(width==160?144u:16u);++y)check(std::equal(native.begin()+y*160,native.begin()+(y+1)*160,view.pixels.begin()+y*width+(width-160)/2),"Oracle native inventory/HUD changed");
 }
 auto before=game.snapshot();check(game.restore(before,error)&&game.snapshot()==before,"current Oracle state roundtrip differs");
 const char* id=c.package->id.c_str();check(p.feature_set_option(p.ctx,id,f.id,"aspect","336")&&p.feature_set_option(p.ctx,id,f.id,"hide-closed-rooms","1")&&p.feature_enable(p.ctx,id,f.id,0),"saved choices rejected");
 check(p.commit(p.ctx,rom.string().c_str())&&p.install_archive(p.ctx,c.bundled_package.string().c_str()),"Oracle package commit/install failed");
 DesktopConfig reload;Options next;save.clear();check(desktop_prepare(reload,rom,boot,save,profile,false,false,false,next),"saved preparation failed");
 check(!reload.adaptive&&reload.view_width==336&&reload.hide_closed_rooms&&next.mods.empty(),"package update overwrote explicit saved choices");
 check(!std::filesystem::exists(save),"QA wrote a native battery");
 std::cout<<"Packaged Oracle Fit/large-room/LLE center/current-state/rewind/install/saved-choices passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
