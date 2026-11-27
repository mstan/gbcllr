// Bounded actual-cartridge provider check, without window or speaker output.
#include "../src/desktop_mods.hpp"
#include <algorithm>
#include <cstring>
#include <iostream>
#include <stdexcept>
namespace gbcllr {void execute_commands(Machine&,const std::filesystem::path&,bool*);}
static void check(bool v,const char* m){if(!v)throw std::runtime_error(m);}
int main(int argc,char** argv){try{
 using namespace gbcllr;check(argc==6,"usage: gen2-desktop-qa ROM BOOT ORIGINAL_BATTERY NEW_PROFILE COMMANDS");
 std::filesystem::path rom=argv[1],boot=argv[2],profile=argv[4],save;
 check(!std::filesystem::exists(profile),"QA requires a new private profile");DesktopConfig c;Options options;
 check(desktop_prepare(c,rom,boot,save,profile,false,false,false,options),"desktop preparation failed");
 check(c.package&&c.adaptive&&c.view_width==-1&&!c.colorization,"fresh packaged Gen2 Fit default failed");
 check(!c.fullscreen&&c.volume==100&&c.audio,"fresh interactive defaults differ");
 check(options.model==Model::cgb&&options.mods.size()==1,"desktop omitted Gen2 CGB module");
 auto p=desktop_mod_provider(c);RecompLauncherCModFeature f{};RecompLauncherCModOption o{};
 check(p.feature_get(p.ctx,0,&f)&&f.option_count==1,"Gen2 provider feature missing");
 RecompLauncherCModFeature gift{};RecompLauncherCModChoice choice{};
 check(p.feature_count(p.ctx)==3+c.sgb_supported&&p.feature_get(p.ctx,1,&gift)&&!gift.enabled,"gift was not elective");
 check(p.feature_option_get(p.ctx,c.package->id.c_str(),gift.id,0,&o)&&o.choice_count==74,"original gift catalogue missing");
 for(int i=0;i<74;++i)check(p.feature_choice_get(p.ctx,c.package->id.c_str(),gift.id,"gift",i,&choice),"gift choice missing");
 check(!p.feature_set_option(p.ctx,c.package->id.c_str(),gift.id,"gift","0")&&!p.feature_set_option(p.ctx,c.package->id.c_str(),gift.id,"gift","75"),"invalid gift accepted");
 check(p.feature_option_get(p.ctx,c.package->id.c_str(),f.id,0,&o)&&!std::strcmp(o.value,"fit"),"provider Fit default differs");
 options.boot_rom=read_file(boot);Machine game(read_file(rom),options);std::string error;
 check(game.load_battery(read_file(argv[3]),error),error.c_str());bool rewound=false;execute_commands(game,argv[5],&rewound);
 const uint16_t map=c.package->target=="fdcc3c8c43813cf8731fc037d2a6d191bac75439c34b24ba1c27526e6acdc8a2"?0xdcb5:0xda00;
 check(game.debug_read(map,4)==std::vector<uint8_t>({26,3,4,29}),"cold native Continue did not reach Cherrygrove");
 game.mod_viewport(1280,720);auto view=game.mod_view();check(view.width==256&&view.expanded&&view.spawn_reads>0,"actual Fit field/caller failed");
 auto framebuffer=game.pixels();for(unsigned y=0;y<144;++y)check(std::equal(framebuffer.begin()+y*160,framebuffer.begin()+(y+1)*160,view.pixels.begin()+y*256+48),"adaptive view changed LLE center/HUD");
 // Exercise the real scrolling caller, including partial16px steps. A
 // steady field must not flicker into the menu/transition fallback.
 for(unsigned frame=0;frame<68;++frame){game.run_frame(frame<48?2:0);game.take_audio();view=game.mod_view();
  if(!view.expanded)throw std::runtime_error("field fallback during actual left walk at frame "+std::to_string(frame));
  framebuffer=game.pixels();for(unsigned y=0;y<144;++y)check(std::equal(framebuffer.begin()+y*160,framebuffer.begin()+(y+1)*160,view.pixels.begin()+y*256+48),"scrolling changed LLE center");
 }
 check(game.debug_read(map,4)==std::vector<uint8_t>({26,3,4,26}),"actual input did not scroll three steps");
 // Current load preserves local Fit geometry in both axes while restoring
 // the exact pending/completed native field and actor publication state.
 auto current=game.snapshot();
 for(auto size:{std::array<unsigned,2>{720,1280},std::array<unsigned,2>{1008,432}}) {
  game.mod_viewport(size[0],size[1]);view=game.mod_view();
  auto width=view.width,height=view.height;std::vector<uint32_t> expected(view.pixels.begin(),view.pixels.end());
  check(game.restore(current,error),error.c_str());view=game.mod_view();
  check(view.width==width&&view.height==height&&std::equal(view.pixels.begin(),view.pixels.end(),expected.begin(),expected.end()),"current load changed Fit geometry or field pixels");
  check(game.snapshot()==current,"local Fit geometry entered serialized native state");
 }
 const char* id=c.package->id.c_str();
 check(p.feature_set_option(p.ctx,id,f.id,"aspect","336")&&p.feature_enable(p.ctx,id,f.id,0),"provider choices rejected");
 check(p.commit(p.ctx,rom.string().c_str()),"provider choices not saved");
 check(p.install_archive(p.ctx,c.bundled_package.string().c_str()),"Gen2 package install failed");
 DesktopConfig reloaded;Options next;save.clear();
 check(desktop_prepare(reloaded,rom,boot,save,profile,false,false,false,next),"saved preparation failed");
 check(!reloaded.adaptive&&reloaded.view_width==336&&next.mods.empty(),"package update overwrote saved choices");
 check(!std::filesystem::exists(save),"QA wrote a native battery");
 auto q=desktop_mod_provider(reloaded);auto selected=reloaded.package->id;
 check(q.feature_set_option(q.ctx,selected.c_str(),gift.id,"gift","38")&&q.feature_enable(q.ctx,selected.c_str(),gift.id,1),"gift arming failed");
 check(!q.commit_netplay(q.ctx,rom.string().c_str()),"gift-active connected selection admitted");
 check(q.commit(q.ctx,rom.string().c_str()),"gift selection persistence failed");
 check(q.install_archive(q.ctx,reloaded.bundled_package.string().c_str()),"armed Gen2 package replacement failed");
 auto installed_version=reloaded.package->version;
 check(q.remove_package(q.ctx,selected.c_str(),installed_version.c_str())&&!std::filesystem::exists(profile/"native-package.gbmod"),"native package removal failed");
 check(q.feature_get(q.ctx,1,&gift)&&gift.enabled,"armed gift lost its unqueue control after replacement/removal");
 check(q.feature_enable(q.ctx,selected.c_str(),gift.id,0)&&q.commit_netplay(q.ctx,rom.string().c_str()),"removed package left a stale gift netplay refusal");
 check(q.feature_enable(q.ctx,selected.c_str(),gift.id,1)&&q.commit(q.ctx,rom.string().c_str()),"gift rearming after removal failed");
 DesktopConfig armed;Options armed_options;save.clear();
 check(desktop_prepare(armed,rom,boot,save,profile,false,false,false,armed_options),"armed gift preparation failed");
 check(armed_options.mods.size()==1&&armed_options.mods[0].gift==38&&armed_options.mods[0].view_width==160&&!armed.adaptive&&!armed.gift_enabled,"gift was coupled to view or not consumed once");
 DesktopConfig cleared;Options clear_options;save.clear();
 check(desktop_prepare(cleared,rom,boot,save,profile,false,false,false,clear_options)&&clear_options.mods.empty()&&cleared.gift_choice==38,"gift silently requeued on later launch");
 std::cout<<"Packaged Gen2 Fit, actual cold Continue/field/caller/LLE center, install and saved choices passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
