#include "../src/xtreme2_coop.hpp"
#include "../src/native_mods.hpp"
#include <algorithm>
#include <cstring>
#include <iostream>
#include <stdexcept>
static void check(bool b,const char* s){if(!b)throw std::runtime_error(s);}
int main(int argc,char** argv){try{
 using namespace gbcllr;
 check(argc==2,"Co-op manifest required");
 auto package=read_mod_package(argv[1],"e37a14a8e61bd64f2226dacc26a7ee75dd90abfcf88c5016eb97b76ecda0ddca");
 check(!package.default_enabled&&package.feature.plugin=="gbcllr.xtreme2.coop","Elective co-op default changed");
 std::array<uint8_t,0x8000> ram{};std::array<uint8_t,0x4000> vram{};std::array<uint8_t,127> hram{};
 // Match the original stage-entry arrival when checking the caller boundary.
 ram[0x101]=2;ram[0x102]=0x22;ram[0x108]=100;ram[0x121]=16;ram[0xc9]=1;ram[0x5f]=6;
 ram[0x84]=ram[0xd9]=ram[0xde]=16;
 ram[0x9b0]=1;
 ram[0xfee]=0x82;ram[0xfef]=6;auto stack=ram;
 ModContext c{};c.rom_bank=5;c.wram=ram.data();c.vram=vram.data();
 std::array<uint16_t,6> cpu{0x5500,1,2,3,0xcfee,0x4001};
 hram[0x49]=0xc3;
 auto a_storage=std::make_unique<Xtreme2Coop>(1);auto& a=*a_storage;a.inputs(1,2);a.instruction(c,cpu,ram,hram);
 check(hram[0xf]==0x10&&hram[0x10]==0x10,"P1 native Right mapping failed");
 // A scanline can publish the pose before the original producer finishes.
 // Its host tiles must already be complete at the native pose observation.
 std::vector<uint8_t> art(49152);c.rom=art.data();c.rom_size=art.size();c.hram=hram.data();
 // A real moving attack uses the far-call graphics wrapper, not just
 // same-bank tile lists. Its original producer must finish before publish.
 art[0x8000]=0x3e;art[0x8001]=1;art[0x8002]=0x21;art[0x8003]=0x10;art[0x8004]=0x40;
 art[0x8005]=0xcd;art[0x8006]=0x75;art[0x8007]=6;art[0x8008]=0xc9;
 art[0x4010]=0x11;art[0x4011]=0;art[0x4012]=0x41;art[0x4013]=0xc3;art[0x4014]=0x3b;art[0x4015]=0x11;
 for(unsigned i=0;i<32;++i)art[0x4100+i]=uint8_t(0xa0+i);
 hram[0x31]=2;hram[0x32]=0;hram[0x33]=0x40;
 a.palette(c,0,1);a.player_piece(0,0x40);
 // A later native emission can reuse this character's tile allocation.
 // Already-emitted pieces must keep their original pixels through restore.
 auto original_piece=std::vector<uint8_t>(art.begin()+0x4100,art.begin()+0x4120);
 std::fill_n(art.begin()+0x4100,32,0x55);a.palette(c,1,2);a.player_piece(1,0x40);
 a.publish(true);a.complete_frame(c);
 check(a.sprite_tile(0)&&std::equal(original_piece.begin(),original_piece.end(),a.sprite_tile(0))&&
       a.sprite_tile(1)&&a.sprite_tile(1)[0]==0x55,"A later body emission replaced an earlier piece's native tiles");
 auto poses=a.snapshot();a.restore(poses);check(a.snapshot()==poses,"Body piece graphics did not restore exactly");
 std::copy(original_piece.begin(),original_piece.end(),art.begin()+0x4100);a.palette(c,0,1);a.publish(true);a.complete_frame(c);
 check(a.sprite_texture(0)&&std::equal(art.begin()+0x4100,art.begin()+0x4120,a.sprite_texture(0)+0x400),"Pose publication depended on unfinished native graphics scratch");
 ram[0x108]=110;ram[0xa7]=3;ram[0x300]=0x22;hram[0x49]=0xc3;hram[0x4a]=0xa0;cpu[0]=0x9900;cpu[5]=0x4078;
 a.instruction(c,cpu,ram,hram);
 check(cpu[5]==0x4001&&cpu[4]==0xcfee&&cpu[0]==0x5500,"Partner re-entry lost original call frame");
 check(ram[0xc9]==0&&hram[0xf]==0x20,"Counterpart identity/input mapping failed");
 check(hram[0x49]==0xc3&&hram[0x4a]==0,"Partner pose did not reuse its isolated native scratch page");
 auto middle=a.snapshot();auto b_storage=std::make_unique<Xtreme2Coop>(1);auto& b=*b_storage;b.restore(middle);
 // A screen-wide pulse can outlive the player call that produced it.
 auto pulse_ram=ram;auto pulse_hram=hram;auto pulse_sp=c.sp;
 b.write(c,0xc0a4,24);ram[0xa4]=24;auto pulse=b.snapshot();b.restore(pulse);
 auto pulse_cpu=cpu;pulse_cpu[5]=0x4078;b.instruction(c,pulse_cpu,ram,hram);
 pulse_cpu[2]=0xb800;pulse_cpu[5]=0x2bef;b.instruction(c,pulse_cpu,ram,hram);c.sp=0xcfee;
 check(b.read(c,0xc0c9,1)==0,"Screen-wide damage lost its producer after snapshot/controller return");
 b.write(c,0xc0a4,0);ram=pulse_ram;hram=pulse_hram;c.sp=pulse_sp;b.restore(middle);
 std::array<uint8_t,8192> objects{};c.eram=objects.data();c.eram_size=objects.size();c.ram_bank=0;
 a.write(c,0xa000,1);objects[0]=1; // Partner-owned shot born in its controller.
 auto edge_ram=ram;auto edge_hram=hram;auto edge_cpu=cpu;
 edge_ram[0x108]=edge_ram[0x109]=0xff;edge_ram[0x105]=0x30;edge_ram[0x106]=1;edge_cpu[5]=0x4af6;
 b.instruction(c,edge_cpu,edge_ram,edge_hram);
 check(edge_ram[0x108]==8&&edge_ram[0x109]==0&&edge_ram[0x121]==0&&edge_ram[0x1a5]==0x8c,"Partner camera replacement lost physical room/pit rules");
 ram[0x108]=90;ram[0xa7]=1;ram[0x300]=0x99;cpu[0]=0x3300;cpu[5]=0x4078;a.instruction(c,cpu,ram,hram);
 check(cpu[5]==0x4078&&cpu[0]==0x9900&&ram[0x108]==110&&ram[0xc9]==1,"Original caller state not restored");
 check(ram[0xfee]==stack[0xfee]&&ram[0xfef]==stack[0xfef],"Actor projection rewrote native return stack");
 check(ram[0x300]==0x22&&hram[0x49]==0xc3&&hram[0x4a]==0xa0,"Partner graphics overwrote primary scratch or allocation");
 check(ram[0xa7]==3,"Partner projection changed the anchor's native projectile count");
 auto actors=a.actors();check(a.passes()==1&&actors[0].x==110&&actors[1].x==90,"Two retained bodies lost independent state");
 c.rom_bank=1;
 objects[0x1800]=1;objects[0x1808]=90;hram[0x22]=0xb8;
 art[0x69ab]=0xcd;art[0x69ac]=0x75;art[0x69ad]=6;art[0x69ae]=0xc9;
 cpu[2]=0xb800;cpu[4]=0xcfec;cpu[5]=0x675;ram[0xfec]=0xae;ram[0xfed]=0x69;
 a.instruction(c,cpu,ram,hram);
 check(ram[0x108]==90&&ram[0xc9]==0,"Native world actor did not target the nearer partner context");
 auto world_middle=a.snapshot();b.restore(world_middle);
 ram[0x121]=13;ram[0x2d]=1;cpu[4]=0xcfee;cpu[5]=0x69ae;a.instruction(c,cpu,ram,hram);
 actors=a.actors();check(actors[1].hp==13&&actors[0].hp==16&&ram[0x121]==16&&ram[0x2d]==0,"World actor effect or hurt state was not owned by its selected player");
 objects[0x1808]=110;objects[0x1821]=8;cpu[4]=0xcfec;cpu[5]=0x675;cpu[2]=0xb800;
 a.instruction(c,cpu,ram,hram);check(ram[0xc9]==1,"Enemy target fixture did not select P1");
 c.rom_bank=8;c.sp=0xcfe8;cpu[4]=0xcfe8;cpu[5]=0x28cb;cpu[2]=0xb8a0;ram[0xfe8]=0x34;ram[0xfe9]=0x50;
 a.instruction(c,cpu,ram,hram);ram[0xacb]=1;cpu[4]=0xcfea;cpu[5]=0x5034;a.instruction(c,cpu,ram,hram);
 check(a.read(c,0xc0c9,1)==0,"Projectile hit used the enemy target's character");
 auto collision=a.snapshot();b.restore(collision);
 b.write(c,0xb821,0);check(!b.reward().owner,"An ordinary enemy defeat claimed the boss reward");
 ram[0x9a1]=0xb8;a.write(c,0xb821,0);objects[0x1821]=0;
 check(a.combat().hits[1]==1&&a.combat().hits[0]==0&&a.combat().defeated_owner==2,"Confirmed native collision lost killing-blow ownership");
 check(a.read(c,0xc0c9,1)==1,"Damage context leaked after HP write");
 c.rom_bank=0xc;c.instruction_pc=0x7a69;
 check(a.reward().owner==2&&a.read(c,0xc0c9,1)==0,"Reward screen did not retain the killing character independently of the enemy target");
 c.instruction_pc=0x7aa0;a.write(c,0xc06f,0x5c);ram[0x6f]=0x5c;a.write(c,0xc06f,0x5c);
 check(a.reward().grants==1,"Native reward grant was lost or counted twice");
 ram[0xac]=1;ram[0x9b0]=1;a.inputs(2,16);cpu[5]=0x062e;a.instruction(c,cpu,ram,hram);
 check(hram[0xf]==0x21,"Campaign menus did not combine host navigation with either actor's confirmation");
 a.inputs(2,1);a.instruction(c,cpu,ram,hram);check(hram[0xf]==0x20,"Conflicting campaign menu directions did not prefer P1");ram[0xac]=0;
 c.rom_bank=8;c.instruction_pc=0;
 cpu[4]=0xcfe8;cpu[5]=0x28cb;a.instruction(c,cpu,ram,hram);ram[0xacb]=0;cpu[4]=0xcfea;cpu[5]=0x5034;a.instruction(c,cpu,ram,hram);
 check(a.read(c,0xc0c9,1)==1,"Missed original collision changed damage context");
 c.rom_bank=1;cpu[4]=0xcfee;cpu[5]=0x69ae;a.instruction(c,cpu,ram,hram);
 // NPC dispatch sets FFA2 before its far callee loads D. A P2-triggered
 // script must keep P2 as the world actor and park P1 using original art.
 for(unsigned table:{0x143du,0x1665u}){art[table+8]=1;art[table+10]=0;art[table+11]=0x43;}
 art[0x4300]=0;art[0x4301]=0x44;art[0x4302]=3;art[0x4304]=1;art[0x4305]=0x10;art[0x4306]=0x40;
 art[0x4400]=1;art[0x4402]=0;art[0x4403]=0;art[0x4404]=0;art[0x4405]=0;
 art[0x6c6c]=0xcd;art[0x6c6d]=0x75;art[0x6c6e]=6;art[0x6c6f]=0xc9;
 objects[0xa00]=1;objects[0xa08]=90;hram[0x22]=0xaa;ram[0xa41]=2;
 cpu[2]=0xca00;cpu[4]=0xcfec;cpu[5]=0x675;ram[0xfec]=0x6f;ram[0xfed]=0x6c;
 a.instruction(c,cpu,ram,hram);check(ram[0xc9]==0,"NPC stub relied on stale D instead of its native actor page");
 ram[0x101]=2;ram[0x160]=128;a.write(c,0xc0c2,1);ram[0xc2]=1;
 cpu[4]=0xcfee;cpu[5]=0x6c6f;a.instruction(c,cpu,ram,hram);
 check(a.anchor()==1&&ram[0xc9]==0&&a.transport(0)==1,"P2 scene did not transfer the world and depart its partner");
 c.rom_bank=5;a.inputs(1,2);cpu[5]=0x4001;hram[0xf]=0x11;hram[0x10]=1;a.instruction(c,cpu,ram,hram);
 check(hram[0xf]==0x11,"Co-op input overwrote original scripted movement");
 cpu[5]=0x4078;a.instruction(c,cpu,ram,hram);
 c.instruction_pc=0x12d5;c.sp=cpu[4];check(a.read(c,0xc10d,1)==0,"Departing partner retained wrapped native OAM");
 cpu[5]=0x4078;a.instruction(c,cpu,ram,hram);
 auto departing=a.snapshot();b.restore(departing);
 for(unsigned f=0;f<20;++f)a.complete_frame(c);
 check(a.transport(0)==2,"Original beam departure did not reach hidden phase");
 c.rom_bank=1;cpu[5]=0x656f;a.instruction(c,cpu,ram,hram);
 check(cpu[5]==0x0a32&&cpu[4]==0xcfec&&ram[0xfec]==0x6f&&ram[0xfed]==0x65&&hram[0x77]==0x36,"Departure did not queue the original sound through a valid call frame");
 cpu[4]=0xcfee;cpu[5]=0x656f;a.instruction(c,cpu,ram,hram);check(a.transport(0)==2,"Partner returned during an active scene");
 ram[0xc2]=0;a.instruction(c,cpu,ram,hram);
 actors=a.actors();check(a.transport(0)==3&&actors[0].hp==16&&actors[1].hp==13&&actors[0].x==actors[1].x,"Scene return lost health or the leader's supported landing");
 auto original=a.snapshot(),bad=original;uint32_t phase=3;std::memcpy(bad.data()+8,&phase,4);
 bool refused=false;try{a.restore(bad);}catch(...){refused=true;}
 check(refused&&a.snapshot()==original,"Invalid co-op restore was not atomic");
 refused=false;try{auto wrong=std::make_unique<Xtreme2Coop>(0);wrong->restore(original);}catch(...){refused=true;}
 check(refused,"Cross-roster state accepted");
 // A Buster shot and its owner draw from different native tile ranges.
 // Zero's native effects in the same physical VRAM must not replace the
 // X package, and the next player pose must retain the static shot tiles.
 art.resize(0xa8000);c.rom=art.data();c.rom_size=art.size();
 art[0x28*0x4000+0x3ba2]=1;art[0x28*0x4000+0x3ba7]=2;
 art[0x573]=0;art[0x574]=0x44;art[0x22*0x4000+0x400]=2;
 art[0x22*0x4000+0x401]=0;art[0x22*0x4000+0x402]=0x45;
 art[0x8500]=0x88;art[0x8501]=0;art[0x8502]=0;
 for(unsigned n=0;n<6;++n)art[0x8503+n]=uint8_t(0x20+n);
 art[0x8509]=art[0x850a]=255;art[0x850b]=0;art[0x850c]=0x80;art[0x850d]=2;
 for(unsigned n=0;n<32;++n)art[0x850e + n]=uint8_t(0x40+n);
 auto shots_storage=std::make_unique<Xtreme2Coop>(0);auto& shots=*shots_storage;ram[0x101]=2;ram[0xc9]=0;ram[0xc2]=0;ram[0x9b0]=1;
 c.rom_bank=5;cpu[5]=0x4001;shots.instruction(c,cpu,ram,hram);
 hram[0x31]=2;hram[0x32]=0;hram[0x33]=0x40;hram[0x48]=0;
 shots.write(c,0xb000,128);objects[0x1000]=128;hram[0x22]=0xb0;
 shots.projectile_piece(c,1,9);shots.palette(c,0,1);shots.publish(true);shots.complete_frame(c);
 check(shots.sprite_texture(1)&&std::equal(art.begin()+0x850e,art.begin()+0x852e,shots.sprite_texture(1)),"Original Buster tiles were replaced by shared VRAM or a later player pose");
 check(shots.sprite_texture(0)&&shots.sprite_palette(1)[1]!=shots.sprite_palette(0)[1],"Buster reused the body palette");
 // Existing Buster and later Fire Wave pieces share native tile slots.
 // Their original package/tile data must remain independent after a change.
 art[0x577]=3;art[0x578]=0x44;art[0x22*0x4000+0x403]=2;
 art[0x22*0x4000+0x404]=0x40;art[0x22*0x4000+0x405]=0x45;
 std::copy_n(art.data()+0x8500,14,art.data()+0x8540);
 for(unsigned n=0;n<32;++n)art[0x854e + n]=uint8_t(0xa0+n);
 ram[0x131]=5;shots.write(c,0xb100,128);objects[0x1100]=128;
 hram[0x22]=0xb0;shots.projectile_piece(c,1,1,0);
 hram[0x22]=0xb1;shots.projectile_piece(c,2,1,0);
 shots.publish(true);shots.complete_frame(c);
 check(shots.sprite_tile(1)&&shots.sprite_tile(2)&&std::equal(art.begin()+0x850e,art.begin()+0x852e,shots.sprite_tile(1))&&std::equal(art.begin()+0x854e,art.begin()+0x856e,shots.sprite_tile(2)),"Changing X weapons replaced an existing shot's original tile pair");
 auto projectile=shots.snapshot();auto shots_restored_storage=std::make_unique<Xtreme2Coop>(0);auto& shots_restored=*shots_restored_storage;shots_restored.restore(projectile);
 check(shots_restored.snapshot()==projectile,"Projectile graphics/ownership did not restore exactly");
 // Fallen players cannot buy a respawn or use voluntary rejoin. Only the
 // shared native team retry restores them, even when P1 has spare lives.
 std::array<uint8_t,0x8000> life_ram{};std::array<uint8_t,127> life_hram{};
 life_ram[0x101]=2;life_ram[0x121]=16;life_ram[0x160]=128;
 life_ram[0xd9]=life_ram[0xde]=16;life_ram[0x9b0]=1;
 auto life_context=c;life_context.wram=life_ram.data();life_context.hram=life_hram.data();life_context.rom_bank=5;
 std::array<uint16_t,6> life_cpu{0,0,0,0,0xcfee,0x4001};
 auto life_storage=std::make_unique<Xtreme2Coop>(0);auto& life=*life_storage;life.instruction(life_context,life_cpu,life_ram,life_hram);
 life_cpu[5]=0x4078;life.instruction(life_context,life_cpu,life_ram,life_hram);
 life_ram[0x121]=0;life_cpu[5]=0x4362;life.instruction(life_context,life_cpu,life_ram,life_hram);
 life_cpu[5]=0x4078;life.instruction(life_context,life_cpu,life_ram,life_hram);
 life_ram[0x65]=1;life_ram[0x9a1]=0xb8;life_context.rom_bank=1;life_cpu[5]=0x656f;
 life.inputs(0,64);life.instruction(life_context,life_cpu,life_ram,life_hram);
 check(life.lifecycle().status[1]==1&&!life.lifecycle().pending&&life_ram[0x65]==1,"Select revived a fallen player or charged P1's life");
 life.inputs(0,0);life_ram[0x65]=0;life_ram[0x9a1]=0;life.instruction(life_context,life_cpu,life_ram,life_hram);
 check(life.lifecycle().status[1]==1&&!life.lifecycle().pending&&!life_ram[0x65],"Empty life pool changed fallen participation");
 auto queued=life.snapshot();auto queued_storage=std::make_unique<Xtreme2Coop>(0);auto& queued_copy=*queued_storage;queued_copy.restore(queued);
 check(queued_copy.snapshot()==queued,"Fallen participation did not restore exactly");
 life_ram[0x65]=1;life.inputs(0,64);life.instruction(life_context,life_cpu,life_ram,life_hram);
 check(life.lifecycle().status[1]==1&&!life.lifecycle().pending&&life_ram[0x65]==1&&!life.actors()[1].hp,"Spare lives allowed a fallen player to rejoin");
 life.instruction(life_context,life_cpu,life_ram,life_hram);
 check(life.lifecycle().status[1]==1&&life_ram[0x65]==1,"Participation polling spent P1's life");
 life_context.rom_bank=5;life_cpu[5]=0x4001;life.instruction(life_context,life_cpu,life_ram,life_hram);
 life_cpu[5]=0x4078;life.instruction(life_context,life_cpu,life_ram,life_hram);
 life_ram[0x121]=16;life_cpu[5]=0x4078;life.instruction(life_context,life_cpu,life_ram,life_hram);
 check(life.lifecycle().status[1]==1&&!life.actors()[1].hp,"A native workspace refill revived the fallen player");
 life_ram[0x5f]=1;life_context.rom_bank=5;life_cpu[5]=0x4001;
 life.instruction(life_context,life_cpu,life_ram,life_hram);
 check(life.lifecycle().status[1]==1&&!life.actors()[1].hp,"A stage change revived a fallen player");
 // An isolated native death owns its particles even while world objects are
 // frozen. Its original animation44 tiles must survive the partner's pose.
 b.restore(middle);c.rom_bank=1;c.wram_bank=1;hram[0x22]=0xda;ram[0x1a00]=0;ram[0x1a2d]=14;
 b.write(c,0xda00,1);ram[0x1a00]=1;ram[0x11]=1;
 cpu[3]=0xda00;cpu[5]=0x6d02;b.instruction(c,cpu,ram,hram);
 check(cpu[5]==0x6d04&&ram[0x11]==1,"Death particle remained frozen or released unrelated world objects");
 ram[0x1a2d]=1;cpu[5]=0x6d02;b.instruction(c,cpu,ram,hram);
 check(cpu[5]==0x6d02,"Death exception advanced an unrelated frozen actor");ram[0x1a2d]=14;
 for(unsigned table:{0x143du,0x1551u,0x1665u}){unsigned e=table+0x44*4;art[e]=2;art[e+2]=0x20;art[e+3]=0x40;}
 art[0x8024]=2;art[0x8025]=0;art[0x8026]=0x40;
 b.projectile_piece(c,5,8,0x40);b.publish(true);b.complete_frame(c);
 check(b.sprite_owner(5)==4&&b.sprite_tile(5)&&std::equal(art.begin()+0x4100,art.begin()+0x4120,b.sprite_tile(5)),"Death particle did not retain its original producer independently of VRAM");
 auto death=b.snapshot();b.restore(death);check(b.snapshot()==death&&b.sprite_tile(5),"Death particle graphics did not restore exactly");
 std::cout<<"Native co-op caller, input, actor contexts and restore contracts passed\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
