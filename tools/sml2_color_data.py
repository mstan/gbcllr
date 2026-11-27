"""Export presentation-only SML2 DX data from pinned owned inputs.
Generate the checkpoints with qa/SML2-COLOR.md first. No game code or ROM
body is emitted; output belongs in an ignored build directory.
"""
import argparse,hashlib,json,pathlib,struct
ORIGINAL='42045a1642f77b568534ab85b9db93c8a33d2b95c38ae84c987f497e8d52f97f'
DX='1dd604a91b9ee99ef381cc795da3ae28c72d04ba9de78b355df829e56ce49c46'
# Original mode20 loader/node bindings; ROM24:5EE6 supplies cursor coordinates.
SUBMAPS=(('tree',2,35),('pumpkin',1,40),('macro',3,50),('space',4,60),('turtle',5,70),('mario',6,80),('wario-castle',7,91),('submap8',8,114))
# Boss bindings encode actual AFC0+1, retaining0 for scenes without a boss.
EXTRA_FIXED=(('castle-gate',28,-1,0),('wario0',4,-1,7),('wario1',4,-1,8),('wario2',4,-1,9))
COIN_FIXED=(('tree-boss',5,1),('pumpkin-boss',9,2),('mario-boss',13,3),('turtle-boss',16,4),('space-boss',19,5),('macro-boss',23,6))
ENDING_FIXED=(('ending-cursed',6),('ending-wave',7),('ending-restored',10),('ending-logo',12),('ending-staff-start',17),('ending-staff',18),('ending-complete',19))
ENDING_EXTRA=(('ending-lead',0,9),('ending-restore-start',8,0),('ending-restore-swap',9,0),('ending-logo-change',15,0))
BONUS_FIXED=(('coin999',9),('coin200',10),('coin50',11),('coin30',12),('bonus13',13),('bonus14',14))
def digest(b):return hashlib.sha256(b).hexdigest()
def checkpoint(path,rom,form,stage=0):
 d=path.read_bytes()
 if len(d)<217 or len(d)>16*1024*1024 or d[4:68]!=rom.encode():raise ValueError('checkpoint ROM identity mismatch: '+str(path))
 off=217+struct.unpack_from('<I',d,213)[0];out=[]
 for _ in range(3):
  n=struct.unpack_from('<I',d,off)[0];off+=4
  if n>0x20000 or off+n>len(d):raise ValueError('invalid checkpoint memory layout')
  out.append(d[off:off+n]);off+=n
 if len(out[1])!=(0x2000 if rom==ORIGINAL else 0x4000):raise ValueError('checkpoint hardware model mismatch')
 if len(out[2])<0x28c or (form is not None and (out[2][0x216]!=form or out[2][0x269]!=stage or out[2][0x28b]&0xf0)):raise ValueError('checkpoint must be the matching small/big stage presentation: '+str(path))
 return out[1],out[2],out[0]
