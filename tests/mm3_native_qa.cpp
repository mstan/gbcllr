#include <gbcllr/machine.hpp>
#include "../src/mm4_native_mods.hpp"
#include "../src/native_mods.hpp"
#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <stdexcept>
using namespace gbcllr;
static void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
static void poke(Machine& m,uint16_t address,uint8_t value){m.debug_write(address,std::span(&value,1));}
static uint8_t peek(Machine& m,uint16_t address){return m.debug_read(address,1)[0];}
static void ppm(const std::filesystem::path& path,ModView v){std::ofstream f(path,std::ios::binary);f<<"P6\n"<<v.width<<' '<<v.height<<"\n255\n";for(auto p:v.pixels){char rgb[]{char(p>>16),char(p>>8),char(p)};f.write(rgb,3);}}
static void dx_timing(std::span<const uint8_t> rom,unsigned title,const std::filesystem::path& out){
 std::filesystem::create_directories(out);std::array<uint64_t,2> work{},ticks{},audio{};
 for(unsigned dx=0;dx<2;++dx){Options options;NativeFeature f;f.plugin="gbcllr.mm"+std::to_string(title)+(dx?".dx":".adaptive-view");f.view_width=160;f.colorization=dx!=0;f.no_flicker=dx==0;options.mods.push_back(f);Machine m(rom,options);m.mod_viewport(160,144);
  auto frames=[&](unsigned n,uint8_t keys=0){for(unsigned i=0;i<n;++i){m.run_frame(keys);m.take_audio();}};frames(240);frames(1,128);frames(180);frames(1,128);frames(180);if(title==3){frames(1,128);frames(900);}else for(unsigned i=0;i<88;++i){frames(1,16);frames(60);}
  check(peek(m,0xd702)==1,"DX timing fixture did not enter original live gameplay");auto counter=uint16_t(title==3?0xde86:0xde93),health=uint16_t(title==3?0xde9c:0xdeae),hurt=uint16_t(title==3?0xd738:0xd73a);auto start=m.stats().cycles;unsigned before=peek(m,counter);
  std::cout<<"timing title="<<title<<" standalone_dx="<<dx<<" updates_per60=";
  for(unsigned sample=0;sample<10;++sample){poke(m,health,152);poke(m,hurt,0);poke(m,uint16_t(hurt+1),64);
   for(unsigned n=0;n<60;++n){check(peek(m,0xd702)==1&&peek(m,health)==152&&!peek(m,title==3?0xde92:0xdeaa),"DX timing fixture left protected live gameplay");m.run_frame();audio[dx]+=m.take_audio().size();}
   unsigned after=peek(m,counter),delta=uint8_t(after-before);before=after;work[dx]+=delta;std::cout<<(sample?",":"")<<delta;
  }
  ticks[dx]=m.stats().cycles-start;check(peek(m,health)==152,"DX timing protection failed");std::cout<<" total="<<work[dx]<<" ticks="<<ticks[dx]<<" pcm="<<audio[dx]<<"\n";auto view=m.mod_view();check(view.width==160&&!view.expanded,"Standalone DX enabled adaptive width");ppm(out/(dx?"dx-timing.ppm":"native-timing.ppm"),view);
  if(dx){auto state=m.snapshot();auto run=[&](){for(unsigned n=0;n<12;++n)m.run_frame();auto pcm=m.take_audio();auto v=m.mod_view();return std::pair{pcm,std::vector<uint32_t>(v.pixels.begin(),v.pixels.end())};};auto expected=run();auto end=m.snapshot();std::string why;check(m.restore(state,why),why.c_str());check(run()==expected&&m.snapshot()==end,"Standalone DX state/PCM/view replay differs");
   // An enhanced instruction may consume CPU headroom without advancing the
   // physical clock. Check that exact boundary and its live budget checkpoint.
   m.take_audio();bool observed=false;
   for(unsigned probe=0;probe<200000&&!observed;++probe){m.take_audio();auto before=m.stats();unsigned length=0;size_t at=before.pc<0x4000?before.pc:size_t(before.rom_bank)*0x4000+before.pc-0x4000;
    if(before.pc<0x8000&&at<rom.size()){auto op=rom[at];if(op==0x3e||op==0x06||op==0x0e||op==0x16||op==0x1e||op==0x26||op==0x2e)length=2;else if(op==0xaf||op==0x00)length=1;}
    if(m.debug_registers().ime&&(peek(m,0xffff)&peek(m,0xff0f)&31))length=0;
    auto mid=length?m.snapshot():std::vector<uint8_t>{};m.debug_step_instruction();auto after=m.stats();
    if(!length||after.cycles!=before.cycles)continue;
    check(after.instructions_native+after.instructions_fallback==before.instructions_native+before.instructions_fallback+1,"DX debug step did not execute exactly one zero-tick instruction");check(after.pc==before.pc+length,"DX zero-tick nextPC was not visible");
    auto done=m.snapshot();auto pcm=m.take_audio();auto v=m.mod_view();std::vector<uint32_t> pixels(v.pixels.begin(),v.pixels.end());check(m.restore(mid,why),why.c_str());m.debug_step_instruction();auto again=m.mod_view();check(m.snapshot()==done&&m.take_audio()==pcm&&std::equal(pixels.begin(),pixels.end(),again.pixels.begin()),"DX mid-budget state/PCM/view continuation differs");observed=true;
   }
   check(observed,"Standalone DX fixture did not observe a zero-physical-tick instruction");std::cout<<"DX-only zero-tick single instruction/nextPC and mid-budget state/PCM/view replay pass\n";
  }
 }
 check(work[1]>=599&&work[1]<=601,"Standalone DX did not complete one update per display in stable fixture");check(audio[0]==audio[1]&&std::abs(int64_t(ticks[0])-int64_t(ticks[1]))<=32,"Standalone DX changed physical/audio duration");std::cout<<"PASS standalone gameplay headroom; neutral travel case is not a general slowdown or host performance claim\n";
}
static void dx_lab(std::span<const uint8_t> rom,const std::filesystem::path& out){
 std::filesystem::create_directories(out);std::array<std::vector<uint8_t>,2> ram,hram;std::array<std::vector<int16_t>,2> pcm;std::array<uint64_t,2> cycles{};
 for(unsigned dx=0;dx<2;++dx){Options options;NativeFeature f;f.plugin="gbcllr.mm4.adaptive-view";f.view_width=160;f.no_flicker=true;options.mods.push_back(f);if(dx){NativeFeature color;color.plugin="gbcllr.mm4.dx";color.view_width=160;color.colorization=true;options.mods.push_back(color);}Machine m(rom,options);m.mod_viewport(160,144);auto frames=[&](unsigned n,uint8_t key=0){while(n--){m.run_frame(key);m.take_audio();}};
  frames(240);frames(1,128);frames(180);frames(1,128);frames(180);
  check(peek(m,0xdf0a)==16&&peek(m,0xdeaa)==1&&peek(m,0xdeae)==152&&peek(m,0xd702)==1&&peek(m,0xd84c)==10&&peek(m,0xd84d)==8,"IV laboratory did not retain original conversation/room/health context");
  auto before=m.snapshot();auto v=m.mod_view();check(v.width==160&&!v.expanded,"IV laboratory changed its original fixed surface");ppm(out/(dx?"lab-dx.ppm":"lab-native.ppm"),v);check(m.snapshot()==before,"IV laboratory presentation changed simulation state");if(dx){bool colored=false;for(auto p:v.pixels)colored|=((p>>16)&255)!=((p>>8)&255)||((p>>8)&255)!=(p&255);check(colored,"IV laboratory still uses monochrome presentation");
   // Authored20:40A4 OBJ-bank1 tile0 and palette4A8C: white/eye/skin
   // pixels in the original stationary lab pose, not a generic color check.
   check(v.pixels[63*160+83]==0xffffffffu&&v.pixels[64*160+84]==0xff000000u&&v.pixels[64*160+85]==0xffffd5a4u,"IV laboratory face did not select its authored OBJ bank1 texture");}
  auto begin=m.stats().cycles;for(unsigned n=0;n<12;++n)m.run_frame();pcm[dx]=m.take_audio();cycles[dx]=m.stats().cycles-begin;ram[dx]=m.debug_read(0xc000,8192);hram[dx]=m.debug_read(0xff80,127);auto end=m.snapshot();auto view=m.mod_view();std::vector<uint32_t> pixels(view.pixels.begin(),view.pixels.end());write_file_atomic(out/(dx?"lab-dx.state":"lab-native.state"),end);
  std::string error;check(m.restore(before,error),error.c_str());for(unsigned n=0;n<12;++n)m.run_frame();auto again=m.mod_view();check(m.take_audio()==pcm[dx]&&m.snapshot()==end&&std::equal(pixels.begin(),pixels.end(),again.pixels.begin()),"IV laboratory current-state/PCM/view replay differs");
 }
 check(ram[0]==ram[1]&&hram[0]==hram[1]&&pcm[0]==pcm[1]&&cycles[0]==cycles[1],"IV authored laboratory changed original guest RAM/HRAM/PCM/physical duration");std::cout<<"PASS original IV laboratory authored presentation, pure render/current replay and exact native-vs-DX RAM/HRAM/PCM/physical duration; original conversation lock/timing retained\n";
}
static void dx_resource(std::span<const uint8_t> rom,const std::filesystem::path& out){
 std::filesystem::create_directories(out);std::array<std::vector<uint8_t>,2> ram,hram,vram;std::array<std::vector<int16_t>,2> pcm;std::array<Registers,2> registers;std::array<uint64_t,2> cycles{};
 for(unsigned dx=0;dx<2;++dx){Options options;NativeFeature f;f.plugin="gbcllr.mm4.adaptive-view";f.view_width=160;f.no_flicker=true;options.mods.push_back(f);if(dx){NativeFeature color;color.plugin="gbcllr.mm4.dx";color.view_width=160;color.colorization=true;options.mods.push_back(color);}Machine m(rom,options);m.mod_viewport(160,144);auto frames=[&](unsigned n,uint8_t key=0){while(n--){m.run_frame(key);m.take_audio();}};
  frames(240);frames(1,128);frames(180);frames(1,128);frames(180);check(peek(m,0xdf0a)==16&&peek(m,0xdeaa)==1,"Resource-copy fixture did not enter the original laboratory");
  // Exercise original0:020D/2162, used by5:4A38 to copy resource2A at88D0.
  // Original68 tiles remain68 in guest VRAM; its two authored additions are
  // presentation-only. A nonzero old tail rules out an assumed empty slot.
  std::array<uint8_t,32> tail{};for(unsigned n=0;n<tail.size();++n)tail[n]=uint8_t(0xa5u+n*7u);m.debug_write(0x8d10,tail);poke(m,0xfffc,0x0a);poke(m,0xfffd,0x02);
  m.debug_registers(Registers{0x2a00,0,0x88d0,0,0xfffc,0x020d,1});m.debug_wake();m.take_audio();auto begin=m.stats().cycles;
  // The serialized pending ID/destination must survive a mid-service save.
  for(unsigned n=0;n<8;++n)m.debug_step_instruction();m.take_audio();auto mid=m.snapshot();auto copy=[&]{unsigned n=0;while(m.stats().pc!=0x020a&&n++<200000)m.debug_step_instruction();check(m.stats().pc==0x020a,"Original generic resource copy did not return");};copy();auto copied=m.snapshot();auto copied_pcm=m.take_audio();auto copied_cycles=m.stats().cycles-begin;
  check(m.debug_read(0x8d10,32)==std::vector<uint8_t>(tail.begin(),tail.end()),"Authored added tiles changed native VRAM");
  auto raw=[&](unsigned bank,unsigned address){return rom[address<0x4000?address:bank*0x4000+address-0x4000];};auto word=[&](unsigned bank,unsigned address){return unsigned(raw(bank,address))|(unsigned(raw(bank,address+1))<<8);};unsigned table=0x41fc+0x2a*3,bank=raw(1,table),header=word(bank,word(1,table+1)),count=raw(bank,header),source=word(bank,header+1);check(count==68,"Original resource2A count changed");for(unsigned n=0;n<count*16;++n)check(peek(m,uint16_t(0x88d0+n))==raw(bank,source+n),"Original resource2A prefix copy differs");
  std::string error;check(m.restore(mid,error),error.c_str());copy();check(m.snapshot()==copied&&m.take_audio()==copied_pcm,"Original resource-copy mid-service replay differs");
  auto before=m.snapshot();auto view=m.mod_view();ppm(out/(dx?"resource-dx.ppm":"resource-native.ppm"),view);check(m.snapshot()==before,"Resource-copy render mutated state");write_file_atomic(out/(dx?"resource-dx.state":"resource-native.state"),before);
  ram[dx]=m.debug_read(0xc000,8192);hram[dx]=m.debug_read(0xff80,127);vram[dx]=m.debug_read(0x8000,8192);pcm[dx]=copied_pcm;registers[dx]=m.debug_registers();cycles[dx]=copied_cycles;
  if(dx){m.debug_registers(Registers{uint16_t(tail[0]<<8),1,0,0x8d10,0xfffc,0x1a81,1});m.debug_wake();copy();check(m.debug_read(0x8d10,32)==std::vector<uint8_t>(tail.begin(),tail.end()),"Original same-value fill changed native tail");write_file_atomic(out/"resource-native-write.state",m.snapshot());}
 }
 check(ram[0]==ram[1]&&hram[0]==hram[1]&&vram[0]==vram[1]&&pcm[0]==pcm[1]&&cycles[0]==cycles[1],"Authored resource additions changed original RAM/HRAM/VRAM/PCM/duration");check(registers[0].af==registers[1].af&&registers[0].bc==registers[1].bc&&registers[0].de==registers[1].de&&registers[0].hl==registers[1].hl&&registers[0].sp==registers[1].sp&&registers[0].pc==registers[1].pc,"Authored resource additions changed original registers");
 std::cout<<"PASS original IV resource2A68-tile copy/native nonzero tail, exact native/DX RAM/HRAM/VRAM/registers/PCM/duration, pure render and mid-service replay; authored cache source proof retained separately\n";
}
static void dx_palette_contract(std::span<const uint8_t> rom,const std::filesystem::path& out){
 // Source-contract contexts exercise native hook boundaries, without donor
 // instructions or guest result writes. Actual ending timing is a separate gate.
 std::array<uint8_t,8192> ram{},vram{};std::array<uint8_t,160> oam{};std::array<uint8_t,127> hram{};
 std::array<uint32_t,4> bg{};std::array<uint32_t,8> obj{};bg.fill(0xffdddddd);obj.fill(0xffdddddd);
 ModPpu ppu{0,0,0x93,7,144,nullptr,nullptr,bg.data(),obj.data(),0x1b,0xe4,0xe4};
 ModContext c{};c.rom=rom.data();c.rom_size=rom.size();c.wram=ram.data();c.vram=vram.data();c.oam=oam.data();c.hram=hram.data();c.ppu=&ppu;
 std::array<uint16_t,6> cpu{};NativeFeature feature;feature.plugin="gbcllr.mm4.dx";feature.view_width=160;feature.colorization=true;
 auto setup=[&](unsigned stage,unsigned shade,unsigned ink){ram.fill(0);vram.fill(0);oam.fill(0);hram.fill(0);ram[0x1f0a]=uint8_t(stage);ram[0x1eae]=152;ram[0x1ea6]=uint8_t(shade);ppu.bgp=uint8_t(shade);cpu={};
  for(unsigned y=0;y<8;++y){vram[y*2]=(ink&1)?255:0;vram[y*2+1]=(ink&2)?255:0;}std::fill(vram.begin()+0x1800,vram.begin()+0x1c00,1);oam[0]=32;oam[1]=24;oam[3]=16;};
 auto invoke=[&](Mm4NativeMods& m,unsigned bank,unsigned pc){c.rom_bank=uint16_t(bank);c.instruction_pc=c.pc=uint16_t(pc);cpu[5]=uint16_t(pc);auto before=ram;auto texture=vram;auto registers=cpu;m.instruction(c,cpu,ram,hram);check(ram==before&&vram==texture&&cpu==registers,"IV palette hook changed guest RAM/VRAM/registers");};
 auto image=[&](Mm4NativeMods& m){for(unsigned y=0;y<144;++y)m.scanline(c,uint8_t(y));m.complete_frame(c,false);std::array<uint32_t,160*144> native{};native.fill(0xffdddddd);auto before=m.snapshot();auto view=m.render(c,native);std::vector<uint32_t> pixels(view.pixels.begin(),view.pixels.end());check(view.width==160&&m.snapshot()==before,"IV palette rendering is impure");m.restore(before);auto again=m.render(c,native);check(std::equal(pixels.begin(),pixels.end(),again.pixels.begin()),"IV palette cache restore/view differs");return pixels;};
 constexpr unsigned pixel=16*160+16;
 {setup(0,0x1b,3);Mm4NativeMods m(std::span(&feature,1),false);cpu[2]=1;invoke(m,6,0x5a41);auto pixels=image(m);
  // Authored6:5A64?OBJ4A6E, bank32, visible ink3 RGB5552417.
  check(pixels[pixel]==0xffbd004a,"IV charged OBJ1 palette source pixel differs");ppm(out/"charge.ppm",m.render(c,std::array<uint32_t,160*144>{}));}
 {setup(0,0xd1,2);Mm4NativeMods m(std::span(&feature,1),false);cpu[2]=1;invoke(m,6,0x5a41);c.instruction_pc=0x4a8b;c.rom_bank=15;m.write(c,0xdeab,2);ram[0x1eab]=2;
  c.instruction_pc=0x513c;c.rom_bank=1;m.write(c,0xdea6,0x1b);ram[0x1ea6]=ppu.bgp=0x1b;check(image(m)[pixel]==0xff83cd10,"IV weapon switch retained an old charged OBJ palette after flash");}
 {setup(22,0xd1,1);Mm4NativeMods m(std::span(&feature,1),false);invoke(m,0,0x3370);auto before=image(m);invoke(m,0x1c,0x531e);auto after=image(m);
  check(before[pixel]==0xffffffff&&after[pixel]==0xffcdc5f6,"IV teleport did not restore authored OBJ shade1B");check(before[0]==after[0],"IV OBJ-only teleport restore changed BG fade");}
 {setup(14,0xd1,1);Mm4NativeMods m(std::span(&feature,1),false);invoke(m,6,0x4adf);m.write(c,0xdea6,0x1b);ram[0x1ea6]=ppu.bgp=0x1b;check(image(m)[pixel]==0xffffffff,"IV Bright Stopper exposed the capsule through native palette reset");}
 {setup(22,0x1b,1);Mm4NativeMods m(std::span(&feature,1),false);invoke(m,0,0x3370);ram[0x1d38]=0xd1;auto stale=image(m);ram[0x1d38]=0x1b;check(image(m)==stale,"IV stale DD38 palette upload queue reached direct host colors");}
 std::cout<<"PASS literal IV charged/weapon/teleport/capsule palette source pixels; guest memory/register purity and current cache/view replay; stale DD38 queue excluded\n";
}

