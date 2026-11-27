// Adapted from Megaman Xtreme 2 e4649e231cc239a0f220f8afe0ae996916e0c6cf.
#include "native_mods.hpp"
#include "oracle_native_mods.hpp"
#include "sml2_native_mods.hpp"
#include "gen2_native_mods.hpp"
#include "gen1_native_mods.hpp"
#include "xtreme2_coop.hpp"
#include "xtreme2_controls.hpp"
#include "megaman_native_mods.hpp"
#include "megaman_titles.hpp"
#include <algorithm>
#include <climits>
#include <cstring>
#include <stdexcept>
#include <type_traits>
namespace gbcllr {
namespace {
using GBContext=ModContext;
constexpr int MAX_SPRITES=1024,MAX_WIDTH=4096;
constexpr int LCDC_OBJ_SIZE=4,LCDC_BG_TILEMAP=8,LCDC_WINDOW_ENABLE=32;
#include "mmx2_map.hpp"
struct Sprite {int x,y;uint8_t tile,attr,weather;};
}
struct NativeMods::Impl {
struct State {
    int valid, bank, cx, cy, left, frame, count, build_count;
    int bound_left,bound_right,spawn_left,spawn_right,previous_left,previous_right;
    int stage,checkpoint,map_left,map_right;
    unsigned captures, rescues,spawn_scans,spawn_reads;
    uint8_t map_wram[0x8000];
    uint8_t vram[0x4000], bg[64], obj[64], lcdc, wx, wy;
    Sprite sprite[MAX_SPRITES], build[MAX_SPRITES];
    uint8_t owned[40], build_owned[40], oam[160],early_actor[64];
    uint32_t bg_rgb[32],obj_rgb[32];
} s{},presented{};
static bool story_ui(const State& frame) {
    return frame.map_wram[0x94d]&&!frame.map_wram[0x94f]&&
        (!frame.map_wram[0x9b0]||frame.map_wram[0xc2]||frame.map_wram[0xc]);
}
bool has_presented=false;
bool atomic_sprites=false,smooth_gameplay=false;
int requested=-1,gb_custom_width=256,render_left=0;
std::vector<uint8_t> opaque=std::vector<uint8_t>(MAX_WIDTH*144),priority=std::vector<uint8_t>(MAX_WIDTH*144);
std::vector<uint32_t> frame_pixels;
uint8_t peek(GBContext *ctx, unsigned a) {
    a&=65535;
    if (a<0x4000) return ctx->rom && a<ctx->rom_size ? ctx->rom[a]:0;
    if (a<0x8000) { unsigned o=ctx->rom_bank*0x4000+a-0x4000; return ctx->rom && o<ctx->rom_size?ctx->rom[o]:0; }
    if (a>=0xa000 && a<0xc000) { unsigned o=ctx->ram_bank*0x2000+a-0xa000; return ctx->eram && o<ctx->eram_size?ctx->eram[o]:0; }
    if (a>=0xc000 && a<0xd000) return ctx->wram[a-0xc000];
    if (a>=0xd000 && a<0xe000) return ctx->wram[ctx->wram_bank*0x1000+a-0xd000];
    if (a>=0xff80 && a<0xffff) return ctx->hram[a-0xff80];
    return 0;
}
int word(GBContext *ctx,unsigned a) { return peek(ctx,a)|(peek(ctx,a+1)<<8); }
uint8_t bank_byte(GBContext *ctx,int bank,int a) {
    unsigned o=bank*0x4000u+(unsigned)(a-0x4000);
    return a>=0x4000&&a<0x8000&&o<ctx->rom_size?ctx->rom[o]:0;
}
bool capture_actor(GBContext *ctx,bool body,bool ownership) {
    int page=peek(ctx,0xffa2),base=page<<8;
    if(page<0xa0||page>=0xe0)return false;
    int bank=peek(ctx,base+0x14),animation=word(ctx,base+0x12);
    if(!animation)return false;
    int ptr=bank_byte(ctx,bank,animation)|(bank_byte(ctx,bank,animation+1)<<8);
    if(!bank_byte(ctx,bank,ptr))return false;
    int x=word(ctx,base+8)-8,y=word(ctx,base+5)-16;
    int flip=peek(ctx,base+0xf)&0x40,flash=peek(ctx,base+0x45)>0;
    int slot=peek(ctx,0xcad5)/4,emitted=!peek(ctx,0xcad6);
    for(int n=0;n<128;n++) {
        int dy=(int8_t)bank_byte(ctx,bank,ptr+2+n*4);
        if(dy==127)break;
        int dx=(int8_t)bank_byte(ctx,bank,ptr+3+n*4);
        if(body&&s.build_count<MAX_SPRITES) {
            Sprite *sp=&s.build[s.build_count++];sp->x=x+(flip?8-dx:dx);sp->y=y+dy;
            sp->tile=bank_byte(ctx,bank,ptr+4+n*4);
            sp->attr=bank_byte(ctx,bank,ptr+5+n*4);
            sp->weather=peek(ctx,base+0x2d)==0x43;
            if(flash)sp->attr|=0x13;
            if(flip)sp->attr^=0x20;
            s.captures++;
        }
        if(ownership&&emitted&&slot<40)s.build_owned[slot]=1;
        slot=(slot+1)&63;
    }
    return true;
}
uint32_t color(const uint8_t *palette,int i) {
    int c=palette[i*2]|(palette[i*2+1]<<8);
    return 0xff000000u|((c&31)*255/31<<16)|(((c>>5)&31)*255/31<<8)|(((c>>10)&31)*255/31);
}
int view_left(const State& s,int cx,int width) {
    int left=cx-(width-160)/2;
    if(s.bound_right-s.bound_left<=width)return s.bound_left-(width-(s.bound_right-s.bound_left))/2;
    if(left<s.bound_left)left=s.bound_left;
    if(left>s.bound_right-width)left=s.bound_right-width;
    return left;
}
uint8_t read_override(GBContext *ctx,uint16_t address,uint8_t value,const Xtreme2Coop* cooperative=nullptr) {
    unsigned pc=ctx->pc;
    // The original stage loader clears CE00..CEFD here. Its first scan
    // must compare against the native previous view, not margins retained
    // from the preceding stage. Observe the opcode fetch even when the
    // world view is invalid or currently native-width; preserve its byte.
    if(ctx->sp!=UINT16_MAX&&ctx->rom_bank==0x3d&&address==0x44c6&&ctx->pc==0x44c7&&value==0x21) {
        s.spawn_left=s.spawn_right=s.previous_left=s.previous_right=0;
    }
    // Pair every completed native scan with the footprint it actually
    // used. Native-width/menu scans must publish zero extra margins before
    // a later resize or return to the world starts another wide scan.
    if(ctx->sp!=UINT16_MAX&&ctx->rom_bank==0x3d) {
        if(address==0xca08&&ctx->instruction_pc==0x44d4&&pc==0x44d5) {
            s.spawn_left=s.spawn_right=0;
            if(s.valid&&(gb_custom_width>160||cooperative)) {
                int camera=word(ctx,0xca08),left=view_left(s,camera,gb_custom_width);
                s.spawn_left=camera-left;s.spawn_right=left+gb_custom_width-(camera+160);
                if(cooperative){auto bounds=cooperative->activation_bounds(*ctx);s.spawn_left=camera-bounds[0];s.spawn_right=bounds[1]-(camera+160);}
            }
            s.spawn_scans++;
        }
        if(address==0xca05&&ctx->instruction_pc==0x454e&&pc==0x4551) {
            s.previous_left=s.spawn_left;s.previous_right=s.spawn_right;
        }
    }
    if(!s.valid||(gb_custom_width<=160&&!cooperative))return value;
    if(cooperative&&ctx->sp!=UINT16_MAX&&(address==0xca05||address==0xca06)&&ctx->d>=0xa0&&ctx->d<0xe0) {
        int guard=-1;
        if(pc==0x3502||pc==0x3508||pc==0x3592||pc==0x3598)guard=64;
        if(pc==0x354a||pc==0x3550)guard=0;
        if(guard>=0){auto bounds=cooperative->activation_bounds(*ctx);int camera=word(ctx,0xca05),y=word(ctx,(ctx->d<<8)+5);
            int relative=int16_t(y-camera);bool inside=y>=bounds[2]-guard&&y<bounds[3]+(guard?92:0);
            bool native_inside=relative>=-guard&&relative<144+(guard?92:0);
            if(inside!=native_inside){unsigned virtual_camera=unsigned(inside?y:y-512)&65535;return address==0xca05?uint8_t(virtual_camera):uint8_t(virtual_camera>>8);}
        }
    }
    if(ctx->sp!=UINT16_MAX&&address==0xcb82&&
       ((ctx->instruction_pc==0x1327&&pc==0x132a)||(ctx->instruction_pc==0x139a&&pc==0x139d))&&
       peek(ctx,0xc05f)==6&&peek(ctx,0xc02d)) {
        // The logical player pose was captured at 1323. Once it is wholly
        // outside the native band, put only its physical OAM below the LCD.
        // Offscreen X alone would still consume native scanline sprite slots.
        bool alternate=ctx->instruction_pc==0x139a;int actor=0xc100;
        if(alternate) {
            int page=peek(ctx,0xffa2);if(page<0xa0||page>=0xe0)return value;actor=page<<8;
            // The mounted trail is a native player attachment. Other users
            // of the alternate emitter keep their original presentation.
            if(peek(ctx,actor+0x2d)!=0x35||peek(ctx,actor+0xb)!=0xc1)return value;
        }
        int count=peek(ctx,ctx->hl),min_x=INT_MAX,max_x=INT_MIN,min_y=INT_MAX,max_y=INT_MIN;
        if(count>0&&count<=64)for(int n=0;n<count;n++) {
            int ptr=ctx->hl+2+n*4,dy=int8_t(peek(ctx,ptr));if(dy==127&&!alternate)break;
            int dx=int8_t(peek(ctx,ptr+1));if(peek(ctx,alternate?0xffcc:0xc16b))dx=8-dx;
            min_x=std::min(min_x,dx);max_x=std::max(max_x,dx);
            min_y=std::min(min_y,dy);max_y=std::max(max_y,dy);
        }
        int expected=int16_t(word(ctx,actor+8)-word(ctx,0xca08))+8;
        int delta=int8_t(peek(ctx,0xcb80)-uint8_t(expected)),rx=expected+delta;
        if(min_y!=INT_MAX&&max_y-min_y<96&&delta>=-64&&delta<=64&&
           (rx+max_x<=0||rx-8+min_x>=160))return uint8_t(160-min_y);
    }
    if(ctx->sp!=UINT16_MAX&&ctx->rom_bank==1&&pc==unsigned(ctx->instruction_pc)+1&&
       peek(ctx,0xc05f)==6&&peek(ctx,0xc02d)) {
        bool lp=(address==0xc108&&ctx->instruction_pc==0x44ce)||(address==0xc109&&ctx->instruction_pc==0x44cf);
        bool lr=(address==0xca0d&&ctx->instruction_pc==0x44ee)||(address==0xca0e&&ctx->instruction_pc==0x44f0);
        bool rp=(address==0xc108&&ctx->instruction_pc==0x448b)||(address==0xc109&&ctx->instruction_pc==0x448c);
        bool rr=(address==0xca40&&ctx->instruction_pc==0x44a5)||(address==0xca41&&ctx->instruction_pc==0x44a7);
        if(lp||lr||rp||rr) {
            // Move both the guard and its clamp target to the visible edge.
            // Adjusting only the fatal check lets the old clamp push the
            // player through terrain as the native camera overtakes them.
            bool left=lp||lr;int rail=word(ctx,left?0xca0d:0xca40);
            int edge=view_left(s,rail,gb_custom_width)+(left?0:gb_custom_width);
            int adjusted=(lp||rp)?word(ctx,0xc108)+(left?rail-edge:rail+160-edge):edge-(left?0:160);
            bool high=address==0xc109||address==0xca0e||address==0xca41;
            return high?uint8_t(unsigned(adjusted)>>8):uint8_t(adjusted);
        }
    }

    if(address==0xca08||address==0xca09) {
        // Mounted state 4C's autoscroll death test compares the player
        // against the left camera rail. Its local camera value is used
        // only for that test; terrain, vehicle motion and hazards retain
        // their original coordinates and collision rules.
        if(ctx->sp!=UINT16_MAX&&ctx->rom_bank==0x2c&&peek(ctx,0xc05f)==6&&peek(ctx,0xc02d)&&
           ((address==0xca08&&ctx->instruction_pc==0x75f6&&pc==0x75f7)||
            (address==0xca09&&ctx->instruction_pc==0x75f7&&pc==0x75f8))) {
            unsigned left=unsigned(view_left(s,word(ctx,0xca08),gb_custom_width))&65535;
            return address==0xca08?uint8_t(left):uint8_t(left>>8);
        }
        int guard=0,test=0;
        if(pc==0x34e0||pc==0x34e6){guard=64;test=1;}
        if(pc==0x3530||pc==0x3536||pc==0x2797||pc==0x279b)test=1;
        if(pc==0x3570||pc==0x3576){guard=144;test=1;}
        if(test && ctx->d>=0xa0&&ctx->d<0xe0) {
            int camera=word(ctx,0xca08),x=word(ctx,(ctx->d<<8)+8);
            int left=view_left(s,camera,gb_custom_width),right=left+gb_custom_width;
            if(cooperative){auto bounds=cooperative->activation_bounds(*ctx);left=bounds[0];right=bounds[1];}
            int native_rel=(int16_t)(x-camera);
            int inside=x>=left-guard&&x<right+guard+(cooperative&&guard?24:0);
            int old_inside=guard==64?(native_rel>=-64&&native_rel<248):guard==144?(native_rel>=-144&&native_rel<328):(native_rel>=0&&native_rel<160);
            if(inside!=old_inside) {
                unsigned virtual_camera=(unsigned)(inside?x:x-512)&65535;
                s.rescues++;
                return address==0xca08?(uint8_t)virtual_camera:(uint8_t)(virtual_camera>>8);
            }
        }
    }
    if(ctx->rom_bank!=0x3d)return value;
    if(address<0x4000||address>=0x8000)return value;
    // These consumers are one-byte LD A,[HL+] instructions. The legacy
    // runtime called this hook only for data; SameBoy also calls it for
    // opcode fetches. Never reinterpret a fetch as an enemy coordinate.
    if(unsigned(ctx->instruction_pc)+1!=pc)return value;
    int high=0,shift=0,match=1;
    switch(pc) {
        case 0x4555:case 0x4556:shift=-s.spawn_right;break;
        case 0x4557:case 0x4558:shift=-s.spawn_right;high=1;break;
        case 0x4570:case 0x4571:shift=s.spawn_left;break;
        case 0x4572:case 0x4573:shift=s.spawn_left;high=1;break;
        case 0x469b:case 0x469c:shift=-s.previous_right;break;
        case 0x469d:case 0x469e:shift=-s.previous_right;high=1;break;
        case 0x46bf:case 0x46c0:shift=s.previous_left;break;
        case 0x46c1:case 0x46c2:shift=s.previous_left;high=1;break;
        default:match=0;
    }
    if(cooperative&&(pc==0x458c||pc==0x458e||pc==0x45b0||pc==0x45b2)) {
        auto bounds=cooperative->activation_bounds(*ctx);int camera=word(ctx,0xca05);
        high=pc==0x458e||pc==0x45b2;match=1;
        shift=pc==0x458c||pc==0x458e?camera+144-bounds[3]:camera-bounds[2];
    }
    if(match) {
        unsigned original=word(ctx,address-(high?1:0));
        unsigned adjusted=(original+shift)&65535;s.spawn_reads++;
        return high?(uint8_t)(adjusted>>8):(uint8_t)adjusted;
    }
    return value;
}
void capture(GBContext *ctx,uint16_t address,bool complete_player=false) {
    if(!s.valid||(address!=0xca08&&address!=0xcad6&&address!=0xcb80))return;
    int page=peek(ctx,0xffa2);
    if(complete_player&&page==0xc9&&address!=0xcb80)return; // Native story portrait, not a world actor.
    // The outer 35D1 emitter rejects far bodies before calling 1C2E. Its
    // world-position read occurs after the native pose is ready, before
    // either hardware draw cull. Do not modify that read or claim OAM slots.
    if(s.valid&&address==0xca08&&ctx->instruction_pc==0x35de&&ctx->pc==0x35df&&
       ctx->hl==0xca08&&ctx->d==page&&page>=0xa0&&page<0xe0) {
        if(capture_actor(ctx,true,false))s.early_actor[page-0xa0]=1;
    }
    if(s.valid&&address==0xcad6&&ctx->pc==0x1c39) {
        bool early=page>=0xa0&&page<0xe0&&s.early_actor[page-0xa0];
        capture_actor(ctx,!early,true);return;
    }
    if (address!=0xcb80 || (ctx->pc!=0x1326 && ctx->pc!=0x1399) || !s.valid) return;
    int ptr=ctx->hl, count=peek(ctx,ptr++), slot=ctx->c/4;
    if (!count || count>64) return;
    int raw=peek(ctx,0xcb80), ry=peek(ctx,0xcb82);
    int cx=word(ctx,0xca08),cy=word(ctx,0xca05);
    int rx=raw;
    if (ctx->pc==0x1326||(page>=0xa0 && page<0xe0)) {
        // The primary emitter reads C16B's player flip. FFA2 can still
        // name the last non-player actor, or camera RAM, at this point.
        int actor=ctx->pc==0x1326?0xc100:page<<8;
        int expected=(int16_t)(word(ctx,actor+8)-cx)+8;
        int delta=(int8_t)(raw-(uint8_t)expected);
        if (delta>=-64 && delta<=64) rx=expected+delta;
    }
    int alternate=ctx->pc==0x1399;
    int flip=peek(ctx,alternate?0xffcc:0xc16b)!=0;
    int tile_add=peek(ctx,0xffc8)+0x40+(alternate?peek(ctx,0xffb0):0);
    ptr++; /* header byte skipped by both ROM emitters */
    for (int n=0;n<count;n++,ptr+=4) {
        int dy=(int8_t)peek(ctx,ptr),dx=(int8_t)peek(ctx,ptr+1);
        if (!alternate && dy==127) break;
        if (s.build_count<MAX_SPRITES) {
            Sprite *sp=&s.build[s.build_count++];
            sp->x=cx+rx-8+(flip?8-dx:dx);
            sp->y=complete_player&&!alternate?word(ctx,0xc105)+dy-16:cy+(int)((uint8_t)(ry+dy))-16;
            sp->tile=(uint8_t)(peek(ctx,ptr+2)+tile_add);
            sp->attr=(peek(ctx,ptr+3)^(flip?0x20:0))|8;
            sp->weather=0;
            s.captures++;
        }
        if (slot>=0 && slot<40) s.build_owned[slot]=1;
        slot=(slot+1)&63;
    }
}
/* A pause/menu can retain the stage bank and camera limits while replacing
 * VRAM with font tiles. Prove that the native scrolling map still belongs to
 * the world before composing it across the extended view. */
int map_matches_native(GBContext *ctx) {
    int matches=0;
    for(int y=16;y<=80;y+=32)for(int x=32;x<=128;x+=32) {
        int wx=s.cx+x,wy=s.cy+y;uint8_t tiles[4],attrs[4];
        if(wx<0||wy<0||!mmx2_bg_block(ctx,s.bank,wx>>4,wy>>4,tiles,attrs))continue;
        int q=((wy&8)?2:0)+((wx&8)?1:0);
        int a=0x1800+((wy>>3)&31)*32+((wx>>3)&31);
        if(s.vram[a]==tiles[q]&&s.vram[0x2000+a]==attrs[q])matches++;
    }
    return matches>=10;
}
void publish_sprites() {
    s.count=s.build_count;
    std::copy_n(s.build,s.count,s.sprite);std::copy_n(s.build_owned,40,s.owned);
    s.build_count=0;std::fill_n(s.build_owned,40,0);std::fill_n(s.early_actor,64,0);
}
void snapshot(GBContext *ctx) {
    ModPpu *p=(ModPpu*)ctx->ppu;
    if(s.frame<INT_MAX)++s.frame;
    int previous_bank=s.bank;
    s.cx=word(ctx,0xca08);s.cy=word(ctx,0xca05);s.bank=peek(ctx,0xcaba);
    /* 01:446A and 01:44B5 clamp the native camera to CA40 / CA0D.
     * Ride Chaser moves both locks with the camera. Retain the room's
     * unlocked map domain while that original vehicle controls scrolling. */
    int stage=peek(ctx,0xc05f),checkpoint=peek(ctx,0xc066);
    int left=word(ctx,0xca0d),right=word(ctx,0xca40)+160;
    bool chaser=stage==6&&peek(ctx,0xc02d);
    // A door moves the native rails through the already loaded intro room.
    // Keep that room's decorative terrain references until its Window changes
    // to the arriving arena; changing a reference with each rail step makes
    // stationary terrain pop in the margins while the native camera scrolls.
    bool closed_intro=stage==0&&checkpoint==3&&s.bank==0x10&&p->wy==128&&
        (p->lcdc&LCDC_WINDOW_ENABLE);
    if((!chaser&&!closed_intro)||s.stage!=stage||s.checkpoint!=checkpoint||previous_bank!=s.bank||s.map_right<=s.map_left) {
        s.map_left=left;s.map_right=right;
    }
    s.stage=stage;s.checkpoint=checkpoint;
    s.bound_left=chaser?s.map_left:left;s.bound_right=chaser?s.map_right:right;
    /* Match the camera uploaded by the game, rather than next-frame logic. */
    s.cx+=(int8_t)(p->scx-(uint8_t)s.cx);
    s.cy+=(int8_t)(p->scy-(uint8_t)s.cy);
    s.lcdc=p->lcdc;s.wx=p->wx;s.wy=p->wy;
    s.valid=s.bank>0 && s.bank<(int)(ctx->rom_size/0x4000) && !(p->lcdc&LCDC_BG_TILEMAP) && s.bound_right>s.bound_left;
    memcpy(s.vram,ctx->vram,sizeof s.vram);
    memcpy(s.map_wram,ctx->wram,sizeof s.map_wram);
    if(s.valid&&!map_matches_native(ctx))s.valid=0;
    memcpy(s.bg,p->bg_palette_ram,64);memcpy(s.obj,p->obj_palette_ram,64);
    memcpy(s.bg_rgb,p->bg_rgb,sizeof s.bg_rgb);memcpy(s.obj_rgb,p->obj_rgb,sizeof s.obj_rgb);
    memcpy(s.oam,ctx->oam,160);
    if (!atomic_sprites&&s.build_count) {
        s.count=s.build_count; memcpy(s.sprite,s.build,s.count*sizeof(Sprite));
        memcpy(s.owned,s.build_owned,40);
    }
    if(!atomic_sprites){s.build_count=0;memset(s.build_owned,0,40);memset(s.early_actor,0,sizeof s.early_actor);}
    if (!s.valid) {s.count=0;memset(s.owned,0,40);}


}
void draw_sprite(const State& s,uint32_t *out,int width,Sprite sp,bool whole=false,const uint32_t* colors=nullptr,const uint8_t* texture=nullptr,const uint8_t* tile=nullptr,bool map_domain=false) {
    int h=(s.lcdc&LCDC_OBJ_SIZE)?16:8,center=s.cx-render_left;
    if(h==16)sp.tile&=0xfe;
    for(int y=0;y<h;y++)for(int x=0;x<8;x++) {
        int sx=sp.x-render_left+x,sy=sp.y-s.cy+y;
        if(sx<0||sx>=width||sy<0||sy>=144)continue;
        if(map_domain ? (priority[sy*width+sx]&1) : (sp.x+x<s.bound_left||sp.x+x>=s.bound_right))continue;
        if(!whole&&sx>=center&&sx<center+160)continue; // authoritative PPU band
        int py=(sp.attr&0x40)?h-1-y:y,px=(sp.attr&0x20)?x:7-x;
        int a=((sp.attr&8)?0x2000:0)+sp.tile*16+py*2;
        int c=tile?((tile[py*2]>>px)&1)|(((tile[py*2+1]>>px)&1)<<1):texture?((texture[a&0x1fff]>>px)&1)|(((texture[(a+1)&0x1fff]>>px)&1)<<1):((s.vram[a]>>px)&1)|(((s.vram[a+1]>>px)&1)<<1),o=sy*width+sx;
        if(c && (!(s.lcdc&1)||!opaque[o]||(!(sp.attr&0x80)&&!(priority[o]&0x80))))out[o]=colors?colors[c]:s.obj_rgb[(sp.attr&7)*4+c];
    }
}
uint32_t tilemap_pixel(const State& s,int base,int x,int y)const {
    int index=base+((y>>3)&31)*32+((x>>3)&31);
    int t=s.vram[index],attr=s.vram[0x2000+index],tx=x&7,ty=y&7;
    if(attr&0x40)ty=7-ty;if(attr&0x20)tx=7-tx;
    int a=((attr&8)?0x2000:0)+((s.lcdc&0x10)?t*16:0x1000+(int8_t)t*16)+ty*2;
    int color=((s.vram[a]>>(7-tx))&1)|(((s.vram[a+1]>>(7-tx))&1)<<1);
    return s.bg_rgb[(attr&7)*4+color];
}
int render(GBContext *ctx,uint32_t *out,int width,const uint32_t *native,const Xtreme2Coop* coop=nullptr,int focus=-1) {
    const auto& completed=has_presented?presented:this->s;
    State focused;
    // The introductory arena uses its window for sixteen rows of floor and
    // then the HUD. Its BG/camera registers animate the boss independently.
    // Keep that native composition centered, with its own floor/backdrop.
    bool arena=!coop&&!smooth_gameplay&&width>160&&completed.stage==0&&
        (completed.lcdc&LCDC_WINDOW_ENABLE)&&completed.wy==112&&completed.wx==7&&
        !story_ui(completed)&&completed.bound_right-completed.bound_left==160;
    // Arrival dialogue wipes the Window from WX39 to WX7, but the incoming
    // room already owns this same resident BG plane. Window placement must
    // not switch its scenery back to columns from the preceding map.
    bool room_plane=arena||(!coop&&!smooth_gameplay&&width>160&&
        completed.stage==0&&completed.checkpoint==3&&completed.bank==0x10&&
        (completed.lcdc&LCDC_WINDOW_ENABLE)&&completed.wy==112&&
        completed.bound_right-completed.bound_left==160);
    if(coop) {
        focused=completed;auto camera=focus>=0?coop->camera(unsigned(focus)):coop->shared_camera();focused.cx=camera[0];focused.cy=camera[1];
        if(focus>=0){const auto& t=coop->presentation_transport(unsigned(focus));if(t.phase==1){focused.bound_left=t.bounds[0];focused.bound_right=t.bounds[1];}}
    }
    else if(room_plane){focused=completed;focused.cx=completed.bound_left;}
    const auto& s=(coop||room_plane)?focused:completed;
    render_left=view_left(s,s.cx,width);
    bool full=coop||smooth_gameplay;
    // Camera locks describe the native viewport, not the loaded terrain.
    // In a locked room the adaptive margins still use its authored map.
    // Keep the full native/co-op compositor's clipping contract unchanged.
    bool map_domain=!full&&width>160;
    if(!s.valid||!ctx->rom||(!full&&width<=160))return 0;
    if(coop&&!(s.lcdc&0x80)){std::fill_n(out,width*144,native[0]);return 1;}
    // Terrain pages can change during the next game update. Decode the
    // completed frame from the same map, camera and graphics as its PPU band.
    auto frozen=*ctx;frozen.wram=s.map_wram;ctx=&frozen;
    // The adjoining map contains the reciprocal entrance's complete door.
    // Keep that entrance out of decorative margins, but retain every other
    // authored terrain cell at its world position. Repeating a single room
    // column changes ground/ceiling heights at the native viewport edges.
    bool antechamber=map_domain&&!room_plane&&s.stage==0&&s.checkpoint==3&&
        s.bank==0x10&&s.bound_right-s.bound_left<=320;
    auto entrance=[](uint8_t id){return id==0xa7||id==0xb7||id==0xc5||id==0xd5;};
    int center=s.cx-render_left;
    int arena_first=0,arena_last=256,arena_base=(completed.lcdc&8)?0x1c00:0x1800;
    uint32_t arena_clear=native[0];
    if(room_plane) {
        // The boss painter leaves outer BG columns untouched. Its clear top
        // row identifies the painted canvas in this completed frame; stale
        // columns can exist on either side, with unrelated tiles/palettes.
        unsigned row=unsigned(arena_base)+unsigned((completed.cy>>3)&31)*32;
        unsigned key=0,best=0,clear_column=0;
        auto cell=[&](unsigned x){return unsigned(completed.vram[row+x])|(unsigned(completed.vram[0x2000+row+x])<<8);};
        for(unsigned x=0;x<32;++x) {
            unsigned count=0,value=cell(x);for(unsigned n=0;n<32;++n)count+=cell(n)==value;
            if(count>best){best=count;key=value;clear_column=x;}
        }
        arena_first=0;arena_last=32;
        // The painter clears rows incrementally. At boss spawn its top row
        // can already span all32 columns while lower rows still contain the
        // preceding room's tiles. Intersect the clear row extents over the
        // visible BG plane before admitting either decorative margin.
        for(int y=completed.cy>>3;y<=((completed.cy+completed.wy-1)>>3);++y) {
            unsigned line=unsigned(arena_base)+unsigned(y&31)*32;
            int first=32,last=0;
            for(unsigned x=0;x<32;++x)if((unsigned(completed.vram[line+x])|
                (unsigned(completed.vram[0x2000+line+x])<<8))==key) {
                first=std::min(first,int(x));last=int(x)+1;
            }
            if(first==32){arena_first=arena_last=0;break;}
            arena_first=std::max(arena_first,first);arena_last=std::min(arena_last,last);
        }
        arena_first*=8;arena_last*=8;
        arena_clear=tilemap_pixel(completed,arena_base,int(clear_column)*8,completed.cy);
    }
    uint32_t bg_colors[32];for(int i=0;i<32;i++)bg_colors[i]=s.bg_rgb[i];
    for(int y=0;y<144;y++) {
      int cached_mx=-1,block_valid=0;uint8_t tiles[4],attrs[4];
      for(int x=0;x<width;x++) {
        int wx=render_left+x,wy=s.cy+y,o=y*width+x,c=0;uint8_t attr=0;
        if(!full&&x>=center&&x<center+160)continue; // copied after margin composition
        if(room_plane){
            int source_x=(completed.cx&255)+x-center;
            // Never wrap the finite canvas or expose its unpainted edges.
            out[o]=source_x>=arena_first&&source_x<arena_last?
                tilemap_pixel(completed,arena_base,source_x,completed.cy+y):arena_clear;
            opaque[o]=0;priority[o]=0;continue;
        }
        bool projected=false;
        bool in_domain=map_domain?wx>=0:(wx>=s.bound_left&&wx<s.bound_right);
        if(in_domain&&wy>=0) {
          if((wx>>4)!=cached_mx) {
              cached_mx=wx>>4;block_valid=mmx2_bg_block(ctx,s.bank,cached_mx,wy>>4,tiles,attrs);
              if(antechamber&&block_valid) {
                  uint8_t id=0;
                  if(mmx2_bg_meta_id(ctx,s.bank,cached_mx,wy>>4,&id)&&entrance(id)) {
                      // Fill just the reciprocal door with adjoining terrain
                      // from this same row, looking toward the native room.
                      int step=x<center?1:-1;
                      int first=step>0?std::max(cached_mx+1,s.cx>>4):
                          std::min(cached_mx-1,(s.cx+159)>>4);
                      block_valid=0;
                      for(int n=0;n<10;++n) {
                          int mx=first+n*step;if(mx<0)break;
                          if(mmx2_bg_meta_id(ctx,s.bank,mx,wy>>4,&id)&&!entrance(id)) {
                              block_valid=mmx2_bg_block(ctx,s.bank,mx,wy>>4,tiles,attrs);break;
                          }
                      }
                  }
              }
          }
          projected=antechamber;
          if(block_valid) {
            int q=((wy&8)?2:0)+((wx&8)?1:0);attr=attrs[q];
            int t=tiles[q],ty=wy&7,tx=wx&7;
            if(attr&0x40)ty=7-ty;if(attr&0x20)tx=7-tx;
            int a=((attr&8)?0x2000:0)+((s.lcdc&0x10)?t*16:0x1000+(int8_t)t*16)+ty*2;
            c=((s.vram[a]>>(7-tx))&1)|(((s.vram[a+1]>>(7-tx))&1)<<1);
          }
        }
        bool outside=map_domain?(!in_domain||wy<0||!block_valid):!in_domain;
        out[o]=outside?0xff000000u:bg_colors[(attr&7)*4+c];opaque[o]=(uint8_t)c;priority[o]=(attr&0x80)|((outside||projected)?1:0);
      }
    }
    for(int i=39;i>=0;i--)if(!coop&&!s.owned[i]){
        Sprite sp={s.cx+s.oam[i*4+1]-8,s.cy+s.oam[i*4]-16,s.oam[i*4+2],s.oam[i*4+3]};
        draw_sprite(s,out,width,sp,full,nullptr,nullptr,nullptr,map_domain);
    }
    for(int i=s.count-1;i>=0;i--) {
        if(coop&&!coop->sprite_visible(unsigned(i)))continue;
        Sprite sp=s.sprite[i];
        // The arena's BG camera animates the boss. Original OAM effects,
        // including the death burst, remain relative to the native viewport.
        if(room_plane)sp.x+=s.cx-completed.cx;
        if(!sp.weather)draw_sprite(s,out,width,sp,full,coop?coop->sprite_palette(unsigned(i)):nullptr,coop?coop->sprite_texture(unsigned(i)):nullptr,coop?coop->sprite_tile(unsigned(i)):nullptr,map_domain);
        else {
            // The original screen-relative dust emitter retains its native
            // two-particle lifecycle and flicker. Extend that density into
            // the margins using its captured poses; never repeat actors.
            int first=(render_left-sp.x-8)/160-1,last=(render_left+width-sp.x)/160+1;
            for(int k=first;k<=last;k++) {
                Sprite dust=sp;dust.x+=k*160;draw_sprite(s,out,width,dust,full,nullptr,nullptr,nullptr,map_domain);
            }
        }
    }
    if(coop)for(unsigned seat=0;seat<2;++seat)if(const auto* a=coop->bike_attachment(seat)) {
        const auto actor=coop->actors(true)[seat];
        for(unsigned n=0;n<a->count;++n){const auto& p=a->pieces[n];Sprite sp={actor.x+p.x,actor.y+p.y,p.tile,p.attributes,0};draw_sprite(s,out,width,sp,true,a->colors.data(),a->texture.data());}
    }
    if(coop)for(unsigned seat=0;seat<2;++seat)if(const auto* t=coop->transport_pose(seat))
        for(unsigned n=0;n<t->count;++n){const auto& p=t->pieces[n];Sprite sp={t->x+p.x,t->y+t->offset/256+p.y,p.tile,p.attributes,0};
            draw_sprite(s,out,width,sp,true,t->colors.data(),t->texture.data());}
    /* The expanded sprite compositor intentionally supplies the margins, but
     * the original PPU remains authoritative inside its aligned viewport,
     * including its per-line sprite admission and priority rules. Keep this
     * before the native-window pass so the existing HUD relocation is intact. */
    int first=std::max(0,-center),last=std::min(160,width-center);
    if(!full&&first<last)for(int y=0;y<144;y++)
        memcpy(out+y*width+center+first,native+y*160+first,
               size_t(last-first)*sizeof(*out));
    /* Split the bottom HUD into left/right groups. Center other window UI,
     * preserving native window pixels and text transitions. */
    // The game also enables its128-line HUD during HBlank, after the frame's
    // initial LCDC sample. It is still screen-space UI at that boundary.
    bool raster_hud=!coop&&s.wy==128&&!story_ui(s);
    // The dialogue owns the lower screen separately from the room BG. Keep
    // its full native composition (including the portrait left of WX), and
    // extend only its UI background into the margins. Old BG cells beneath
    // the Window are not exposed scenery.
    if(room_plane&&!arena)for(int y=s.wy;y<144;++y) {
        std::fill_n(out+y*width,center,native[y*160+159]);
        std::fill_n(out+y*width+center+160,width-center-160,native[y*160+159]);
    }
    if((!room_plane||arena)&&((s.lcdc&LCDC_WINDOW_ENABLE)||raster_hud)&&s.wy<144&&s.wx<167) {
        int start=s.wx>7?s.wx-7:0;
        if(arena)for(int y=112;y<128;++y)for(int x=0;x<width;++x) {
            if(x>=center&&x<center+160)continue;
            int wx=(x<center?0:144)+int(unsigned(x-center)&15),wy=y-112;
            out[y*width+x]=tilemap_pixel(s,(s.lcdc&0x40)?0x1c00:0x1800,wx,wy);
        }
        for(int y=arena?128:s.wy;y<144;y++) {
            bool split_hud=s.wy==128||arena;
            for(int x=0;x<width;x++)out[y*width+x]=split_hud?0xff000000u:native[y*160+159];
            for(int x=start;x<160;x++) {
                // Keep the meters, lives count and character icon together
                // in the first twelve tiles; the remaining boss HUD stays right.
                int dst=split_hud?(x<96?x:width-160+x):(width-160)/2+x;
                out[y*width+dst]=native[y*160+x];
            }
        }
    }
    // Original story text is a screen-space native composition, including
    // its portrait and font tiles. Keep that exact UI over either world
    // camera; it must not be replaced by scrolling terrain or a player HUD.
    if(coop&&story_ui(s)) {
        int top=std::clamp(int(s.map_wram[0x905]|unsigned(s.map_wram[0x906])<<8)-completed.cy-16,0,144);
        for(int y=top;y<144;++y){std::fill_n(out+y*width,width,0xff000000u);std::copy_n(native+y*160,160,out+y*width+(width-160)/2);}
    }
    return 1;
}

};
NativeMods::NativeMods(std::string_view rom,std::span<const NativeFeature> features,bool model_cgb) {
 if(features.empty())return;
 if(megaman_title_for_plugin(features[0].plugin)) {
  megaman_=std::make_unique<MegaManNativeMods>(rom,features,model_cgb);observe_scanlines_=megaman_->observes_scanlines();return;
 }
 for(const auto& feature:features)if(feature.no_flicker)
  throw std::runtime_error("No flicker requires a supported Mega Man native package");
 for(const auto& feature:features)if(feature.smooth_gameplay&&feature.plugin!="gbcllr.xtreme2.adaptive-view"&&feature.plugin!="gbcllr.xtreme2.coop")
  throw std::runtime_error("Smooth gameplay requires the native Xtreme2 package");
 for(const auto& feature:features)if(feature.weapon_cycle&&feature.plugin!="gbcllr.xtreme2.adaptive-view"&&feature.plugin!="gbcllr.xtreme2.coop")
  throw std::runtime_error("Quick weapon selection requires the native Xtreme2 package");
 for(const auto& feature:features)if(feature.expanded_encounters&&feature.plugin!="gbcllr.gen1.extended")
  throw std::runtime_error("Expanded encounters require the native Gen1 Extended package");
 for(const auto& feature:features)if(feature.player1_character>1||(feature.player1_character&&feature.plugin!="gbcllr.xtreme2.coop"))
  throw std::runtime_error("Character selection requires Xtreme2 co-op");
 for(const auto& feature:features)if(feature.gift&&feature.plugin!="gbcllr.gen2.adaptive-view")
  throw std::runtime_error("Native gifts require a qualified Gen2 cartridge");
 if(features[0].plugin=="gbcllr.oracle.adaptive-view") {
  if(features.size()!=1)throw std::runtime_error("Oracle view requires one qualified feature");
  oracle_=std::make_unique<OracleNativeMods>(rom,features[0],model_cgb);return;
 }
 if(features[0].plugin=="gbcllr.gen1.adaptive-view"||features[0].plugin=="gbcllr.gen1.extended") {
  if(features.size()!=1)throw std::runtime_error("Gen1 view requires one qualified feature");
  gen1_=std::make_unique<Gen1NativeMods>(rom,features[0],model_cgb);observe_scanlines_=model_cgb;return;
 }
 if(features[0].plugin=="gbcllr.gen2.adaptive-view") {
  if(features.size()!=1)throw std::runtime_error("Gen2 view requires one qualified feature");
  gen2_=std::make_unique<Gen2NativeMods>(rom,features[0],model_cgb);return;
 }
 if(features[0].plugin=="gbcllr.sml2.adaptive-view") {
  if(rom!="42045a1642f77b568534ab85b9db93c8a33d2b95c38ae84c987f497e8d52f97f")throw std::runtime_error("SML2 native package does not target this exact ROM");
#if !GBCLLR_OBSERVED_DISPLAY
  if(features[0].colorization)throw std::runtime_error("SML2 colorization requires the observed display service; rebuild with SML2 color assets and GBCLLR_DISPLAY_SERVICE=AUTO or OBSERVED");
#endif
  sml2_=std::make_unique<Sml2NativeMods>(features,model_cgb);observe_scanlines_=features[0].colorization;return;
 }
 const bool cooperative=features[0].plugin=="gbcllr.xtreme2.coop";
 if(features.size()!=1||features[0].colorization||!features[0].asset_input.empty()||(!cooperative&&features[0].plugin!="gbcllr.xtreme2.adaptive-view")||features[0].abi!=1)
  throw std::runtime_error("unsupported native feature set or ABI");
 if(rom!="e37a14a8e61bd64f2226dacc26a7ee75dd90abfcf88c5016eb97b76ecda0ddca")
  throw std::runtime_error("adaptive-view package does not target this exact ROM");
 if(!model_cgb)throw std::runtime_error("Xtreme2 adaptive-view module requires CGB hardware");
 auto width=features[0].view_width;
 if(width!=-1&&(width<160||width>MAX_WIDTH))throw std::runtime_error("invalid adaptive-view width");
 impl_=std::make_unique<Impl>();impl_->requested=width;impl_->gb_custom_width=width<0?256:width;
 if(cooperative){coop_=std::make_unique<Xtreme2Coop>(features[0].player1_character);impl_->atomic_sprites=true;}
 impl_->smooth_gameplay=features[0].smooth_gameplay;
 if(features[0].smooth_gameplay)impl_->atomic_sprites=true;
 if(features[0].weapon_cycle)controls_=std::make_unique<Xtreme2Controls>(true);
}
NativeMods::~NativeMods()=default;
bool NativeMods::has_coop()const{return bool(coop_);}
bool NativeMods::uses_instruction_service()const{return bool(megaman_)||bool(coop_)||bool(controls_)||(impl_&&impl_->smooth_gameplay);}
void NativeMods::virtual_inputs(uint8_t a,uint8_t b){if(megaman_)megaman_->inputs(a,b);if(controls_){controls_->inputs(a,b);if(coop_&&coop_->menus_active())controls_->discard_pending();}}
void NativeMods::coop_inputs(uint8_t a,uint8_t b){if(coop_)coop_->inputs(a,b);}
std::span<const uint16_t> NativeMods::instruction_sites()const{return megaman_?megaman_->instruction_sites():std::span<const uint16_t>{};}
std::span<const uint16_t> NativeMods::read_sites()const{return megaman_?megaman_->read_sites():std::span<const uint16_t>{};}
bool NativeMods::poll_instructions()const{return !megaman_||megaman_->poll_instructions();}
void NativeMods::coop_instruction(ModContext& c,std::span<uint16_t,6> cpu,std::span<uint8_t> ram,std::span<uint8_t> hram){
 if(megaman_){megaman_->instruction(c,cpu,ram,hram);return;}
 // 00:0711 is the original completed-work VBlank wait. A display scanline
 // can interrupt either player's producer; publish only the whole native list.
 if(impl_&&impl_->atomic_sprites&&cpu[5]==0x0711&&(!coop_||!coop_->menus_active())){
  if(coop_)coop_->publish(true);impl_->publish_sprites();
 }
 if(controls_)controls_->instruction(c,cpu);
 if(coop_)coop_->instruction(c,cpu,ram,hram,controls_.get());
 else if(controls_&&!controls_->active()&&c.rom_bank==5&&cpu[5]==0x4001)
  controls_->dispatch(c,cpu,ram,0,!ram[0xc2]&&!ram[0xc]&&ram[0x9b0]&&!ram[0xac]);
}
std::vector<uint8_t> NativeMods::coop_snapshot()const{return coop_?coop_->snapshot():std::vector<uint8_t>{};}
void NativeMods::restore_coop_menu(Machine& owner){if(coop_)coop_->restore_menu(owner);}
bool NativeMods::active()const{return bool(megaman_)||bool(impl_)||bool(sml2_)||bool(gen2_)||bool(gen1_)||bool(oracle_);}
bool NativeMods::completes_frames()const{return bool(megaman_)||bool(gen1_)||bool(gen2_)||bool(impl_);}
uint32_t NativeMods::cpu_budget(const ModContext& c,bool cleared)const {
 if(megaman_)return megaman_->cpu_budget(c,cleared);
 return impl_&&impl_->smooth_gameplay&&!cleared&&c.wram[0xb0]==1&&c.wram[0x101]==2?280896u:0u;
}
bool NativeMods::ends_cpu_work(uint16_t pc)const {
 if(megaman_)return megaman_->ends_cpu_work(pc);
 return impl_&&impl_->smooth_gameplay&&pc==0x0711;
}
bool NativeMods::polls_physical_clock(uint16_t pc)const {
 return megaman_&&megaman_->polls_physical_clock(pc);
}
void NativeMods::complete_frame(ModContext& c,bool cleared,bool freeze_gen1){
 if(megaman_)megaman_->complete_frame(c,cleared);
 else if(gen1_)gen1_->complete_frame(c,cleared,freeze_gen1);
 else if(gen2_)gen2_->complete_frame(c,cleared);
 else if(impl_&&(!coop_||!coop_->menus_active())){
  // Retry/fade uploads temporarily replace the tilemap. Keep the last actual
  // world geometry while its current palettes fade, across the entire canvas.
  bool retain=coop_&&(cleared||!impl_->s.valid)&&impl_->has_presented&&impl_->presented.valid&&c.wram[0xb0]==1;
  if(retain){
   impl_->presented.lcdc=c.ppu->lcdc;
   std::copy_n(c.ppu->bg_rgb,32,impl_->presented.bg_rgb);
   std::copy_n(c.ppu->obj_rgb,32,impl_->presented.obj_rgb);
  }else impl_->presented=cleared?Impl::State{}:impl_->s;
  impl_->has_presented=true;if(coop_)coop_->complete_frame(c);
 }
}
bool NativeMods::observes_scanlines()const{return observe_scanlines_;}
bool NativeMods::tracks_pixel_history()const{return bool(sml2_);}
bool NativeMods::first_line(ModContext& c,std::span<const uint32_t> native){return sml2_&&sml2_->first_line(c,native);}
void NativeMods::framebuffer_cleared(){if(sml2_)sml2_->framebuffer_cleared();}
void NativeMods::background_row(ModContext& c,const ModBackgroundRow& row){if(sml2_)sml2_->background_row(c,row);}
ModPixelSink NativeMods::pixel_sink(uint8_t line){return sml2_?sml2_->pixel_sink(line):ModPixelSink{};}
bool NativeMods::uses_oracle_cpu_service()const{return bool(oracle_);}
bool NativeMods::observes_writes()const{return bool(megaman_)||bool(coop_)||(gen1_&&gen1_->observes_writes());}
bool NativeMods::write(ModContext& c,uint16_t address,uint8_t value){if(megaman_)megaman_->write(c,address,value);if(coop_)coop_->write(c,address,value);return !gen1_||gen1_->write(c,address,value);}
uint8_t NativeMods::oracle_cpu_read(ModContext& c,uint16_t address,uint8_t value,std::span<uint16_t,6> cpu) {
 return oracle_?oracle_->cpu_read(c,address,value,cpu):read(c,address,value);
}
uint8_t NativeMods::read(ModContext& c,uint16_t address,uint8_t value) {
 if(megaman_)return megaman_->read(c,address,value);
 if(oracle_) {
  if(address!=0xcd00&&address!=0xcd06&&address!=0xcd0c&&address!=0xcd0d&&
     address!=0x4126&&address!=0x4133&&address!=0xd00b&&address!=0xd00d&&address!=0xc49d)return value;
  return oracle_->read(c,address,value);
 }
 if(gen2_)return gen2_->read(c,address,value);
 if(gen1_)return gen1_->read(c,address,value);
 if(sml2_)return sml2_->read(c,address,value);
 if(!impl_)return value;
 if(coop_)value=coop_->read(c,address,value);
 if(coop_&&coop_->menus_active())return value;
 // Only the original drawing predicates see a visible body. Injury timers,
 // contact rules and the stored native visibility flag stay untouched.
 if(impl_->smooth_gameplay&&c.sp!=UINT16_MAX&&address==0xc10d&&
    ((c.instruction_pc==0x12d5&&c.pc==0x12d8)||(c.rom_bank==5&&c.instruction_pc==0x4b63&&c.pc==0x4b66))&&
    c.wram[0x101]==2&&(c.wram[0x121]&127)&&c.wram[0x103]!=0x0c)value=1;
 auto first=impl_->s.build_count;
 impl_->capture(&c,address,bool(coop_));
 if(coop_&&address==0xcb80&&c.pc==0x1326){
  coop_->palette(c,unsigned(first),unsigned(impl_->s.build_count));
  for(unsigned n=unsigned(first);n<unsigned(impl_->s.build_count);++n)coop_->player_piece(n,impl_->s.build[n].tile);
 }
 if(coop_&&address==0xcb80&&c.pc==0x1399) {
  unsigned count=unsigned(impl_->s.build_count)-unsigned(first);
  if(count<=64){std::array<Xtreme2Coop::Piece,64> pieces{};int x=impl_->word(&c,0xc108),y=impl_->word(&c,0xc105);
   for(unsigned n=0;n<count;++n){auto& sp=impl_->s.build[first+n];pieces[n]={int16_t(sp.x-x),int16_t(sp.y-y),sp.tile,sp.attr};}
   if(coop_->capture_attachment(c,std::span(pieces).first(count),unsigned(c.c)/4))impl_->s.build_count=int(first);
   else for(unsigned n=unsigned(first);n<unsigned(impl_->s.build_count);++n)coop_->attachment_piece(c,n,impl_->s.build[n].tile,impl_->s.build[n].attr);
  }
 }
 if(coop_&&((address==0xca08&&c.instruction_pc==0x35de&&c.pc==0x35df)||(address==0xcad6&&c.pc==0x1c39)))
  for(unsigned n=unsigned(first);n<unsigned(impl_->s.build_count);++n)coop_->projectile_piece(c,n,impl_->s.build[n].attr,impl_->s.build[n].tile);
 // The full co-op compositor supplies both complete player poses. Keep their
 // physical LCD sprites hidden so the native story UI cannot inherit a body
 // whose shared tiles were subsequently replaced by the other character.
 if(coop_&&coop_->player_drawing()&&address==0xcb82&&c.sp!=UINT16_MAX&&c.instruction_pc==0x1327&&c.pc==0x132a){
  int count=impl_->peek(&c,c.hl),minimum=INT_MAX,maximum=INT_MIN;
  if(count>0&&count<=64)for(int n=0;n<count;++n){int y=int8_t(impl_->peek(&c,c.hl+2+n*4));if(y==127)break;minimum=std::min(minimum,y);maximum=std::max(maximum,y);}
  if(minimum!=INT_MAX&&maximum-minimum<96)return uint8_t(160-minimum);
 }
 // The native OAM writer retains its original draw bounds. Widening that
 // guard aliases far-margin objects through eight-bit OAM coordinates into
 // the authoritative PPU band. The early world-position observer supplies
 // complete bodies even when the original outer draw guard rejects them.
 return impl_->read_override(&c,address,value,coop_.get());
}
void NativeMods::scanline(ModContext& c,uint8_t line){if(megaman_)megaman_->scanline(c,line);else if(oracle_)oracle_->scanline(c,line);else if(gen1_)gen1_->scanline(c,line);else if(gen2_)gen2_->scanline(c,line);else if(sml2_)sml2_->scanline(c,line);else if(impl_&&!line&&(!coop_||!coop_->menus_active())){impl_->snapshot(&c);}}
void NativeMods::viewport(unsigned width,unsigned height) {
 if(megaman_){megaman_->viewport(width,height);return;}
 if(oracle_){oracle_->viewport(width,height);return;}
 if(gen2_){gen2_->viewport(width,height);return;}
 if(gen1_){gen1_->viewport(width,height);return;}
 if(sml2_){sml2_->viewport(width,height);return;}
 if(!impl_)return;
 int64_t resolved=impl_->requested;
 if(resolved<0)resolved=height?(uint64_t(width)*144+height/2)/height:256;
 impl_->gb_custom_width=int(std::clamp<int64_t>(resolved,160,MAX_WIDTH));
}
void NativeMods::frame(Machine& machine){if(coop_)coop_->menu_frame(machine);else if(oracle_)oracle_->frame(machine);else if(gen2_)gen2_->frame(machine);}
void NativeMods::oracle_visibility(bool hide){if(oracle_)oracle_->visibility(hide);}
void NativeMods::coop_view(unsigned mode,unsigned actor) {
 if(mode>2||actor>1)throw std::runtime_error("Invalid co-op presentation");
 coop_view_=mode;coop_actor_=actor;
}
void NativeMods::coop_shared_world(bool enabled) {if(coop_&&impl_)coop_->shared_world(enabled,unsigned(impl_->gb_custom_width));}
void NativeMods::preserve_host_presentation(const NativeMods& from) noexcept {
 coop_view_=from.coop_view_;coop_actor_=from.coop_actor_;
 if(oracle_&&from.oracle_)oracle_->preserve_host_presentation(*from.oracle_);
 if(gen1_&&from.gen1_)gen1_->preserve_host_presentation(*from.gen1_);
 if(gen2_&&from.gen2_)gen2_->preserve_host_presentation(*from.gen2_);
}
ModView NativeMods::render_host_gen2(ModContext& c,std::span<const uint32_t> native,unsigned w,unsigned h) {
 if(!gen2_)throw std::runtime_error("Host-only view requires the qualified Gen2 adapter");
 return gen2_->render_host(c,native,w,h);
}
ModView NativeMods::render_host_coop(ModContext& c,std::span<const uint32_t> native,unsigned w,unsigned h,int menu_actor) {
 if(!coop_||!impl_)throw std::runtime_error("Host-only co-op view requires the native co-op adapter");
 if(menu_actor< -1||menu_actor>1)throw std::runtime_error("Invalid local co-op menu player");
 // Rendering uses this width for placement only. Restore the simulation's
 // effective width before returning, including on an allocation exception.
 struct RestoreWidth {int& width;int saved;~RestoreWidth(){width=saved;}} restore{impl_->gb_custom_width,impl_->gb_custom_width};
 RestoreWidth restore_menu{coop_menu_actor_,coop_menu_actor_};coop_menu_actor_=menu_actor;
 impl_->gb_custom_width=int(std::clamp<uint64_t>(h?(uint64_t(w)*144+h/2)/h:256,160,MAX_WIDTH));
 return render(c,native);
}
ModView NativeMods::render(ModContext& c,std::span<const uint32_t> native) {
 if(megaman_)return megaman_->render(c,native);
 if(oracle_)return oracle_->render(c,native);
 if(gen2_)return gen2_->render(c,native);
 if(gen1_)return gen1_->render(c,native);
 if(sml2_)return sml2_->render(c,native);
 if(!impl_)return {160,false,native};
 ModView out;out.width=unsigned(impl_->gb_custom_width);
 if(coop_&&coop_view_==2)out.width=2*std::max(160u,out.width/2);
 auto& pixels=impl_->frame_pixels;pixels.resize(size_t(out.width)*144);
 if(coop_&&coop_->draw_menus(pixels,out.width,native,coop_menu_actor_>=0?coop_menu_actor_:coop_view_==1?int(coop_actor_):-1)){out.expanded=true;out.pixels=pixels;return out;}
 if(coop_)native=coop_->menu_world_native(native);
 if(coop_&&coop_view_==2) {
  unsigned pane_width=out.width/2;std::vector<uint32_t> pane(pane_width*144);
  out.expanded=true;
  for(unsigned i=0;i<2;++i) {
   out.expanded&=impl_->render(&c,pane.data(),int(pane_width),native.data(),coop_.get(),int(i))!=0;
   for(unsigned y=0;y<144;++y)std::copy_n(pane.data()+y*pane_width,pane_width,pixels.data()+y*out.width+i*pane_width);
  }
 } else out.expanded=impl_->render(&c,pixels.data(),out.width,native.data(),coop_.get(),coop_&&coop_view_==1?int(coop_actor_):-1)!=0;
 if(!out.expanded) {
  std::fill(pixels.begin(),pixels.end(),0xff000000u);
  for(unsigned y=0;y<144;++y)std::copy_n(native.data()+y*160,160,pixels.data()+y*out.width+(out.width-160)/2);
 }
 // Host HUD composition is the final pass over the complete canvas. It is
 // independent of native window timing and either camera's map composition.
 if(coop_&&out.expanded) {
  auto frozen=c;const auto& completed=impl_->has_presented?impl_->presented:impl_->s;
  frozen.wram=completed.map_wram;
  if((completed.lcdc&(0x80|LCDC_WINDOW_ENABLE))==(0x80|LCDC_WINDOW_ENABLE)&&completed.wy==128&&!Impl::story_ui(completed))
   coop_->draw_hud(frozen,pixels,out.width,native,coop_view_==1?int(coop_actor_):-1,coop_view_==2);
  if(coop_view_==2)for(unsigned y=0;y<144;++y)pixels[y*out.width+out.width/2-1]=0xff202830u;
 }
 out.pixels=pixels;
 const auto& s=impl_->has_presented?impl_->presented:impl_->s;
 out.camera_x=s.cx;out.camera_y=s.cy;out.left=impl_->render_left;
 out.bound_left=s.bound_left;out.bound_right=s.bound_right;
 out.sprites=unsigned(s.count);out.captures=s.captures;out.activation_adjustments=s.rescues;
 out.spawn_scans=impl_->s.spawn_scans;out.spawn_reads=impl_->s.spawn_reads;
 return out;
}
std::vector<uint8_t> NativeMods::snapshot(bool simulation,bool sealed_children)const {
 if(megaman_)return megaman_->snapshot();
 if(oracle_)return oracle_->snapshot();
 if(gen2_)return gen2_->snapshot();
 if(gen1_)return gen1_->snapshot();
 if(sml2_)return sml2_->snapshot();
 if(!impl_)return {};
 static_assert(std::is_trivially_copyable_v<Impl::State>);
 std::vector<uint8_t> out(16+2*sizeof(Impl::State));uint32_t header[]={controls_?(coop_?5u:4u):(coop_?3u:2u),uint32_t(impl_->requested),uint32_t(impl_->gb_custom_width),uint32_t(impl_->has_presented)};
 std::memcpy(out.data(),header,16);std::memcpy(out.data()+16,&impl_->s,sizeof impl_->s);std::memcpy(out.data()+16+sizeof impl_->s,&impl_->presented,sizeof impl_->presented);
 if(controls_){auto extra=controls_->snapshot();uint32_t size=uint32_t(extra.size());auto at=out.size();out.resize(at+4+size);std::memcpy(out.data()+at,&size,4);std::memcpy(out.data()+at+4,extra.data(),size);}
 if(coop_){auto extra=coop_->snapshot(simulation,sealed_children);out.insert(out.end(),extra.begin(),extra.end());}return out;
}
void NativeMods::checkpoint(void* context,void (*write)(void*,const uint8_t*,size_t))const {
 if(gen2_){gen2_->checkpoint(context,write);return;}
 auto put=[&](const auto& value){write(context,reinterpret_cast<const uint8_t*>(&value),sizeof value);};
 if(impl_&&!megaman_&&!oracle_&&!gen1_&&!sml2_&&!controls_&&!coop_) {
  static_assert(std::is_trivially_copyable_v<Impl::State>);
  const uint32_t size=16+2*sizeof(Impl::State);
  const uint32_t header[]={2u,uint32_t(impl_->requested),uint32_t(impl_->gb_custom_width),uint32_t(impl_->has_presented)};
  put(size);put(header);put(impl_->s);put(impl_->presented);return;
 }
 // Other service profiles retain their existing complete state contract.
 auto state=snapshot(false,false);const uint32_t size=uint32_t(state.size());put(size);
 if(size)write(context,state.data(),state.size());
}
void NativeMods::restore(std::span<const uint8_t> bytes,bool sealed_children) {
 if(megaman_){megaman_->restore(bytes);return;}
 if(oracle_){oracle_->restore(bytes);return;}
 if(gen2_){gen2_->restore(bytes);return;}
 if(gen1_){gen1_->restore(bytes);return;}
 if(sml2_){sml2_->restore(bytes);return;}
 if(!impl_){if(!bytes.empty())throw std::runtime_error("snapshot native feature mismatch");return;}
 const auto base=16+2*sizeof(Impl::State);
 if(bytes.size()<base||(!coop_&&!controls_&&bytes.size()!=base))throw std::runtime_error("invalid native feature state size");
 uint32_t header[4];std::memcpy(header,bytes.data(),16);
 if(header[0]!=(controls_?(coop_?5u:4u):(coop_?3u:2u))||int32_t(header[1])!=impl_->requested||header[2]<160||header[2]>MAX_WIDTH||header[3]>1)throw std::runtime_error("snapshot native feature profile mismatch");
 Impl::State states[2]{};std::memcpy(states,bytes.data()+16,sizeof states);
 for(const auto& staged:states) {
 // Before the cartridge clears WRAM, CABA is an arbitrary byte. Invalid
 // views retain it for exact early-boot rollback, without admitting that
 // bank as a renderable map or weakening the bounds on a valid view.
 if(staged.valid<0||staged.valid>1||staged.bank<0||staged.bank>255||(staged.valid&&(staged.bank==0||staged.bank>=64))||staged.frame<0||staged.count<0||staged.count>MAX_SPRITES||staged.build_count<0||staged.build_count>MAX_SPRITES)
  throw std::runtime_error("invalid native feature state");
 for(int value:{staged.cx,staged.cy,staged.bound_left,staged.bound_right,staged.map_left,staged.map_right,staged.spawn_left,staged.spawn_right,staged.previous_left,staged.previous_right})if(value< -131072||value>131072)throw std::runtime_error("invalid native view bounds");
 for(auto list:{std::span(staged.sprite),std::span(staged.build)})for(auto sp:list)if(sp.x< -131072||sp.x>131072||sp.y< -131072||sp.y>131072)throw std::runtime_error("invalid native sprite position");
 }
 auto extra=bytes.subspan(base);
 if(controls_){uint32_t size=0;if(extra.size()<4)throw std::runtime_error("Missing virtual controls state");std::memcpy(&size,extra.data(),4);extra=extra.subspan(4);if(size>extra.size())throw std::runtime_error("Truncated virtual controls state");controls_->restore(extra.first(size));extra=extra.subspan(size);}
 if(coop_)coop_->restore(extra,sealed_children);else if(!extra.empty())throw std::runtime_error("Unexpected native feature state");
 impl_->s=states[0];impl_->presented=states[1];impl_->has_presented=header[3];impl_->gb_custom_width=int(header[2]);
}
}
