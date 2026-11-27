#include "native_sgb_service.hpp"
#include <algorithm>
#include <cstring>
#include <stdexcept>
namespace gbcllr {
namespace {
uint32_t rgb(uint16_t c){auto expand=[](unsigned n){return (n<<3)|(n>>2);};return 0xff000000u|expand(c&31)<<16|expand((c>>5)&31)<<8|expand((c>>10)&31);}
void put(std::vector<uint8_t>& out,const void* p,size_t n){auto b=static_cast<const uint8_t*>(p);out.insert(out.end(),b,b+n);}
struct Reader {std::span<const uint8_t> data;void get(void* p,size_t n){if(n>data.size())throw std::runtime_error("Truncated native SGB state");std::memcpy(p,data.data(),n);data=data.subspan(n);}};
}
NativeSgbService::NativeSgbService(bool dmg):state_(llr_sgb_create(),llr_sgb_destroy),dmg_(dmg){if(!state_)throw std::bad_alloc();llr_sgb_set_enabled(state_.get(),true);}
uint8_t NativeSgbService::read(uint8_t base){NativeSgbContext c{state_.get(),nullptr,&frame_};return llr_sgb_modify_joyp_read(&c,base);}
void NativeSgbService::write(uint8_t value,const uint8_t* vram){NativeSgbContext c{state_.get(),vram,&frame_};llr_sgb_on_joyp_write(&c,value);}
bool NativeSgbService::freezes_frame() const {return llr_sgb_mask_mode(state_.get())==1;}
void NativeSgbService::refresh_border(){
 auto revision=llr_sgb_border_revision(state_.get());
 if(llr_sgb_border_ready(state_.get())&&(!border_ready_||revision!=border_revision_)){
  llr_sgb_capture_border(state_.get(),&published_border_);llr_sgb_render_border_data(&published_border_,border_.data());border_ready_=true;
 }
 border_revision_=revision;
}
void NativeSgbService::rebuild_colors(){for(size_t i=0;i<colors_.size();++i)colors_[i]=rgb(frame_.color_framebuffer[i]);for(size_t i=0;i<16;++i)palette_rgb_[i]=rgb(palette_[i]);}
void NativeSgbService::complete_frame(std::span<const uint32_t> native){
 if(native.size()!=160*144)throw std::runtime_error("Invalid native SGB frame");
 shades_valid_=dmg_;
 for(size_t i=0;i<native.size();++i){auto p=native[i];unsigned r=(p>>16)&255,g=(p>>8)&255,b=p&255;
  if(r!=g||g!=b||(r!=0&&r!=85&&r!=170&&r!=255)){shades_valid_=false;frame_.framebuffer[i]=0;}else frame_.framebuffer[i]=uint8_t(3-r/85);
 }
 frame_mask_=llr_sgb_mask_mode(state_.get());
 // MASK freeze publishes the preceding live image and its matching metadata,
 // even while new palettes/attributes are arriving for the next live frame.
 if(frame_mask_!=1){frame_palette_=llr_sgb_palettes_active(state_.get());
  std::copy_n(llr_sgb_palette_data(state_.get()),16,palette_.begin());std::copy_n(llr_sgb_attribute_data(state_.get()),360,attributes_.begin());}
 frame_valid_=true;
 if(shades_valid_){NativeSgbContext c{state_.get(),nullptr,&frame_};llr_sgb_apply_to_frame(&c);}rebuild_colors();refresh_border();
}
NativeSgbView NativeSgbService::view() const {
 NativeSgbView v;v.supported=true;v.active=true;v.colors_qualified=dmg_;v.colors_ready=frame_valid_&&frame_palette_&&shades_valid_;v.border_ready=border_ready_;
 v.mask_mode=frame_mask_;v.packets=llr_sgb_packet_count(state_.get());v.border_revision=border_revision_;for(unsigned i=0;i<32;++i)v.commands[i]=llr_sgb_command_count(state_.get(),i);
 v.uniform_attributes=std::all_of(attributes_.begin(),attributes_.end(),[&](uint8_t n){return n==attributes_[0];});
 if(v.colors_ready)v.colors=colors_;if(v.border_ready)v.border=border_;v.palettes=palette_rgb_;v.attributes=attributes_;return v;
}
std::vector<uint8_t> NativeSgbService::snapshot() const {
 std::vector<uint8_t> result(llr_sgb_state_size());if(!llr_sgb_save_state(state_.get(),result.data(),result.size()))throw std::runtime_error("Cannot save native SGB state");
 put(result,&frame_,sizeof frame_);put(result,&published_border_,sizeof published_border_);put(result,&border_revision_,sizeof border_revision_);const uint8_t flags[]={uint8_t(frame_valid_),uint8_t(frame_palette_),uint8_t(shades_valid_),frame_mask_,uint8_t(border_ready_)};put(result,flags,sizeof flags);
 put(result,palette_.data(),sizeof palette_);put(result,attributes_.data(),sizeof attributes_);return result;
}
void NativeSgbService::restore(std::span<const uint8_t> data){
 auto size=llr_sgb_state_size();if(data.size()!=size+sizeof frame_+sizeof published_border_+sizeof border_revision_+5+sizeof palette_+sizeof attributes_)throw std::runtime_error("Native SGB state size mismatch");
 if(!llr_sgb_load_state(state_.get(),data.data(),size))throw std::runtime_error("Invalid native SGB packet state");
 Reader r{data.subspan(size)};r.get(&frame_,sizeof frame_);for(auto shade:frame_.framebuffer)if(shade>3)throw std::runtime_error("Invalid native SGB shade");
 r.get(&published_border_,sizeof published_border_);r.get(&border_revision_,sizeof border_revision_);
 uint8_t flags[5];r.get(flags,5);for(unsigned i=0;i<3;++i)if(flags[i]>1)throw std::runtime_error("Invalid native SGB frame flag");frame_valid_=flags[0];frame_palette_=flags[1];shades_valid_=flags[2];frame_mask_=flags[3];border_ready_=flags[4]!=0;if(flags[4]>1)throw std::runtime_error("Invalid native SGB border flag");if(frame_mask_>3)throw std::runtime_error("Invalid native SGB frame mask");
 if(shades_valid_&&!dmg_)throw std::runtime_error("Unqualified native SGB color profile");
 r.get(palette_.data(),sizeof palette_);r.get(attributes_.data(),sizeof attributes_);for(auto a:attributes_)if(a>3)throw std::runtime_error("Invalid native SGB attribute");
 rebuild_colors();if(border_ready_)llr_sgb_render_border_data(&published_border_,border_.data());
}
}
