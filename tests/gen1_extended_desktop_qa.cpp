// Actual private package/provider, native-save isolation and cold Continue.
#include "../src/desktop_mods.hpp"
#include <cstring>
#include <iostream>
#include <stdexcept>
namespace gbcllr {void execute_commands(Machine&,const std::filesystem::path&,bool*);}
static void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
int main(int argc,char** argv){try{
 using namespace gbcllr;check(argc==7,"usage: gen1-extended-desktop-qa ROM BOOT BATTERY PACKAGE NEW_PROFILE COMMANDS");
 std::filesystem::path rom=argv[1],boot=argv[2],save,profile=argv[5];auto input=read_file(argv[3]);
 check(!std::filesystem::exists(profile),"QA requires a new disposable profile");DesktopConfig c;Options options;
 check(desktop_prepare(c,rom,boot,save,profile,false,false,false,options),"fresh desktop preparation failed");
 check(c.adaptive&&c.view_width==-1&&!c.extended_content&&!c.expanded_encounters&&!c.fullscreen&&c.audio&&c.volume==100,"fresh private package defaults differ");
 check(c.package&&c.package->feature.plugin=="gbcllr.gen1.extended"&&c.package_override.empty(),"fresh build did not discover its bundled native package");
 check(options.mods.size()==1&&options.mods[0].plugin=="gbcllr.gen1.adaptive-view"&&options.mods[0].asset_input.empty(),"disabled content did not select ordinary native view");
 auto p=desktop_mod_provider(c);RecompLauncherCModFeature view{},content{},encounters{};
 check(p.feature_count(p.ctx)==4+c.sgb_supported&&p.feature_get(p.ctx,0,&view)&&p.feature_get(p.ctx,1,&content)&&p.feature_get(p.ctx,2,&encounters),"Extended provider controls missing");
 check(view.enabled&&!content.enabled&&!encounters.enabled,"optional content was enabled by default");
 write_file_atomic(save,input);const auto stock_save=save;auto id=c.package->id;
 check(p.feature_set_option(p.ctx,id.c_str(),view.id,"aspect","336")&&p.feature_enable(p.ctx,id.c_str(),view.id,0),"saved view selection failed");
 check(p.feature_enable(p.ctx,id.c_str(),encounters.id,1)&&c.extended_content&&c.expanded_encounters,"encounter selection did not enable required content");
 check(!p.commit_netplay(p.ctx,rom.string().c_str()),"unqualified Extended linked selection admitted");
 check(p.commit(p.ctx,rom.string().c_str())&&p.install_archive(p.ctx,argv[4]),"private package replacement failed");
 auto& selected=c;selected.package_override=profile/"native-package.gbmod";Options enhanced;
 check(desktop_prepare(selected,rom,boot,save,profile,false,false,false,enhanced),"saved Extended preparation failed");
 check(!selected.adaptive&&selected.view_width==336&&selected.extended_content&&selected.expanded_encounters,"replacement lost explicit content/view choices");
 check(enhanced.mods.size()==1&&enhanced.mods[0].plugin=="gbcllr.gen1.extended"&&enhanced.mods[0].view_width==160&&enhanced.mods[0].expanded_encounters,"content was coupled to widescreen");
 check(save.filename()=="extended-native.sav"&&read_file(save)==input&&read_file(stock_save)==input,"default Extended save did not preserve/copy original progress");
 auto preserved=input;preserved.back()^=1;write_file_atomic(save,preserved);const auto extended_save=save;
 auto& again=selected;Options next;
 check(desktop_prepare(again,rom,boot,save,profile,false,false,false,next)&&save==extended_save&&read_file(save)==preserved,"later preparation replaced existing Extended progress");
 next.boot_rom=read_file(boot);Machine game(read_file(rom),next);std::string error;
 check(game.load_battery(read_file(save),error),error.c_str());bool rewound=false;execute_commands(game,argv[6],&rewound);
 check(game.debug_read(0xd163,4)==std::vector<uint8_t>({2,153,31,255}),"selected native content failed actual captured-party Continue");
 game.mod_viewport(1280,720);check(game.mod_view().width==160,"disabled view width changed while content active");
 auto q=desktop_mod_provider(again);check(q.feature_enable(q.ctx,id.c_str(),content.id,0)&&q.commit(q.ctx,rom.string().c_str()),"content disabling failed");
 auto& disabled=again;Options ordinary;
 check(desktop_prepare(disabled,rom,boot,save,profile,false,false,false,ordinary)&&ordinary.mods.empty()&&save==stock_save,"disabled content selected or wrote Extended progress");
 check(disabled.expanded_encounters&&read_file(stock_save)==input&&read_file(extended_save)==preserved&&read_file(argv[3])==input,"saved encounter choice or either native save was damaged");
 check(q.feature_enable(q.ctx,id.c_str(),content.id,1)&&q.commit(q.ctx,rom.string().c_str()),"content reenabling failed");
 std::filesystem::path custom=profile/"explicit-native.sav";DesktopConfig explicit_profile;explicit_profile.package_override=selected.package_override;Options explicit_options;
 check(desktop_prepare(explicit_profile,rom,boot,custom,profile,false,false,false,explicit_options)&&custom.filename()=="explicit-native.sav"&&!std::filesystem::exists(custom)&&explicit_options.mods.size()==1&&explicit_options.mods[0].plugin=="gbcllr.gen1.extended","explicit native-save path was replaced or prewritten");
 auto custom_path=custom;auto explicit_provider=desktop_mod_provider(explicit_profile);Options explicit_stock;
 check(explicit_provider.feature_enable(explicit_provider.ctx,id.c_str(),content.id,0)&&explicit_provider.commit(explicit_provider.ctx,rom.string().c_str())&&desktop_prepare(explicit_profile,rom,boot,custom,profile,false,false,false,explicit_stock)&&custom==custom_path&&explicit_stock.mods.empty()&&!std::filesystem::exists(custom),"launcher return lost original explicit save intent");
 std::cout<<"Private Extended provider choices, replacement, independent view/content/encounters, isolated native saves, linked refusal and actual cold Continue passed\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