def export(args):
 old=args.original_rom.read_bytes();r=args.dx_rom.read_bytes()
 if digest(old)!=ORIGINAL or digest(r)!=DX:raise ValueError('owned input ROM identity mismatch')
 if args.bonus_sources and not all([args.stage_sources,args.dx_fire_state,args.fixed_sources,args.submap_sources,args.extra_fixed_sources,args.ending_sources,args.coin_boss_sources,args.extra_ending_sources]):raise ValueError('bonus profile requires the complete existing stage/fixed/boss/ending sources')
 if args.paid_bonus_sources and not args.bonus_sources:raise ValueError('paid bonus profile requires the complete F15 sources')
 if args.gameover_data and not args.paid_bonus_sources:raise ValueError('Game Over profile requires the complete F17 sources')
 paths=[args.original_small_state,args.original_big_state,args.dx_small_state,args.dx_big_state]
 if not args.stage_sources and not all(paths):raise ValueError('provide four intro checkpoints or --stage-sources')
 stages=range(32) if args.stage_sources else range(1)
 paths=([args.stage_sources/body/str(form)/f'stage{stage:02}.state' for body in ('original','dx') for stage in stages for form in (0,1)] if args.stage_sources else paths)
 count=len(stages);checkpoints=[checkpoint(p,ORIGINAL if i<2*count else DX,i%2,(i//2)%count) for i,p in enumerate(paths)];v=[q[0] for q in checkpoints]
 lines=['// Generated presentation data; retain under ignored build/input directories.','#pragma once','#include <cstdint>','namespace gbcllr::sml2_color {']
 def arr(t,name,data,cols=24):
  lines.append('inline constexpr '+t+' '+name+'['+str(len(data))+']={')
  for i in range(0,len(data),cols):lines.append(','.join(str(x) for x in data[i:i+cols])+',')
  lines.append('};')
 arr('uint8_t','bg_attributes',r[33*0x4000:33*0x4000+8192],32)
 arr('uint8_t','bg_palettes',r[38*0x4000:38*0x4000+2048],32)
 arr('uint8_t','obj_palettes',r[38*0x4000+0x1000:38*0x4000+0x1800],32)
 blocks=[]
 for stage in stages:
  small=checkpoints[2*count+stage*2][1][0x600:0x800];big=checkpoints[2*count+stage*2+1][1][0x600:0x800]
  if small!=big:raise ValueError('DX block presentation depends on power form')
  blocks.extend(small)
 arr('uint8_t','block_tiles',blocks,32)
 hud=[]
 for stage in stages:
  small=checkpoints[2*count+stage*2][0][0x3c00:0x3c20];big=checkpoints[2*count+stage*2+1][0][0x3c00:0x3c20]
  if small!=big:raise ValueError('DX HUD attributes depend on power form')
  hud.extend(small)
 arr('uint8_t','hud_attributes',hud,32)
 geometry=[];rows=[]
 for stage in stages:
  for power in (0,1):
   a=checkpoints[stage*2+power];b=checkpoints[2*count+stage*2+power]
   original_map=a[1][0x1000:0x2000]+a[2][:0x2000];color_map=b[1][0x1000:0x2000]+b[2][:0x2000]
   for row in range(48):
    pair=original_map[row*256:(row+1)*256]+color_map[row*256:(row+1)*256]
    if pair not in geometry:geometry.append(pair)
    rows.append(geometry.index(pair))
 arr('uint16_t','geometry_rows',rows)
 arr('uint8_t','geometry_cells',list(b''.join(geometry)),32)
 seq=[()];index=[]
 for bank,table in [(35,0x40b1),(35,0x4f11),(40,0x40b1),(40,0x4f11),(58,0x40b1),(58,0x4f11),(44,0x4000),(45,0x4000)]:
  for frame in range(256):
   a=bank*0x4000+table-0x4000+frame*2;ptr=r[a]|(r[a+1]<<8);q=[]
   for _ in range(64):
    if not 0x4000<=ptr<0x7ffc:break
    a=bank*0x4000+ptr-0x4000
    if r[a]==128:break
    q.append(tuple(r[a:a+4]));ptr+=4
   q=tuple(q)
   if q not in seq:seq.append(q)
   index.append(seq.index(q))
 # ROM18:51B1 has49 sprite frames; the following5213 table is a
 # different three-byte BG command format. The actual native frame index
 # selects its paired DX record even when clocks/positions differ.
 bonus_index=[]
 if args.bonus_sources:
  for frame in range(49):
   q=[];ptr=int.from_bytes(r[0x611b1+frame*2:0x611b3+frame*2],'little')
   for _ in range(40):
    if not 0x4000<=ptr<=0x7ffc:raise ValueError('bonus sprite pointer out of range')
    a=0x5c000+ptr
    if r[a+2]==255:break
    q.append(tuple(r[a:a+4]));ptr+=4
   else:raise ValueError('unterminated bonus sprite record')
   q=tuple(q)
   if q not in seq:seq.append(q)
   bonus_index.append(seq.index(q))
 arr('uint16_t','bonus_presentation_index',bonus_index or [0])
 # The paid panel is a complete6x9 rectangle copied by0F:6BD5 through
 # home3E51 from bank10. Both ROMs use identical tile IDs, including the
 # blank/erase cells; their textures/palettes supply the color differences.
 # Static per-cell checkpoint translation cannot describe later prize cycles.
 paid_tiles=[]
 if args.paid_bonus_sources:
  if old[0x3ebdf:0x3ebe3]!=bytes.fromhex('21756c09') or old[0x3ec0b:0x3ec11]!=bytes.fromhex('018a98cd513e') or old[0x3e51:0x3e5a]!=bytes.fromhex('3e10ea4ea2ea00302a'):raise ValueError('paid BG producer source mismatch')
  if old[0x3ebd5:0x3ec75]!=r[0x3ebd5:0x3ec75] or old[0x3ec64:0x3ec75]!=bytes.fromhex('3ed4223ed577011f00093ed8223ed977c9'):raise ValueError('paired paid BG/highlight producer mismatch')
  for course in range(4):
   op=int.from_bytes(old[0x3ec75+course*2:0x3ec77+course*2],'little');dp=int.from_bytes(r[0x3ec75+course*2:0x3ec77+course*2],'little')
   if op!=dp or not 0x4000<=op<=0x7f1c:raise ValueError('paired paid BG table pointer mismatch')
   a=old[0x3c000+op:0x3c000+op+228];b=r[0x3c000+dp:0x3c000+dp+228]
   if len(a)!=228 or a!=b:raise ValueError('paired paid BG records changed geometry or tile IDs')
   paid_tiles.extend(a)
 arr('uint8_t','paid_bonus_tiles',paid_tiles or [0])
 arr('uint16_t','presentation_index',index)
 arr('uint16_t','presentation_offset',[sum(len(q) for q in seq[:i]) for i in range(len(seq))])
 arr('uint8_t','presentation_length',[len(q) for q in seq])
 lines.append('struct Piece{int8_t y,x;uint8_t tile,attr;};\ninline constexpr Piece pieces[]={')
 for q in seq:
  for y,x,t,a in q:lines.append('{'+','.join(str(z if z<128 else z-256) for z in (y,x))+','+str(t)+','+str(a)+'},')
 lines.append('};');patterns=[];old_index=[];color_index=[];fire_index=[]
 def intern(b):
  if b not in patterns:patterns.append(b)
  return patterns.index(b)
 for data in v[:2*count]:
  for i in range(0,0x1800,16):old_index.append(intern(data[i:i+16]))
 for data in v[2*count:]:
  if len(data)!=0x4000:raise ValueError('DX checkpoint requires CGB VRAM')
  for bank in (0,1):
   for i in range(0,0x1800,16):color_index.append(intern(data[bank*0x2000+i:bank*0x2000+i+16]))
 if args.dx_fire_state:
  fire,ram,_=checkpoint(args.dx_fire_state,DX,3,0)
  if ram[0x217]:raise ValueError('Fire texture source must have ordinary, non-flashing presentation')
  base=v[2*count+1]
  if fire[0x400:0x1000]!=base[0x400:0x1000] or fire[0x2000:0x3000]!=base[0x2000:0x3000]:raise ValueError('Fire sprite texture changes exceed the qualified Mario tile region')
  fire_index=[intern(fire[i:i+16]) for i in range(0,0x400,16)]
 else:fire_index=[0]*64
 fixed=[];fixed_paths=[];world_old=b'';world_dx=b'';world_attributes=b''
 if args.fixed_sources:
  boss_stages={name:stage for name,stage,_ in COIN_FIXED}
  bindings=[(x,m,s,b,-1) for x,m,s,b in [('title',1,-1,0),('file',26,-1,0),('map',12,0,0)]+([(name,12,submode,0) for name,submode,_ in SUBMAPS] if args.submap_sources else [])+(list(EXTRA_FIXED) if args.extra_fixed_sources else [])]+([(name,19,-1,0,phase) for name,phase in ENDING_FIXED] if args.ending_sources else [])+([(name,4,-1,boss,-1) for name,_,boss in COIN_FIXED] if args.coin_boss_sources else [])+([(name,19,-1,boss,phase) for name,phase,boss in ENDING_EXTRA] if args.extra_ending_sources else [])+([(name,12,submode,0,-1) for name,submode in BONUS_FIXED] if args.bonus_sources else [])
  for name,mode,submode,boss,phase in bindings:
   folder=args.bonus_sources if any(name==q[0] for q in BONUS_FIXED) else args.extra_ending_sources if any(name==q[0] for q in ENDING_EXTRA) else args.coin_boss_sources if name in boss_stages else args.ending_sources if phase>=0 else args.extra_fixed_sources if (name,mode,submode,boss) in EXTRA_FIXED else args.submap_sources if submode>0 else args.fixed_sources;source=[];stage=boss_stages.get(name,24) if boss else 255
   paid=args.paid_bonus_sources and any(name==q[0] for q in BONUS_FIXED[:4])
   if paid:folder=args.paid_bonus_sources
   for body,identity in [('original',ORIGINAL),('dx',DX)]:
    state=folder/body/(name+'.state');memory=checkpoint(state,identity,None,None);source.append(memory[0]);fixed_paths.append(state)
    if name=='castle-gate' and memory[1][0x2cb]!=6:raise ValueError('castle gate source must retain the actual six-coin panel')
    if name=='ending-lead':
     if memory[1][0x269]!=24 or memory[1][0x216]!=0 or memory[1][0xfc0]!=8 or memory[1][0xfc5]!=0:raise ValueError('ending lead must retain actual defeated Wario and the cleared Small form')
    elif boss and (memory[1][0x269]!=stage or memory[1][0x216]!=3 or memory[1][0xfc0]+1!=boss):raise ValueError('boss source must be the matching actual Fire-form arena')
    if phase>=0 and memory[1][0x2dd]!=phase:raise ValueError('finale source phase mismatch')
    if submode>=0 and memory[1][0x68b]!=submode:raise ValueError('fixed map submode mismatch')
    if submode>0:
     sub=folder/body/(name+'-submode.bin');fixed_paths.append(sub)
     if sub.read_bytes()!=bytes([submode]):raise ValueError('captured map submode mismatch')
    if paid:
     balance=folder/body/(name+'-balance.bin');fixed_paths.append(balance)
     expected={'coin30':bytes([0x69,9]),'coin50':bytes([0x49,9]),'coin200':bytes([0x99,7]),'coin999':bytes([0,0])}[name]
     if balance.read_bytes()!=expected or memory[1][0x262:0x264]!=expected:raise ValueError('paid source must retain the actual fee deduction from its supplied999 prerequisite')
    if name=='map':
     if body=='original':world_old=memory[1][0x1000:0x2000]
     else:
      world_dx=memory[1][0x1000:0x2000];world_attributes=memory[2][0x2000:0x2100]
      if world_attributes!=r[0xa6542:0xa6642] or any(source[-1][0x3800+i]!=world_attributes[source[-1][0x1800+i]] for i in range(1024)):raise ValueError('DX main world-map attribute source mismatch')
    mode_path=folder/body/(name+'-mode.bin');fixed_paths.append(mode_path)
    if mode_path.read_bytes()!=bytes([mode]):raise ValueError('fixed scene mode mismatch')
   old_ppu=folder/'original'/(name+'-ppu.bin');dx_ppu=folder/'dx'/(name+'-ppu.bin');bg=folder/'dx'/(name+'-bg.bin');obj=folder/'dx'/(name+'-obj.bin');old_oam=folder/'original'/(name+'-oam.bin');dx_oam=folder/'dx'/(name+'-oam.bin')
   fixed_paths.extend([old_ppu,dx_ppu,bg,obj,old_oam,dx_oam]);regs=[old_ppu.read_bytes(),dx_ppu.read_bytes()];pal=[bg.read_bytes(),obj.read_bytes()]
   if any(len(x)!=16 for x in regs) or any(len(x)!=64 or x==b'\xff'*64 for x in pal):raise ValueError('fixed scene requires VBlank PPU/palette captures')
   a=old_oam.read_bytes();b=dx_oam.read_bytes()
   if len(a)!=160 or len(b)!=160:raise ValueError('fixed scene OAM capture size mismatch')
   attributes=[set() for _ in range(256)]
   for i in range(0,160,4):
    if not 0<a[i]<160:continue
    for j in range(0,160,4):
     if a[i:i+3]==b[j:j+3] and not (a[i+3]^b[j+3])&0x60:attributes[a[i+2]].add(b[j+3]&0x9f)
   fixed.append((mode,source,regs,pal,[next(iter(x)) if len(x)==1 else 255 for x in attributes],submode,boss,phase,stage))
 if args.gameover_data:
  # Native0:27B2 and DX0:27AF copy the paired bank0C graphics/map.
  # DX27:4505 supplies attributes27:4000 and palette pointers7C39/7C51;
  # home1948 selects bank26 for both64-byte palette uploads. These are
  # read-only presentation tables, not donor execution or synthetic states.
  loader=bytes.fromhex('01001821924c110080cd3603110098219264')
  pointers=bytes.fromhex('afea1aa23e7ceadca13e39eadda13e7ceadea13e51eadfa1')
  if old[0x27c3:0x27c3+len(loader)]!=loader:raise ValueError('Game Over native loader mismatch')
  if r[0x9c505:0x9c505+len(pointers)]!=pointers:raise ValueError('Game Over DX palette pointers mismatch')
  if r[0x1948:0x1952]!=bytes.fromhex('3e26ea00213e800640c9'):raise ValueError('Game Over DX palette bank mismatch')
  tilemap=old[0x32492:0x32792]
  if tilemap!=r[0x32492:0x32792]:raise ValueError('Game Over paired map geometry changed')
  attributes=[255]*256
  # Native0C:4884 selects six retained/lost coin icons;497C selects
  # their four fall frames. The existing native emitter retains live poses.
  gameover_frames=[old[0x2c000+a] for a in (0x49ba,0x49be,0x49c2,0x49c5,0x49c8,0x49cc)]+list(range(0xde,0xe2))+[0xe3,0xe4]
  for frame in gameover_frames:
   op=int.from_bytes(old[0x4000+frame*2:0x4002+frame*2],'little')
   dp=int.from_bytes(r[0xb0000+frame*2:0xb0002+frame*2],'little')
   for _ in range(40):
    if not 0x4000<=op<=0x7ffc or not 0x4000<=dp<=0x7ffc:raise ValueError('Game Over sprite pointer out of range')
    a=old[op:op+4];b=r[0xac000+dp:0xac004+dp]
    if a[0]==128:
     if b[0]!=128:raise ValueError('Game Over paired sprite terminator changed')
     break
    if a[:3]!=b[:3] or (a[3]^b[3])&0x60:raise ValueError('Game Over paired sprite geometry changed')
    if attributes[a[2]] not in (255,b[3]&0x1f):raise ValueError('ambiguous Game Over sprite palette')
    attributes[a[2]]=b[3]&0x1f;op+=4;dp+=4
   else:raise ValueError('unterminated Game Over sprite')
  source=[old[0x30c92:0x32492]+tilemap+bytes(1280),r[0x30c92:0x32492]+tilemap+bytes(1280)+bytes(6144)+r[0x9c000:0x9c240]+bytes(1472)]
  # Only LCDC/BGP/SCY/SCX are used by this fixed-screen binding. The
  # native loader publishesC3/E4 and zero scroll; its OBJ priority stays live.
  ppu=bytes([0xc3,0,0,0,0,0,0,0xe4,0xe4,0xe4,0,0,0,0,0,0])
  fixed.append((7,source,[ppu,ppu],[r[0x9bc39:0x9bc79],r[0x9bc51:0x9bc91]],attributes,-1,0,-1,255))
  bindings.append(('gameover',7,-1,0,-1))
 arr('uint8_t','fixed_modes',[q[0] for q in fixed] or [0])
 arr('int8_t','fixed_submodes',[q[5] for q in fixed] or [-1])
 arr('uint8_t','fixed_bosses',[q[6] for q in fixed] or [0])
 arr('uint8_t','fixed_stages',[q[8] for q in fixed] or [255])
 arr('int8_t','fixed_phases',[q[7] for q in fixed] or [-1])
 arr('uint8_t','world_old_map',list(world_old) or [0],32);arr('uint8_t','world_color_map',list(world_dx) or [0],32);arr('uint8_t','world_attributes',list(world_attributes) or [0],32)
 animation_addresses=[0x8e0,0x8f0,0x9e0,0x9f0,0xce0,0xcf0,0xde0,0xdf0]
 arr('uint8_t','world_animation_original',list(b''.join(old[0x34000+a:0x34000+a+16] for a in animation_addresses)),32)
 arr('uint8_t','world_animation_color',list(b''.join(r[0x34000+a:0x34000+a+16] for a in animation_addresses)),32)
 # DX29:6062/6083/60A6 select the bank26 palettes6A00/69C0 through
 # A1DC/A1DD. The original castle's A797 state drives the same flash.
 arr('uint8_t','castle_flash_palette',list(r[0x9aa00:0x9aa40]),32)
 # Wario's submap uses the shared ROM18:62E6 emitter, not the main-map
 # 0F:4C4D prop table. Original palette/priority disambiguate shared tiles.
 castle_attributes=[255]*1024
 for frame in range(53,67):
  a=0x622e6+frame*2;op=0x5c000+int.from_bytes(old[a:a+2],'little');dp=0x5c000+int.from_bytes(r[a:a+2],'little')
  for _ in range(64):
   if old[op+2]==255:
    if r[dp+2]!=255:raise ValueError('DX castle prop terminator changed')
    break
   if old[op:op+3]!=r[dp:dp+3] or (old[op+3]^r[dp+3])&0x60:raise ValueError('DX castle prop geometry changed')
   key=old[op+2]+256*(((old[op+3]>>4)&1)|((old[op+3]>>6)&2));attr=r[dp+3]&0x9f
   if castle_attributes[key] not in (255,attr):raise ValueError('ambiguous DX castle prop attribute')
   castle_attributes[key]=attr;op+=4;dp+=4
  else:raise ValueError('unterminated DX castle prop record')
 arr('uint8_t','castle_object_attributes',castle_attributes,32)
 # Main-map props keep native geometry. Their actual17 records have a
 # unique palette/bank choice per tile; HUD shares tiles but not this region.
 prop_attributes=[255]*256
 for frame in range(17):
  a=0x3cc4d+frame*2;op=0x38000+int.from_bytes(old[a:a+2],'little');dp=0x38000+int.from_bytes(r[a:a+2],'little')
  for _ in range(64):
   if old[op+2]==255:break
   if old[op:op+3]!=r[dp:dp+3] or (old[op+3]^r[dp+3])&0x60:raise ValueError('DX main-map prop geometry changed')
   tile=old[op+2];attr=r[dp+3]&0x9f
   if prop_attributes[tile] not in (255,attr):raise ValueError('ambiguous DX main-map prop attribute')
   prop_attributes[tile]=attr;op+=4;dp+=4
  else:raise ValueError('unterminated DX main-map prop record')
 arr('uint8_t','world_object_attributes',prop_attributes,32)
 arr('uint8_t','fixed_object_attributes',[x for q in fixed for x in q[4]] or [255])
 for name,values in [('fixed_old_map',b''.join(q[1][0][0x1800:0x2000] for q in fixed)),('fixed_color_map',b''.join(q[1][1][0x1800:0x2000] for q in fixed)),('fixed_attributes',b''.join(q[1][1][0x3800:0x4000] for q in fixed)),('fixed_old_ppu',b''.join(q[2][0] for q in fixed)),('fixed_color_ppu',b''.join(q[2][1] for q in fixed)),('fixed_bg_palettes',b''.join(q[3][0] for q in fixed)),('fixed_obj_palettes',b''.join(q[3][1] for q in fixed))]:arr('uint8_t',name,list(values) or [0],32)
 arr('uint16_t','fixed_original_tiles',[intern(q[1][0][a:a+16]) for q in fixed for a in range(0,0x1800,16)] or [0])
 arr('uint16_t','fixed_color_tiles',[intern(q[1][1][bank*0x2000+a:bank*0x2000+a+16]) for q in fixed for bank in (0,1) for a in range(0,0x1800,16)] or [0])
 arr('uint16_t','fire_tiles',fire_index)
 arr('uint16_t','original_tiles',old_index);arr('uint16_t','color_tiles',color_index)
 arr('uint8_t','texture_patterns',list(b''.join(patterns)),32)
 lines+=['inline constexpr unsigned stage_count='+str(count)+';',
  'inline constexpr bool fire_available='+('true' if args.dx_fire_state else 'false')+';',
  'inline constexpr unsigned fixed_count='+str(len(fixed))+';',
  'inline constexpr unsigned bonus_frame_count='+str(len(bonus_index))+';',
  'inline constexpr bool paid_bonus_available='+('true' if args.paid_bonus_sources else 'false')+';',
  'inline constexpr bool world_animation_available='+('true' if len(fixed)>=3 else 'false')+';',
  'inline constexpr bool castle_flash_available='+('true' if args.submap_sources else 'false')+';',
  'inline constexpr bool castle_objects_available='+('true' if args.submap_sources else 'false')+';',
  'inline constexpr bool ending_extra_available='+('true' if args.extra_ending_sources else 'false')+';',
  'inline const uint8_t* fixed_original(unsigned scene,unsigned address){return scene<fixed_count&&address<0x1800?texture_patterns+fixed_original_tiles[scene*384+address/16]*16:nullptr;}',
  'inline const uint8_t* fixed_texture(unsigned scene,unsigned bank,unsigned address){return scene<fixed_count&&bank<2&&address<0x1800?texture_patterns+fixed_color_tiles[(scene*2+bank)*384+address/16]*16:nullptr;}',
  'inline const uint8_t* fire_texture(unsigned bank,unsigned address){return fire_available&&bank==0&&address<0x400?texture_patterns+fire_tiles[address/16]*16:nullptr;}',
  'inline int block_tile(unsigned stage,unsigned power,unsigned cell,unsigned live,unsigned part){if(stage>=stage_count||power>1||cell>=12288||part>=4)return -1;unsigned a=geometry_rows[(stage*2+power)*48+cell/256]*512,col=cell&255;if(geometry_cells[a+col]!=live)return -1;unsigned dx=geometry_cells[a+256+col];return dx<128?block_tiles[stage*512+dx*4+part]:-1;}',
  'inline unsigned presentation(unsigned table,unsigned frame){return table<8&&frame<256?presentation_index[table*256+frame]:0;}',
  'inline const uint8_t* texture(unsigned stage,unsigned power,unsigned bank,unsigned address){if(stage>=stage_count||power>1||bank>1||address>=0x1800)return nullptr;return texture_patterns+color_tiles[((stage*2+power)*2+bank)*384+address/16]*16;}',
  'inline const uint8_t* original(unsigned stage,unsigned power,unsigned address){if(stage>=stage_count||power>1||address>=0x1800)return nullptr;return texture_patterns+original_tiles[(stage*2+power)*384+address/16]*16;}', '}']
 text=('\n'.join(lines)+'\n').encode();args.output_dir.mkdir(parents=True,exist_ok=True)
 (args.output_dir/'sml2_color_data.hpp').write_bytes(text)
 meta=dict(format=17 if args.paid_bonus_sources else 15 if args.bonus_sources else 14,original_rom_sha256=ORIGINAL,dx_rom_sha256=DX,header_sha256=digest(text),extractor_sha256=digest(pathlib.Path(__file__).read_bytes()),map_animation_kind='paired-row-rotation-v1',map_actor_kind='native-props-mario-overlay-v1',castle_flash_kind='native-A797-DX26-6A00-v1',castle_actor_kind='native-props53-66-v1',ending_extra_kind='native-phase0-8-9-15-v1',fixed_scenes=[dict(name=x,mode=m,submode=s,boss_type=b-1 if b else None,stage=boss_stages.get(x,24) if b else None,phase=p) for x,m,s,b,p in bindings] if args.fixed_sources else [],
  scope=f'{count} stage texture sets, small/big presentation; full color parity remains unqualified',checkpoints=[dict(name=p.as_posix(),sha256=digest(p.read_bytes())) for p in paths])
 if args.dx_fire_state:meta['fire_checkpoint']=dict(name=args.dx_fire_state.as_posix(),sha256=digest(args.dx_fire_state.read_bytes()))
 if args.bonus_sources:
  meta['bonus_scene_kind']='native-map-submodes9-14-v1'
  meta['bonus_actor_kind']='native-18-51B1-frame-anchor-v1'
 if args.paid_bonus_sources:
  meta['coin_scene_kind']='native-paid-30-50-200-999-v1'
  meta['coin_bg_kind']='native-0F-6BD5-bank10-complete54-v1'
 if args.gameover_data:
  meta['format']=18
  meta['gameover_kind']='native-mode7-bank0C-DX27-v1'
  meta['gameover_frames']=gameover_frames
  meta['gameover_sources']={name:dict(offset=at,size=n,sha256=digest(data[at:at+n])) for name,data,at,n in [('original_tiles',old,0x30c92,6144),('color_tiles',r,0x30c92,6144),('map',r,0x32492,768),('attributes',r,0x9c000,576),('bg_palette',r,0x9bc39,64),('obj_palette',r,0x9bc51,64)]}
 if fixed_paths:meta['fixed_checkpoints']=[dict(name=p.as_posix(),sha256=digest(p.read_bytes())) for p in fixed_paths]
 (args.output_dir/'sml2_color_data.json').write_bytes((json.dumps(meta,indent=2)+'\n').encode())
 print(json.dumps(dict(format=meta['format'],header_sha256=meta['header_sha256'],stages=count,fixed_scenes=len(fixed),fire=bool(args.dx_fire_state))))
if __name__=='__main__':
 p=argparse.ArgumentParser(description=__doc__)
 for name in ['original-rom','dx-rom','output-dir']:p.add_argument('--'+name,type=pathlib.Path,required=True)
 for name in ['original-small-state','original-big-state','dx-small-state','dx-big-state']:p.add_argument('--'+name,type=pathlib.Path)
 p.add_argument('--stage-sources',type=pathlib.Path,help='Existing original/dx, form0/1, stage00..31 checkpoints from normal stage travel')
 p.add_argument('--dx-fire-state',type=pathlib.Path,help='Earned normal Fire Mario intro checkpoint; presentation-only tile override')
 p.add_argument('--fixed-sources',type=pathlib.Path,help='Existing title/file/map checkpoints, VBlank PPU registers and DX BG/OBJ palettes')
 p.add_argument('--submap-sources',type=pathlib.Path,help='Existing mode12 submaps1..8 checkpoints, VBlank PPU registers and DX palettes')
 p.add_argument('--extra-fixed-sources',type=pathlib.Path,help='Earned six-coin panel and three actual Fire-form Wario arena checkpoints/palettes')
 p.add_argument('--ending-sources',type=pathlib.Path,help='Actual ending restoration and credits checkpoints/palettes')
 p.add_argument('--coin-boss-sources',type=pathlib.Path,help='Actual six coin-boss Fire-form arena checkpoints/palettes')
 p.add_argument('--extra-ending-sources',type=pathlib.Path,help='Natural phase0/8/9/15 ending lead-in/restoration/logo checkpoints/palettes')
 p.add_argument('--bonus-sources',type=pathlib.Path,help='Paired actual map submode9..14 coin/bonus UI checkpoints and palettes; preserve F14 output and use a fresh F15 directory')
 p.add_argument('--paid-bonus-sources',type=pathlib.Path,help='Actual native paid original/DX coin-course captures with observed post-fee balances; preserve F14/F15 and use a fresh F16 directory')
 p.add_argument('--gameover-data',action='store_true',help='Append the native Game Over binding from exact read-only original/DX presentation tables; use a fresh F18 directory')
 export(p.parse_args())
