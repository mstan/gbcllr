#include <gbcllr/machine.hpp>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <set>
#include <stdexcept>
#include <tuple>
using namespace gbcllr;
namespace {
void check(bool okay,const char* message){if(!okay)throw std::runtime_error(message);}
void poke(Machine& m,unsigned address,unsigned value){uint8_t v=uint8_t(value);m.debug_write(uint16_t(address),{&v,1});}
unsigned byte(Machine& m,unsigned address){return m.debug_read(uint16_t(address),1)[0];}
void frames(Machine& m,unsigned count,uint8_t buttons=0,uint8_t shoulders=0){for(unsigned n=0;n<count;++n){m.run_frame(LogicalInput{buttons,shoulders});m.take_audio();}}
void capture(Machine& m,const std::filesystem::path& path){auto v=m.mod_view();std::ofstream f(path,std::ios::binary);f<<"P6\n"<<v.width<<" "<<v.height<<"\n255\n";for(auto p:v.pixels){f.put(char(p>>16));f.put(char(p>>8));f.put(char(p));}check(bool(f),"Capture failed");}
void fixed_dx_scenes(Machine& m,const std::filesystem::path& out){
 auto boot=m.snapshot();std::string error;frames(m,1200);capture(m,out/"dx-intro.ppm");frames(m,2,128);frames(m,120);
 auto view=m.mod_view();check(view.width==160&&!view.expanded,"Fixed DX title lost its original aspect");std::set<uint32_t> colors(view.pixels.begin(),view.pixels.end());check(colors.size()>4,"Original-input DX title has no authored color presentation");
 capture(m,out/"dx-title.ppm");m.write_frame(out/"dx-title-native.ppm");write_file_atomic(out/"dx-title.wram",m.debug_read(0xc000,8192));write_file_atomic(out/"dx-title.vram",m.debug_read(0x8000,8192));
 auto point=m.snapshot();auto replay=[&]{for(unsigned n=0;n<30;++n)m.run_frame(0);auto pcm=m.take_audio();auto v=m.mod_view();return std::pair{pcm,std::vector<uint32_t>(v.pixels.begin(),v.pixels.end())};};auto expected=replay();auto end=m.snapshot();check(m.restore(point,error),error.c_str());check(replay()==expected&&m.snapshot()==end,"Fixed title state/PCM/raster continuation differs");
 check(m.restore(point,error),error.c_str());frames(m,2,128);frames(m,120);frames(m,2,16);frames(m,240);for(unsigned n=0;n<5;++n){frames(m,2,16);frames(m,60);}frames(m,30);
 check(byte(m,0xdf0a)==16&&byte(m,0xdf01)==0x16,"Original-input Light lab was not reached");view=m.mod_view();auto physical=m.pixels();int origin=(int(view.width)-160)/2;unsigned ink=0,differences=0;
 for(unsigned y:{96u,112u})for(unsigned dy=0;dy<8;++dy)for(unsigned x=16;x<144;++x){bool native_ink=physical[(y+dy)*160+x]==0xff000000,view_ink=view.pixels[(y+dy)*view.width+unsigned(origin+int(x))]==0xff000000;ink+=native_ink;differences+=native_ink!=view_ink;}
 capture(m,out/"dx-lab-dialogue.ppm");m.write_frame(out/"dx-lab-dialogue-native.ppm");write_file_atomic(out/"dx-lab-dialogue.wram",m.debug_read(0xc000,8192));write_file_atomic(out/"dx-lab-dialogue.vram",m.debug_read(0x8000,8192));
 check(ink>32&&!differences,"Native dynamic lab dialogue was lost from the completed view");point=m.snapshot();expected=replay();end=m.snapshot();check(m.restore(point,error),error.c_str());check(replay()==expected&&m.snapshot()==end,"Lab dialogue state/PCM/raster continuation differs");check(m.restore(boot,error),error.c_str());
 std::cout<<"original-input DX title authored colors and current-build state/PCM/scanline replay pass\n";
 std::cout<<"original-input lab dialogue retains two native glyph rows and state/PCM/raster replay\n";
}
void ending_scenes(Machine& m,const std::filesystem::path& out,bool dx){
 auto start=m.snapshot();std::string error;auto normal=[&]{return m.stats().pc==0x09ef&&m.debug_registers().sp==0xfffe&&!byte(m,0xdd2b);};unsigned remaining=2000000;while(remaining&&!normal()){--remaining;m.debug_step_instruction();}check(normal(),"The native ending-art boundary was not reached");const auto boundary=m.snapshot();std::vector<uint8_t> credits;
 auto enter=[&](unsigned stage,bool wait){poke(m,0xdf0a,stage);poke(m,0xde8a,0);poke(m,0xde9e,152);poke(m,0x2100,1);poke(m,0xdf00,1);m.debug_registers({0,0,0,0,0xfffe,0x094c,1});m.debug_wake();if(wait){unsigned left=2000000;while(left&&!normal()){--left;m.debug_step_instruction();}check(normal(),"The native credits graphics prerequisite did not load");}};
 // Native entries are explicit art prerequisites, not campaign results.
 //28/29/31 are custom scripts; they do not return to the stage main loop.
 //The final thanks is reached by native credits continuation, retaining stage30.
 for(unsigned stage:{28u,29u,30u,31u}){check(m.restore(stage==31?credits:boundary,error),error.c_str());if(stage!=31)enter(stage,stage==30);if(stage==31){unsigned seen=0,previous=0,stable=0;for(unsigned n=0;n<18000;++n){frames(m,1);if(n%30)continue;const auto ram=m.debug_read(0xc000,8192);unsigned card=ram[0xb81];unsigned prefix=ram[0xbc0]|(unsigned(ram[0xbc1])<<8)|(unsigned(ram[0xbc2])<<16)|(unsigned(ram[0xbc3])<<24);bool pose=card==1?prefix==0x1000f3ea:card==6?prefix==0x1000f7e2:card==10?prefix==0x1000f1ea:false;bool active=pose&&ram[0xb80]&&!ram[0xb40]&&!ram[0xb52]&&!ram[0xb5b]&&!ram[0xb64]&&ram[0x1e96]==0x1b;stable=active&&card==previous?stable+1:0;previous=card;if(active&&(card==1||card==6||card==10)&&stable>=1&&!(seen&(1u<<(card-1)))){seen|=1u<<(card-1);auto name="ending-cast"+std::to_string(card);auto pure=m.snapshot();capture(m,out/(name+".ppm"));check(m.snapshot()==pure,"A credits cast presentation changed simulation state");write_file_atomic(out/(name+".wram"),m.debug_read(0xc000,8192));write_file_atomic(out/(name+".vram"),m.debug_read(0x8000,8192));}}check(seen==545,"Native credits did not show the three representative cast services");}else frames(m,stage==30?900:300);check(byte(m,0xdf0a)==(stage==31?30:stage),"The native ending-art script changed before its sample");auto name="ending-stage"+std::to_string(stage);capture(m,out/(name+".ppm"));m.write_frame(out/(name+"-physical.ppm"));write_file_atomic(out/(name+".state"),m.snapshot());write_file_atomic(out/(name+".wram"),m.debug_read(0xc000,8192));write_file_atomic(out/(name+".vram"),m.debug_read(0x8000,8192));write_file_atomic(out/(name+".ppu"),m.debug_read(0xff40,12));
  auto view=m.mod_view();check(view.width==160&&!view.expanded,"A scripted ending lost its original camera");if(dx){bool colored=std::any_of(view.pixels.begin(),view.pixels.end(),[](uint32_t p){return ((p>>16)&255)!=((p>>8)&255)||((p>>8)&255)!=(p&255);});check(colored,"A scripted authored ending remained grayscale");}else check(std::equal(view.pixels.begin(),view.pixels.end(),m.pixels().begin()),"A native scripted ending differs from the physical raster");
if(stage==30)credits=m.snapshot();auto point=m.snapshot();m.take_audio();auto run=[&]{for(unsigned n=0;n<30;++n)m.run_frame(0);auto pcm=m.take_audio();auto v=m.mod_view();return std::pair{pcm,std::vector<uint32_t>(v.pixels.begin(),v.pixels.end())};};auto expected=run();auto end=m.snapshot();check(m.restore(point,error),error.c_str());check(run()==expected&&m.snapshot()==end,"Scripted ending state/PCM/view replay differs");
 }
 check(m.restore(start,error),error.c_str());std::cout<<"native scripted ending entries28/29/30 and natural final thanks retain fixed160 cameras, authored color/native raster and state/PCM/view replay; entry prerequisites are explicit\n";
}
void sunstar_background(Machine& m,const std::filesystem::path& out){
 auto start=m.snapshot();std::string error;auto normal=[&]{return m.stats().pc==0x09ef&&m.debug_registers().sp==0xfffe&&!byte(m,0xdd2b);};auto until=[&]{unsigned left=2000000;while(left&&!normal()){--left;m.debug_step_instruction();}check(normal(),"The native final-room art entry boundary was not reached");};until();
 // Native stage11 entry is an art prerequisite, not a final-fight result.
 poke(m,0xdf0a,11);poke(m,0xde8a,0);poke(m,0xde9e,152);poke(m,0x2100,1);poke(m,0xdf00,1);m.debug_registers({0,0,0,0,0xfffe,0x094c,1});m.debug_wake();until();for(unsigned n=0;n<10;++n){poke(m,0xde9e,152);frames(m,60);}auto point=m.snapshot();capture(m,out/"sunstar-bg-bank1.ppm");check(m.snapshot()==point,"A shared-bank background presentation changed simulation state");write_file_atomic(out/"sunstar-bg-bank1.state",point);write_file_atomic(out/"sunstar-bg-bank1.wram",m.debug_read(0xc000,8192));m.take_audio();auto run=[&]{for(unsigned n=0;n<40;++n)m.run_frame(0);auto pcm=m.take_audio();auto view=m.mod_view();return std::pair{pcm,std::vector<uint32_t>(view.pixels.begin(),view.pixels.end())};};auto expected=run();auto end=m.snapshot();check(m.restore(point,error),error.c_str());check(run()==expected&&m.snapshot()==end,"Shared-bank final-room state/PCM/view replay differs");check(m.restore(start,error),error.c_str());std::cout<<"source-qualified final-room background preserves native state and PCM/view replay; native entry prerequisite explicit\n";
}
void selector_scene(Machine& m,const std::filesystem::path& out,std::span<const uint8_t> rom){
 auto start=m.snapshot();std::string error;unsigned left=2000000;while(left&&(m.stats().pc!=0x09ef||m.debug_registers().sp!=0xfffe||byte(m,0xdd2b))){--left;m.debug_step_instruction();}check(left,"Selector setup discarded an active native interrupt");
 // UI entry/one completed-master progress are explicit art prerequisites,
 // corroborated by a retained genuine Neptune award. This is no boss result.
 poke(m,0xdf0a,4);poke(m,0xde9c,97);poke(m,0xde9d,0);poke(m,0xdf35,8);poke(m,0xdf36,0);poke(m,0xdf67,0);poke(m,0x2100,0x19);poke(m,0xdf00,0x19);m.debug_registers({0,0,0,0,0xfffe,0x4010,1});m.debug_wake();frames(m,360);
 auto view=m.mod_view();check(view.width==160&&!view.expanded&&byte(m,0xcb00)==1,"The native selector did not retain its fixed camera/current cursor");auto point=m.snapshot();capture(m,out/"selector-owned-locked.ppm");check(m.snapshot()==point,"Selector view changed simulation state");write_file_atomic(out/"selector-owned-locked.state",point);write_file_atomic(out/"selector-owned-locked.wram",m.debug_read(0xc000,8192));write_file_atomic(out/"selector-owned-locked.vram",m.debug_read(0x8000,8192));write_file_atomic(out/"selector-owned-locked.ppu",m.debug_read(0xff40,12));unsigned updates=byte(m,0xde83);m.take_audio();auto run=[&]{for(unsigned n=0;n<40;++n)m.run_frame(0);auto pcm=m.take_audio();auto v=m.mod_view();return std::pair{pcm,std::vector<uint32_t>(v.pixels.begin(),v.pixels.end())};};auto expected=run();auto end=m.snapshot();check(byte(m,0xde83)==updates,"The selector admitted stale gameplay work");check(m.restore(point,error),error.c_str());check(run()==expected&&m.snapshot()==end,"Selector state/PCM/view replay differs");check(m.restore(point,error),error.c_str());
 frames(m,2,1);frames(m,120);check(byte(m,0xcb00)==2&&byte(m,0xde9c)==97&&byte(m,0xdf35)==8,"Native selector cursor changed progress or chose the wrong neighbor");capture(m,out/"selector-cursor.ppm");write_file_atomic(out/"selector-cursor.state",m.snapshot());write_file_atomic(out/"selector-cursor.wram",m.debug_read(0xc000,8192));write_file_atomic(out/"selector-cursor.vram",m.debug_read(0x8000,8192));write_file_atomic(out/"selector-cursor.ppu",m.debug_read(0xff40,12));unsigned chosen=rom[0x19*0x4000+0x8b1+byte(m,0xcb00)];frames(m,2,16);frames(m,600);check(byte(m,0xdf0a)==chosen&&byte(m,0xde9c)==97,"Native selector A did not enter its chosen original stage");write_file_atomic(out/"selector-native-selected.wram",m.debug_read(0xc000,8192));check(m.restore(start,error),error.c_str());std::cout<<"native selector fixed camera/available-cleared display, cursor/A-to-stage and pure current-state/PCM/view replay pass; UI/progress prerequisites explicit\n";
}
void terra_absorption(Machine& m,const std::filesystem::path& out){
 auto start=m.snapshot();std::string error;
 // A native selector UI from this build is the declared prior context. The
 // original presentation caller is entered directly: defeat and award are
 // bypassed. No ownership, completion, boss HP or result flag is supplied.
 check(m.restore(read_file(out/"selector-owned-locked.state"),error),error.c_str());
 auto until=[&](unsigned pc,unsigned bank){unsigned remaining=10000000;while(remaining&&(m.stats().pc!=pc||m.stats().rom_bank!=bank)){--remaining;m.debug_step_instruction();}if(!remaining){write_file_atomic(out/"terra-boundary-failed.state",m.snapshot());write_file_atomic(out/"terra-boundary-failed.wram",m.debug_read(0xc000,8192));throw std::runtime_error("Terra presentation expected="+std::to_string(bank)+":"+std::to_string(pc)+" actual="+std::to_string(m.stats().rom_bank)+":"+std::to_string(m.stats().pc));}};
 until(0x01c5,0);check(!byte(m,0xdd2b),"Terra presentation discarded an active interrupt");auto owner=m.debug_read(0xde9c,2),progress=m.debug_read(0xdf35,2);
 poke(m,0xdf0a,10);poke(m,0xdf0e,10);m.set_buttons(0);m.debug_registers({0,0,0,0,0xfffe,0x2094,1});m.debug_wake();
 // Original2094 cleans graphics, CALLs1B:401C/62E0 and returns at20A0.
 auto sample=[&](unsigned pc){until(pc,0x1b);frames(m,2);auto name="terra-absorption-"+std::to_string(pc);auto point=m.snapshot();auto view=m.mod_view();check(view.width==160&&!view.expanded,"Terra presentation retained stale world admission");capture(m,out/(name+".ppm"));check(m.snapshot()==point,"An absorption presentation changed simulation state");write_file_atomic(out/(name+".state"),point);write_file_atomic(out/(name+".wram"),m.debug_read(0xc000,8192));write_file_atomic(out/(name+".vram"),m.debug_read(0x8000,8192));write_file_atomic(out/(name+".ppu"),m.debug_read(0xff40,12));};
 sample(0x6397);auto point=m.snapshot();m.take_audio();auto run=[&]{std::vector<int16_t> pcm;for(unsigned n=0;n<40;++n){m.run_frame(uint8_t(n<2?16:0));auto block=m.take_audio();pcm.insert(pcm.end(),block.begin(),block.end());if(n==1)check(byte(m,0xde91)==1,"Terra presentation did not poll originalA input");}auto view=m.mod_view();return std::pair{pcm,std::vector<uint32_t>(view.pixels.begin(),view.pixels.end())};};auto expected=run();auto end=m.snapshot();check(m.restore(point,error),error.c_str());check(run()==expected&&m.snapshot()==end,"Terra presentation state/PCM/view replay differs");
 frames(m,120);sample(0x63bb);until(0x20a0,0);check(byte(m,0xdf0a)==10&&m.debug_read(0xde9c,2)==owner&&m.debug_read(0xdf35,2)==progress,"Terra presentation changed supplied progress before its original return");write_file_atomic(out/"terra-native-return.state",m.snapshot());write_file_atomic(out/"terra-native-return.wram",m.debug_read(0xc000,8192));check(m.restore(start,error),error.c_str());std::cout<<"original Terra presentation caller2094/62E0 flash/restore, nativeA input, pure state/PCM/view replay and natural20A0 return pass; defeat/award bypassed, no result flags supplied\n";
}
void salt_water(Machine& m,const std::filesystem::path& out){
 auto original=m.snapshot();std::string error;auto until=[&](unsigned pc,unsigned bank){unsigned remaining=2000000;while(remaining&&(m.stats().pc!=pc||m.stats().rom_bank!=bank)){--remaining;m.debug_step_instruction();}check(remaining,"The native Salt Water producer did not reach its source boundary");};until(0x09ef,0);check(m.debug_registers().sp==0xfffe&&!byte(m,0xdd2b),"Salt Water setup discarded an active interrupt");
 // Availability, full energy and original pause cursor are selection prerequisites.
 // The original initializer, producer, terrain contact and children execute normally.
 poke(m,0xde9c,byte(m,0xde9c)|32);poke(m,0xdea3,152);poke(m,0xde9e,152);frames(m,2,128);frames(m,90);poke(m,0xd1ed,5);frames(m,2,128);frames(m,90);check(byte(m,0xde9b)==5&&byte(m,0xdea3)==152,"Original Salt Water selection/energy failed");auto ready=m.snapshot();m.set_buttons(32);until(0x6133,6);check(byte(m,0xc23b)&&!byte(m,0xc250)&&byte(m,0xdea3)==136,"Original Salt Water spawn or energy cost differs");write_file_atomic(out/"salt-water-spawn.state",m.snapshot());frames(m,20,32);capture(m,out/"salt-water-flight.ppm");write_file_atomic(out/"salt-water-flight.wram",m.debug_read(0xc000,8192));write_file_atomic(out/"salt-water-flight.vram",m.debug_read(0x8000,8192));until(0x634d,6);until(0x63fa,6);bool child=false;for(unsigned base=0xc23b;base<=0xc29b;base+=32)child|=byte(m,base)&&byte(m,base+0x15)==1;check(child,"Salt Water terrain contact did not produce its native secondary effect");frames(m,20);capture(m,out/"salt-water-split.ppm");write_file_atomic(out/"salt-water-split.wram",m.debug_read(0xc000,8192));write_file_atomic(out/"salt-water-split.vram",m.debug_read(0x8000,8192));auto point=m.snapshot();m.take_audio();auto run=[&]{for(unsigned n=0;n<60;++n)m.run_frame(0);auto audio=m.take_audio();auto v=m.mod_view();return std::pair{audio,std::vector<uint32_t>(v.pixels.begin(),v.pixels.end())};};auto expected=run();auto end=m.snapshot();check(m.restore(point,error),error.c_str());check(run()==expected&&m.snapshot()==end,"Salt Water trajectory/secondary state/PCM/view replay differs");check(m.restore(ready,error),error.c_str());frames(m,160);for(unsigned base=0xc23b;base<=0xc29b;base+=32)check(!byte(m,base),"Salt Water no-fire control produced an effect");check(byte(m,0xdea3)==152,"Salt Water no-fire control spent energy");check(m.restore(original,error),error.c_str());std::cout<<"actual original Salt Water selection, producer, terrain split/secondary, unfired control and state/PCM/view replay pass\n";
}
void first_stage(Machine& m){
 frames(m,1200);frames(m,2,128);frames(m,120);frames(m,2,128);frames(m,120);frames(m,2,16);frames(m,240);
 for(unsigned n=0;n<30&&byte(m,0xdf01)!=0x11;++n){frames(m,2,16);frames(m,60);}
 for(unsigned n=0;n<30&&(byte(m,0xdf01)!=0x11||(byte(m,0xdeaf)&8));++n){poke(m,0xde9e,152);frames(m,2,16);frames(m,240);}
 check(byte(m,0xdf0a)==1&&byte(m,0xdf01)==0x11&&!(byte(m,0xdeaf)&8),"Original first stage did not finish its native input-lock introduction");
 for(unsigned n=0;n<600;++n){poke(m,0xde9e,0x98);frames(m,1);if(m.mod_view().sprites)break;}
}
void flicker_control(std::span<const uint8_t> rom,Options options){
 options.mods[0].view_width=160;options.mods[0].no_flicker=false;Machine original(rom,options);first_stage(original);
 for(unsigned n=0;n<20;++n){frames(original,1,n<10?32:0);auto view=original.mod_view();check(view.width==160&&!view.expanded&&std::equal(view.pixels.begin(),view.pixels.end(),original.pixels().begin()),"Native160 no-flicker opt-out changed the physical raster");}
 std::cout<<"Native160 no-flicker opt-out retains original physical raster through twenty fire/release frames\n";
}
void moving_center(Machine& m,const std::filesystem::path& out){
 auto point=m.snapshot();m.mod_viewport(256,144);poke(m,0xc68a,120);poke(m,0xc68b,0);poke(m,0xc68d,120);frames(m,4);std::ofstream log(out/"moving-center.jsonl");unsigned differences=0;
 for(unsigned frame=0;frame<20;++frame){frames(m,1,1);auto view=m.mod_view();auto native=m.pixels();int origin=view.camera_x-view.left;unsigned hero=0,enemy=0;
  auto compare=[&](unsigned x0,unsigned x1,unsigned y0,unsigned y1){unsigned count=0;for(unsigned y=y0;y<y1;++y)for(unsigned x=x0;x<x1;++x){int hx=int(x)+origin;if(hx>=0&&hx<int(view.width)&&view.pixels[y*view.width+unsigned(hx)]!=native[y*160+x])++count;}return count;};
  hero=compare(24,112,72,126);enemy=compare(90,160,16,72);log<<"{\"frame\":"<<frame<<",\"hero_differences\":"<<hero<<",\"enemy_differences\":"<<enemy<<"}\n";differences+=hero+enemy;
  if(hero||enemy){capture(m,out/("moving-difference-"+std::to_string(frame)+".ppm"));m.write_frame(out/("moving-native-"+std::to_string(frame)+".ppm"));}
 }
 std::string error;check(m.restore(point,error),error.c_str());check(!differences,"Moving canonical hero/enemy display phase differs from native DMA raster");std::cout<<"twenty Rightframes: moving hero and canonical enemy match native DMA raster\n";
}
void missed_update_pulse(Machine& m){
 auto point=m.snapshot();std::vector<uint8_t> interval;unsigned wait=0;
 for(;wait<360;++wait){bool eligible=byte(m,0xd702)==1&&!(byte(m,0xdeaf)&0xfb)&&!byte(m,0xd81c)&&!byte(m,0xde2c)&&byte(m,0xde9e);auto before=m.snapshot();unsigned update=byte(m,0xde83);frames(m,1,32);if(eligible&&byte(m,0xde83)==update){interval=std::move(before);break;}}
 std::string error;if(!interval.empty()){check(m.restore(interval,error),error.c_str());unsigned weapon=byte(m,0xde9b);frames(m,1,32,2);frames(m,12);check(byte(m,0xde9b)==(weapon+1)%13,"A one-frame shoulder pulse was lost across a native repeated frame");std::cout<<"one-frame shoulder pulse across actual zero-update interval atframe="<<wait<<" retained\n";}else std::cout<<"zero-update shoulder interval not encountered in360 sampled frames\n";
 check(m.restore(point,error),error.c_str());
}
void dx_hurt_flash(Machine& m,const std::filesystem::path& out){
 auto point=m.snapshot();std::string error;
 // Original160-pixel activation does not allocate the distant cannon at the
 // opening spawn. Walk into its real activation footprint before testing it. The first raised step needs a fresh Jump edge.
 for(unsigned n=0;n<240;++n){unsigned actor=byte(m,0xc680)|(byte(m,0xc681)<<8),camera=byte(m,0xd711)|(byte(m,0xd712)<<8),x=byte(m,0xc68a)|(byte(m,0xc68b)<<8);if(actor>=0x4000&&actor<0x8000&&int(x)-int(camera)<144)break;poke(m,0xde9e,152);frames(m,1,n%60<20?17:1);}
 unsigned actor=byte(m,0xc680)|(byte(m,0xc681)<<8);if(!(actor>=0x4000&&actor<0x8000)){write_file_atomic(out/"hurt-unallocated.state",m.snapshot());write_file_atomic(out/"hurt-unallocated.wram",m.debug_read(0xc000,8192));capture(m,out/"hurt-unallocated.ppm");}check(actor>=0x4000&&actor<0x8000,"Hurt fixture has no actual original cannon");
 auto active=m.snapshot();unsigned flags=byte(m,0xc694),camera=byte(m,0xd711)|(byte(m,0xd712)<<8);
 auto phase=[&](unsigned timer,bool immune,const char* name){check(m.restore(active,error),error.c_str());poke(m,0xc68a,(camera+120)&255);poke(m,0xc68b,(camera+120)>>8);poke(m,0xc68d,120);
  for(unsigned n=0;n<6;++n){poke(m,0xc694,immune?flags|4:flags&~4u);poke(m,0xc6a1,timer);frames(m,1);}
  check(bool(byte(m,0xc6a1)&2)==bool((timer-1)&2),"Original actor hurt-timer phase was not reached");auto count=m.mod_view().sprites;capture(m,out/name);return count;};
 unsigned visible=phase(5,false,"hurt-visible.ppm"),hidden=phase(7,false,"hurt-hidden.ppm"),immune=phase(7,true,"hurt-immune.ppm");
 std::cout<<"hurt counts visible="<<visible<<" hidden="<<hidden<<" immune="<<immune<<" actor="<<byte(m,0xc680)<<","<<byte(m,0xc681)<<"\n";check(visible==hidden+4&&immune==visible,"DX intentional native hurt blink or original immunity bypass differs");check(m.restore(point,error),error.c_str());
 std::cout<<"authored DX enemy hurt blink removes four real-producer pieces at native timer phase; immunity remains visible\n";
}
void collision_queries(Machine& m,std::span<const uint8_t> rom,const Options& options,const std::filesystem::path& out){
 const auto point=m.snapshot();std::string error;std::ofstream log(out/"collision-queries.jsonl");unsigned passed=0;
 auto word=[&](unsigned a){auto b=m.debug_read(uint16_t(a),2);return unsigned(b[0])|(unsigned(b[1])<<8);};
 auto query=[&](unsigned world_x,unsigned camera,unsigned y,unsigned width,unsigned caller,int motion=0){
  check(m.restore(point,error),error.c_str());m.mod_viewport(width,144);poke(m,0x2000,13);poke(m,0xdf00,13);
  poke(m,0xd711,camera&255);poke(m,0xd712,camera>>8);poke(m,0xc68a,world_x&255);poke(m,0xc68b,world_x>>8);poke(m,0xc68d,(world_x-camera+motion)&255);
  const std::array<uint8_t,6> stack{0x31,0x0e,uint8_t(caller),uint8_t(caller>>8),0x80,0xc6};m.debug_write(0xffa0,stack);
  m.debug_registers({0,0,uint16_t((y<<8)|((world_x-camera+motion)&255)),0xc680,0xffa0,0x0f32,0});m.debug_wake();
  unsigned limit=1000;while(m.stats().pc!=0x0e35&&limit--)m.debug_step_instruction();check(m.stats().pc==0x0e35,"Original collision query did not return");return byte(m,0xd74e);
 };
 // Execute the unchanged cartridge's query service with its real family
 // caller stack. This qualifies address lookup, separately from enemy motion.
 for(unsigned camera:{0u,128u})for(unsigned y=0;y<128;y+=16)for(unsigned x:{80u,160u,200u,208u,224u,248u,264u,320u}){
  unsigned world_x=camera?x==80?16:x==160?32:x==200?64:x==208?80:x==224?112:x==248?336:x==264?376:392:x;
  check(m.restore(point,error),error.c_str());unsigned row=word(0xd7eb+2*(y/16)),bank=byte(m,0xdf01);unsigned expected=rom[bank*0x4000+((row+world_x/16)&0x3fff)];
  unsigned actual=query(world_x,camera,y,512,0x49ec);log<<"{\"world_x\":"<<world_x<<",\"camera\":"<<camera<<",\"y\":"<<y<<",\"expected\":"<<expected<<",\"actual\":"<<actual<<"}\n";
  check(actual==expected,"Extended actor collision differs from the original room table");++passed;
 }
 for(int motion:{-1,1})for(unsigned x:{208u,248u,256u,264u}){
  check(m.restore(point,error),error.c_str());unsigned row=word(0xd7eb),bank=byte(m,0xdf01),expected=rom[bank*0x4000+((row+(x+motion)/16)&0x3fff)];
  check(query(x,0,0,512,0x49ec,motion)==expected,"A moving extended actor queried the wrong world page");++passed;
 }
 unsigned original=query(208,0,0,160,0x49ec),unclaimed=query(208,0,0,512,0x49ed);check(unclaimed==original,"An unrelated collision caller was changed");
 check(m.restore(point,error),error.c_str());std::cout<<"original actor collision query samples="<<passed<<"; left/right page lookup and unrelated caller isolation pass\n";
 // Stop the actual controller call on physical clocks; a CPU-headroom burst
 // can otherwise pass several instructions within a one-display-tick step.
 Options physical=options;physical.mods[0].smooth_gameplay=false;Machine controller(rom,physical);controller.trace_enabled(true);controller.mod_viewport(512,144);first_stage(controller);
 unsigned limit=200000;while(limit--){auto stats=controller.stats();auto r=controller.debug_registers();if(stats.pc==0x5236&&stats.rom_bank==0x1d&&r.bc==0x0180&&r.hl==0xc680)break;controller.debug_step_instruction();}
 check(limit&&controller.stats().pc==0x5236,"Original Mercury controller call was not reached");const auto entry=controller.snapshot();
 // Teleport only its position and resume the original registers/stack.
 for(unsigned x:{200u,208u,248u,264u,320u}){
  check(controller.restore(entry,error),error.c_str());poke(controller,0xc68a,x&255);poke(controller,0xc68b,x>>8);poke(controller,0xc68d,x&255);unsigned remaining=10000;
  while(controller.stats().pc!=0x2d29&&remaining--)controller.debug_step_instruction();check(controller.stats().pc==0x2d29,"Original Mercury controller did not complete world writeback");
  auto position=controller.debug_read(0xc68a,2);check((unsigned(position[0])|(unsigned(position[1])<<8))==x,"Original Mercury controller wrapped its world position");
 }
 // Put the real cannon into its ordinary firing pose and aim phase. Its
 // original controller creates and advances both shots; no projectile or
 // collision result is supplied. Their low bytes overlap the player, while
 // their actual world positions stay more than128 pixels away.
 check(controller.restore(entry,error),error.c_str());poke(controller,0xc68a,8);poke(controller,0xc68b,1);poke(controller,0xc68d,8);poke(controller,0xc6aa,1);poke(controller,0xc6ab,1);poke(controller,0xc6ac,7);poke(controller,0xde9e,152);
 auto firing=controller.snapshot();auto shots=[&]{for(unsigned n=0;n<80;++n)controller.run_frame(0);auto pcm=controller.take_audio();auto view=controller.mod_view();return std::pair{pcm,std::vector<uint32_t>(view.pixels.begin(),view.pixels.end())};};auto expected_shots=shots();auto fired=controller.snapshot();check(byte(controller,0xcd0d)>=2,"Original cannon did not retain its two far-world shots");check(byte(controller,0xde9e)==152,"The far-world encounter damaged the player's aliased low byte");capture(controller,out/"mercury-far-shots.ppm");check(controller.restore(firing,error),error.c_str());check(shots()==expected_shots&&controller.snapshot()==fired,"Far-world shot state/PCM/raster replay differs");
 std::cout<<"actual cannon far-world shots remain visible, do not hit an aliased player and replay exactly\n";
 for(bool far:{true,false}){check(controller.restore(entry,error),error.c_str());unsigned x=far?292:36;poke(controller,0xc68a,x&255);poke(controller,0xc68b,x>>8);poke(controller,0xc68d,36);poke(controller,0xc68f,107);poke(controller,0xde9e,152);frames(controller,4);check(far?byte(controller,0xde9e)==152:byte(controller,0xde9e)<152,far?"A far actor hit the player's aliased low byte":"Original nearby actor contact damage stopped");if(far)capture(controller,out/"mercury-far-contact.ppm");}
 std::cout<<"actual nearby actor contact still damages; matching low-byte actor256pixels away does not\n";
 // Retain the actual cannon and its native HP. A position/timer prerequisite
 // isolates the weapon point query; no victim damage or defeat is supplied.
 check(controller.restore(entry,error),error.c_str());frames(controller,90);frames(controller,2,1);poke(controller,0xcd0d,0);
 const auto weapon_point=controller.snapshot();unsigned victim_hp=byte(controller,0xc698);check(victim_hp&&byte(controller,0xc69a)==4,"Weapon fixture lost its actual native cannon");
 unsigned player_x=byte(controller,0xd70d)|(byte(controller,0xd70e)<<8),camera_x=byte(controller,0xd711)|(byte(controller,0xd712)<<8),target_y=byte(controller,0xd725)-8;
 struct WeaponResult {std::vector<uint8_t> state;std::vector<int16_t> pcm;std::vector<uint32_t> pixels;unsigned hp=0;bool emitted=false;bool operator==(const WeaponResult&)const=default;};
 auto weapon_case=[&](bool far,bool fire,const char* name){check(controller.restore(weapon_point,error),error.c_str());WeaponResult result;unsigned x=player_x+40+(far?256:0);std::ofstream log;if(name)log.open(out/(std::string(name)+".jsonl"));
  for(unsigned n=0;n<80;++n){poke(controller,0xc68a,x&255);poke(controller,0xc68b,x>>8);poke(controller,0xc68d,(x-camera_x)&255);poke(controller,0xc68f,target_y);poke(controller,0xc6aa,120);poke(controller,0xc6ab,1);
   controller.run_frame(uint8_t(fire&&n<2?32:0));auto block=controller.take_audio();result.pcm.insert(result.pcm.end(),block.begin(),block.end());result.emitted|=byte(controller,0xc209)||byte(controller,0xc21d);
   if(name)log<<"{\"frame\":"<<n<<",\"hp\":"<<byte(controller,0xc698)<<",\"world\":"<<(byte(controller,0xc68a)|(byte(controller,0xc68b)<<8))<<",\"primary_live\":"<<byte(controller,0xc204)<<",\"point_token\":"<<byte(controller,0xc209)<<",\"point_x\":"<<byte(controller,0xc20a)<<",\"secondary_token\":"<<byte(controller,0xc21d)<<",\"weapon_count\":"<<byte(controller,0xc200)<<",\"weapon_x\":"<<byte(controller,0xc23f)<<",\"arm_phase\":"<<byte(controller,0xc2ea)<<"}\n";
  }
  result.hp=byte(controller,0xc698);result.state=controller.snapshot();auto view=controller.mod_view();result.pixels.assign(view.pixels.begin(),view.pixels.end());if(name)capture(controller,out/(std::string(name)+".ppm"));return result;
 };
 auto far_no_fire=weapon_case(true,false,"weapon-far-no-fire"),near_no_fire=weapon_case(false,false,"weapon-near-no-fire");check(far_no_fire.hp==victim_hp&&near_no_fire.hp==victim_hp,"An unfired weapon fixture changed native victim HP");
 auto far_fire=weapon_case(true,true,"weapon-far-fire");check(far_fire.emitted&&far_fire.hp==victim_hp,"An actual native weapon point damaged a different world page");check(weapon_case(true,true,nullptr)==far_fire,"Native far-weapon state/PCM/view replay differs");
 auto near_fire=weapon_case(false,true,"weapon-near-fire");check(near_fire.emitted&&near_fire.hp==0,"The original nearby Mega Arm hit stopped working");
 std::cout<<"actual native Mega Arm retains nearby defeat, rejects256-pixel aliases, preserves both no-fire controls and replays state/PCM/view\n";
 // A page-zero actor rectangle can itself cross256. Exercise a genuine
 // left Buster near world0 against both a nearby and a wrapped far cannon.
 auto edge=[&](unsigned x,bool fire,const char* name){check(controller.restore(weapon_point,error),error.c_str());frames(controller,2,2);poke(controller,0xd70d,32);poke(controller,0xd70e,0);poke(controller,0xd722,32);poke(controller,0xd724,0);poke(controller,0xd725,97);poke(controller,0xd729,0);poke(controller,0xd72a,0);poke(controller,0xde9e,152);WeaponResult result;
  for(unsigned n=0;n<40;++n){poke(controller,0xc68a,x);poke(controller,0xc68b,0);poke(controller,0xc68d,x);poke(controller,0xc68f,100);poke(controller,0xc6aa,120);poke(controller,0xc6ab,1);poke(controller,0xc6ca,700&255);poke(controller,0xc6cb,700>>8);poke(controller,0xc6cd,700&255);poke(controller,0xc6ee,700&255);poke(controller,0xc6ef,700>>8);controller.run_frame(uint8_t(fire&&n<2?32:0));auto pcm=controller.take_audio();result.pcm.insert(result.pcm.end(),pcm.begin(),pcm.end());result.emitted|=byte(controller,0xc209)||byte(controller,0xc21d);}
  result.hp=byte(controller,0xc698);result.state=controller.snapshot();auto v=controller.mod_view();result.pixels.assign(v.pixels.begin(),v.pixels.end());if(name){capture(controller,out/(std::string(name)+".ppm"));write_file_atomic(out/(std::string(name)+".wram"),controller.debug_read(0xc000,8192));}return result;
 };
 auto edge_near_quiet=edge(15,false,"edge-near-quiet"),edge_far_quiet=edge(255,false,"edge-far-quiet");check(!edge_near_quiet.emitted&&!edge_far_quiet.emitted&&edge_near_quiet.hp==victim_hp&&edge_far_quiet.hp==victim_hp,"An unfired wrap-edge cannon changed HP");auto edge_far=edge(255,true,"edge-far-fire"),edge_near=edge(15,true,"edge-near-fire");check(edge_far.emitted&&edge_far.hp==victim_hp&&edge_near.emitted&&edge_near.hp==0,"A tracked native weak point aliased a rectangle crossing256 or lost its true near hit");check(edge(255,true,nullptr)==edge_far,"Wrap-edge weak-point state/PCM/view replay differs");std::cout<<"actual left Buster retains near cannon defeat, rejects a page-zero rectangle crossing256, preserves both no-fire controls and replays state/PCM/view\n";
 struct Flight {WeaponResult result;unsigned last_token=0,child_hp=0,minimum_child_hp=255;bool operator==(const Flight&)const=default;};
 auto flight=[&](unsigned width,bool fire,bool hit,const char* name,bool isolate=true){check(controller.restore(weapon_point,error),error.c_str());controller.mod_viewport(width,144);Flight result;unsigned x=player_x+296,limit=hit?260:430;std::ofstream log;if(name)log.open(out/(std::string(name)+".jsonl"));
  for(unsigned n=0;n<limit;++n){poke(controller,0xc68a,x&255);poke(controller,0xc68b,x>>8);poke(controller,0xc68d,(x-camera_x)&255);poke(controller,0xc68f,hit?target_y:16);poke(controller,0xc6aa,120);poke(controller,0xc6ab,1);
   // The ordinary moving child otherwise intercepts this single-hit shot.
   // Move its current/return-origin X beyond the cannon, never its HP/results.
   if(isolate){poke(controller,0xc6ca,700&255);poke(controller,0xc6cb,700>>8);poke(controller,0xc6cd,(700-camera_x)&255);poke(controller,0xc6ee,700&255);poke(controller,0xc6ef,700>>8);}
   controller.run_frame(uint8_t(fire&&n<2?32:0));auto block=controller.take_audio();result.result.pcm.insert(result.result.pcm.end(),block.begin(),block.end());unsigned current_child_hp=byte(controller,0xc6d8);result.minimum_child_hp=std::min(result.minimum_child_hp,current_child_hp);if(name&&!isolate&&fire&&current_child_hp==2&&result.minimum_child_hp==2&&n&&result.child_hp!=2){write_file_atomic(out/(std::string(name)+"-positive.state"),controller.snapshot());write_file_atomic(out/(std::string(name)+"-positive.wram"),controller.debug_read(0xc000,8192));}result.child_hp=current_child_hp;bool token=byte(controller,0xc209)||byte(controller,0xc21d);result.result.emitted|=token;if(token)result.last_token=n;
   if(hit&&n<80)check(byte(controller,0xc698)==victim_hp,"An extended shot damaged the distant cannon before its actual flight");
   if(name){log<<"{\"frame\":"<<n<<",\"hp\":"<<byte(controller,0xc698)<<",\"world\":"<<(byte(controller,0xc68a)|(byte(controller,0xc68b)<<8))<<",\"screen\":"<<byte(controller,0xc68d)<<",\"child_hp\":"<<byte(controller,0xc6d8)<<",\"child_world\":"<<(byte(controller,0xc6ca)|(byte(controller,0xc6cb)<<8))<<",\"point_token\":"<<byte(controller,0xc209)<<",\"point_x\":"<<byte(controller,0xc20a)<<",\"weapon_count\":"<<byte(controller,0xc200)<<",\"weapon_x\":"<<byte(controller,0xc23f)<<",\"arm_phase\":"<<byte(controller,0xc2ea)<<"}\n";if(n==120)capture(controller,out/(std::string(name)+"-flight.ppm"));if(hit&&fire&&n==160)write_file_atomic(out/(std::string(name)+"-before-touch.state"),controller.snapshot());}
  }
  result.result.hp=byte(controller,0xc698);result.child_hp=byte(controller,0xc6d8);result.result.state=controller.snapshot();auto v=controller.mod_view();result.result.pixels.assign(v.pixels.begin(),v.pixels.end());check(controller.snapshot()==result.result.state,"A native weapon/child view changed simulation state");if(name){capture(controller,out/(std::string(name)+".ppm"));write_file_atomic(out/(std::string(name)+".state"),result.result.state);write_file_atomic(out/(std::string(name)+".wram"),controller.debug_read(0xc000,8192));}return result;
 };
 auto unfired=flight(512,false,true,"weak-range-no-fire");check(!unfired.result.emitted&&unfired.result.hp==victim_hp,"An unfired distant cannon fixture changed");
 auto hit=flight(512,true,true,"weak-range-hit");check(hit.result.emitted&&hit.result.hp==0,"An actual weak shot did not reach its distant native cannon");check(flight(512,true,true,nullptr)==hit,"Extended native weak-shot state/PCM/view replay differs");
 auto native_clip=flight(160,true,false,"weak-native-clip"),wide_clip=flight(512,true,false,"weak-wide-clip");check(native_clip.result.emitted&&native_clip.last_token<100&&wide_clip.last_token>200&&wide_clip.last_token<429&&!byte(controller,0xc200),"Weak-shot viewport clipping or finite lifetime differs");
 check(flight(512,true,false,nullptr)==wide_clip,"Weak-shot full-flight state/PCM/view replay differs");
 auto child_control=flight(512,false,true,"weak-child-no-fire",false),child_hit=flight(512,true,true,"weak-child-hit",false);check(child_control.minimum_child_hp==3&&child_hit.minimum_child_hp==2&&child_hit.result.hp==victim_hp,"The original moving child no longer intercepts the actual extended shot");
 std::cout<<"actual weak shot crosses the native screen and256-pixel page, defeats only after flight, retains unfired HP and finite viewport clipping, and replays state/PCM/view\n";
 std::cout<<"actual moving child takes its original one-point hit; matched no-fire retains HP3\n";
 // Complete an actual native IRQ return before invoking the original room
 // loader. Discarding an audio ISR would leave its DD2B reentrancy lock set.
 check(controller.restore(weapon_point,error),error.c_str());limit=200000;auto normal_boundary=[&]{return controller.stats().pc==0x09ef&&controller.debug_registers().sp==0xfffe&&!byte(controller,0xdd2b);};
 while(limit&&!normal_boundary()){--limit;controller.debug_step_instruction();}check(normal_boundary(),"A normal native stage boundary was not reached");const auto boundary=controller.snapshot();
 auto arena=[&](unsigned width){check(controller.restore(boundary,error),error.c_str());controller.mod_viewport(width,144);poke(controller,0xdf0a,9);poke(controller,0xde8a,1);poke(controller,0xde9e,152);poke(controller,0x2100,1);poke(controller,0xdf00,1);controller.debug_registers({0,0,0,0,0xfffe,0x094c,1});controller.debug_wake();
  unsigned remaining=2000000;while(remaining&&!normal_boundary()){--remaining;controller.debug_step_instruction();}check(normal_boundary(),"The original one-screen boss loader did not return");controller.take_audio();for(unsigned n=0;n<600;++n)controller.run_frame(0);auto pcm=controller.take_audio();check(byte(controller,0xc647)==81&&byte(controller,0xc658)==19,"The original one-screen boss did not initialize");
  auto name="stage9-native"+std::to_string(width);write_file_atomic(out/(name+".state"),controller.snapshot());auto ram=controller.debug_read(0xc000,8192);write_file_atomic(out/(name+".wram"),ram);capture(controller,out/(name+".ppm"));controller.write_frame(out/(name+"-physical.ppm"));return std::pair{ram,pcm};
 };
 check(arena(160)==arena(256),"A wider host view changed original one-screen boss staging, contact or audio");
 std::cout<<"actual one-screen boss retains identical600-frame native RAM/PCM at160/256 after an IRQ-safe original loader\n";
 // Use the boss's real open-eye phase and an original-produced weak shot.
 // Only hero position/health are prerequisites; victim HP and results remain
 // native. The damage-only branches then distinguish defeat from a miss.
 check(controller.restore(read_file(out/"stage9-native256.state"),error),error.c_str());
 // Preserve the hero while the original long assembly phase completes; the
 // 600-frame staging snapshot is earlier than the native open-eye phase.
 for(unsigned n=0;n<20;++n){poke(controller,0xde9e,152);frames(controller,32);}
 check(byte(controller,0xc658)==19,"Native stage9 assembly changed boss HP");
 auto until=[&](unsigned pc,unsigned bank){unsigned remaining=10000000;while(remaining&&(controller.stats().pc!=pc||controller.stats().rom_bank!=bank)){--remaining;controller.debug_step_instruction();}check(remaining,"The original stage9-boss boundary was not reached");};
 poke(controller,0xde9e,152);poke(controller,0xd70d,32);poke(controller,0xd70e,0);poke(controller,0xd722,32);until(0x5634,0x0e);poke(controller,0xde9e,152);controller.debug_step_instruction();until(0x5469,0x0e);frames(controller,16,17);frames(controller,6,33);controller.set_buttons(33);until(0x2907,0);
 auto hit_registers=controller.debug_registers();check(hit_registers.bc==0xc640&&(hit_registers.de>>8)==1&&byte(controller,0xc658)==19,"The stage9 boss did not receive its original positive weak-shot hit");const auto boss_hit=controller.snapshot();
 for(unsigned damage:{0u,19u}){check(controller.restore(boss_hit,error),error.c_str());auto r=controller.debug_registers();r.de=uint16_t((damage<<8)|(r.de&255));controller.debug_registers(r);until(0x2911,0);check(byte(controller,0xc658)==19-damage,"The native stage9-boss HP store differs");
  for(unsigned n=0;n<20;++n){poke(controller,0xde9e,152);frames(controller,60);}
  std::string name=damage?"stage9-password":"stage9-zero-control";capture(controller,out/(name+".ppm"));controller.write_frame(out/(name+"-physical.ppm"));write_file_atomic(out/(name+".state"),controller.snapshot());write_file_atomic(out/(name+".wram"),controller.debug_read(0xc000,8192));
  if(!damage){check(byte(controller,0xc658)==19&&!byte(controller,0xded7),"A zero-damage stage9-boss control completed the stage");continue;}
  check(byte(controller,0xdf00)==0x10&&byte(controller,0xded7)==255,"Native defeat did not reach the original password screen");auto v=controller.mod_view();check(v.width==160&&!v.expanded,"The original password screen retained a world viewport");
  if(!std::any_of(options.mods.begin(),options.mods.end(),[](const auto& f){return f.colorization;})){auto p=controller.pixels();check(std::equal(v.pixels.begin(),v.pixels.end(),p.begin()),"The original password screen differs from the physical raster");}
  else{std::set<uint32_t> colors(v.pixels.begin(),v.pixels.end());check(colors.size()>4,"Native DX password has no authored colors");}
 }
 std::cout<<"actual stage9-boss weak-shot hit, damage-only native defeat/password and zero-damage control pass; password retains fixed presentation\n";
 {std::ofstream trace(out/"mm5-executed.sym",std::ios::app);for(auto p:controller.take_trace())trace<<std::hex<<(p/0x4000)<<":"<<std::setfill('0')<<std::setw(4)<<((p&0x3fff)+(p>=0x4000?0x4000:0))<<" controller_"<<p<<'\n';}
 check(m.restore(point,error),error.c_str());std::cout<<"original Mercury controller preserves five far-world positions through native writeback\n";
}
}
int main(int argc,char** argv){try{
 check(argc==3||argc==4||argc==5,"usage: mm5-native-qa ROM OUTPUT_DIRECTORY [CURRENT_STATE | --stock-clock | --dx ASSET_ID | --dx-only ASSET_ID | --dx-terra ASSET_ID]");
 auto rom=read_file(argv[1]);check(sha256(rom)=="7a108770a7c1ad592b52d0c46d7ead422d0a20961abaafca3d40086e3f2f588f","Wrong cartridge");
 std::filesystem::path out=argv[2];std::filesystem::create_directories(out);
 Options options;options.model=Model::dmg;options.mods={read_mod_package(std::filesystem::path(__FILE__).parent_path().parent_path()/"mods/mm5-adaptive.gbmod",sha256(rom)).feature};
 const bool terra_only=argc==5&&std::string(argv[3])=="--dx-terra";const bool dx_only=argc==5&&std::string(argv[3])=="--dx-only";const bool dx=argc==5&&(std::string(argv[3])=="--dx"||dx_only||terra_only);if(dx){NativeFeature feature;feature.plugin="gbcllr.mm5.dx";feature.view_width=160;feature.colorization=true;feature.asset_input=argv[4];options.mods.push_back(feature);if(dx_only)options.mods.erase(options.mods.begin());}
 const bool stock_clock=argc==4&&std::string(argv[3])=="--stock-clock";if(stock_clock)options.mods[0].smooth_gameplay=false;
 Machine m(rom,options);m.trace_enabled(true);m.mod_viewport(256,144);
 if(terra_only){terra_absorption(m,out);return 0;}
 if(dx)fixed_dx_scenes(m,out);
 else flicker_control(rom,options);
 if(argc==4&&!stock_clock){std::string error;check(m.restore(read_file(argv[3]),error),error.c_str());}else{
 // Advance the original Light-lab introduction and select the original first
 // stage through native input. No result/weapon/encounter flag is supplied.
 first_stage(m);
 }
 ending_scenes(m,out,dx);
 if(dx){sunstar_background(m,out);selector_scene(m,out,rom);}
 salt_water(m,out);
 if(dx_only){auto view=m.mod_view();check(view.width==160&&view.sprites,"Standalone DX original-aspect poses missing");capture(m,out/"dx-only-gameplay.ppm");dx_hurt_flash(m,out);auto point=m.snapshot();frames(m,30);auto expected=m.snapshot();std::string error;check(m.restore(point,error),error.c_str());frames(m,30);check(m.snapshot()==expected,"Standalone DX replay differs");auto workload_start=m.snapshot();auto workload=[&]{unsigned updates=0;bool produced=false;std::vector<int16_t> pcm;for(unsigned n=0;n<300;++n){poke(m,0xde9e,152);unsigned before=byte(m,0xde83);m.run_frame(uint8_t(n%32<2?32:0));auto block=m.take_audio();pcm.insert(pcm.end(),block.begin(),block.end());updates+=(byte(m,0xde83)-before)&255;produced|=byte(m,0xc209)||byte(m,0xc21d);}auto view=m.mod_view();return std::tuple{updates,produced,pcm,m.snapshot(),std::vector<uint32_t>(view.pixels.begin(),view.pixels.end())};};auto firing=workload();check(std::get<0>(firing)==300&&std::get<1>(firing),"Standalone DX firing field lost native updates or weapon production");check(m.restore(workload_start,error),error.c_str());check(workload()==firing,"Standalone DX firing state/PCM/view replay differs");std::cout<<"standalone DX live firing native updates=300 display_frames=300; weapon production and state/PCM/view replay pass\n";frames(m,2,128);frames(m,90);view=m.mod_view();std::set<uint32_t> colors(view.pixels.begin(),view.pixels.end());check(colors.size()>4,"Standalone DX native menu colors missing");capture(m,out/"dx-only-menu.ppm");std::cout<<"standalone160 DX first-stage poses/replay and authored native menu pass\n";return 0;}
 check(byte(m,0xdf0a)==1&&byte(m,0xdf01)==0x11&&!(byte(m,0xdeaf)&8),"Original first stage did not finish its native input-lock introduction");
 for(unsigned n=0;n<600;++n){poke(m,0xde9e,0x98);frames(m,1);if(m.mod_view().expanded&&m.mod_view().sprites)break;}
 write_file_atomic(out/"mercury-probe.state",m.snapshot());auto view=m.mod_view();std::cout<<"scene="<<byte(m,0xdf0a)<<" bank="<<byte(m,0xdf01)<<" room="<<byte(m,0xd88c)<<"x"<<byte(m,0xd88d)<<" lcdc="<<byte(m,0xff40)<<" pc="<<m.stats().pc<<" mapped="<<m.stats().rom_bank<<" view="<<view.width<<"\n";check(view.width==256&&view.expanded,"Adaptive view did not activate");check(view.captures&&view.sprites,"Original logical sprites were not captured");capture(m,out/"mercury-start.ppm");
 {std::ofstream trace(out/"mm5-executed.sym");for(auto p:m.take_trace())trace<<std::hex<<(p/0x4000)<<":"<<std::setfill('0')<<std::setw(4)<<((p&0x3fff)+(p>=0x4000?0x4000:0))<<" executed_"<<p<<'\n';}
 write_file_atomic(out/"mercury-current.state",m.snapshot());
 m.write_frame(out/"mercury-native.ppm");
 std::cout<<"first stage native input, width="<<view.width<<" sprites="<<view.sprites<<" captures="<<view.captures<<"\n";
 m.mod_viewport(512,144);frames(m,4);check(m.mod_view().sprites>10,"Native cumulative-list actor pieces were not retained");capture(m,out/"mercury-wide-actors.ppm");m.mod_viewport(256,144);frames(m,2);std::cout<<"native cumulative-list enemy visible in512-pixel world alongside complete player body\n";
 if(!dx)moving_center(m,out);
 if(dx)dx_hurt_flash(m,out);
 collision_queries(m,rom,options,out);
 // Availability is supplied only for weapon-selection qualification. Energy
 // values are deliberately nonuniform and must not be changed by selection.
 poke(m,0xde9c,0xff);poke(m,0xde9d,0x1f);std::array<uint8_t,12> energy{};for(unsigned n=0;n<12;++n)energy[n]=uint8_t(n*8);m.debug_write(0xde9f,energy);poke(m,0xde9e,0x98);const auto before=m.debug_read(0xde9f,12);
 unsigned current=byte(m,0xde9b);
 auto shoulder=[&](unsigned key){frames(m,8,0,uint8_t(key));frames(m,8);};
 for(unsigned n=0;n<13;++n){shoulder(2);current=(current+1)%13;check(byte(m,0xde9b)==current,"Original next-weapon service failed");check(m.debug_read(0xde9f,12)==before,"Weapon selection changed energy");}
 frames(m,8,0,2);current=(current+1)%13;check(byte(m,0xde9b)==current,"Fresh shoulder did not cycle");frames(m,60,0,2);check(byte(m,0xde9b)==current,"Held shoulder repeated");frames(m,8);shoulder(3);check(byte(m,0xde9b)==current,"Opposed shoulders changed weapon");shoulder(1);current=(current+12)%13;if(byte(m,0xde9b)!=current){std::cout<<"previous context current="<<byte(m,0xde9b)<<" expected="<<current<<" flags="<<byte(m,0xdeaf)<<" player="<<byte(m,0xd702)<<" health="<<byte(m,0xde9e)<<" transition="<<byte(m,0xd81c)<<" freeze="<<byte(m,0xde2c)<<"\n";write_file_atomic(out/"failed-previous.state",m.snapshot());}check(byte(m,0xde9b)==current,"Previous shoulder did not cycle");
 std::cout<<"original13-entry next/previous, wrap, edge-only, opposed input and energy pass\n";
 if(stock_clock)missed_update_pulse(m);
 auto point=m.snapshot();auto replay=[&]{frames(m,2,0,2);for(unsigned n=0;n<60;++n)m.run_frame(LogicalInput{uint8_t(n<30?1:0),0});auto pcm=m.take_audio();auto v=m.mod_view();return std::pair{pcm,std::vector<uint32_t>(v.pixels.begin(),v.pixels.end())};};auto expected=replay();auto end=m.snapshot();std::string error;check(m.restore(point,error),error.c_str());check(replay()==expected&&m.snapshot()==end,"Current-build state/PCM/raster replay differs");
 check(sha256(rom)==sha256(read_file(argv[1])),"Cartridge modified");
 std::cout<<"current-build state/PCM/raster replay and cartridge identity pass\n";
 {unsigned weapon=byte(m,0xde9b);frames(m,2,128);frames(m,90);capture(m,out/"native-pause.ppm");auto point=m.snapshot();unsigned update=byte(m,0xde83);auto replay=[&]{for(unsigned n=0;n<20;++n)m.run_frame(LogicalInput{uint8_t(n<2?1:0),0});auto pcm=m.take_audio();auto v=m.mod_view();return std::pair{pcm,std::vector<uint32_t>(v.pixels.begin(),v.pixels.end())};};auto first=replay();auto end=m.snapshot();std::string error;check(m.restore(point,error),error.c_str());check(replay()==first&&m.snapshot()==end,"Menu current-build state/PCM/raster replay differs");check(byte(m,0xde83)==update,"The paused native world advanced");check(m.restore(point,error),error.c_str());if(dx){auto v=m.mod_view();std::set<uint32_t> colors(v.pixels.begin(),v.pixels.end());check(colors.size()>4,"Native DX pause did not use authored colors");std::cout<<"DX native pause colors="<<colors.size()<<"; gameplay frozen and state/PCM/raster replay pass\n";}frames(m,8,0,2);frames(m,2,128,2);frames(m,90,0,2);check(byte(m,0xde9b)==weapon,"A shoulder held through the original menu leaked on resume");frames(m,8);std::cout<<"original pause/resume discards held shoulders\n";}
 for(unsigned n=0;n<13&&byte(m,0xde9b);++n)shoulder(2);
 unsigned updates=0,low=255,high=0;for(unsigned n=0;n<300;++n){poke(m,0xde9e,0x98);unsigned before=byte(m,0xde83);frames(m,1,32);unsigned delta=(byte(m,0xde83)-before)&255;updates+=delta;low=std::min(low,delta);high=std::max(high,delta);}std::cout<<"Mercury fire workload smooth="<<!stock_clock<<" native updates="<<updates<<" display_frames=300 min="<<low<<" max="<<high<<" actors="<<byte(m,0xc398)<<"\n";
 {std::ofstream trace(out/"mm5-executed.sym",std::ios::app);for(auto p:m.take_trace())trace<<std::hex<<(p/0x4000)<<":"<<std::setfill('0')<<std::setw(4)<<((p&0x3fff)+(p>=0x4000?0x4000:0))<<" executed_"<<p<<'\n';}
 if(dx)terra_absorption(m,out);
 return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
