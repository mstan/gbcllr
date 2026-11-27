#pragma once
#include <array>
#include <cstdint>
#include <span>
#include <vector>
#include <memory>
namespace gbcllr {
struct ModContext;
class Machine;
class Xtreme2Controls;
// One original controller call is projected over two retained player bodies.
// Simulation state and private native menus belong to the Machine snapshot.
class Xtreme2Coop {
 friend struct RewindChildFixture;
public:
 explicit Xtreme2Coop(unsigned first);
 ~Xtreme2Coop();
 Xtreme2Coop(Xtreme2Coop&&)noexcept;
 Xtreme2Coop& operator=(Xtreme2Coop&&)noexcept;
 void inputs(uint8_t first,uint8_t second);
 void instruction(ModContext&,std::span<uint16_t,6>,std::span<uint8_t>,std::span<uint8_t>,Xtreme2Controls* controls=nullptr);
 void write(const ModContext&,uint16_t,uint8_t);
 uint8_t read(const ModContext&,uint16_t,uint8_t)const;
 std::vector<uint8_t> snapshot(bool simulation=false,bool sealed_child=true)const;
 void restore(std::span<const uint8_t>,bool sealed_child=true);
 struct Actor {int x,y;unsigned hp,character,action,status,max_hp;};
 std::array<Actor,2> actors(bool presented=false)const;
 std::array<int,2> camera(unsigned actor)const;
 std::array<int,2> shared_camera()const;
 void shared_world(bool enabled,unsigned width);
 unsigned transport(unsigned actor)const{return state_.transport.at(actor).phase;}
 bool sprite_visible(unsigned index)const;
 unsigned sprite_owner(unsigned index)const{return index<1024?state_.presented_palette[index].owned:0;}
 struct Piece {int16_t x,y;uint8_t tile,attributes;};
 struct Transport {
  uint8_t phase=0,timer=0,hold=0,count=0;
  int32_t x=0,y=0,offset=0;
  std::array<int32_t,2> camera{},bounds{};
  std::array<Piece,64> pieces{};
  std::array<uint8_t,0x2000> texture{};
  std::array<uint32_t,4> colors{};
 };
 const Transport* transport_pose(unsigned actor)const;
 const Transport& presentation_transport(unsigned actor)const{return state_.presented_transport.at(actor);}
 struct Attachment {
  uint16_t key=0;
  uint8_t active=0,count=0,slot=0,age=0;
  int32_t x=0,y=0;
  std::array<int32_t,2> camera{};
  std::array<Piece,64> pieces{};
  std::array<uint8_t,0x2000> texture{};
  std::array<uint32_t,4> colors{};
 };
 bool capture_attachment(const ModContext&,std::span<const Piece>,unsigned);
 const Attachment* bike_attachment(unsigned)const;
 bool attachment_oam(unsigned,std::span<const uint8_t>)const;
 unsigned passes()const{return state_.passes;}
 std::array<uint8_t,2> weapons()const{return {state_.body[0].ram[0x31],state_.body[1].ram[0x31]};}
 std::array<int,4> activation_bounds(const ModContext&)const;
 bool bike_actor()const{return state_.bikes.phase!=0;}
 struct Bikes {bool active;std::array<uint32_t,2> mounts,passes;uint32_t scrolls,ends;std::array<uint8_t,2> riding,mounted;std::array<uint32_t,2> exits;};
 Bikes bikes()const {auto ready=[&](unsigned i){const auto& b=state_.body[i].ram;return uint8_t(b[2]==0x4c&&b[0xcc]&&!b[0xf4]);};return {bool(state_.bikes.active),state_.bikes.mounts,state_.bikes.passes,state_.bikes.scrolls,state_.bikes.ends,{ready(0),ready(1)},{uint8_t(bool(state_.body[0].ram[0xe5])),uint8_t(bool(state_.body[1].ram[0xe5]))},state_.bikes.exits};}
 unsigned anchor()const{return state_.anchor;}
 unsigned departures()const{return state_.departures;}
 unsigned arrivals()const{return state_.arrivals;}
 struct Combat {std::array<uint32_t,2> hits;unsigned defeated_owner,defeated_key;};
 Combat combat()const{return {state_.damage_events,state_.defeated_owner,state_.defeated_key};}
 struct Reward {unsigned owner,stage,grants;};
 Reward reward()const{return {state_.reward_owner,state_.reward_stage,state_.reward_grants};}
 struct Lifecycle {std::array<uint8_t,2> status;unsigned pending,retries;}; // 0 active, 1 fallen, 2 withdrawn.
 Lifecycle lifecycle()const{return {state_.status,state_.join_pending,state_.team_retries};}
 void menu_frame(Machine&);
 void restore_menu(Machine&);
 bool menus_active()const{return state_.menu.active;}
 std::span<const uint32_t> menu_world_native(std::span<const uint32_t> current)const;
 bool draw_menus(std::span<uint32_t>,unsigned,std::span<const uint32_t>,int focus)const;
 struct Menus {unsigned active,primary,frames,ready,idle;std::array<uint8_t,2> page,cursor;};
 Menus menus()const{return {state_.menu.active,state_.menu.primary,state_.menu.frames,state_.menu.ready,state_.menu.last_idle,state_.menu.page,state_.menu.cursor};}
 void palette(ModContext&,unsigned,unsigned);
 void player_piece(unsigned,uint8_t);
 bool player_drawing()const{return state_.phase!=0;}
 void projectile_piece(const ModContext&,unsigned,uint8_t,uint8_t tile=0);
 void attachment_piece(const ModContext&,unsigned,uint8_t,uint8_t);
 void publish(bool);
 void complete_frame(const ModContext&);
 const uint32_t* sprite_palette(unsigned)const;
 const uint8_t* sprite_texture(unsigned)const;
 const uint8_t* sprite_tile(unsigned)const;
 void draw_hud(const ModContext&,std::span<uint32_t>,unsigned,std::span<const uint32_t>,int focus=-1,bool split=false)const;
private:
 struct Body {
  std::array<uint8_t,256> ram{};
  std::array<uint8_t,12> graphics{}; // FFB0..FFBB original animation/graphics producer.
  std::array<uint8_t,8> palette{}; // Native character palette workspace C08D.
  std::array<uint8_t,4> equipment{}; // C083/C084/C0D3/C0D4 active character workspace.
  std::array<uint8_t,10> personal{};
  std::array<uint8_t,10> hitbox{}; // CAAE..CAB7 native projected collision workspace.
 };
 struct State {
  uint32_t schema=16,first=0,phase=0,initialized=0,passes=0;
  uint32_t stage=0,checkpoint=0;
  uint8_t anchor=0,pending_anchor=0;
  uint8_t scene_owner=0,world_previous=0,shared=1;
  uint8_t pending_checkpoint=0;
  std::array<int32_t,2> pending_bounds{};
  uint32_t shared_width=384,departures=0,arrivals=0;
  std::array<Transport,2> transport{},presented_transport{};
  std::array<Body,2> body{};
  std::array<Body,2> presented_body{};
  std::array<std::array<int32_t,2>,2> cameras{},presented_cameras{};
  std::array<int32_t,2> scan_vertical{};
  uint8_t scan_vertical_changed=0;
  struct Bikes {
   uint8_t active=0,phase=0,done=0;
   std::array<std::array<uint8_t,256>,2> body{};
   std::array<int32_t,2> bounds{};
   std::array<uint16_t,6> entry{};
   std::array<uint32_t,2> mounts{},passes{};
   uint32_t scrolls=0,ends=0;
   std::array<uint32_t,2> exits{};
   std::array<Body,2> riders{};
   std::array<uint8_t,2> returning{};
   std::array<Attachment,2> attachments{},presented_attachments{};
  } bikes;
  struct Palette {std::array<uint32_t,4> colors{};uint32_t owned=0,tile_known=0;std::array<uint8_t,32> tile{};}; // Body1/2, projectile3/4.
  std::array<Palette,1024> build_palette{},active_palette{},presented_palette{};
  using Textures=std::array<std::array<uint8_t,0x2000>,2>;
  Textures build_texture{},active_texture{},presented_texture{};
  std::array<uint16_t,6> entry{},exit{};
  std::array<uint8_t,2> held{},previous{},native_input{};
  std::array<uint8_t,2048> first_graphics{};
  std::array<uint16_t,2> graphics_base{},graphics_size{};
  uint16_t graphics_start=0,graphics_end=0;
  std::array<uint8_t,656> object_owner{};
  std::array<uint8_t,656> object_weapon{};
  std::array<uint8_t,656> object_package{};
  std::array<uint8_t,2> current_package{1,1};
  uint16_t actor_return=0,actor_sp=0,actor_key=0;
  uint8_t actor_owner=0,actor_source=0;
  std::array<uint8_t,656> last_damage{};
  std::array<uint32_t,2> damage_events{};
  uint16_t collision_sp=0,collision_return=0,collision_bank=0,collision_key=0,hit_key=0,defeated_key=0;
  uint8_t collision_owner=0,hit_owner=0,defeated_owner=0;
  uint8_t original_character=0,sound_pending=0,tile_base=0;
  uint8_t reward_owner=0,reward_stage=0,area_owner=0;
  uint8_t door_owner=0;
  uint16_t door_key=0;
  uint32_t reward_grants=0;
  std::array<uint8_t,2> status{},presented_status{},select{},select_previous{},select_hold{};
  std::array<uint8_t,8> death_globals{}; // C00D..C013 and C0AC during one solo death controller.
  uint8_t death_isolated=0,join_pending=0,retry_pending=0,retry_initializing=0;
  uint32_t team_retries=0;
  struct Menu {
   uint8_t active=0,primary=0,request=0,resume=0,stage_exit=0,returned=0,idle=0,last_idle=0,selecting=0,ready=0,busy=0,options_save=0;
   std::array<uint8_t,2> raw{},previous{},choice{},page{},cursor{};
   std::array<uint8_t,8> sounds{};
   uint8_t sound_count=0;
   uint32_t frames=0;
  } menu;
 } state_;
 std::unique_ptr<Machine> menu_copy_;
 std::vector<uint8_t> menu_saved_;
 bool menu_saved_sealed_=true; // Derived codec provenance, never guest state.
 std::vector<uint32_t> menu_pixels_,menu_native_;
 void capture(Body&,std::span<uint8_t>,std::span<uint8_t>);
 static void project(const Body&,std::span<uint8_t>,std::span<uint8_t>);
 static uint8_t native_buttons(uint8_t);
 static int object_key(const ModContext&,unsigned);
 static uint8_t peek(const ModContext&,unsigned);
 static std::vector<uint8_t> graphics(const ModContext&,unsigned,unsigned);
 static uint8_t rom_byte(const ModContext&,unsigned,unsigned);
 bool scene_active(const ModContext&)const;
 static bool encounter_active(const ModContext&);
 void depart(const ModContext&,unsigned);
 void world_tick(const ModContext&,std::span<uint8_t>,std::span<uint8_t>);
 void isolate_death(unsigned,std::span<uint8_t>);
 void participation(const ModContext&,std::span<uint8_t>,std::span<uint8_t>);
 bool menu_eligible(unsigned)const;
 void static_package(const ModContext&,unsigned);
 void common_items(std::span<uint8_t>);
 void bike_project(unsigned,std::span<uint8_t>);
 void bike_capture(unsigned,std::span<uint8_t>);
 void bike_idle(std::span<uint8_t>);
 void bike_depart(unsigned);
 void bike_arrive(unsigned);
 void bike_write(const ModContext&,uint16_t,uint8_t);
 bool bike_hook(ModContext&,std::span<uint16_t,6>,std::span<uint8_t>,std::span<uint8_t>);
 void menu_begin(ModContext&,std::span<uint16_t,6>,std::span<uint8_t>,std::span<uint8_t>);
 void menu_hook(unsigned,unsigned,std::span<uint16_t,6>,std::span<uint8_t>,std::span<uint8_t>);
 void menu_step(Machine&,unsigned,uint8_t,uint64_t);
};
}
