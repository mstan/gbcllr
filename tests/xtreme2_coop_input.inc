#include "../src/xtreme2_coop.hpp"
static int xtreme2_coop_input(char** argv,bool shoulders=false) {
 DesktopConfig c;c.profile=argv[2];check(!std::filesystem::exists(c.profile),"New isolated controller profile required");
 std::filesystem::create_directories(c.profile);c.package=read_mod_package(argv[5],sha256(read_file(argv[3])));
 c.adaptive=true;c.coop_enabled=true;c.source=c.partner_source=2;c.scale=2;c.volume=0;c.audio=false;c.shortcuts=false;
 if(shoulders){SDL_setenv("LNG_TEST_HIDDEN","1",1);SDL_setenv("LNG_SCRIPT","[]",1);SDL_setenv("SDL_AUDIODRIVER","dummy",1);}
 Options options;options.model=Model::cgb;options.boot_rom=read_file(argv[4]);options.mods={c.package->feature};
 options.mods[0].weapon_cycle=c.weapon_cycle=shoulders;
 Machine game(read_file(argv[3]),options);std::string error;
 check(game.restore(read_file(argv[6]),error),error.c_str());game.mod_viewport(512,144);
 check(!SDL_Init(SDL_INIT_GAMECONTROLLER),"SDL controller setup failed");
 SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS,"1");
 SDL_Joystick* handles[2]{};
 for(unsigned i=0;i<2;++i) {
  int index=SDL_JoystickAttachVirtual(SDL_JOYSTICK_TYPE_GAMECONTROLLER,SDL_CONTROLLER_AXIS_MAX,SDL_CONTROLLER_BUTTON_MAX,0);
  check(index>=0&&SDL_IsGameController(index),"Virtual game controller unavailable");handles[i]=SDL_JoystickOpen(index);check(handles[i],"Virtual joystick open failed");
  char guid[64]{};SDL_JoystickGetGUIDString(SDL_JoystickGetDeviceGUID(index),guid,sizeof guid);
  if(i){c.partner_guid=guid;c.partner_instance=uint32_t(SDL_JoystickInstanceID(handles[i]))+1;}
  else{c.gamepad_guid=guid;c.gamepad_instance=uint32_t(SDL_JoystickInstanceID(handles[i]))+1;}
 }
 struct Probe {
  SDL_Joystick *first,*second;unsigned character;bool shoulders;std::array<Xtreme2Coop::Actor,2> start{},middle{},end{};
  std::array<uint8_t,2> previous_weapons{};std::array<uint8_t,10> energy{};
  static bool present(void* context,uint32_t tick,const Machine& game,std::span<const int16_t>) {
   auto& p=*static_cast<Probe*>(context);
   if(tick==40||tick==70||tick==100) {
    Xtreme2Coop state(p.character);state.restore(game.coop_state());auto actors=state.actors();
    if(tick==40){p.start=actors;check(!SDL_JoystickSetVirtualButton(p.first,SDL_CONTROLLER_BUTTON_DPAD_LEFT,1),"First controller input failed");}
    if(tick==70){p.middle=actors;SDL_JoystickSetVirtualButton(p.first,SDL_CONTROLLER_BUTTON_DPAD_LEFT,0);SDL_JoystickSetVirtualButton(p.second,SDL_CONTROLLER_BUTTON_DPAD_RIGHT,1);}
    if(tick==100){p.end=actors;SDL_JoystickSetVirtualButton(p.second,SDL_CONTROLLER_BUTTON_DPAD_RIGHT,0);write_file_atomic("input-ready.state",game.snapshot());}
    SDL_JoystickUpdate();
   }
   if(p.shoulders&&(tick==120||tick==140||tick==160)) {
    Xtreme2Coop state(p.character);state.restore(game.coop_state());auto weapons=state.weapons();auto current_energy=game.debug_read(0xc06d,10);
    if(tick==120){p.previous_weapons=weapons;std::copy(current_energy.begin(),current_energy.end(),p.energy.begin());SDL_JoystickSetVirtualButton(p.first,SDL_CONTROLLER_BUTTON_LEFTSHOULDER,1);}
    else {
     unsigned seat=tick==140?0:1;auto expected=p.previous_weapons;
     if(!(p.character^seat)) {
      unsigned next=expected[seat];do{next=(next+(seat?1:9))%10;}while(next&&!(p.energy[next]&0x40));expected[seat]=uint8_t(next);
     }
     check(weapons==expected,"Actual SDL shoulder crossed logical seats or selected incorrectly");
     check(std::equal(current_energy.begin(),current_energy.end(),p.energy.begin()),"Actual SDL shoulder changed weapon energy");p.previous_weapons=weapons;
     if(tick==140){SDL_JoystickSetVirtualButton(p.first,SDL_CONTROLLER_BUTTON_LEFTSHOULDER,0);SDL_JoystickSetVirtualButton(p.second,SDL_CONTROLLER_BUTTON_RIGHTSHOULDER,1);}
     else{SDL_JoystickSetVirtualButton(p.second,SDL_CONTROLLER_BUTTON_RIGHTSHOULDER,0);write_file_atomic("shoulder-input-ready.state",game.snapshot());}
    }
    SDL_JoystickUpdate();
   }
   return tick<(p.shoulders?181:101);
  }
 } probe{handles[0],handles[1],c.package->feature.player1_character,shoulders};
 DesktopFrameQa qa{&probe,nullptr,Probe::present};desktop(game,{},true,c,qa);
 std::cout<<"Device sample: "<<probe.start[0].x<<","<<probe.start[1].x<<" -> "<<probe.middle[0].x<<","<<probe.middle[1].x<<" -> "<<probe.end[0].x<<","<<probe.end[1].x<<"\n";
 check(probe.middle[0].x<probe.start[0].x&&probe.middle[1].x==probe.start[1].x,"First device did not exclusively control P1");
 check(probe.end[1].x>probe.middle[1].x&&probe.end[0].x==probe.middle[0].x,"Second device did not exclusively control P2");
 check(!std::filesystem::exists(c.profile/"native.sav"),"Controller check wrote a native save");
 std::cout<<"Actual SDL device isolation passed: "<<probe.start[0].x<<","<<probe.start[1].x<<" -> "<<probe.middle[0].x<<","<<probe.middle[1].x<<" -> "<<probe.end[0].x<<","<<probe.end[1].x<<"\n";
 if(shoulders)std::cout<<"Actual SDL virtual shoulders preserve per-seat weapon ownership, energy and Zero technique controls\n";
 return 0;
}
