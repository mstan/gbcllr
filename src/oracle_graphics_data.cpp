#include "oracle_graphics_data.hpp"
#include "native_mods.hpp"
#include <algorithm>
#include <stdexcept>
namespace gbcllr {
bool oracle_graphics_data(const ModContext& c,Registers r,OracleGraphicsData& result) {
 unsigned entry=r.pc==0x672?0x672:0x64e,finish=entry+0x7b;
 if(!c.rom||!c.vram||!c.wram||!c.hram||!c.ppu||c.rom_size!=0x100000||
    c.pc!=entry||r.pc!=entry||r.sp<0xc180||r.sp>0xc21e||
    (c.ppu->lcdc&128)||c.rom[finish]!=0xc9)return false;
 unsigned caller=c.wram[r.sp-0xc000]|(unsigned(c.wram[r.sp+1-0xc000])<<8);
 bool caller_ok=entry==0x672?(caller==0x656||caller==0x16a9||caller==0x16ca||caller==0x3865):
  (caller==0x632||caller==0x37f6||caller==0x1691);
 if(!caller_ok||c.rom[caller-3]!=0xcd||c.rom[caller-2]!=(entry&255)||c.rom[caller-1]!=(entry>>8))return false;
 unsigned selector=r.de&15,destination=r.de&0xfff0,size=((r.bc>>8)+1)*16;bool video=destination<0xa000;
 if((video?(selector>1||destination<0x8000||destination+size>0xa000):
    (selector>7||destination<0xd000||destination+size>0xe000))||size>2048||r.hl<0x4000||r.hl>=0x8000)return false;
 OracleGraphicsData out;out.registers=r;out.destination=uint16_t(destination);out.size=uint16_t(size);out.bank_selector=uint8_t(selector);out.video=video;
 std::copy_n(c.hram,128,out.hram.begin());
 if(video)std::copy_n(c.vram+selector*8192,8192,out.bank.begin());
 else std::copy_n(c.wram+(selector?selector:1)*4096,4096,out.bank.begin());
 unsigned bank=r.bc&63,source=r.hl,written=0,base=destination-(video?0x8000:0xd000),mode=r.bc&0xc0;
 try {
  auto read=[&]() -> uint8_t {
   size_t at=size_t(bank)*0x4000+source-0x4000;if(at>=c.rom_size)throw std::runtime_error("graphics source extent");
   auto value=c.rom[at];if(++source==0x8000){source=0x4000;++bank;}return value;
  };
  auto emit=[&](uint8_t value){if(written>=size)throw std::runtime_error("graphics output extent");out.bank[base+written++]=value;};
  if(mode==0) {
   uint8_t last=0;while(written<size){last=read();emit(last);}r.bc=0;r.af=uint16_t((unsigned(last)<<8)|0xc0);
  }else if(mode==0x80) {
   uint8_t final_c=0;
   while(written<size) {
    uint8_t first=read(),second=read();out.hram[0xa]=second;
    if(!(first|second)){for(unsigned i=0;i<16;++i)emit(read());final_c=0;continue;}
    uint8_t fill=read();out.hram[0xb]=fill;bool carry=false;
    for(auto mask:{first,second})for(unsigned i=0;i<8;++i) {
     bool bit=bool(mask&128);mask=uint8_t((unsigned(mask)<<1)|unsigned(carry));carry=bit;
     emit(bit?fill:read());final_c=mask;
    }
   }r.bc=final_c;r.af=0xc0;
  }else {
   bool big=mode==0xc0;out.hram[0xe]=big?255:0;if(!big)out.hram[0x13]=0;
   uint8_t counter=1,flags=0;
   while(written<size) {
    out.hram[0xb]=--counter;if(!counter){counter=8;out.hram[0xb]=counter;flags=read();out.hram[0xa]=flags;}
    bool reference=bool(flags&128);flags=uint8_t(flags<<1);out.hram[0xa]=flags;
    if(!reference){emit(read());continue;}
    unsigned distance,length;
    if(big) {
     auto low=read(),high=read();out.hram[0x12]=low;out.hram[0x13]=high&7;
     distance=((unsigned(high)&7)<<8)|low;length=(high&0xf8)?(high>>3)+2:read();
    }else {
     auto code=read();out.hram[0x12]=code&31;distance=code&31;length=(code&0xe0)?(code>>5)+1:read();
    }
    ++distance;out.hram[0xf]=uint8_t(length);if(!length)length=256;
    for(unsigned i=0;i<length;++i) {
     if(base+written<distance)throw std::runtime_error("graphics back reference extent");
     emit(out.bank[base+written-distance]);if(written==size)break;--out.hram[0xf];
    }
   }r.bc=0;r.af=0x80;
  }
 }catch(const std::exception&){return false;}
 if(bank>=c.rom_size/0x4000)return false;
 r.hl=uint16_t(source);r.de=uint16_t(destination+size);r.pc=uint16_t(finish);
 out.hram[0x17]=uint8_t(bank);out.rom_bank=uint8_t(bank);out.registers=r;result=std::move(out);return true;
}
bool oracle_layout_data(const ModContext& c,Registers r,OracleGraphicsData& result) {
 unsigned entry=r.pc==0x7fe?0x7fe:0x7da,call=entry-0x20,finish=entry-0x111;
 if(!c.rom||!c.vram||!c.wram||!c.hram||!c.ppu||c.rom_size!=0x100000||
    c.pc!=entry||r.pc!=entry||r.sp<0xc180||r.sp>0xc21e||(c.ppu->lcdc&128)||
    c.rom[call]!=0xcd||c.rom[call+1]!=(entry&255)||c.rom[call+2]!=(entry>>8)||c.rom[finish]!=0xc9||
    c.wram[r.sp-0xc000]!=((call+3)&255)||c.wram[r.sp+1-0xc000]!=((call+3)>>8))return false;
 unsigned selector=r.de&15,destination=r.de&0xfff0,size=c.hram[0xc]|(unsigned(c.hram[0xd])<<8);
 bool video=destination<0xa000;
 if(!size||(video?(selector>1||destination<0x8000||destination+size>0xa000):
    (selector>7||destination<0xd000||destination+size>0xe000))||r.hl<0x4000||r.hl>=0x8000)return false;
 OracleGraphicsData out;out.registers=r;out.destination=uint16_t(destination);out.size=uint16_t(size);out.bank_selector=uint8_t(selector);out.video=video;
 std::copy_n(c.hram,128,out.hram.begin());
 if(video)std::copy_n(c.vram+selector*8192,8192,out.bank.begin());
 else std::copy_n(c.wram+(selector?selector:1)*4096,4096,out.bank.begin());
 unsigned source=r.hl,compressed_bank=c.hram[0xe],dictionary_bank=c.hram[0xf]&63;
 unsigned dictionary=c.hram[0x10]|(unsigned(c.hram[0x11])<<8),base=destination-(video?0x8000:0xd000);
 unsigned written=0,bits=0,last_bank=compressed_bank;uint8_t key=0;
 try {
  auto rom_byte=[&](unsigned address,unsigned bank) {
   if(address>=0x8000)throw std::runtime_error("layout dictionary extent");
   size_t at=address<0x4000?address:size_t(bank)*0x4000+address-0x4000;
   if(at>=c.rom_size)throw std::runtime_error("layout ROM extent");return c.rom[at];
  };
  auto read=[&](){if(source<0x4000||source>=0x8000)throw std::runtime_error("layout stream extent");return rom_byte(source++,compressed_bank);};
  auto emit=[&](uint8_t value){if(written>=size)throw std::runtime_error("layout output extent");out.bank[base+written++]=value;};
  while(written<size) {
   last_bank=compressed_bank;if(!bits){key=read();bits=8;}
   bool copy=bool(key&1);key=uint8_t((key>>1)|(key<<7));out.hram[0xb]=key;
   if(!copy)emit(read());
   else {
    unsigned offset,length;
    if(c.hram[0xf]&128){length=read();auto low=read();offset=low|(unsigned(read())<<8);}
    else {auto low=read(),high=read();offset=low|((unsigned(high)&15)<<8);length=(high>>4)+3;}
    out.hram[0xa]=uint8_t(length);if(!length)length=256;last_bank=dictionary_bank;
    unsigned address=uint16_t(dictionary+offset);
    for(unsigned i=0;i<length&&written<size;++i)emit(rom_byte(address++,dictionary_bank));
   }
   if(written<size)--bits;
  }
 }catch(const std::exception&){return false;}
 if(last_bank>=c.rom_size/0x4000)return false;
 r.bc=uint16_t((bits<<8)|(r.bc&255));r.af=0x80;r.de=uint16_t(destination+size);r.hl=uint16_t(source);r.pc=uint16_t(finish);
 out.hram[0xc]=0;out.hram[0xd]=0;out.hram[0x17]=uint8_t(last_bank);out.rom_bank=uint8_t(last_bank);out.registers=r;
 result=std::move(out);return true;
}
bool oracle_memory_data(const ModContext& c,Registers r,unsigned vram_bank,OracleGraphicsData& result) {
 constexpr std::array<uint8_t,10> code{0xaf,0x5f,0x7b,0x22,0x0b,0x79,0xb0,0x20,0xf9,0xc9};
 unsigned entry=r.pc==0x475||r.pc==0x476?0x475:0x451;
 if(!c.rom||!c.vram||!c.wram||!c.hram||!c.ppu||c.rom_size!=0x100000||
    (r.pc!=entry&&r.pc!=entry+1)||c.pc!=r.pc||r.sp<0xc180||r.sp>0xc21e||
    (c.ppu->lcdc&128)||!std::equal(code.begin(),code.end(),c.rom+entry))return false;
 unsigned caller=c.wram[r.sp-0xc000]|(unsigned(c.wram[r.sp+1-0xc000])<<8);
 if(!caller||caller>=0x8000)return false;
 unsigned destination=r.hl,size=r.bc;bool video=destination<0xa000;
 unsigned selector=video?vram_bank:c.wram_bank;
 if(!size||(video?(selector>1||destination<0x8000||destination+size>0xa000):
    (selector>7||destination<0xd000||destination+size>0xe000)))return false;
 OracleGraphicsData out;out.destination=uint16_t(destination);out.size=uint16_t(size);out.bank_selector=uint8_t(selector);out.video=video;out.select_banks=false;
 std::copy_n(c.hram,128,out.hram.begin());
 if(video)std::copy_n(c.vram+selector*8192,8192,out.bank.begin());
 else std::copy_n(c.wram+(selector?selector:1)*4096,4096,out.bank.begin());
 unsigned base=destination-(video?0x8000:0xd000);uint8_t fill=r.pc==entry?0:uint8_t(r.af>>8);
 std::fill_n(out.bank.begin()+base,size,fill);
 r.af=0x80;r.bc=0;r.de=uint16_t((r.de&0xff00)|fill);r.hl=uint16_t(destination+size);r.pc=uint16_t(entry+9);
 out.rom_bank=uint8_t(c.rom_bank);out.registers=r;result=std::move(out);return true;
}

}
