#include <gbcllr/machine.hpp>
#include <algorithm>
#include <filesystem>
#include <iostream>
#include <stdexcept>
using namespace gbcllr;
static void check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
static std::vector<uint32_t> region(ModView v,unsigned x,unsigned y,unsigned width,unsigned height){
 std::vector<uint32_t> result;for(unsigned row=y;row<y+height;++row)result.insert(result.end(),v.pixels.begin()+row*v.width+x,v.pixels.begin()+row*v.width+x+width);return result;
}
static int resource_palette_case(const std::filesystem::path& root,const std::filesystem::path& fixture,const std::filesystem::path& input_path){
 auto rom=read_file(root/"data/game.rom");Options o;o.boot_rom=read_file(root/"data/boot.bin");
 o.mods={read_mod_package(root/"data/feature.gbmod",sha256(rom)).feature,read_mod_package(root/"data/dx.gbmod",sha256(rom)).feature};
 Machine m(rom,o);std::string error;bool restored=m.restore(read_file(fixture),error);check(restored,error.c_str());m.mod_viewport(256,144);
 auto inputs=read_file(input_path);check(inputs.size()>=2&&inputs.size()%2==0,"resource palette inputs must contain button/shoulder pairs");
 unsigned red_frames=0,red_pixels=0;
 for(size_t n=0;n<inputs.size();n+=2){
  m.run_frame({inputs[n],inputs[n+1]},{});m.take_audio();auto point=m.snapshot();auto v=m.mod_view();
  check(m.snapshot()==point,"late resource presentation changed simulation");check(v.width==256,"late resource changed the Fit canvas");
  unsigned red=0;for(unsigned y=0;y<128;++y)for(unsigned x=0;x<v.width;++x)red+=v.pixels[y*v.width+x]==0xffd52900u;
  red_frames+=red!=0;red_pixels+=red;
 }
 check(red_frames>=8&&red_pixels>=32,"late actor lost its authored red/metallic palette after charge cache initialization");
 auto point=m.snapshot();auto run=[&]{for(unsigned n=0;n<4;++n)m.run_frame();auto pcm=m.take_audio();auto v=m.mod_view();return std::pair{pcm,std::vector<uint32_t>(v.pixels.begin(),v.pixels.end())};};
 auto expected=run();auto done=m.snapshot();restored=m.restore(point,error);check(restored,error.c_str());check(run()==expected&&m.snapshot()==done,"late resource current-state PCM/view replay differs");
 check(read_file(root/"data/game.rom")==rom,"late resource qualification changed cartridge bytes");
 std::cout<<"PASS IV late resource colors: "<<red_frames<<" frames / "<<red_pixels<<" authored red pixels; pure render and current-state PCM/view replay\n";return 0;
}
static int effects_case(const std::filesystem::path& root,const std::filesystem::path& fixture,const std::filesystem::path& input_path={}){
 auto rom=read_file(root/"data/game.rom");Options o;o.boot_rom=read_file(root/"data/boot.bin");
 o.mods={read_mod_package(root/"data/feature.gbmod",sha256(rom)).feature,read_mod_package(root/"data/dx.gbmod",sha256(rom)).feature};
 Machine m(rom,o);std::string error;bool restored=m.restore(read_file(fixture),error);check(restored,error.c_str());m.mod_viewport(256,144);
 bool snail=!input_path.empty();auto inputs=snail?read_file(input_path):std::vector<uint8_t>(400,0);check(inputs.size()%2==0,"effect inputs must contain button/shoulder pairs");
 unsigned max_sprites=0,left_frames=0,ghosts=0,observed=0;
 for(size_t n=0;n<inputs.size();n+=2){
  m.run_frame({inputs[n],inputs[n+1]},{});m.take_audio();auto point=m.snapshot();auto v=m.mod_view();check(m.snapshot()==point,"effect rendering changed native simulation");
  if(n<6)continue;check(v.width==256,"effect changed the Fit canvas");max_sprites=std::max(max_sprites,v.sprites);++observed;
  if(snail){check(v.sprites<=64,"fixed-allocation pose emitted following executable bytes as sprite pieces");continue;}
  if(m.debug_read(0xde9e,1)[0]||m.debug_read(0xd702,1)[0]!=3)break;
  auto particles=m.debug_read(0xd32a,192);for(unsigned p=0;p<particles.size();p+=16)left_frames+=particles[p]<64&&particles[p+4]>=224;
  if(v.framing_background.size()==v.pixels.size())for(unsigned y=20;y<120;++y)for(unsigned x=216;x<256;++x){unsigned at=y*256+x;ghosts+=v.pixels[at]==0xffffffffu&&v.pixels[at]!=v.framing_background[at];}
 }
 check(observed>=32,"effect fixture did not exercise its native sequence");
 if(!snail){check(left_frames>=4,"death fixture did not exercise left-offscreen particles");check(max_sprites>=20,"native death effect pieces were omitted");check(!ghosts,"left death particles wrapped into the far-right margin");}
 auto point=m.snapshot();auto run=[&]{for(unsigned n=0;n<3;++n)m.run_frame();auto pcm=m.take_audio();auto v=m.mod_view();return std::pair{pcm,std::vector<uint32_t>(v.pixels.begin(),v.pixels.end())};};
 auto expected=run();auto done=m.snapshot();restored=m.restore(point,error);check(restored,error.c_str());check(run()==expected&&m.snapshot()==done,"effect current-state PCM/view replay differs");
 check(read_file(root/"data/game.rom")==rom,"effect qualification changed original cartridge");
 std::cout<<"PASS "<<(snail?"IV bounded snail pose":"V signed complete death effects")<<": max sprites="<<max_sprites<<" left particle observations="<<left_frames<<" false right pixels="<<ghosts<<"; pure render and current-state PCM/view replay\n";return 0;
}
static int death_case(const std::filesystem::path& root,unsigned title,const std::filesystem::path& fixture){
 auto rom=read_file(root/"data/game.rom");Options o;o.boot_rom=read_file(root/"data/boot.bin");
 o.mods={read_mod_package(root/"data/feature.gbmod",sha256(rom)).feature,read_mod_package(root/"data/dx.gbmod",sha256(rom)).feature};
 Machine m(rom,o);std::string error;check(m.restore(read_file(fixture),error),error.c_str());m.mod_viewport(256,144);
 unsigned health=title==1?0xdfa3:title==2?0xcfd0:title==3?0xde9c:title==4?0xdeae:0xde9e;
 unsigned deaths=0,visible=0,black=0,retry=0;
 for(unsigned n=0;n<640;++n){
  uint8_t keys=title==2?(n<6?1:n==6?33:n<25?1:0):title==4?0:title==5?1:(n%64<16?17:1);
  m.run_frame(keys);m.take_audio();unsigned hp=m.debug_read(health,1)[0];
  bool dead=!hp&&(title==2?m.debug_read(0xcf60,1)[0]!=0:m.debug_read(title==1?0xd802:0xd702,1)[0]==3);
  if(dead)++deaths;
  if(!deaths)continue;
  auto point=m.snapshot();auto view=m.mod_view();check(m.snapshot()==point,"death/respawn rendering changed native simulation");
  auto native=m.pixels();bool varied=std::any_of(native.begin(),native.begin()+160*128,[&](auto p){return p!=native[0];});
  if(varied){
   check(view.width==256,"visible death/respawn changed the Fit canvas");
   unsigned painted=0;for(auto p:view.pixels)painted+=p!=0xff000000u;
   if(painted<1000)std::cerr<<"death title="<<title<<" frame="<<n<<" hp="<<hp<<" painted="<<painted<<'\n';
   check(painted>=1000,"native visible death/respawn picture was replaced by black");if(dead)++visible;
  }else if(std::all_of(native.begin(),native.end(),[](auto p){return p==0xff000000u;}))++black;
  // Hardware OAM can already belong to the next DMA while mod_view owns the
  // last completed picture. Let that queue settle before comparing ownership.
  if(title==4&&dead&&deaths>3){
   auto saved=m.snapshot();uint8_t off=0;m.debug_write(0xff40,{&off,1});auto oam=m.debug_read(0xfe00,160);check(m.restore(saved,error),error.c_str());
   bool actor=false;for(unsigned p=0;p<160;p+=4)actor|=oam[p]&&oam[p]<160;
   if(!actor)check(view.sprites==0,"death wait retained the previous live player pose");
  }
  bool live=hp&&m.debug_read(title==1?0xd802:0xd702,1)[0]==1;
  if(title==2)live=hp&&!m.debug_read(0xcf60,1)[0]&&!m.debug_read(0xcf00,1)[0];
  if(live&&varied&&++retry>=32)break;
 }
 check(deaths>=60&&visible>=60,"fixture did not exercise a complete visible native death wait");
 check(retry>=32,"native death controller did not complete its visible retry");
 auto point=m.snapshot();m.take_audio();auto run=[&]{for(unsigned n=0;n<3;++n)m.run_frame();auto pcm=m.take_audio();auto v=m.mod_view();return std::pair{pcm,std::vector<uint32_t>(v.pixels.begin(),v.pixels.end())};};
 auto expected=run();auto done=m.snapshot();check(m.restore(point,error),error.c_str());check(run()==expected&&m.snapshot()==done,"death/retry current-state PCM/view replay differs");
 check(read_file(root/"data/game.rom")==rom,"death qualification changed the original cartridge");
 std::cout<<"PASS title"<<title<<" native death frames="<<deaths<<" visible="<<visible<<" original black="<<black<<" retry frames="<<retry<<"; pure render and current-state PCM/view replay\n";return 0;
}
int main(int argc,char** argv)try{
 if(argc==5&&std::string_view(argv[1])=="--snail-effects")return effects_case(argv[2],argv[3],argv[4]);
 if(argc==4&&std::string_view(argv[1])=="--death-effects")return effects_case(argv[2],argv[3]);
 if(argc==5&&std::string_view(argv[1])=="--resource-palette")return resource_palette_case(argv[2],argv[3],argv[4]);
 if(argc==5&&std::string_view(argv[1])=="--death"){
  auto root=std::filesystem::path(argv[2]);unsigned title=std::stoul(argv[3]);check(title>=1&&title<=5,"invalid death title");
  return death_case(root,title,argv[4]);
 }
 check(argc>=4&&argc<=7,"usage: megaman-presentation-qa GAME_ROOT TITLE FIELD_STATE [FOLLOWUP_STATE [EFFECT_STATE [TRANSITION_STATE]]]");
 std::filesystem::path root=argv[1];unsigned title=std::stoul(argv[2]);check(title>=1&&title<=5,"invalid title");
 auto rom=read_file(root/"data/game.rom");Options options;options.boot_rom=read_file(root/"data/boot.bin");
 options.mods={read_mod_package(root/"data/feature.gbmod",sha256(rom)).feature,read_mod_package(root/"data/dx.gbmod",sha256(rom)).feature};
 Machine m(rom,options);std::string error;check(m.restore(read_file(argv[3]),error),error.c_str());
 unsigned width=title==1?512:256;m.mod_viewport(width,144);auto field=m.snapshot();
 auto frames=[&](unsigned count,uint8_t keys=0){while(count--){m.run_frame(keys);m.take_audio();}};
 if(title==1){
  auto before=region(m.mod_view(),176,0,16,16);frames(16,1);auto after=m.mod_view();
  check(after.camera_x>0&&after.camera_x<16,"fixture did not reach fractional first-column scroll");
  check(region(after,176,0,16,16)==before,"unloaded collision-ring column replaced stable room art");
  check(m.restore(field,error),error.c_str());m.mod_viewport(512,144);frames(4);unsigned visible_shots=0;
  for(unsigned n=4;n<=480;n+=4){frames(4,n%60<=16?49:33);if(n<240||n%8)continue;
   // OAM is bus-inaccessible during some PPU phases. Observe it with the LCD
   // disabled in a disposable snapshot, then restore the exact native frame.
   auto point=m.snapshot();uint8_t off=0;m.debug_write(0xff40,{&off,1});auto oam=m.debug_read(0xfe00,160);
   check(m.restore(point,error),error.c_str());auto v=m.mod_view();
   for(unsigned p=0;p<160;p+=4){if(oam[p+2]!=0x66||oam[p]<48||oam[p]>=120||oam[p+1]<16||oam[p+1]>152)continue;
    int x=int(oam[p+1])-8+v.camera_x-v.left,y=int(oam[p])-16;if(x<0||x+8>int(v.width))continue;
    auto shot=region(v,unsigned(x),unsigned(y),8,8);unsigned bright=0;
    for(auto pixel:shot)bright+=((pixel>>16)&255)>200;
    check(bright>=12,"native spinning enemy shot is missing from enhanced picture");++visible_shots;
   }
  }
  check(visible_shots>=12,"fixture did not exercise enough native enemy-shot observations");
  std::cout<<"native enemy-shot picture observations="<<visible_shots<<'\n';
 }
 if(title==3||title==4){
  for(unsigned n=0;n<50;++n){frames(1,1);auto v=m.mod_view();check(v.width==width,"field left adaptive view");
   for(unsigned y=128;y<144;++y)for(unsigned x=0;x<width;++x)if(x<(width-160)/2||x>=(width+160)/2)check(v.pixels[y*width+x]==0xff000000u,"HUD moved into a camera-relative margin");
  }
 }
 if(title==5){
  frames(1);auto body=region(m.mod_view(),16,80,64,48);
  check(std::count(body.begin(),body.end(),0xff00e6d5u)>20,"hero lost authored first opaque cyan color (palette word0)");
 }
 if(title==3||title==4){
  // Matched no-fire control: a left-going pellet whose hardware X wraps
  // must not appear in the unrelated right-hand part of the wider room.
  check(m.restore(field,error),error.c_str());m.mod_viewport(512,144);frames(4);frames(2,2);frames(3);
  std::vector<std::vector<uint32_t>> controls;for(unsigned n=0;n<96;++n){frames(1);auto v=m.mod_view();controls.emplace_back(v.pixels.begin(),v.pixels.end());}
  check(m.restore(field,error),error.c_str());m.mod_viewport(512,144);frames(4);frames(2,2);frames(3,32);unsigned wrapped=0;
  for(unsigned n=0;n<96;++n){frames(1);auto oam=m.debug_read(0xc000,160);auto v=m.mod_view();for(unsigned p=0;p<160;p+=4){
   if(oam[p+2]!=(title==3?0x65:0x63)||oam[p+1]<248||oam[p+1]>254||oam[p]<16||oam[p]>=144)continue;
   int x=int(oam[p+1])-8+v.camera_x-v.left,y=int(oam[p])-16;if(x<0||x+8>int(v.width)||y+8>144)continue;++wrapped;
   for(unsigned yy=0;yy<8;++yy)for(unsigned xx=0;xx<8;++xx){auto at=unsigned(y+int(yy))*v.width+unsigned(x+int(xx));check(v.pixels[at]==controls[n][at],"left Buster wrapped into unrelated right-hand pixels");}
  }}check(wrapped>0,"left Buster fixture never exercised wrapped hardware X");
 }
 if(title==4){
  check(m.restore(field,error),error.c_str());m.mod_viewport(256,144);frames(4);unsigned negative=0;int last=m.mod_view().left;
  for(unsigned n=4;n<=424;n+=4){frames(4,n%60<=16?49:33);auto v=m.mod_view();int global=v.left+(v.bound_right==480?320:0);
   check(std::abs(global-last)<=12,"room-link camera jumped between adjoining room coordinates");last=global;
   if(v.bound_right==480&&v.camera_x<0){++negative;check(v.left<=0,"room link interpreted a negative camera as the far right edge");}
  }
  check(negative>0,"room-link fixture never exercised the native negative camera");
 }
 check(m.restore(field,error),error.c_str());m.mod_viewport(width,144);
 auto header_portrait=region(m.mod_view(),(width-160)/2+96,128,16,16);
 auto header_gauge=region(m.mod_view(),(width-160)/2+56,128,40,16);
 frames(2,128);
 for(unsigned n=0;n<120;++n){frames(1);auto before=m.snapshot();auto v=m.mod_view();
  check(v.width==width&&v.height==144&&v.expanded,"pause transition changed adaptive canvas dimensions");
  check(m.snapshot()==before,"menu rendering changed native simulation");
 }
 auto menu=m.mod_view();check(std::any_of(menu.pixels.begin(),menu.pixels.end(),[](uint32_t p){return uint8_t(p>>16)!=uint8_t(p>>8)||uint8_t(p>>8)!=uint8_t(p);}),"settled pause menu is black/monochrome");
 if(title==2||title==5){
  check(region(menu,(width-160)/2+96,0,16,16)==header_portrait,"moving pause HUD changed portrait art or background colors");
  check(region(menu,(width-160)/2+56,0,40,16)==header_gauge,"moving pause HUD changed health-bar colors");
 }
 if(title==1){auto portrait=region(menu,(width-160)/2+96,24,16,8);check(std::count(portrait.begin(),portrait.end(),0xffffffffu)==40,"pause portrait lost its authored white eye/background pixels");}
 auto point=m.snapshot();m.take_audio();auto replay=[&]{for(unsigned i=0;i<3;++i)m.run_frame();auto audio=m.take_audio();auto v=m.mod_view();return std::pair{audio,std::vector<uint32_t>(v.pixels.begin(),v.pixels.end())};};auto expected=replay();auto done=m.snapshot();check(m.restore(point,error),error.c_str());check(replay()==expected&&m.snapshot()==done,"pause state/PCM/view replay differs");
 frames(2,128);frames(120);check(m.mod_view().width==width,"resume lost adaptive canvas");
 if(title==5){
  check(m.restore(field,error),error.c_str());m.mod_viewport(width,144);
  // Route entry is bypassed. Original094C initializes the same first-stage
  // room and produces its own beam, palette fades and input unlock.
  uint8_t bank=1;m.debug_write(0x2100,{&bank,1});m.debug_write(0xdf00,{&bank,1});m.debug_registers({0,0,0,0,0xfffe,0x094c,1});m.debug_wake();
  unsigned visible=0,locked=0;
  for(unsigned n=0;n<360;++n){frames(1);auto oam=m.debug_read(0xc000,40);bool actor=false;for(unsigned i=0;i<10;++i)actor|=oam[i*4]>0&&oam[i*4]<160;
   if(n>30&&actor&&m.debug_read(0xdf01,1)[0]==0x11&&m.debug_read(0xd702,1)[0]==1){++visible;locked+=(m.debug_read(0xdeaf,1)[0]&8)!=0;auto v=m.mod_view();if(v.width!=width)std::cerr<<"entry frame="<<n<<" pc="<<std::hex<<m.stats().pc<<" bank="<<m.stats().rom_bank<<" lcdc="<<unsigned(m.debug_read(0xff40,1)[0])<<" lock="<<unsigned(m.debug_read(0xdeaf,1)[0])<<std::dec<<'\n';check(v.width==width,"visible native spawn changed the Fit canvas");}
  }
  check(visible>0,"native entry fixture produced no visible first-stage body/beam");std::cout<<"native entry visible="<<visible<<" input-locked="<<locked<<'\n';
 }
 check(sha256(read_file(root/"data/game.rom"))==sha256(rom),"cartridge changed");
 if(title==1&&argc>=5){
  check(m.restore(read_file(argv[4]),error),error.c_str());
  check(m.debug_read(0xd8c0,2)==std::vector<uint8_t>({10,8})&&m.debug_read(0xd860,1)[0]==128,"room fixture lost its alternate physical cache page");
  for(unsigned room_width:{256u,512u}){m.mod_viewport(room_width,144);auto before=m.snapshot();auto view=m.mod_view();check(m.snapshot()==before,"cache-origin room rendering changed native state");unsigned edge=(room_width-160)/2;
   unsigned wall=0;for(unsigned y=0;y<128;++y)for(unsigned x=0;x<8;++x){auto p=view.pixels[y*room_width+edge+x];wall+=((p>>16)&255)>180&&((p>>8)&255)>180&&(p&255)>180;}
   check(wall>100,"physical X/Y cache origin hid the room's left wall");
   for(unsigned y=0;y<144;++y)for(unsigned x=0;x<room_width;++x)if(x<edge||x>=edge+160)check(view.pixels[y*room_width+x]==0xff000000u,"narrow-room matte is not black");
  }
  auto point=m.snapshot();m.take_audio();auto expected=replay();auto done=m.snapshot();check(m.restore(point,error),error.c_str());check(replay()==expected&&m.snapshot()==done,"cache-origin room state/PCM/view replay differs");
  std::cout<<"physical X/Y cache-origin walls and256/512 black room mattes pass\n";
 }
 if(title==2&&argc>=5){
  check(m.restore(read_file(argv[4]),error),error.c_str());
  check(m.debug_read(0xcf0b,2)==std::vector<uint8_t>({2,20})&&(m.debug_read(0xcf00,1)[0]&128),"ladder fixture lost its native vertical room-scroll phase");
  for(unsigned width:{256u,512u}){m.mod_viewport(width,144);auto before=m.snapshot();auto view=m.mod_view();check(m.snapshot()==before,"vertical room rendering changed native state");
   check(view.framing_background.size()==view.pixels.size(),"ladder fixture lacks terrain-only picture");int origin=view.camera_x-view.left;
   auto rungs=[&](unsigned x){unsigned n=0;for(unsigned y=24;y<40;++y)for(unsigned dx=0;dx<16;++dx){auto p=view.framing_background[y*width+unsigned(origin)+x+dx];n+=((p>>16)&255)>220&&((p>>8)&255)>80&&((p>>8)&255)<200&&(p&255)<100;}return n;};
   check(rungs(48)>=20&&rungs(32)==0,"climbing ladder selected the adjoining room's wrong tile column");
  }
  auto point=m.snapshot();m.take_audio();auto expected=replay();auto done=m.snapshot();check(m.restore(point,error),error.c_str());check(replay()==expected&&m.snapshot()==done,"vertical ladder room state/PCM/view replay differs");
  std::cout<<"native vertical-room ladder alignment at256/512 and pure state/PCM/view replay pass\n";
 }
 if(title==3&&argc>=5){
  check(m.restore(read_file(argv[4]),error),error.c_str());
  check(m.debug_read(0xd84c,2)==std::vector<uint8_t>({10,8})&&m.debug_read(0xd792,1)[0]>=128,"second-room fixture lost its alternate physical cache page");
  unsigned terrain=0;for(unsigned output_width:{256u,512u}){m.mod_viewport(output_width,144);auto before=m.snapshot();auto view=m.mod_view();check(m.snapshot()==before,"vertical room rendering changed native state");unsigned origin=unsigned(view.camera_x-view.left);
  terrain=0;for(unsigned y=0;y<128;++y)for(unsigned x=origin;x<origin+32;++x)terrain+=view.pixels[y*view.width+x]!=0xff000000u;
  check(terrain>500,"alternate physical cache Y page hid ordinary room terrain");
  if(m.debug_read(0xd792,1)[0]==216){
   // The original physical picture is black here. Wrapping the new room's
   // logical map instead paints a white rung from its left-hand ladder.
   check(m.pixels()[40*160+10]==0xff000000u,"vertical pan fixture lost its native black BG sample");
   check(view.framing_background[40*view.width+unsigned(view.camera_x-view.left)+10]==0xff000000u,"vertical pan duplicated a ladder from the other room");
  }
  }
  auto point=m.snapshot();m.take_audio();auto expected=replay();auto done=m.snapshot();check(m.restore(point,error),error.c_str());check(replay()==expected&&m.snapshot()==done,"second-room state/PCM/view replay differs");
  std::cout<<"alternate cache page terrain pixels="<<terrain<<'\n';
 }
 if(title==5&&argc>=5){
  check(m.restore(read_file(argv[4]),error),error.c_str());m.mod_viewport(256,144);
  auto actor_x=[&]{auto x=m.debug_read(0xc68a,2);return int(x[0])|(int(x[1])<<8);};
  int last=actor_x();check(last>=256&&last<512,"pause-resume bird fixture lost its extended world page");
  frames(2,128);frames(120);frames(2,128);
  for(unsigned n=0;n<60;++n){frames(1);int x=actor_x();check(std::abs(x-last)<=8,"first resumed bird controller collapsed its sixteen-bit world page");last=x;}
  std::cout<<"native bird world page retained through Start pause/resume\n";
 }
 if(title==1&&argc>=6){
  check(m.restore(read_file(argv[5]),error),error.c_str());
  // Original input produces a real enemy destruction. Do not set its death
  // flag or inject a decorative effect into the guest state.
  frames(2,33);frames(4,1);frames(7,33);frames(2,1);frames(7,33);frames(10,1);
  auto v=m.mod_view();auto oam=m.debug_read(0xc000,160);unsigned effects=0,bright=0;
  for(unsigned p=0;p<160;p+=4)if(oam[p+2]>=0x6d&&oam[p+2]<=0x70&&oam[p]>=16&&oam[p]<136&&oam[p+1]>=8&&oam[p+1]<160){
   int x=int(oam[p+1])-8+v.camera_x-v.left,y=int(oam[p])-16;if(x<0||x+8>int(v.width))continue;++effects;
   for(auto pixel:region(v,unsigned(x),unsigned(y),8,8))bright+=((pixel>>16)&255)>210&&((pixel>>8)&255)>210&&(pixel&255)>180;
  }
  std::cout<<"native destruction pieces="<<effects<<" bright pixels="<<bright<<'\n';
  check(effects>=1&&bright>=12,"native enemy destruction lost its visible explosion pieces");
 }
 if(title==1&&argc>=7){
  check(m.restore(read_file(argv[6]),error),error.c_str());m.mod_viewport(256,144);unsigned pans=0;
  for(unsigned n=0;n<96;++n){frames(1);if(!m.debug_read(0xd869,1)[0])continue;auto before=m.snapshot();auto v=m.mod_view();
   check(v.width==256&&m.snapshot()==before,"native room pan changed canvas or simulation");
   unsigned painted=0;for(unsigned y=0;y<128;++y)for(unsigned x=48;x<208;++x)painted+=v.pixels[y*256+x]!=0xff000000u;
   check(painted>2000,"native room-pan picture was replaced by full black");++pans;
  }
  check(pans>=8,"fixture did not exercise original room scrolling");std::cout<<"native nonblank room-pan frames="<<pans<<'\n';
 }
 if(title==2&&argc>=6){
  check(m.restore(read_file(argv[5]),error),error.c_str());m.mod_viewport(256,144);frames(4);unsigned shots=0;
  for(unsigned n=0;n<120;++n){frames(1);auto v=m.mod_view();auto oam=m.debug_read(0xdf00,160);
   if(v.framing_background.size()!=v.pixels.size())continue;
   for(unsigned p=0;p<160;p+=4){if(oam[p+2]!=0xe6||oam[p]<16||oam[p]>=128||oam[p+1]<16||oam[p+1]>152)continue;
    int x=int(oam[p+1])-8+v.camera_x-v.left,y=int(oam[p])-16;if(x<0||x+8>int(v.width))continue;unsigned contrast=0;
    for(unsigned yy=0;yy<8;++yy)for(unsigned xx=0;xx<8;++xx){unsigned at=unsigned(y+int(yy))*v.width+unsigned(x+int(xx));contrast+=v.pixels[at]!=v.framing_background[at];}
    if(contrast<4)std::cerr<<"enemy shot frame="<<n<<" x="<<x<<" y="<<y<<" contrast="<<contrast<<'\n';
    check(contrast>=4,"native enemy shot was drawn on an unrelated extended room page");++shots;
   }
  }
  check(shots>=20,"fixture lacks native enemy-shot observations");std::cout<<"native locked-room enemy-shot observations="<<shots<<'\n';
 }
 if((title==3||title==4)&&argc>=6){
  check(m.restore(read_file(argv[5]),error),error.c_str());m.mod_viewport(256,144);unsigned deaths=0;
  for(unsigned n=0;n<96;++n){frames(1,n<12?17:n<32?1:0);if(m.debug_read(0xd702,1)[0]!=3)continue;
   auto before=m.snapshot();auto v=m.mod_view();unsigned painted=0;for(unsigned y=0;y<128;++y)for(unsigned x=48;x<208;++x)painted+=v.pixels[y*256+x]!=0xff000000u;
   check(painted>1000,"pit death blanked the room before its native fade");check(m.snapshot()==before,"death picture changed simulation");++deaths;
  }
  check(deaths>=20,"fixture did not exercise native pit death");std::cout<<"native visible pit-death frames="<<deaths<<'\n';
  frames(200);check(m.debug_read(0xd702,1)[0]==1&&m.debug_read(title==3?0xde9c:0xdeae,1)[0]>0,"original pit-death controller did not complete its retry");
 }
 if(argc>=6){auto point=m.snapshot();m.take_audio();auto expected=replay();auto done=m.snapshot();
  check(m.restore(point,error),error.c_str());check(replay()==expected&&m.snapshot()==done,"effect/transition current-state PCM/view replay differs");
 }
 std::cout<<"PASS title"<<title<<": targeted presentation,120 pause frames, native Start resume, render purity and state/PCM/view replay; no native save exported\n";
 return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
