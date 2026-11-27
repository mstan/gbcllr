// Actual hidden SDL desktop + in-game Mods navigation. No owner profile or save.
#include "../src/desktop_mods.hpp"
#include "../src/megaman_titles.hpp"
#ifndef SDL_MAIN_HANDLED
#define SDL_MAIN_HANDLED
#endif
#include <SDL.h>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
using namespace gbcllr;
namespace gbcllr {void execute_commands(Machine&,const std::filesystem::path&,bool*);}
static void check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
static void key(SDL_Scancode sc,int count=1){while(count--){SDL_Event e{};e.type=SDL_KEYDOWN;e.key.keysym.scancode=sc;SDL_PushEvent(&e);e.type=SDL_KEYUP;SDL_PushEvent(&e);}}
static void capture(const std::filesystem::path& path){auto window=SDL_GetWindowFromID(1);check(window,"QA window missing");auto r=SDL_GetRenderer(window);int w=0,h=0;check(!SDL_GetRendererOutputSize(r,&w,&h),"output size failed");std::vector<uint32_t> pixels(size_t(w)*h);check(!SDL_RenderReadPixels(r,nullptr,SDL_PIXELFORMAT_ARGB8888,pixels.data(),w*4),"output capture failed");std::ofstream out(path,std::ios::binary);out<<"P6\n"<<w<<' '<<h<<"\n255\n";for(auto p:pixels){char rgb[]={char(p>>16),char(p>>8),char(p)};out.write(rgb,3);}}
static void frame(const std::filesystem::path& path,const ModView& v){std::ofstream out(path,std::ios::binary);out<<"P6\n"<<v.width<<' '<<v.height<<"\n255\n";for(auto p:v.pixels){char rgb[]={char(p>>16),char(p>>8),char(p)};out.write(rgb,3);}}
static int support_shortcuts(const std::filesystem::path& profile){
 DesktopConfig c;c.profile=profile;check(!std::filesystem::exists(profile),"shortcut check requires a new disposable profile");std::filesystem::create_directories(profile);
 c.scale=2;c.audio=false;c.volume=0;c.source=1;check(c.diagnostics,"fresh desktop diagnostics disabled");
 SDL_setenv("GBCLLR_DIAGNOSTICS_DIR",(profile/"diagnostics").string().c_str(),1);
 SDL_setenv("LNG_TEST_HIDDEN","1",1);SDL_setenv("LNG_SCRIPT","[]",1);SDL_setenv("SDL_AUDIODRIVER","dummy",1);
 std::vector<uint8_t> rom(32768);rom[0x100]=0xc3;rom[0x101]=0x50;rom[0x102]=1;rom[0x150]=0x18;rom[0x151]=0xfe;Options options;options.native=false;Machine game(rom,options);
 const uint8_t buttons_row=0x10;game.debug_write(0xff00,{&buttons_row,1});
 struct Shortcuts {
  DesktopConfig& config;unsigned shown=0;std::array<uint8_t,SDL_NUM_SCANCODES> keys{};
  static const uint8_t* keyboard(void* p,const uint8_t*){return static_cast<Shortcuts*>(p)->keys.data();}
  void event(SDL_Scancode key,bool down,bool repeat=false,SDL_Keymod modifiers=KMOD_NONE){SDL_Event e{};e.type=down?SDL_KEYDOWN:SDL_KEYUP;e.key.keysym.scancode=key;e.key.keysym.mod=modifiers;e.key.repeat=repeat;check(SDL_PushEvent(&e)==1,"shortcut event injection failed");}
  static bool present(void* p,uint32_t,const Machine& machine,std::span<const int16_t>){auto& s=*static_cast<Shortcuts*>(p);++s.shown;auto window=SDL_GetWindowFromID(1);check(window,"shortcut window missing");
   auto start_released=[&]{return bool(machine.debug_read(0xff00,1)[0]&8);};
   if(s.shown==1){s.keys[SDL_SCANCODE_RETURN]=s.keys[SDL_SCANCODE_LALT]=1;s.event(SDL_SCANCODE_RETURN,true,false,KMOD_ALT);s.event(SDL_SCANCODE_RETURN,true,true,KMOD_ALT);}
   if(s.shown==2){check(s.config.fullscreen==1&&(SDL_GetWindowFlags(window)&SDL_WINDOW_FULLSCREEN_DESKTOP)&&start_released(),"Alt+Enter did not toggle once or leaked Start");s.event(SDL_SCANCODE_RETURN,true,true,KMOD_ALT);}
   if(s.shown==3){check(s.config.fullscreen==1&&start_released(),"Alt+Enter repeat toggled or leaked Start");s.keys[SDL_SCANCODE_LALT]=0;s.event(SDL_SCANCODE_LALT,false);}
   if(s.shown==4){check(s.config.fullscreen==1&&start_released(),"held Return leaked Start after releasing Alt");s.keys[SDL_SCANCODE_RETURN]=0;s.event(SDL_SCANCODE_RETURN,false);key(SDL_SCANCODE_F10);}
   if(s.shown==5){check(start_released(),"Alt+Enter release leaked Start");s.event(SDL_SCANCODE_RETURN,true,false,KMOD_ALT);s.event(SDL_SCANCODE_RETURN,true,true,KMOD_ALT);s.event(SDL_SCANCODE_RETURN,false);}
   if(s.shown==6){check(!s.config.fullscreen&&!(SDL_GetWindowFlags(window)&SDL_WINDOW_FULLSCREEN_DESKTOP),"second Alt+Enter did not return to a normal window");s.keys[SDL_SCANCODE_RETURN]=1;s.event(SDL_SCANCODE_RETURN,true);}
   if(s.shown==7){check(!start_released(),"ordinary Return no longer reaches Start");s.keys[SDL_SCANCODE_RETURN]=0;s.event(SDL_SCANCODE_RETURN,false);}
   if(s.shown==8)check(start_released(),"ordinary Return release remained pressed");
   return s.shown<8;
  }
 } state{c};
 DesktopFrameQa qa{&state,nullptr,Shortcuts::present,Shortcuts::keyboard};check(!desktop(game,{},true,c,qa)&&state.shown==8,"shortcut desktop loop failed");
 unsigned reports=0,replays=0;for(auto& entry:std::filesystem::directory_iterator(profile/"diagnostics")){reports+=entry.path().extension()==".zip";replays+=entry.path().extension()==".replay";}
 check(reports==1&&replays>=1,"F10 failed to export default-on desktop history");check(!std::filesystem::exists(profile/"native.sav"),"shortcut check wrote native progress");
 std::cout<<"PASS actual SDL Alt+Enter down/repeat/up, held Return suppression, ordinary Start and F10 report\n";return 0;
}
static int oracle_seams(char** argv) {
 DesktopConfig c;c.profile=argv[2];check(!std::filesystem::exists(c.profile),"seam check requires a new disposable profile");
 std::filesystem::create_directories(c.profile);c.rom=argv[3];c.boot=argv[4];c.package_override=argv[5];c.package=read_mod_package(argv[5],sha256(read_file(argv[3])));
 c.scale=2;c.volume=0;c.audio=true;c.source=0;c.shortcuts=false;c.adaptive=true;c.view_width=-1;
 Options o;o.model=Model::cgb;o.boot_rom=read_file(argv[4]);o.mods.push_back(c.package->feature);Machine game(read_file(argv[3]),o);
 bool ignored=false;execute_commands(game,argv[6],&ignored);game.take_audio();game.mod_viewport(512,144);
 struct Seam {
  std::ofstream log;unsigned room_at,start,previous,crossings=0;uint64_t begin=0,pcm=0,nonzero=0;uint8_t buttons=1;bool returned=false;
  static uint8_t input(void* context,uint32_t tick,const Machine& game) {
   auto& s=*static_cast<Seam*>(context);s.begin=SDL_GetPerformanceCounter();auto ram=game.debug_read(0xc000,8192);
   unsigned room=ram[s.room_at],lead=ram[s.room_at-4]==0xd1?0x1100:0x1000;
   if(room!=s.start&&ram[lead+13]>=16)s.buttons=2;
   s.returned=s.crossings>=2&&room==s.start;
   check(tick<240,"bounded seam route did not return");return s.buttons;
  }
  static bool present(void* context,uint32_t tick,const Machine& game,std::span<const int16_t> samples) {
   auto& s=*static_cast<Seam*>(context);auto elapsed=double(SDL_GetPerformanceCounter()-s.begin)*1000/SDL_GetPerformanceFrequency();
   auto ram=game.debug_read(0xc000,8192);unsigned room=ram[s.room_at];if(room!=s.previous){++s.crossings;s.previous=room;}
   s.pcm+=samples.size();for(auto sample:samples)s.nonzero+=sample!=0;
   s.log<<"{\"frame\":"<<tick<<",\"work_ms\":"<<elapsed<<",\"room\":"<<room<<",\"scroll\":"<<unsigned(ram[0xd00])<<",\"phase\":"<<unsigned(ram[0xd04])<<",\"x\":"<<unsigned(ram[0x100d])<<",\"y\":"<<unsigned(ram[0x100b])<<",\"buttons\":"<<unsigned(s.buttons)<<",\"crossings\":"<<s.crossings<<"}\n";
   return !s.returned;
  }
 };
 auto initial=game.debug_read(0xc000,8192);unsigned room_at=game.rom_identity()=="0b56b78a9e45452e98c33edd111234931f1e034dc097f6f23082eb8db6055474"?0xc30:0xc4c;
 Seam s{std::ofstream(c.profile/"frames.jsonl"),room_at,initial[room_at],initial[room_at]};
 DesktopFrameQa qa{&s,Seam::input,Seam::present};
 if(std::string(argv[1])=="--oracle-seams-cpu") {
  // Attribution control for an observed desktop stall: retain simulation,
  // preparation, snapshot hashing, PCM and rendering, omitting SDL submission.
  for(unsigned tick=0;;++tick) {auto buttons=Seam::input(&s,tick,game);game.run_frame(buttons);auto state=game.snapshot();auto pcm=game.take_audio();game.mod_view();if(!Seam::present(&s,tick+1,game,pcm))break;}
 }else check(!desktop(game,{},false,c,qa),"seam desktop failed");
 check(s.crossings==2&&s.pcm&&s.nonzero,"seam desktop/audio route failed");
 check(!std::filesystem::exists(c.profile/"native.sav"),"seam check exported native progress");
 std::cout<<"PASS bounded cold preparation and two real crossings; snapshot, PCM, rendering and frame costs retained\n";return 0;
}
static int sgb_house_transition(Machine& game,DesktopConfig& c){
 // The accepted native Continue ends immediately south of the actual home
 // door at Pallet(5,6). Only ordinary Up enters it; no position/result pokes.
 auto ram=[&](uint16_t address){return game.debug_read(address,1)[0];};
 check(ram(0xd35d)==0&&ram(0xd360)==6&&ram(0xd361)==5,"house approach prerequisite differs");
 game.mod_viewport(1280,720);PresentationTransform transform;std::vector<uint32_t> last_live;
 std::ofstream trace(c.profile/"house.jsonl");unsigned frozen=0,first=0,last=0;bool fade=false;auto initial_commands=game.sgb_view().commands;
 for(unsigned n=0;n<=150;++n){if(n){game.run_frame(n<=16?4:0);game.take_audio();}
  auto raw=game.mod_view();auto sgb=game.sgb_view();auto v=transform.cartridge_sgb_colors(raw,sgb);
  trace<<"{\"frame\":"<<n<<",\"map\":"<<unsigned(ram(0xd35d))<<",\"mask\":"<<unsigned(sgb.mask_mode)<<",\"width\":"<<v.width<<",\"height\":"<<v.height<<",\"expanded\":"<<v.expanded<<",\"uniform\":"<<sgb.uniform_attributes<<",\"packets\":"<<sgb.packets<<",\"mask_packets\":"<<sgb.commands[0x17]<<"}\n";
  if(!n)frame(c.profile/"house-before.ppm",v);
  check(v.width==256&&v.height==144,"actual SGB house transition exposed native160 pillarboxes");
  if(!raw.expanded&&!fade){frame(c.profile/"house-fade.ppm",v);fade=true;}
  if(sgb.mask_mode==1&&sgb.colors_ready&&sgb.uniform_attributes){
   check(v.width==256&&v.height==144&&v.expanded,"actual SGB MASK1 exposed native160 pillarboxes");
   check(last_live.size()==v.pixels.size(),"MASK1 has no qualified previous field");
   for(unsigned y=0;y<144;++y){
    check(std::equal(sgb.colors.begin()+y*160,sgb.colors.begin()+(y+1)*160,v.pixels.begin()+y*256+48),"actual MASK1 changed frozen original center");
    check(std::equal(v.pixels.begin()+y*256,v.pixels.begin()+y*256+48,last_live.begin()+y*256)&&std::equal(v.pixels.begin()+y*256+208,v.pixels.begin()+(y+1)*256,last_live.begin()+y*256+208),"actual MASK1 changed the matching frozen margins");
   }
   if(!frozen){first=n;frame(c.profile/"house-freeze-first.ppm",v);auto state=game.snapshot();write_file_atomic(c.profile/"house-freeze.state",state);
    game.mod_viewport(720,1280);auto tall=transform.cartridge_sgb_colors(game.mod_view(),game.sgb_view());check(tall.width==160&&tall.height==284&&tall.expanded&&game.snapshot()==state,"frozen portrait discarded world or changed canonical state");frame(c.profile/"house-freeze-portrait.ppm",tall);game.mod_viewport(1280,720);
    uint8_t input=n<16?4:0;game.run_frame(input);auto audio=game.take_audio();auto next=game.snapshot();auto next_view=transform.cartridge_sgb_colors(game.mod_view(),game.sgb_view());auto pixels=std::vector<uint32_t>(next_view.pixels.begin(),next_view.pixels.end());
    std::string error;check(game.restore(state,error),error.c_str());game.run_frame(input);check(game.take_audio()==audio&&game.snapshot()==next,"actual frozen current-load state/PCM diverged");auto replay=transform.cartridge_sgb_colors(game.mod_view(),game.sgb_view());check(std::equal(pixels.begin(),pixels.end(),replay.pixels.begin()),"actual frozen current-load pixels diverged");check(game.restore(state,error),error.c_str());
   }
   ++frozen;last=n;frame(c.profile/"house-freeze-last.ppm",transform.cartridge_sgb_colors(game.mod_view(),game.sgb_view()));
  }else if(!sgb.mask_mode&&v.expanded)last_live.assign(v.pixels.begin(),v.pixels.end());
  if(n==150)frame(c.profile/"house-after.ppm",v);
 }
 auto commands=game.sgb_view().commands;
 check(commands[0x0a]==initial_commands[0x0a]+1&&commands[0x04]==initial_commands[0x04]+1&&commands[0x17]==initial_commands[0x17]&&ram(0xd35d)==0x25&&game.sgb_view().mask_mode==0,"actual house palette/attribute packet outcome differs");
 check(game.mod_view().expanded,"actual completed house did not restore expanded field");
 std::cout<<"PASS actual native SGB house Up transition, all151 frames wide256, PAL_SET+1/ATTR_BLK+1/MASK_EN+0, MASK1 frames="<<frozen<<" ["<<first<<','<<last<<"]. MASK1 frozen-world/portrait/current-load coverage is separate synthetic fixture evidence.\n";return 0;
}
struct Test {DesktopConfig* c;unsigned shown=0;uint32_t paused_tick=0;std::vector<uint8_t> paused_state;bool sgb=false;};
static bool present_sgb(Test& t,uint32_t tick,const Machine& game){auto& c=*t.c;++t.shown;
 auto open_sgb=[&]{key(SDL_SCANCODE_RETURN);key(SDL_SCANCODE_DOWN);key(SDL_SCANCODE_RETURN);};
 auto close_menu=[&]{key(SDL_SCANCODE_ESCAPE,3);};
 if(t.shown==4){auto s=game.sgb_view();check(s.colors_ready&&s.border_ready&&s.uniform_attributes&&c.adaptive&&c.view_width==-1,"genuine SGB field prerequisite missing");capture(c.profile/"sgb-fit.ppm");t.paused_tick=tick;t.paused_state=game.snapshot();key(SDL_SCANCODE_ESCAPE);}
 if(t.shown==5){key(SDL_SCANCODE_DOWN,3);open_sgb();key(SDL_SCANCODE_LEFT);}
 if(t.shown==6){check(!c.sgb_colors&&!c.sgb_border&&game.snapshot()==t.paused_state,"live SGB colors changed simulation");capture(c.profile/"sgb-colors-off-menu.ppm");close_menu();}
 if(t.shown==7){capture(c.profile/"sgb-colors-off.ppm");t.paused_state=game.snapshot();key(SDL_SCANCODE_ESCAPE);}
 if(t.shown==8){open_sgb();key(SDL_SCANCODE_RIGHT);key(SDL_SCANCODE_DOWN);key(SDL_SCANCODE_RIGHT);}
 if(t.shown==9){check(c.sgb_colors&&c.sgb_border&&game.snapshot()==t.paused_state,"classic border changed profile/state");capture(c.profile/"sgb-classic-menu.ppm");close_menu();}
 if(t.shown==10){capture(c.profile/"sgb-classic.ppm");t.paused_state=game.snapshot();key(SDL_SCANCODE_ESCAPE);}
 if(t.shown==11){open_sgb();key(SDL_SCANCODE_DOWN);key(SDL_SCANCODE_LEFT);key(SDL_SCANCODE_DOWN);key(SDL_SCANCODE_LEFT);}
 if(t.shown==12){check(!c.sgb_border&&!c.sgb_requested&&game.sgb_view().active&&game.snapshot()==t.paused_state,"next-start setting changed current SGB session");capture(c.profile/"sgb-next-native-menu.ppm");close_menu();}
 if(t.shown==13){capture(c.profile/"sgb-fit-restored.ppm");check(c.adaptive&&c.view_width==-1,"classic border lost stored Fit choice");}
 return t.shown<13;
}
static bool present(void* context,uint32_t tick,const Machine& game,std::span<const int16_t>){auto& t=*static_cast<Test*>(context);if(t.sgb)return present_sgb(t,tick,game);auto& c=*t.c;++t.shown;
 if(t.shown==4){capture(c.profile/"native.ppm");t.paused_tick=tick;t.paused_state=game.snapshot();key(SDL_SCANCODE_ESCAPE);}
 if(t.shown==5){check(tick==t.paused_tick&&game.snapshot()==t.paused_state,"menu changed simulation while paused");key(SDL_SCANCODE_DOWN,3);key(SDL_SCANCODE_RETURN,2);key(SDL_SCANCODE_DOWN);key(SDL_SCANCODE_RIGHT,4);}
 if(t.shown==6){check(c.presentation.palette==4,"actual Mods palette navigation failed");check(game.snapshot()==t.paused_state,"host palette entered Machine snapshot");capture(c.profile/"mods-bw.ppm");key(SDL_SCANCODE_ESCAPE,3);}
 if(t.shown==7){capture(c.profile/"bw.ppm");t.paused_state=game.snapshot();key(SDL_SCANCODE_ESCAPE);}
 if(t.shown==8){key(SDL_SCANCODE_RETURN,2);key(SDL_SCANCODE_DOWN);key(SDL_SCANCODE_RIGHT,14);key(SDL_SCANCODE_DOWN);key(SDL_SCANCODE_RIGHT,2);key(SDL_SCANCODE_DOWN);key(SDL_SCANCODE_RIGHT);key(SDL_SCANCODE_DOWN);key(SDL_SCANCODE_RIGHT,3);}
 if(t.shown==9){check(c.presentation.palette==18&&c.presentation.correction==2&&c.linear&&c.presentation.effect==3,"actual Mods options did not apply live");check(game.snapshot()==t.paused_state,"host options entered Machine snapshot");capture(c.profile/"mods-sgb-lcd.ppm");key(SDL_SCANCODE_ESCAPE,3);}
 if(t.shown==10){check(tick>t.paused_tick,"resume did not advance machine");capture(c.profile/"sgb-lcd.ppm");key(SDL_SCANCODE_ESCAPE);}
 if(t.shown==11){key(SDL_SCANCODE_RETURN,2);key(SDL_SCANCODE_LEFT);}
 if(t.shown==12){check(!c.presentation.enabled,"actual Mods disable failed");key(SDL_SCANCODE_DOWN);key(SDL_SCANCODE_RIGHT);}
 if(t.shown==13){check(c.presentation.palette==18,"disabled palette row remained editable");capture(c.profile/"mods-disabled.ppm");key(SDL_SCANCODE_ESCAPE,3);}
 if(t.shown==14)capture(c.profile/"disabled.ppm");
 return t.shown<14;
}
#include "xtreme2_coop_input.inc"
static int megaman_resize(char** argv) {
 const bool input_only=std::string(argv[1])=="--megaman-input";
 const bool dx_resize=std::string(argv[1])=="--megaman-resize-dx";
 const bool dx_only=input_only&&argv[6]&&std::string(argv[6])=="dx-only";
 const bool stock_clock=input_only&&argv[6]&&std::string(argv[6])=="stock-clock";
 DesktopConfig c;c.profile=argv[2];check(!std::filesystem::exists(c.profile),"resize QA requires a new disposable profile");
 if(dx_resize){std::filesystem::create_directories(c.profile);std::ofstream(c.profile/"desktop.cfg")<<"gbcllr-desktop 1\ncolorization 1\n";}
 c.package_override=argv[5];std::filesystem::path rom=argv[3],boot,save;Options options;options.model=Model::dmg;
 check(desktop_prepare(c,rom,boot,save,c.profile,false,false,true,options),"native title desktop preparation failed");
 check(c.adaptive&&c.view_width==-1&&c.weapon_cycle&&c.smooth_gameplay&&c.no_flicker&&c.colorization==dx_resize,"title desktop defaults or saved DX choice differ");
 if(dx_resize)check(c.dx_package&&options.mods.size()==2&&options.mods.back().colorization,"saved DX choice did not select its real native package");
 auto image=read_file(rom);auto identity=sha256(image);auto title=megaman_title_for_rom(identity);check(title,"resize QA requires an exact Mega Man cartridge");
 if(stock_clock)options.mods.clear();
 if(dx_only){options.mods.clear();NativeFeature color;
  if(c.dx_package)color=c.dx_package->feature;
  else{color.plugin=std::string(title->dx);color.view_width=160;color.colorization=true;}
  options.mods.push_back(color);}
 Machine game(image,options);bool ignored=false;execute_commands(game,argv[4],&ignored);game.take_audio();
 if(input_only){
  auto initial=game.snapshot();std::ostringstream report;report<<"[\n";unsigned count=0,errors=0;
  uint16_t address=title->key=="mm2"?0xff8e:title->key=="mm4"?0xdea1:title->key=="mm5"?0xde91:0xff8a;
  uint16_t begin_poll=title->key=="mm1"?0x157e:title->key=="mm2"?0x0371:title->key=="mm3"?0x14a5:title->key=="mm4"?0x1760:0x1879;
  uint16_t completed_poll=title->key=="mm1"?0x15b6:title->key=="mm2"?0x0399:title->key=="mm3"?0x14e3:title->key=="mm4"?0x17a1:0x18be;
  for(unsigned buttons:{0u,1u,2u,4u,8u,16u,32u,64u,128u,0x11u,0x24u,0xc0u}){
   std::string why;check(game.restore(initial,why),why.c_str());game.set_buttons(uint8_t(buttons));game.debug_step_instruction();
   unsigned limit=1000000;while(game.stats().pc!=begin_poll&&limit--)game.debug_step_instruction();
   check(game.stats().pc==begin_poll,"fresh original joypad poll was not reached");game.debug_step_instruction();
   limit=1000000;while(game.stats().pc!=completed_poll&&limit--)game.debug_step_instruction();
   check(game.stats().pc==completed_poll,"original joypad poll completion was not reached");
   unsigned expected=((buttons&15)<<4)|(buttons>>4),actual=game.debug_read(address,1)[0];errors+=actual!=expected;
   if(count++)report<<",\n";report<<" {\"physical_buttons\":"<<buttons<<",\"native_latch\":"<<actual<<",\"expected\":"<<expected<<"}";
  }
  report<<"\n]\n";auto text=report.str();write_file_atomic(c.profile/"joypad.json",{reinterpret_cast<const uint8_t*>(text.data()),text.size()});
  check(!errors,"original joypad poll produced phantom, missing or swapped buttons");
  check(sha256(read_file(rom))==identity,"input polling changed the original cartridge");
  check(!std::filesystem::exists(c.profile/"native.sav"),"input QA exported native progress");
  std::cout<<"PASS twelve actual original joypad button/chord latches, "<<(dx_only?"DX-only":stock_clock?"stock clock":"fresh enhanced defaults")<<", no phantom inputs\n";return 0;
 }
 check(game.mod_view().width==256&&game.mod_view().sprites,"resize setup did not enter a live expanded field");
 struct Resize {DesktopConfig* config;Machine* game;std::string title;unsigned shown=0;};Resize test{&c,&game,std::string(title->name)};
 auto present=[](void* context,uint32_t,const Machine& machine,std::span<const int16_t>){
  auto& t=*static_cast<Resize*>(context);++t.shown;auto window=SDL_GetWindowFromID(1);check(window,"native title window missing");
  check((SDL_GetWindowFlags(window)&SDL_WINDOW_HIDDEN)!=0,"automated native title window was visible");
  check(SDL_GetWindowTitle(window)==t.title,"native title window caption differs");
  check(&machine==t.game,"resize observer received a different machine");auto view=t.game->mod_view();
  if(t.shown==1){capture(t.config->profile/"initial.ppm");SDL_SetWindowSize(window,1280,720);}
  if(t.shown==3){check(view.width==256&&view.expanded&&view.sprites,"actual 16:9 Fit field differs");capture(t.config->profile/"fit-16-9.ppm");SDL_SetWindowSize(window,960,720);}
  if(t.shown==5){check(view.width==192&&view.expanded&&view.sprites,"actual 4:3 Fit field differs");capture(t.config->profile/"fit-4-3.ppm");SDL_SetWindowSize(window,800,720);}
  if(t.shown==7){check(view.width==160&&view.sprites,"actual native-aspect Fit field differs");capture(t.config->profile/"fit-native.ppm");SDL_SetWindowSize(window,1280,720);}
  if(t.shown==9){check(view.width==256&&view.expanded&&view.sprites,"actual widened Fit return differs");capture(t.config->profile/"fit-return.ppm");}
  return t.shown<9;
 };
 SDL_setenv("LNG_TEST_HIDDEN","1",1);SDL_setenv("LNG_SCRIPT","wait:1",1);SDL_setenv("SDL_AUDIODRIVER","dummy",1);
 DesktopFrameQa qa{&test,nullptr,present};check(!desktop(game,{},true,c,qa)&&test.shown==9,"bounded native resize loop failed");
 check(sha256(read_file(rom))==identity&&game.rom_identity()==identity,"desktop resize changed the original cartridge");
 DesktopConfig loaded;loaded.profile=c.profile;loaded.load();
 check(loaded.adaptive&&loaded.view_width==-1&&loaded.weapon_cycle&&loaded.smooth_gameplay&&loaded.no_flicker&&loaded.colorization==dx_resize,"actual resize changed saved Fit or mod choices");
 check(!std::filesystem::exists(c.profile/"native.sav"),"resize QA exported native progress");
 std::cout<<"PASS actual hidden native title window, fresh defaults, 16:9/4:3/native/16:9 Fit, original cartridge and saved choices\n";return 0;
}
static int sml2_picture_fixture(int argc,char** argv){
 DesktopConfig c;c.profile=argv[5];check(!std::filesystem::exists(c.profile),"picture check requires a new private profile");std::filesystem::create_directories(c.profile);
 c.scale=2;c.audio=true;c.volume=0;c.presentation.enabled=false;c.rom=argv[2];c.boot=argv[3];
 SDL_setenv("LNG_TEST_HIDDEN","1",1);SDL_setenv("LNG_SCRIPT","[]",1);SDL_setenv("SDL_AUDIODRIVER","dummy",1);
 Options options;options.native=false;options.model=Model::dmg;options.boot_rom=read_file(argv[3]);auto rom=read_file(argv[2]);
 check(sha256(rom)=="42045a1642f77b568534ab85b9db93c8a33d2b95c38ae84c987f497e8d52f97f","picture fixture requires the qualified cartridge");
 if(argc==7){c.package_override=argv[6];c.package=read_mod_package(argv[6],sha256(rom));c.colorization=true;options.mods.push_back(c.package->feature);}
 Machine game(rom,options);std::string error;auto restored=game.restore(read_file(argv[4]),error);check(restored,error.c_str());
 struct Picture {
  std::filesystem::path root;std::ofstream log;std::vector<uint8_t> pcm;unsigned shown=0;bool enhanced=false;Machine* game=nullptr;
  static uint8_t input(void*,uint32_t tick,const Machine&){unsigned end=0;for(auto [count,buttons]:std::array<std::pair<unsigned,uint8_t>,6>{{{60,1},{40,17},{60,1},{30,17},{60,1},{30,17}}}){end+=count;if(tick<end)return buttons;}return 1;}
  static bool present(void* context,uint32_t tick,const Machine& m,std::span<const int16_t> samples){auto& p=*static_cast<Picture*>(context);check(tick==++p.shown,"desktop picture route skipped a frame");
   auto window=SDL_GetWindowFromID(1);check(window,"picture window missing");auto renderer=SDL_GetRenderer(window);int w=0,h=0;check(!SDL_GetRendererOutputSize(renderer,&w,&h),"picture output size failed");
   std::vector<uint32_t> pixels(size_t(w)*h);check(!SDL_RenderReadPixels(renderer,nullptr,SDL_PIXELFORMAT_ARGB8888,pixels.data(),w*4),"picture readback failed");
   auto bytes=std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(pixels.data()),pixels.size()*4);auto raw=m.pixels();auto raw_bytes=std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(raw.data()),raw.size()*4);
   check(p.game==&m,"picture observer changed machines");auto view=p.game->mod_view();auto view_bytes=std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(view.pixels.data()),view.pixels.size()*4);if(p.enhanced)check(view.width==256&&(tick>280||view.expanded),"enhanced desktop lost the qualified Fit field");
   bool raw_lit=false,shown_lit=false;for(auto pixel:raw)raw_lit|=bool(pixel&0xffffff);for(auto pixel:pixels)shown_lit|=bool(pixel&0xffffff);check(!raw_lit||shown_lit,"desktop readback lost the presented picture");
   p.log<<tick<<' '<<sha256(bytes)<<' '<<sha256(raw_bytes)<<' '<<sha256(view_bytes)<<' '<<m.stats().cycles<<' '<<m.stats().pc;if(p.enhanced)p.log<<' '<<view.width<<' '<<view.expanded;p.log<<'\n';
   for(auto sample:samples){auto value=uint16_t(sample);p.pcm.push_back(uint8_t(value));p.pcm.push_back(uint8_t(value>>8));}
   if(tick==60||tick==100||tick==160||tick==190||tick==250||tick==280||tick==880){auto before=m.snapshot();capture(p.root/("frame-"+std::to_string(tick)+".ppm"));check(m.snapshot()==before,"desktop picture access changed canonical state");}
   return tick<880;
  }
 } probe{c.profile,std::ofstream(c.profile/"frames.txt")};
 probe.enhanced=argc==7;
 probe.game=&game;
 DesktopFrameQa qa{&probe,Picture::input,Picture::present};check(!desktop(game,{},true,c,qa)&&probe.shown==880,"bounded desktop picture route failed");
 std::ofstream audio(c.profile/"audio.raw",std::ios::binary);audio.write(reinterpret_cast<const char*>(probe.pcm.data()),probe.pcm.size());
 check(!std::filesystem::exists(c.profile/"native.sav"),"picture check wrote native progress");std::cout<<game.picture_costs()<<"\nPASS actual hidden SDL picture fixture, current picture purity and PCM\n";return 0;
}
int main(int argc,char** argv){try{
 if((argc==6||argc==7)&&std::string(argv[1])=="--sml2-picture-fixture")return sml2_picture_fixture(argc,argv);
 if((argc==6||argc==7)&&std::string(argv[1])=="--megaman-input")return megaman_resize(argv);
 if(argc==6&&std::string(argv[1])=="--megaman-resize")return megaman_resize(argv);
 if(argc==6&&std::string(argv[1])=="--megaman-resize-dx")return megaman_resize(argv);
 if(argc==3&&std::string(argv[1])=="--support-shortcuts")return support_shortcuts(argv[2]);
 if(argc==7&&std::string(argv[1])=="--xtreme2-coop-input")return xtreme2_coop_input(argv);
 if(argc==7&&std::string(argv[1])=="--xtreme2-coop-shoulders")return xtreme2_coop_input(argv,true);
 if(argc==7&&(std::string(argv[1])=="--oracle-seams"||std::string(argv[1])=="--oracle-seams-cpu"))return oracle_seams(argv);
 check(argc==2||argc==7||(argc==9&&(std::string(argv[7])=="--sgb"||std::string(argv[7])=="--sgb-house")),"usage: presentation-desktop-qa NEW_PROFILE [ROM BOOT BATTERY COMMANDS PACKAGE [--sgb|--sgb-house NATIVE_CGB_BOOT]]");bool real=argc>=7,sgb=argc==9,house=sgb&&std::string(argv[7])=="--sgb-house";DesktopConfig c;c.profile=argv[1];check(!std::filesystem::exists(c.profile),"QA requires new private profile");std::filesystem::create_directories(c.profile);c.scale=3;c.audio=false;c.volume=0;
 if(real){c.package=read_mod_package(argv[6],sha256(read_file(argv[2])));c.rom=argv[2];c.boot=argv[3];c.package_override=argv[6];c.sgb_supported=read_file(argv[2])[0x146]==3;if(sgb){c.sgb_requested=true;c.sgb_current=true;c.sgb_colors=true;c.sgb_boot=argv[3];c.boot=argv[8];}}
 auto provider=desktop_mod_provider(c);check(provider.package_count(provider.ctx)==((c.package?2:1)+c.sgb_supported)&&provider.feature_count(provider.ctx)>=((c.package?2:1)+c.sgb_supported),"host mod missing without game package");RecompLauncherCModFeature f{};check(provider.feature_get(provider.ctx,provider.feature_count(provider.ctx)-1-c.sgb_supported,&f)&&!std::strcmp(f.id,desktop_presentation_feature)&&f.option_count==4,"host mod options missing");
 RecompLauncherCModOption o{};RecompLauncherCModChoice ch{};for(int i=0;i<4;++i){check(provider.feature_option_get(provider.ctx,desktop_presentation_package,desktop_presentation_feature,i,&o),"launcher option missing");for(int j=0;j<o.choice_count;++j)check(provider.feature_choice_get(provider.ctx,desktop_presentation_package,desktop_presentation_feature,o.id,j,&ch),"launcher choice missing");}
 check(!provider.feature_set_option(provider.ctx,desktop_presentation_package,desktop_presentation_feature,"palette","50")&&!provider.feature_set_option(provider.ctx,desktop_presentation_package,desktop_presentation_feature,"palette","2oops"),"provider accepted malformed setting");
 SDL_setenv("LNG_TEST_HIDDEN","1",1);SDL_setenv("LNG_SCRIPT","[]",1);SDL_setenv("SDL_AUDIODRIVER","dummy",1);
 std::vector<uint8_t> rom(32768);Options options;options.native=false;
 if(real){rom=read_file(argv[2]);options.model=sgb?Model::dmg:Model::cgb;options.sgb_service=sgb;options.boot_rom=read_file(argv[3]);options.mods.push_back(c.package->feature);c.color=!sgb;
  if(sgb){c.persist();Options prepared;prepared.native=false;prepared.model=Model::cgb;std::filesystem::path selected_rom=argv[2],selected_boot,selected_save;
   check(desktop_prepare(c,selected_rom,selected_boot,selected_save,c.profile,false,false,true,prepared),"next-start SGB profile preparation failed");
   check(prepared.sgb_service&&prepared.model==Model::dmg&&selected_boot==std::filesystem::path(argv[3])&&c.boot==argv[8],"SGB start discarded native CGB boot choice");
   if(house){c.sgb_requested=false;c.persist();prepared.mods.clear();check(desktop_prepare(c,selected_rom,selected_boot,selected_save,c.profile,false,false,true,prepared)&&!prepared.sgb_service&&prepared.model==Model::cgb&&selected_boot==std::filesystem::path(argv[8]),"same-object SGB/native loop lost explicit CGB model");
    c.sgb_requested=true;c.persist();prepared.mods.clear();check(desktop_prepare(c,selected_rom,selected_boot,selected_save,c.profile,false,false,true,prepared)&&prepared.sgb_service&&prepared.model==Model::dmg&&selected_boot==std::filesystem::path(argv[3]),"same-object native/SGB loop lost DMG boot");}
  }
 }
 else {rom[0x100]=0xc3;rom[0x101]=0x50;rom[0x102]=1;rom[0x150]=0x18;rom[0x151]=0xfe;}
 Machine game(rom,options);
 if(real){std::string error;check(game.load_battery(read_file(argv[4]),error),error.c_str());bool rewound=false;execute_commands(game,argv[5],&rewound);game.take_audio();}
 else {uint8_t off=0,on=0x91,bgp=0xe4;game.debug_write(0xff40,{&off,1});std::array<uint8_t,16> tile{};for(unsigned y=0;y<8;++y){tile[y*2]=0x55;tile[y*2+1]=0x33;}game.debug_write(0x8000,tile);game.debug_write(0xff47,{&bgp,1});game.debug_write(0xff40,{&on,1});}
 if(house){auto battery=read_file(argv[4]);auto result=sgb_house_transition(game,c);check(read_file(argv[4])==battery&&!std::filesystem::exists(c.profile/"native.sav"),"house QA changed native save");check(sha256(read_file(argv[2]))==game.rom_identity(),"house QA changed ROM");return result;}
 if(sgb)check(game.mod_view().width==256,"actual SGB pre-window Fit field missing");auto initial=game.rom_identity();auto original_battery=real?read_file(argv[4]):std::vector<uint8_t>{};Test t{&c};t.sgb=sgb;DesktopFrameQa qa{&t,nullptr,present};check(!desktop(game,{},true,c,qa)&&t.shown==(sgb?13u:14u),"bounded desktop loop failed");check(game.rom_identity()==initial,"presentation changed cartridge");if(real){check(sha256(read_file(argv[2]))==initial,"presentation changed original ROM file");check(read_file(argv[4])==original_battery,"presentation changed supplied native save");}
 if(sgb)check(game.mod_view().width==256,"actual post-border Fit field missing");
 DesktopConfig loaded;loaded.profile=c.profile;loaded.load();if(sgb){check(!loaded.sgb_requested&&loaded.sgb_colors&&!loaded.sgb_border&&loaded.boot==argv[8]&&loaded.sgb_boot==argv[3],"saved SGB controls/boot choices differ");
  loaded.package_override=argv[6];Options next;next.native=false;std::filesystem::path selected_rom=argv[2],selected_boot,selected_save;
  check(desktop_prepare(loaded,selected_rom,selected_boot,selected_save,c.profile,false,false,false,next)&&!next.sgb_service&&next.model==Model::cgb&&selected_boot==std::filesystem::path(argv[8]),"next native session did not restore original CGB choice");
 }else check(!loaded.presentation.enabled&&loaded.presentation.palette==18&&loaded.presentation.correction==2&&loaded.presentation.effect==3&&loaded.linear,"per-ROM reload lost live choices");
 check(!std::filesystem::exists(c.profile/"native.sav"),"host test wrote native progress");
 std::cout<<(sgb?"PASS actual SGB cartridge colors/Fit/classic border/live controls,immutable current profile,next-start DMG/nativeCGB boot preservation,no native save\n":"PASS actual in-game Mods live palette/correction/filter/LCD/disable,paused state unchanged,per-ROM persistence,no native save\n");return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
