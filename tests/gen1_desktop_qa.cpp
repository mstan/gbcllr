// Bounded actual-cartridge provider check, without window or speaker output.
#include "../src/desktop_mods.hpp"
#include <algorithm>
#include <cstring>
#include <iostream>
#include <stdexcept>
namespace gbcllr {void execute_commands(Machine&,const std::filesystem::path&,bool*);}
static void check(bool v,const char* m){if(!v)throw std::runtime_error(m);}
int main(int argc,char** argv){try{
 using namespace gbcllr;check(argc==6,"usage: gen1-desktop-qa ROM BOOT ORIGINAL_BATTERY NEW_PROFILE COMMANDS");
 std::filesystem::path rom=argv[1],boot=argv[2],profile=argv[4],save;
 check(!std::filesystem::exists(profile),"QA requires a new private profile");DesktopConfig c;Options options;
 check(desktop_prepare(c,rom,boot,save,profile,false,false,false,options),"desktop preparation failed");
 check(c.package&&c.adaptive&&c.view_width==-1&&!c.colorization,"fresh packaged Gen1 Fit default failed");
 check(!c.fullscreen&&c.volume==100&&c.audio,"fresh interactive defaults differ");
 auto model=read_file(boot).size()>256?Model::cgb:Model::dmg;
 check(options.model==model&&options.mods.size()==1,"desktop omitted selected Gen1 hardware/module");
 auto p=desktop_mod_provider(c);RecompLauncherCModFeature f{};RecompLauncherCModOption o{};
 check(p.feature_get(p.ctx,0,&f)&&f.option_count==1,"Gen1 provider feature missing");
 check(p.feature_option_get(p.ctx,c.package->id.c_str(),f.id,0,&o)&&!std::strcmp(o.value,"fit"),"provider Fit default differs");
 options.boot_rom=read_file(boot);Machine game(read_file(rom),options);std::string error;
 check(game.load_battery(read_file(argv[3]),error),error.c_str());bool rewound=false;execute_commands(game,argv[5],&rewound);
 const uint16_t map=c.package->target=="8cbaa499397e4f1a679c992ea9382a2dd7942ab398b48c19829c2d9529de47bf"?0xd35d:0xd35e;
 check(game.debug_read(map,1)[0]==0&&game.debug_read(map+3,2)==std::vector<uint8_t>({6,5}),"cold Continue did not reach Pallet");
 game.mod_viewport(1280,720);auto view=game.mod_view();check(view.width==256&&view.expanded&&view.spawn_reads>0,"actual Fit field/caller failed");
 auto framebuffer=game.pixels();for(unsigned y=0;y<144;++y)check(std::equal(framebuffer.begin()+y*160,framebuffer.begin()+(y+1)*160,view.pixels.begin()+y*256+48),"adaptive view changed LLE center/HUD");
 // Exercise the real scrolling caller, including partial16px steps. A
 // steady field must not flicker into the menu/transition fallback.
 for(unsigned frame=0;frame<68;++frame){game.run_frame(frame<48?1:0);game.take_audio();view=game.mod_view();
  if(!view.expanded)throw std::runtime_error("field fallback during actual right walk at frame "+std::to_string(frame));
  framebuffer=game.pixels();for(unsigned y=0;y<144;++y)check(std::equal(framebuffer.begin()+y*160,framebuffer.begin()+(y+1)*160,view.pixels.begin()+y*256+48),"scrolling changed LLE center");
 }
 check(game.debug_read(map+3,2)==std::vector<uint8_t>({6,8}),"actual input did not scroll three steps");
 const char* id=c.package->id.c_str();
 check(p.feature_set_option(p.ctx,id,f.id,"aspect","336")&&p.feature_enable(p.ctx,id,f.id,0),"provider choices rejected");
 check(p.commit(p.ctx,rom.string().c_str()),"provider choices not saved");
 check(p.install_archive(p.ctx,c.bundled_package.string().c_str()),"Gen1 package install failed");
 DesktopConfig reloaded;Options next;save.clear();
 check(desktop_prepare(reloaded,rom,boot,save,profile,false,false,false,next),"saved preparation failed");
 check(!reloaded.adaptive&&reloaded.view_width==336&&next.mods.empty(),"package update overwrote saved choices");
 check(!std::filesystem::exists(save),"QA wrote a native battery");
 std::cout<<"Packaged Gen1 Fit, actual cold Continue/field/caller/LLE center, install and saved choices passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