int main(int argc,char** argv){try{
 check(argc==4||argc==5,"usage: mm34-native-qa ROM TITLE OUTPUT_DIRECTORY [dx|smooth|dx-smooth|dx-timing|dx-lab|dx-resource|arena19|dx-palettes]");unsigned title=std::stoul(argv[2]);check(title==3||title==4,"title must be3 or4");
 std::string mode=argc==5?argv[4]:"stock";check(mode=="stock"||mode=="dx"||mode=="smooth"||mode=="dx-smooth"||mode=="dx-timing"||mode=="dx-lab"||mode=="dx-resource"||mode=="arena19"||mode=="dx-palettes","unknown QA profile");check(mode!="arena19"||title==4,"arena19 requires original IV");bool dx=mode.starts_with("dx"),smooth=mode.ends_with("smooth");
 std::filesystem::path out=argv[3];std::filesystem::create_directories(out);auto rom=read_file(argv[1]);auto hash=sha256(rom);if(mode=="dx-palettes"){check(title==4,"dx-palettes requires original IV");dx_palette_contract(rom,out);return 0;}if(mode=="dx-resource"){check(title==4,"dx-resource requires original IV");dx_resource(rom,out);return 0;}if(mode=="dx-lab"){check(title==4,"dx-lab requires original IV");dx_lab(rom,out);return 0;}if(mode=="dx-timing"){dx_timing(rom,title,out);return 0;}
 Options options;options.mods.push_back(NativeFeature{});auto& feature=options.mods[0];feature.plugin="gbcllr.mm"+std::to_string(title)+".adaptive-view";feature.view_width=-1;feature.weapon_cycle=true;feature.no_flicker=true;feature.smooth_gameplay=smooth;
 if(dx){NativeFeature color;color.plugin="gbcllr.mm"+std::to_string(title)+".dx";color.view_width=160;color.colorization=true;options.mods.push_back(color);}
 if(dx){auto invalid=options;invalid.mods.back().asset_input=std::string(64,'0');bool rejected=false;try{Machine wrong_pin(rom,invalid);}catch(const std::runtime_error& e){rejected=std::string(e.what()).find("asset pin")!=std::string::npos;}check(rejected,"DX accepted a package pin that differs from the compiled assets");}
 Machine m(rom,options);m.mod_viewport(1280,720);m.trace_enabled(true);
 auto frames=[&](unsigned count,uint8_t pad=0,uint8_t virtual_pad=0){for(unsigned i=0;i<count;++i){m.run_frame(LogicalInput{pad,virtual_pad});m.take_audio();}};
 std::vector<uint8_t> recoil_off_title;
 frames(240);frames(1,128);frames(180);
 if(title==3&&dx){
  frames(3);auto before=m.snapshot();auto view=m.mod_view();check(view.width==160&&view.sprites==12,"III authored title did not bind its original asset surface");ppm(out/"authored-title.ppm",view);check(m.snapshot()==before,"III title presentation changed simulation state");
  auto replay=[&]{frames(3);auto v=m.mod_view();return std::pair{m.snapshot(),std::vector<uint32_t>(v.pixels.begin(),v.pixels.end())};};auto expected=replay();std::string error;check(m.restore(before,error),error.c_str());check(replay()==expected,"III authored title completed-frame checkpoint differs");
  auto title_state=m.snapshot();frames(1,8);frames(3);frames(1,128);frames(120);
  check(peek(m,0xcca5)==11&&peek(m,0xcc9c)==5,"III original password input did not reach its native cursor/five-marker state");
  auto password=[&](const char* name){auto state=m.snapshot();auto v=m.mod_view();check(v.width==160&&!v.expanded,"III password changed its original fixed surface");bool colored=false;for(auto p:v.pixels)colored|=((p>>16)&255)!=((p>>8)&255)||((p>>8)&255)!=(p&255);check(colored,"III authored password did not bind its palette/art surface");ppm(out/name,v);check(m.snapshot()==state,"III password rendering changed original simulation state");};password("password-input.ppm");
  frames(1,1);frames(3);check(peek(m,0xccae)==1,"III password Right did not move the native cursor");frames(1,16);frames(3);check(peek(m,0xcc9c)==4&&(peek(m,0xcc9b)&64),"III password A did not place its native marker");password("password-marker.ppm");
  auto password_state=m.snapshot();auto replay_password=[&]{frames(8);auto v=m.mod_view();return std::pair{m.snapshot(),std::vector<uint32_t>(v.pixels.begin(),v.pixels.end())};};auto expected_password=replay_password();check(m.restore(password_state,error),error.c_str());check(replay_password()==expected_password,"III password current-state/view replay differs");check(m.restore(title_state,error),error.c_str());

 }
 if(title==4&&dx){
  frames(3);check(peek(m,0xdf46)==1,"original IV title did not select Start");auto on=m.snapshot();auto on_view=m.mod_view();std::vector<uint32_t> on_pixels(on_view.pixels.begin(),on_view.pixels.end());check(on_view.width==160,"authored IV title did not use its fixed surface");
  frames(1,8);frames(3);check(peek(m,0xdf46)==2,"IV title Down did not select Password");frames(1,8);frames(3);check(peek(m,0xdf46)==4,"IV title Down did not select Recoil");
  frames(1,16);frames(3);check(peek(m,0xdf46)==4,"IV recoil toggle left the original menu");recoil_off_title=m.snapshot();auto off_view=m.mod_view();check(!std::equal(on_pixels.begin()+96*160+96,on_pixels.begin()+104*160,off_view.pixels.begin()+96*160+96),"IV recoil checkbox did not change");ppm(out/"title-recoil-off.ppm",off_view);
  std::string error;check(m.restore(on,error),error.c_str());auto restored=m.mod_view();check(std::equal(on_pixels.begin(),on_pixels.end(),restored.pixels.begin()),"IV title option checkpoint did not restore the completed view");ppm(out/"title-recoil-on.ppm",restored);check(m.snapshot()==on,"IV title rendering changed simulation state");
 }
 frames(1,128);frames(180);
 if(title==3&&dx){
  auto board=[&](const char* name){check(peek(m,0xcc96)==15&&peek(m,0xccb2)==1,"III original stage-select controller did not reach its live board");auto before=m.snapshot();auto view=m.mod_view();check(view.width==160,"III authored stage select did not retain its fixed surface");bool colored=false;for(auto pixel:view.pixels)colored|=((pixel>>16)&255)!=((pixel>>8)&255)||((pixel>>8)&255)!=(pixel&255);check(colored,"III authored stage select did not bind its color surface");ppm(out/name,view);check(m.snapshot()==before,"III stage-select rendering changed simulation state");};
  frames(3);board("stage-select-first.ppm");auto initial_board=m.snapshot();auto selected=peek(m,0xcc98);frames(8,1);frames(4);check(peek(m,0xcc98)==(selected^1),"III authored board did not retain original Right navigation");board("stage-select-cursor.ppm");
  auto replay=[&]{frames(4);auto v=m.mod_view();return std::pair{m.snapshot(),std::vector<uint32_t>(v.pixels.begin(),v.pixels.end())};};auto checkpoint=m.snapshot();auto expected=replay();std::string error;check(m.restore(checkpoint,error),error.c_str());check(replay()==expected,"III stage-select completed-frame checkpoint differs");
  // Native completion/ownership values are supplied only as UI fixture
  // prerequisites here; these captures do not qualify any boss result.
  for(bool complete:{false,true}){check(m.restore(initial_board,error),error.c_str());poke(m,0xde9a,uint8_t(peek(m,0xde9a)|0x3c));poke(m,0xdf09,complete?255:15);if(complete)poke(m,0xde9b,uint8_t(peek(m,0xde9b)|15));m.debug_registers(Registers{0,0,0,0,0xfffe,0x1bd3,1});m.debug_wake();frames(180);check(peek(m,0xdf07)==1,"III original second-board loader did not publish its group");board(complete?"stage-select-complete.ppm":"stage-select-second.ppm");}
  check(m.restore(initial_board,error),error.c_str());
  frames(1,128);
  for(unsigned sample=1;sample<=5;++sample){frames(60);auto state=m.snapshot();auto v=m.mod_view();check(peek(m,0xcc96)==15&&peek(m,0xccb2)>=3&&peek(m,0xccb2)<=8,"III representative intro left its original controller");check(v.width==160&&!v.expanded,"III boss intro changed its fixed original surface");ppm(out/("boss-intro-"+std::to_string(sample*60)+".ppm"),v);check(m.snapshot()==state,"III authored boss intro rendering changed simulation state");
   if(sample>=2){bool colored=false;for(auto p:v.pixels)colored|=((p>>16)&255)!=((p>>8)&255)||((p>>8)&255)!=(p&255);check(colored,"III authored introduction did not bind its color art/attributes");if(sample==2){unsigned native=0;auto oam=m.debug_read(0xc000,160);for(unsigned i=0;i<40;++i)native+=oam[i*4]>0&&oam[i*4]<160&&oam[i*4+1]>0&&oam[i*4+1]<168;check(v.sprites==native+1,"III stable intro did not retain native OAM plus its actual one-piece source overlay");} /* Later native buffers may already belong to the next DMA; retained source oracles check the completed poses/pixels. */}
   if(sample==4){auto replay=[&]{frames(12);auto v=m.mod_view();return std::pair{m.snapshot(),std::vector<uint32_t>(v.pixels.begin(),v.pixels.end())};};auto expected=replay();check(m.restore(state,error),error.c_str());check(replay()==expected,"III authored intro current-state/view replay differs");check(m.restore(state,error),error.c_str());}
  }
  check(m.restore(initial_board,error),error.c_str());
 }
 if(title==3){frames(1,128);frames(900);}else for(unsigned i=0;i<88;++i){frames(1,16);frames(60);}
 check(peek(m,0xd702)==1,"original startup did not reach a live stage");frames(8);
 auto v=m.mod_view();std::cout<<"entry stage="<<unsigned(peek(m,title==3?0xdf08:0xdf0a))<<" width="<<v.width<<" sprites="<<v.sprites<<" captures="<<v.captures<<"\n";
 ppm(out/"entry.ppm",v);write_file_atomic(out/"entry.state",m.snapshot());
 check(v.width==256&&v.expanded,"native room publication did not expand");
 if(dx){bool colored=false;for(auto p:v.pixels)colored|=((p>>16)&255)!=((p>>8)&255)||((p>>8)&255)!=(p&255);check(colored,"authored DX gameplay palettes did not produce color");}
 else{size_t mismatch=0;int offset=v.camera_x-v.left;auto native=m.pixels();for(unsigned y=0;y<144;++y)for(unsigned x=0;x<160;++x){int px=offset+int(x);if(px>=0&&px<int(v.width)&&native[y*160+x]!=v.pixels[y*v.width+unsigned(px)])++mismatch;}std::cout<<"initial native-center pixel differences="<<mismatch<<"/23040 (uncapped sprites may differ)\n";m.write_frame(out/"native-entry.ppm");}
 // Invoke the actual original actor helper, including its PUSH HL/CALL
 // wrapper, and compare its tile query with the original room data. The
 // 160px run records the unmodified service's page/sign alias separately.
 auto entry_state=m.snapshot();
 if(mode=="arena19"){
  // One representative original Hunter arena, selected through its existing
  // source room stream. Original1C:51DD writes its live mode13.
  poke(m,0xdf0a,19);poke(m,0xde9a,0);poke(m,0xdeae,152);poke(m,0xdeac,255);poke(m,0xdead,15);poke(m,0xdf00,1);poke(m,0x2100,1);m.debug_registers(Registers{0,0,0,0,0xfffe,0x0a01,1});m.debug_wake();unsigned budget=2000000;while(m.stats().pc!=0x352d&&budget--)m.debug_step_instruction();check(m.stats().pc==0x352d,"IV Hunter arena did not reach original room loader");poke(m,0xd7bb,0xbf);poke(m,0xd7bc,0x59);
  auto protect=[&]{poke(m,0xdeae,152);poke(m,0xd73a,0);poke(m,0xd73b,64);};for(unsigned i=0;i<9;++i){protect();frames(20);}check(peek(m,0xde44)==13&&peek(m,0xd84c)==10&&peek(m,0xd84d)==8&&peek(m,0xc647)==0x4a&&peek(m,0xc658)==16,"IV representative source arena lost its actual Hunter/mode/room geometry");auto room=m.snapshot();
  std::array<std::vector<uint8_t>,2> before_ram,after_ram,after_hram;std::array<decltype(m.take_audio()),2> audio;std::array<uint64_t,2> ticks{};bool exact=true;
  for(unsigned index=0;index<2;++index){std::string error;check(m.restore(room,error),error.c_str());unsigned width=index?256:160;m.mod_viewport(width,144);protect();frames(4);auto before=m.snapshot();auto view=m.mod_view();ppm(out/("arena19-"+std::to_string(width)+".ppm"),view);m.write_frame(out/("arena19-native-"+std::to_string(width)+".ppm"));write_file_atomic(out/("arena19-"+std::to_string(width)+".state"),m.snapshot());check(m.snapshot()==before,"IV Hunter arena rendering changed simulation state");check(view.width==width&&view.expanded==bool(index),"IV actual custom-mode Hunter arena did not use requested width");
   unsigned differences=0;int offset=view.camera_x-view.left;auto native=m.pixels();for(unsigned y=0;y<144;++y)for(unsigned x=0;x<160;++x)differences+=native[y*160+x]!=view.pixels[y*width+unsigned(int(x)+offset)];exact&=differences==0;std::cout<<"actual IV Hunter arena mode13 width="<<width<<" native center differences="<<differences<<" uncapped="<<view.uncapped_sprites<<"\n";
   before_ram[index]=m.debug_read(0xc000,8192);auto start=m.stats().cycles;m.take_audio();for(unsigned i=0;i<60;++i){if(!(i%20))protect();m.run_frame(uint8_t(i<15?1:i<30?2:0));auto pcm=m.take_audio();audio[index].insert(audio[index].end(),pcm.begin(),pcm.end());}ticks[index]=m.stats().cycles-start;after_ram[index]=m.debug_read(0xc000,8192);after_hram[index]=m.debug_read(0xff80,127);
  }
  check(before_ram[0]==before_ram[1]&&after_ram[0]==after_ram[1]&&after_hram[0]==after_hram[1]&&audio[0]==audio[1]&&ticks[0]==ticks[1],"IV single-screen custom arena changed native RAM/HRAM/PCM/physical duration with width");check(exact,"IV Hunter arena native center needs source/raster correction");std::cout<<"PASS representative original IV Hunter mode13 adaptive surface; native center and160/256 RAM/HRAM/PCM/physical duration exact (stock cadence)\n";return 0;
 }
 if(title==3&&dx){
  // Bounded caller-contract check: invoke the actual projectile controller's
  // animation call site, with an allocated actor as a fixture prerequisite.
  // The original 015F service must advance its own5242 table and preserve BC.
  poke(m,0xc6a5,0);poke(m,0xc6a6,0);poke(m,0xc697,0);poke(m,0xde93,13);poke(m,0x2100,13);
  auto finish=[&]{unsigned budget=10000;while(m.stats().pc!=0x43e9&&budget--)m.debug_step_instruction();check(m.stats().pc==0x43e9,"III original projectile animator did not resume its controller");};
  for(unsigned tick=0;tick<16;++tick){
   m.debug_registers(Registers{0,0x0080,0,0xc680,0xfffe,0x5236,0});m.debug_wake();m.debug_step_instruction();auto active=m.snapshot();finish();check(m.debug_registers().bc==0x0080,"III projectile animator did not preserve original BC");check(peek(m,0xc697)==(((tick+1)/8)%2?6:4)&&peek(m,0xc6a5)==(tick+1)%8,"III projectile did not advance its actual native animation table");
   if(!tick){auto expected=m.snapshot();std::string error;check(m.restore(active,error),error.c_str());finish();check(m.snapshot()==expected,"III active native animation continuation did not checkpoint");}
  }
  std::string error;check(m.restore(entry_state,error),error.c_str());std::cout<<"III actual original projectile animation call advances4/6 over16 ticks; BC and active-continuation replay pass (bounded caller contract)\n";
 }
 if(title==3&&dx&&!smooth){
  // Native stage6 supplies this vulnerable ordinary actor and its untouched
  // HP4. Position is a declared fixture prerequisite; B produces the shot.
  m.mod_viewport(400,144);poke(m,0xdf08,6);poke(m,0xde8c,0);poke(m,0xde9c,152);poke(m,0xde93,1);poke(m,0x2100,1);m.debug_registers(Registers{0,0,0,0,0xfffe,0x08e8,1});m.debug_wake();frames(180);auto field=m.snapshot();
  for(unsigned x:{100u,356u}){
   std::string error;check(m.restore(field,error),error.c_str());check(peek(m,0xc740)==0x18&&peek(m,0xc758)==4,"III original stage did not supply the native HP4 shot fixture");poke(m,0xc74a,uint8_t(x));poke(m,0xc74b,uint8_t(x>>8));poke(m,0xc74d,uint8_t(x));poke(m,0xc74f,peek(m,0xd724));poke(m,0xde9c,152);poke(m,0xd738,0);poke(m,0xd739,64);
   // Hold through the actual cartridge poll; a one-physical-frame B pulse
   // can miss an original work update in this busy room.
   m.set_buttons(33);unsigned budget=2000000;while(m.stats().pc!=0x0fe3&&budget--)m.debug_step_instruction();check(m.stats().pc==0x0fe3&&m.debug_read(0xcc40,2)==std::vector<uint8_t>({0x40,0xc7}),"III actual B shot did not reach the native actor point service");check(peek(m,0xc246)==1,"III shot fixture did not retain its actual native projectile");
   budget=100000;while(m.stats().pc!=0x2989&&budget--)m.debug_step_instruction();check(m.stats().pc==0x2989,"III native point collision did not return to its actor");check(peek(m,0xc758)==(x==100?3:4)&&peek(m,0xc246)==(x==100?0:1),"III native point shot damaged or consumed against the wrong world page");std::cout<<"actual III held native B targetworld="<<x<<" untouched HP4 ->"<<unsigned(peek(m,0xc758))<<"; shot="<<unsigned(peek(m,0xc246))<<"\n";
  }
  for(bool charged:{false,true}){
   std::string error;check(m.restore(field,error),error.c_str());auto protect=[&]{poke(m,0xde9c,152);poke(m,0xd738,0);poke(m,0xd739,64);};poke(m,0xd70d,40);poke(m,0xd70e,0);poke(m,0xd721,40);protect();frames(4,2);frames(2);
   if(charged){poke(m,0xc74a,100);poke(m,0xc74b,1);poke(m,0xc74d,100);protect();frames(20,32);protect();frames(20,32);check(peek(m,0xc758)==4&&peek(m,0xd755)>=30,"III original charge hold lost the native HP4 actor or did not charge");}
   m.set_buttons(charged?0:32);unsigned budget=2000000;while((m.stats().pc!=0x4144||m.stats().rom_bank!=1)&&budget--)m.debug_step_instruction();check(m.stats().pc==0x4144&&peek(m,0xc758)==4,"III Buster regression did not reach its original emitter with untouched HP4");
   auto word=[&](uint16_t at){auto bytes=m.debug_read(at,2);return unsigned(bytes[0])|(unsigned(bytes[1])<<8);};auto byte=[&](unsigned bank,unsigned at){return rom[bank*0x4000+at-0x4000];};unsigned bank=peek(m,0xc75b),table=word(0xc755)+peek(m,0xc757)*2,rect=byte(bank,table)|(unsigned(byte(bank,table+1))<<8);int center=(int(int8_t(byte(bank,rect+2)))+int(int8_t(byte(bank,rect+3))))/2;unsigned gun=0x420f+2*(2*peek(m,0xd746)+peek(m,0xd703));int y=int(peek(m,0xd724))+int8_t(byte(1,gun+1))-center;
   // Position-only prerequisite, derived from the original pose/gun tables.
   // The real Buster and charged Buster then enter the original point service.
   poke(m,0xc74a,248);poke(m,0xc74b,0);poke(m,0xc74d,248);poke(m,0xc74f,uint8_t(y));m.debug_step_instruction();budget=2000000;while((m.stats().pc!=0x4352||m.stats().rom_bank!=1)&&budget--)m.debug_step_instruction();check(m.stats().pc==0x4352&&peek(m,0xc23b)==7&&(peek(m,0xc244)&3)==unsigned(charged),"III original weak/charged Buster source was not emitted");m.take_audio();auto flight=m.snapshot();
   auto finish=[&]{frames(12,charged?0:32);check(peek(m,0xc758)==4,"III native weak/charged Buster hit a page-zero actor whose rectangle wraps at256");auto v=m.mod_view();return std::pair{m.snapshot(),std::vector<uint32_t>(v.pixels.begin(),v.pixels.end())};};auto expected=finish();ppm(out/(charged?"straddle-charged.ppm":"straddle-weak.ppm"),m.mod_view());check(m.restore(flight,error),error.c_str());check(finish()==expected,"III active weak/charged Buster world source did not replay");std::cout<<"actual III "<<(charged?"charged":"weak")<<" Buster straddle source: actorworld248/HP4 retained, native emission and in-flight checkpoint replay pass\n";
  }
  std::string error;check(m.restore(entry_state,error),error.c_str());
 }
 if(title==4&&dx){
  auto word=[&](uint16_t address){auto b=m.debug_read(address,2);return unsigned(b[0])|(unsigned(b[1])<<8);};
  auto until=[&](unsigned pc,unsigned budget){while((m.stats().pc!=pc||m.stats().rom_bank!=6)&&budget--)m.debug_step_instruction();check(m.stats().pc==pc&&m.stats().rom_bank==6,"original charged-shot control did not reach its recoil gate");};
  std::vector<uint8_t> projectile;
  for(unsigned disabled:{0u,1u}){
   if(disabled){std::string error;check(m.restore(recoil_off_title,error),error.c_str());frames(1,4);frames(3);frames(1,4);frames(3);check(peek(m,0xdf46)==1,"IV recoil menu could not return to Start");frames(1,128);frames(180);for(unsigned i=0;i<88;++i){frames(1,16);frames(60);}frames(8);check(peek(m,0xd702)==1,"IV recoil OFF startup did not reach gameplay");}
   poke(m,0xdeae,152);poke(m,0xd73a,0);
   // Original body collision rejects contact during mercy, while values below
   // 5A preserve the actual charged-shot controls. Never set a shot/result flag.
   for(unsigned i=0;i<6;++i){poke(m,0xd73b,64);frames(20,32);}check(peek(m,0xdeae)==152,"IV charged-shot fixture took unrelated damage");m.set_buttons(0);until(0x5b5e,2000000);
   check(peek(m,0xd74c)==1,"IV charged-shot fixture did not take its native vertical impulse path");unsigned x=word(0xd72c),y=word(0xd72a),speed=peek(m,0xd733);auto shot=m.debug_read(0xc200,256);if(disabled)check(shot==projectile,"IV recoil option changed the actual native projectile");else projectile=shot;
   until(0x5b81,100000);check(((word(0xd72c)-x)&65535)==(disabled?0:65408)&&((word(0xd72a)-y)&65535)==(disabled?0:65280)&&uint8_t(peek(m,0xd733)-speed)==(disabled?0:252),"IV recoil option changed the wrong charged-shot impulse");check(!peek(m,0xd74c),"IV recoil option skipped native shot cleanup");
  }
  std::string error;check(m.restore(entry_state,error),error.c_str());std::cout<<"actual IV title option/checkpoint and protected native charged-shot recoil ON/OFF pass; original projectile identical\n";
 }
 {
 auto room_fixture=m.debug_read(0xc000,8192);auto physical_options=options;for(auto& f:physical_options.mods)f.smooth_gameplay=false;
 Machine physical(rom,physical_options);physical.debug_write(0xc000,room_fixture);auto& m=physical;auto query_state=m.snapshot();unsigned native_aliases=0,queries=0;
 auto source=[&](unsigned bank,unsigned address){size_t at=address<0x4000?address:size_t(bank)*0x4000+address-0x4000;check(at<rom.size(),"collision source address outside original cartridge");return rom[at];};
 auto word=[&](uint16_t address){auto bytes=m.debug_read(address,2);return unsigned(bytes[0])|(unsigned(bytes[1])<<8);};
 for(unsigned x:{200u,208u,248u,264u})for(unsigned y:{16u,32u,48u,64u}){
  unsigned expected=0;
  for(unsigned width:{160u,256u}){
   std::string restore_error;check(m.restore(query_state,restore_error),restore_error.c_str());m.mod_viewport(width,144);
   poke(m,0xd711,0);poke(m,0xd712,0);poke(m,0xc68a,uint8_t(x));poke(m,0xc68b,uint8_t(x>>8));poke(m,0xc68d,uint8_t(x));
   poke(m,title==3?0xcc74:0xcc6a,0);poke(m,title==3?0xffb1:0xffaa,0);
   unsigned bank=title==3?9:3;poke(m,title==3?0xde93:0xdf00,uint8_t(bank));poke(m,0x2100,uint8_t(bank));
   unsigned stage_bank=peek(m,title==3?0xde94:0xdf01),row=word(uint16_t((title==3?0xd7a9:0xd7ab)+(y/16)*2));expected=source(stage_bank,row+x/16);
   m.debug_registers(Registers{uint16_t(y<<8),0x0080,uint16_t((y<<8)|uint8_t(x)),0xc680,0xfffe,uint16_t(title==3?0x5cdd:0x6103),0});m.debug_wake();
   unsigned limit=10000,done=title==3?0x0c19:0x0e6d;
   while(m.stats().pc!=done&&limit--)m.debug_step_instruction();
   check(m.stats().pc==done,"original actor tile service did not return");unsigned actual=m.debug_registers().af>>8;
   if(width==256){check(actual==expected,"far-margin original tile service queried an aliased world column");++queries;}
   else native_aliases+=actual!=expected;
  }
 }
 check(native_aliases>0,"far-margin fixture did not distinguish the original alias");std::cout<<"original far-world tile queries="<<queries<<" exact; native160 aliases="<<native_aliases<<"\n";
 unsigned stores=0,store_aliases=0;
 for(unsigned x:{200u,208u,248u,264u})for(int movement:{-1,0,1})for(unsigned width:{160u,256u}){
  std::string restore_error;check(m.restore(query_state,restore_error),restore_error.c_str());m.mod_viewport(width,144);
  poke(m,0xd711,0);poke(m,0xd712,0);poke(m,0xc68a,uint8_t(x));poke(m,0xc68b,uint8_t(x>>8));poke(m,0xc68d,uint8_t(int(x)+movement));
  m.debug_registers(Registers{0,0x0080,0,0xc680,0xfffe,uint16_t(title==3?0x2d1e:0x2e7a),0});m.debug_wake();unsigned limit=1000,done=title==3?0x2d39:0x2e95;
  while(m.stats().pc!=done&&limit--)m.debug_step_instruction();
  check(m.stats().pc==done,"original actor world writeback did not return");unsigned actual=word(0xc68a),expected=unsigned(int(x)+movement);
  if(width==256){check(actual==expected,"original actor world writeback lost its wide page");++stores;}else store_aliases+=actual!=expected;
 }
 check(store_aliases>0,"world writeback fixture did not distinguish the original page loss");std::cout<<"original far-world stores="<<stores<<" exact; native160 aliases="<<store_aliases<<"\n";
 {std::string restore_error;check(m.restore(query_state,restore_error),restore_error.c_str());m.take_audio();}
 }
 auto word=[&](uint16_t address){auto bytes=m.debug_read(address,2);return unsigned(bytes[0])|(unsigned(bytes[1])<<8);};
 auto prior_trace=m.take_trace();
 if(title==4){poke(m,0xdf0a,7);poke(m,0xde9a,0);poke(m,0xdeae,152);m.debug_registers(Registers{0,0,0,0,0xfffe,0x0a01,0});m.debug_wake();frames(180);check(peek(m,0xd702)==1,"original far-actor stage loader did not reach gameplay");}
 {auto loading_trace=m.take_trace();prior_trace.insert(prior_trace.end(),loading_trace.begin(),loading_trace.end());}
 auto near_actor_state=m.snapshot();
 uint16_t actor=title==3?0xc680:0xc640;check(peek(m,actor+0x10)<0x40,"far actor fixture selected an inactive native object");
 m.mod_viewport(320,144);poke(m,actor+0x0a,8);poke(m,actor+0x0b,1);poke(m,actor+0x0d,8);
 for(unsigned i=0;i<6;++i){frames(1);auto x=word(actor+0x0a);check(x>=208&&x<320,"actual original far actor controller lost its world page");check(peek(m,actor+0x10)<0x40,"actual original far actor lost its native sprite allocation");}
 auto actor_trace=m.take_trace();check(std::find(actor_trace.begin(),actor_trace.end(),uint32_t(title==3?0x2d2b:0x2e87))!=actor_trace.end(),"actual far actor did not execute original world writeback");prior_trace.insert(prior_trace.end(),actor_trace.begin(),actor_trace.end());
 ppm(out/"far-actor.ppm",m.mod_view());write_file_atomic(out/"far-actor.state",m.snapshot());std::cout<<"actual original controller retained allocated far actor world="<<word(actor+0x0a)<<" over6 physical frames\n";
 auto far_actor_state=m.snapshot();
 if(title==4)for(unsigned x:{52u,308u}){
  std::string error;check(m.restore(far_actor_state,error),error.c_str());poke(m,0xd70d,36);poke(m,0xd70e,0);poke(m,0xd723,36);poke(m,actor+0x0a,uint8_t(x));poke(m,actor+0x0b,uint8_t(x>>8));poke(m,actor+0x0d,uint8_t(x));poke(m,actor+0x0f,107);poke(m,0xdeae,152);poke(m,0xd73a,0);poke(m,0xd73b,64);
  check(peek(m,actor+0x18)==1,"IV actual shot fixture did not retain native untouched enemy HP1");frames(1,32);frames(5);check(peek(m,0xd702)==1&&peek(m,0xdeae)==152,"IV actual shot fixture left protected gameplay");check(peek(m,actor+0x18)==(x==52?0:1),x==52?"IV native B press/release failed to defeat a genuinely nearby actor":"IV native B shot damaged an actor on another world page");check(peek(m,0xc204)==(x==52?0:1),"IV native point shot was consumed by the wrong world page");ppm(out/(x==52?"shot-near.ppm":"shot-far.ppm"),m.mod_view());std::cout<<"actual IV native B press/release targetworld="<<x<<" untouched HP1 ->"<<unsigned(peek(m,actor+0x18))<<"; active point list="<<unsigned(peek(m,0xc204))<<"\n";
 }
 for(unsigned x:{292u,36u}){
  std::string restore_error;check(m.restore(far_actor_state,restore_error),restore_error.c_str());poke(m,0xd70d,36);poke(m,0xd70e,0);poke(m,title==3?0xd721:0xd723,36);
  poke(m,actor+0x0a,uint8_t(x));poke(m,actor+0x0b,uint8_t(x>>8));poke(m,actor+0x0d,uint8_t(x));poke(m,actor+0x0f,107);poke(m,title==3?0xde9c:0xdeae,152);frames(4);
  auto hp=peek(m,title==3?0xde9c:0xdeae);check(x==292?hp==152:hp<152,x==292?"far native actor damaged the player's wrapped screen position":"genuine near native actor contact stopped causing damage");
  ppm(out/(x==292?"contact-far.ppm":"contact-near.ppm"),m.mod_view());std::cout<<"actual body contact actorworld="<<x<<" playerworld36 health="<<unsigned(hp)<<"\n";
 }
 if(!dx){
  std::string restore_error;check(m.restore(near_actor_state,restore_error),restore_error.c_str());unsigned exact=0,source_review=0;
  for(unsigned frame=0;frame<20;++frame){frames(1,1);auto view=m.mod_view();auto native=m.pixels();int offset=view.camera_x-view.left;unsigned differences=0;
   for(unsigned y=0;y<144;++y)for(unsigned x=0;x<160;++x)differences+=native[y*160+x]!=view.pixels[y*view.width+unsigned(int(x)+offset)];
   if(view.uncapped_sprites){
    // The hardware image omits genuine source pieces when its allocator is
    // exhausted. Preserve current-build inputs for independent source/art/
    // priority qualification; raw hardware equality cannot decide this case.
    ppm(out/("source-view-"+std::to_string(frame)+".ppm"),view);m.write_frame(out/("source-native-"+std::to_string(frame)+".ppm"));write_file_atomic(out/("source-frame-"+std::to_string(frame)+".state"),m.snapshot());++source_review;
    std::cout<<"frame"<<frame<<" original allocation shortfall="<<view.uncapped_sprites<<" native pixel differences="<<differences<<"; retained for independent source-pose qualification\n";
   }else{if(differences){ppm(out/("phase-failure-"+std::to_string(frame)+".ppm"),view);m.write_frame(out/("phase-native-"+std::to_string(frame)+".ppm"));throw std::runtime_error("moving uncapped-free native center differs at frame"+std::to_string(frame)+" pixels="+std::to_string(differences));}++exact;}
  }
  std::cout<<"moving20 exact hardware frames="<<exact<<"; uncapped frames requiring independent source/art/priority proof="<<source_review<<"\n";
 }

 if(title==4)for(unsigned stage:{14u,18u,21u}){
  std::string restore_error;check(m.restore(entry_state,restore_error),restore_error.c_str());poke(m,0xdf0a,uint8_t(stage));poke(m,0xde9a,0);poke(m,0xdeae,152);m.debug_registers(Registers{0,0,0,0,0xfffe,0x0a01,0});m.debug_wake();frames(180);
  auto view=m.mod_view();check(view.width==256&&view.expanded,"original custom-raster live room did not expand");ppm(out/("raster-stage"+std::to_string(stage)+".ppm"),view);
  if(!dx){unsigned differences=0;int offset=view.camera_x-view.left;auto native=m.pixels();for(unsigned y=0;y<30;++y)for(unsigned x=0;x<80;++x)differences+=native[y*160+x]!=view.pixels[y*view.width+unsigned(int(x)+offset)];check(!differences,"original custom-raster background differs in its native band");std::cout<<"custom raster stage="<<stage<<" native background0/2400 pixel differences\n";}
 }
 if(title==4&&!dx&&!smooth){
  // The ordinary native room supplies kind13/HP10 at world248. Its rectangle
  // spans229..267 although its origin still has an ordinary page-zero X.
  // No enemy, projectile or player position is edited in this regression.
  std::string error;check(m.restore(entry_state,error),error.c_str());m.mod_viewport(256,144);poke(m,0xdf0a,19);poke(m,0xde9a,0);poke(m,0xdeae,152);poke(m,0xdeac,255);poke(m,0xdead,15);poke(m,0xdf00,1);poke(m,0x2100,1);m.debug_registers(Registers{0,0,0,0,0xfffe,0x0a01,1});m.debug_wake();
  unsigned budget=2000000;while(m.stats().pc!=0x352d&&budget--)m.debug_step_instruction();check(m.stats().pc==0x352d,"IV natural point regression did not reach its original room loader");poke(m,0xd7bb,0xd3);poke(m,0xd7bc,0x59);frames(180);check(peek(m,0xc687)==0x13&&peek(m,0xc698)==10,"IV natural point regression lost the original kind13/HP10 actor");
  auto protect=[&]{poke(m,0xdeae,152);poke(m,0xd73a,0);poke(m,0xd73b,64);};for(uint8_t keys:{uint8_t(1),uint8_t(2),uint8_t(16)}){protect();frames(24,keys);}
  auto before_shot=m.snapshot();protect();m.set_buttons(32);budget=2000000;while((m.stats().pc!=0x4709||m.stats().rom_bank!=6)&&budget--)m.debug_step_instruction();check(m.stats().pc==0x4709&&m.stats().rom_bank==6,"IV natural B press did not reach the original weak-shot movement");check(peek(m,0xc698)==10,"IV natural Buster damaged a distant actor before its first movement");m.take_audio();auto flight=m.snapshot();
  auto finish=[&]{protect();frames(12,32);protect();frames(30);check(peek(m,0xc698)==10&&peek(m,0xdeae)==152,"IV natural leftward Buster hit the enemy box on the opposite world edge");auto v=m.mod_view();return std::pair{m.snapshot(),std::vector<uint32_t>(v.pixels.begin(),v.pixels.end())};};auto expected=finish();ppm(out/"natural-left-buster.ppm",m.mod_view());check(m.restore(flight,error),error.c_str());check(finish()==expected,"IV actual in-flight Buster source did not checkpoint/replay");
  check(m.restore(before_shot,error),error.c_str());protect();frames(12);protect();frames(30);check(peek(m,0xc698)==10,"IV matched no-fire control changed the distant enemy health");std::cout<<"actual IV natural left Buster: source room59D3 kind13 HP10 retained across wrapped box; native movement checkpoint/replay and no-fire control pass\n";
 }
 {std::string restore_error;check(m.restore(entry_state,restore_error),restore_error.c_str());m.take_audio();}
 uint16_t weapon=title==3?0xde99:0xdeab;unsigned count=title==3?12:13;
 // Availability and empty energy are explicit fixture prerequisites. Original
 // cleanup, selection and graphics reload produce every result below.
 poke(m,weapon+1,255);poke(m,weapon+2,title==3?15:31);
 for(unsigned i=1;i<count;++i)poke(m,weapon+3+uint16_t(i),0);
 auto energy=m.debug_read(weapon+4,count-1),owned=m.debug_read(weapon+1,2);
 auto press=[&](uint8_t pad){frames(8,0,pad);frames(4);};
 unsigned selected=peek(m,weapon);check(selected<count,"invalid original starting equipment");
 for(unsigned i=0;i<count;++i){selected=(selected+1)%count;press(virtual_r);check(peek(m,weapon)==selected,"native next weapon differs");check(m.debug_read(weapon+4,count-1)==energy,"selection changed energy");check(m.debug_read(weapon+1,2)==owned,"selection changed ownership");}
 frames(8,0,virtual_r);selected=(selected+1)%count;check(peek(m,weapon)==selected,"fresh held shoulder failed");frames(30,0,virtual_r);check(peek(m,weapon)==selected,"held shoulder repeated");frames(4);press(3);check(peek(m,weapon)==selected,"opposed shoulders changed selection");
 press(virtual_l);selected=(selected+count-1)%count;check(peek(m,weapon)==selected,"native previous weapon differs");
 poke(m,weapon+1,5);poke(m,weapon+2,0);for(unsigned i=0;i<3;++i){press(virtual_r);selected=selected==0?2:0;check(peek(m,weapon)==selected,"locked ownership skip/wrap differs");}
 frames(2,128);frames(90);check(peek(m,title==3?0xde59:0xde5a)==(title==3?1:0),"original pause menu did not open");m.write_frame(out/"original-menu.ppm");
 if(dx){auto before=m.snapshot();auto menu=m.mod_view();check(menu.width==256&&menu.expanded,"DX pause lost the adaptive canvas");bool colored=false;for(auto p:menu.pixels)colored|=((p>>16)&255)!=((p>>8)&255)||((p>>8)&255)!=(p&255);check(colored,"authored NES menu did not produce color");ppm(out/"dx-menu.ppm",menu);check(before==m.snapshot(),"native menu presentation changed simulation state");}
 frames(8,0,virtual_r);frames(2,128,virtual_r);frames(90,0,virtual_r);check(peek(m,weapon)==selected,"menu shoulder request leaked into resumed gameplay");frames(4);
 auto checkpoint=m.snapshot();auto replay=[&]{frames(7,1,virtual_r);frames(9,16);auto view=m.mod_view();auto pixels=std::vector<uint32_t>(view.pixels.begin(),view.pixels.end());return std::pair{m.snapshot(),pixels};};auto expected=replay();std::string error;check(m.restore(checkpoint,error),error.c_str());check(replay()==expected,"state/view replay differs");
 auto trace=m.take_trace();prior_trace.insert(prior_trace.end(),trace.begin(),trace.end());write_file_atomic(out/"native.trace",{reinterpret_cast<const uint8_t*>(prior_trace.data()),prior_trace.size()*sizeof(uint32_t)});m.trace_enabled(false);
 // Same current native module/profile, decoder CPU. Bounded actual gameplay,
 // including frame pixels and PCM, rather than an attract-only comparison.
 options.native=false;Machine ref(rom,options);ref.mod_viewport(1280,720);check(ref.restore(m.snapshot(),error),error.c_str());
 for(unsigned i=0;i<60;++i){LogicalInput input{uint8_t(i<20?1:0),uint8_t(i==2?virtual_r:0)};m.run_frame(input);ref.run_frame(input);check(m.snapshot()==ref.snapshot(),"generated/decoder simulation differs");check(m.take_audio()==ref.take_audio(),"generated/decoder PCM differs");auto a=m.mod_view(),b=ref.mod_view();check(a.width==b.width&&std::equal(a.pixels.begin(),a.pixels.end(),b.pixels.begin(),b.pixels.end()),"generated/decoder view differs");}
 check(sha256(read_file(argv[1]))==hash,"original cartridge changed");ppm(out/"qualified.ppm",m.mod_view());write_file_atomic(out/"qualified.state",m.snapshot());
 std::cout<<"PASS bounded stage native weapon cycling, empty-energy ownership, held/opposed edges, locked skips, replay and generated/decoder state/PCM/view; no battery exported\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
