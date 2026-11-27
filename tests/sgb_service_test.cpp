#include "../src/native_sgb_service.hpp"
#include <gbcllr/machine.hpp>
#include <gbcllr/presentation.hpp>
#include <algorithm>
#include <iostream>
#include <stdexcept>
using namespace gbcllr;
static void check(bool v,const char* s){if(!v)throw std::runtime_error(s);}
static void send(NativeSgbService& s,uint8_t command,std::initializer_list<uint8_t> payload,std::array<uint8_t,8192>& vram){
 std::array<uint8_t,16> packet{};packet[0]=uint8_t(command*8+1);std::copy(payload.begin(),payload.end(),packet.begin()+1);
 s.write(0,vram.data());s.write(0x30,vram.data());for(auto byte:packet)for(unsigned bit=0;bit<8;++bit){s.write((byte&(1u<<bit))?0x10:0x20,vram.data());s.write(0x30,vram.data());}s.write(0x20,vram.data());s.write(0x30,vram.data());
}
static std::vector<uint32_t> copy(std::span<const uint32_t> x){return {x.begin(),x.end()};}
static void direct_palettes(std::array<uint8_t,8192>& vram,std::span<const uint32_t> native){
 constexpr unsigned first[]={0,2,0,1},second[]={1,3,3,2};
 constexpr uint32_t a[]={0xffff0000,0xff00ff00,0xff0000ff},b[]={0xffc68442,0xffffffff,0xff000000};
 auto direct=std::make_unique<NativeSgbService>(true);direct->complete_frame(native);
 for(uint8_t cmd=0;cmd<4;++cmd){auto old=copy(direct->view().palettes);
  send(*direct,cmd,{0x04,0x41,0x1f,0,0xe0,3,0,0x7c,0x18,0x22,0xff,0x7f,0,0},vram);
  check(std::equal(old.begin(),old.end(),direct->view().palettes.begin()),"direct PAL bypassed completed-frame publication");
  direct->complete_frame(native);auto v=direct->view();check(v.colors_ready&&v.commands[cmd]==1,"direct PAL family did not activate without PAL_TRN");
  for(unsigned slot=0;slot<4;++slot){check(v.palettes[slot*4]==0xff214284,"direct PAL color0 was not shared across all four palettes");
   for(unsigned c=1;c<4;++c)check(v.palettes[slot*4+c]==(slot==first[cmd]?a[c-1]:slot==second[cmd]?b[c-1]:old[slot*4+c]),"direct PAL pair selected wrong colors/palettes or changed an untouched color");
  }
 }
 auto state=direct->snapshot();auto restored=std::make_unique<NativeSgbService>(true);restored->restore(state);check(restored->snapshot()==state&&copy(restored->view().colors)==copy(direct->view().colors),"direct PAL current-load changed palette frame");
}
int main(){try{
 std::array<uint8_t,8192> vram{};NativeSgbService s(true);std::array<uint32_t,160*144> native{};constexpr uint32_t grays[]={0xffffffff,0xffaaaaaa,0xff555555,0xff000000};for(size_t i=0;i<native.size();++i)native[i]=grays[i%4];
 s.complete_frame(native);check(!s.view().colors_ready&&!s.view().border_ready&&s.view().packets==0,"service fabricated authored SGB output");
 direct_palettes(vram,native);
 auto word=[&](size_t at,uint16_t n){vram[at]=uint8_t(n);vram[at+1]=uint8_t(n>>8);};
 for(unsigned p=0;p<4;++p){word(0x800+p*8,0x7fff);word(0x802+p*8,p==1?0x001f:0x03e0);word(0x804+p*8,0x7c00);word(0x806+p*8,0);}
 send(s,0x0b,{},vram);s.complete_frame(native);check(!s.view().colors_ready,"PAL_TRN alone fabricated installed colors");
 send(s,0x0a,{0,0,1,0,2,0,3,0},vram);s.complete_frame(native);auto view=s.view();
 check(view.colors_ready&&view.uniform_attributes&&view.colors[0]==0xffffffff&&view.colors[1]==0xff00ff00&&view.colors[2]==0xff0000ff&&view.colors[3]==0xff000000,"post-BGP/OBP native shade or RGB555 mapping differs");
 std::vector<uint32_t> wide(256*144);for(size_t i=0;i<wide.size();++i)wide[i]=grays[i%4];ModView expanded;expanded.width=256;expanded.expanded=true;expanded.pixels=wide;PresentationTransform presentation;
 auto colored=presentation.cartridge_sgb_colors(expanded,view);check(colored.width==256&&colored.expanded&&colored.pixels[1]==0xff00ff00,"uniform authored SGB palette did not retain exact Fit margins");
 auto live=copy(view.colors);send(s,0x17,{1},vram);check(s.freezes_frame(),"canonical MASK1 gate missed received packet");
 auto cleared=native;cleared.fill(0xffffffff);s.complete_frame(cleared);view=s.view();
 for(unsigned y=0;y<144;++y)std::fill_n(wide.begin()+y*256+48,160,0xffffffff);
 colored=presentation.cartridge_sgb_colors(expanded,view);check(colored.width==256&&colored.expanded&&colored.pixels[1]==0xff00ff00,"MASK1 discarded uniform Fit margins");
 for(unsigned y=0;y<144;++y)check(std::equal(live.begin()+y*160,live.begin()+(y+1)*160,colored.pixels.begin()+y*256+48),"MASK1 leaked current native transfer pixels into frozen center");
 auto uniform_frozen=s.snapshot();auto same=std::make_unique<NativeSgbService>(true);same->restore(uniform_frozen);
 auto invalid=[&](ModView v,NativeSgbView data){bool rejected=false;try{presentation.cartridge_sgb_colors(v,data);}catch(const std::invalid_argument&){rejected=true;}check(rejected,"invalid frozen SGB input accepted");};
 auto small=expanded;small.width=80;invalid(small,view);auto short_data=view;short_data.colors=short_data.colors.first(1);invalid(expanded,short_data);short_data=view;short_data.palettes={};invalid(expanded,short_data);short_data=view;std::array<uint8_t,360> bad_attrs{};bad_attrs[0]=4;short_data.attributes=bad_attrs;invalid(expanded,short_data);
 std::vector<uint32_t> tall(160*284);for(size_t i=0;i<tall.size();++i)tall[i]=grays[i%4];std::fill_n(tall.begin()+70*160,160*144,0xffffffff);
 ModView portrait;portrait.width=160;portrait.height=284;portrait.expanded=true;portrait.pixels=tall;
 auto color_tall=presentation.cartridge_sgb_colors(portrait,same->view());check(color_tall.height==284&&color_tall.expanded&&color_tall.pixels[1]==0xff00ff00,"uniform portrait SGB palette did not retain vertical margins");
 for(unsigned y=0;y<144;++y)check(std::equal(live.begin()+y*160,live.begin()+(y+1)*160,color_tall.pixels.begin()+(y+70)*160),"MASK1 portrait/current-load lost frozen center");
 check(same->snapshot()==uniform_frozen,"frozen presentation changed simulation state");send(s,0x17,{0},vram);s.complete_frame(native);
 for(size_t i=0;i<wide.size();++i)wide[i]=grays[i%4];
 send(s,0x04,{1,7,5,1,1,18,16},vram);s.complete_frame(native);view=s.view();check(!view.uniform_attributes&&view.colors[9+9*160]==0xffff0000&&view.colors[1]==0xff00ff00,"actual ATTR_BLK regions not applied");
 auto restricted=presentation.cartridge_sgb_colors(expanded,view);check(restricted.width==160&&!restricted.expanded,"nonuniform SGB margins were invented");
 auto frozen=copy(view.colors);auto attrs=std::vector<uint8_t>(view.attributes.begin(),view.attributes.end());send(s,0x17,{1},vram);
 for(unsigned p=0;p<4;++p)word(0x802+p*8,0x7c00);send(s,0x0b,{},vram);send(s,0x0a,{0,0,1,0,2,0,3,0},vram);send(s,0x04,{1,7,0,0,0,19,17},vram);s.complete_frame(native);
 view=s.view();check(view.mask_mode==1&&copy(view.colors)==frozen&&std::equal(attrs.begin(),attrs.end(),view.attributes.begin()),"MASK freeze published new palette/attribute metadata");
 auto checkpoint=s.snapshot();NativeSgbService loaded(true);loaded.restore(checkpoint);check(copy(loaded.view().colors)==frozen&&loaded.snapshot()==checkpoint,"current frozen SGB replay changed output/state");
 auto before=loaded.snapshot();(void)loaded.view();check(loaded.snapshot()==before,"read-only SGB view mutated protocol state");
 send(s,0x17,{0},vram);s.complete_frame(native);send(loaded,0x17,{0},vram);loaded.complete_frame(native);check(s.snapshot()==loaded.snapshot()&&s.view().colors[1]==0xff0000ff,"unfreeze/current-load replay diverged");
 std::fill(vram.begin(),vram.end(),0);for(unsigned y=0;y<8;++y)vram[0x800+y*2]=0xff;send(s,0x13,{0},vram);check(!s.view().border_ready,"CHR_TRN alone fabricated border readiness");
 std::fill(vram.begin(),vram.end(),0);word(0x1002,0x4104);send(s,0x14,{},vram);check(!s.view().border_ready,"border transfer bypassed completed publication");s.complete_frame(native);view=s.view();check(view.border_ready&&view.commands[0x13]==1&&view.commands[0x14]==1,"native transfer border state missing");
 check(view.border[0]==0xff214284&&view.border[48+40*256]==0&&view.border[255+223*256]==0xff214284,"authored 4bpp border channels/window differ");
 // A mid-frame received transfer must not replace published border art on query/load.
 word(0x1002,0x2218);send(s,0x14,{},vram);check(s.view().border[0]==0xff214284&&s.view().border_revision==2,"mid-frame border published early");
 NativeSgbService border_loaded(true);border_loaded.restore(s.snapshot());check(border_loaded.view().border[0]==0xff214284,"load published newer border packets");
 s.complete_frame(native);border_loaded.complete_frame(native);check(s.view().border[0]==0xffc68442&&s.snapshot()==border_loaded.snapshot(),"completed border replay or bit expansion differs");
 NativeSgbService mid(true);std::fill(vram.begin(),vram.end(),0);word(0x800,0x4104);word(0x802,0x2218);send(mid,0x0b,{},vram);send(mid,0x0a,{0,0,0,0,0,0,0,0},vram);mid.complete_frame(native);
 check(mid.view().colors[0]==0xff214284&&mid.view().colors[1]==0xffc68442,"game/border RGB555 expansion disagrees");
 std::vector<uint8_t> rom(32768);rom[0x146]=3;rom[0x100]=0xc3;rom[0x101]=0x50;rom[0x102]=1;rom[0x150]=0x18;rom[0x151]=0xfe;Options off;off.native=false;Machine baseline(rom,off);auto base=baseline.snapshot();check(!baseline.sgb_view().active&&baseline.sgb_view().supported,"default native profile enabled SGB detection");
 Options on=off;on.sgb_service=true;Machine active(rom,on);std::string error;auto current=active.snapshot();check(!active.restore(base,error)&&active.snapshot()==current,"cross-profile SGB load accepted or mutated owner");
 active.run_frame();active.take_audio();current=active.snapshot();check(active.restore(current,error)&&active.snapshot()==current,"current SGB machine snapshot changed");
 auto corrupt=current;corrupt[corrupt.size()-65]=255;auto hash=sha256(std::span(corrupt).first(corrupt.size()-64));std::copy(hash.begin(),hash.end(),corrupt.end()-64);check(!active.restore(corrupt,error)&&active.snapshot()==current,"invalid SGB state was not atomically rejected");
 check(uint32_t(base[0])==Machine::snapshot_schema,"snapshot schema did not use public constant");
 std::cout<<"PASS native SGB packet vectors,exact shades/regions,authored border,freeze metadata/current replay,read-only view,default/profile guards,atomic invalid-state rejection\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
