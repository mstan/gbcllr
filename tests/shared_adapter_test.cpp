#include <gbcllr/machine.hpp>
#include <cstring>
#include <iostream>
#include <stdexcept>
using namespace gbcllr;
static void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
static void hook(Machine&,void* user)noexcept{++*static_cast<unsigned*>(user);}
int main(int argc,char** argv){try{
    if(argc!=3)return 2;
    auto original=read_file(argv[1]);auto digest=sha256(original);
    Options options;options.model=Model::cgb;
    Machine machine(original,options);
    auto catalog=std::filesystem::path(argv[2]);std::filesystem::create_directories(catalog);
    auto* context=rmod_create();check(context,"context allocation");
    struct Destroy{rmod_context* p;~Destroy(){rmod_destroy(p);}} destroy{context};
    check(rmod_initialize(context,catalog.string().c_str(),"pokemon-crystal",digest.c_str()),rmod_error(context));
    check(rmod_commit(context,argv[1]),rmod_error(context));
    std::array<uint8_t,3> overlay_data{4,5,6};machine.import_overlay("scene-overlay",overlay_data);
    unsigned calls=0;machine.register_runtime_hook("fixture.hook",0x100,hook,&calls);
    auto before_ram=machine.debug_read(0xc000,1);uint8_t replacement=uint8_t(before_ram[0]^0x80);
    uint8_t before_cart=original[0x14f],cart=uint8_t(before_cart^1),overlay_expected=5,overlay_replacement=8;
    rmod_patch patches[]{
        {"ram",0xc000,before_ram.data(),&replacement,1},
        {"cartridge",0x14f,&before_cart,&cart,1},
        {"scene-overlay",1,&overlay_expected,&overlay_replacement,1}};
    rmod_hook valid[]{ {"fixture.hook"} };std::string error;
    check(machine.apply_mod_plan(context,"pokemon-crystal",patches,valid,error),error.c_str());
    check(machine.debug_read(0xc000,1)[0]==replacement,"RAM plan");
    check(machine.debug_read(0x14f,1)[0]==cart,"loaded cartridge plan");
    check(machine.overlay("scene-overlay")[1]==8,"content overlay plan");
    check(sha256(read_file(argv[1]))==digest,"original input changed");
    auto state=machine.snapshot();check(machine.restore(state,error),error.c_str());
    check(machine.debug_read(0x14f,1)[0]==cart,"restore lost loaded cartridge patch");
    check(machine.overlay("scene-overlay")[1]==8,"restore lost content overlay");
    auto accepted=machine.snapshot();
    check(!machine.apply_mod_plan(context,"pokemon-crystal",patches,{},error),"expected guard accepted twice");
    check(machine.snapshot()==accepted,"guard failure mutated state");
    uint8_t next=uint8_t(replacement^1);rmod_patch rollback[]{ {"ram",0xc000,&replacement,&next,1} };
    rmod_hook invalid[]{ {"fixture.hook"},{"fixture.missing"} };
    check(!machine.apply_mod_plan(context,"pokemon-crystal",rollback,invalid,error),"unknown hook accepted");
    check(machine.snapshot()==accepted,"failed activation did not roll back");
    rmod_patch conflict[]{rollback[0],rollback[0]};
    check(!machine.apply_mod_plan(context,"pokemon-crystal",conflict,{},error),"overlap accepted");
    check(machine.snapshot()==accepted,"overlap failure mutated state");
    machine.debug_step_instruction();check(calls==1,"registered hook did not execute");
    check(machine.stats().instructions_fallback==0,"interpreter executed");
    auto path=catalog/"unicode-\u03a9-state.bin";write_file_atomic(path,accepted);check(read_file(path)==accepted,"UTF-8 atomic write");
    std::cout<<"RAM/cartridge/overlay/hooks/guards/rollback/current-state/atomic-write pass\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
