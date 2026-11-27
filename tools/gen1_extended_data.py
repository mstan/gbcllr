#!/usr/bin/env python3
"""Export owned donor content for a native Gen1 adapter; never emit a guest ROM.

The source ROM is a read-only parity/data donor. Runtime code continues to use
the exact owned stock Red/Blue cartridge. Pictures, moves/effects and expanded
Dex persistence are separate unfinished parts of the native feature contract.
"""
from pathlib import Path
import argparse, hashlib, json, re

def symbols(path):
    out={}
    for line in path.read_text(encoding='utf-8').splitlines():
        m=re.fullmatch(r'([0-9a-fA-F]+):([0-9a-fA-F]+) (\S+)',line.strip())
        if m:
            bank,at=int(m[1],16),int(m[2],16)
            out[m[3]]=at if bank==0 else bank*0x4000+at-0x4000
    return out

def move_constants(path):
    values={};at=0
    for line in path.read_text(encoding='utf-8').splitlines():
        line=line.split(';',1)[0].strip()
        if line.startswith('const_def'):
            raw=line[len('const_def'):].strip();at=int(raw.replace('$','0x'),0) if raw else 0
        elif line.startswith('const_skip'):
            raw=line[len('const_skip'):].strip();at+=int(raw.replace('$','0x'),0) if raw else 1
        elif line.startswith('const '):
            values[line.split()[1]]=at;at+=1
    return values

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--donor-rom',type=Path,required=True)
    p.add_argument('--donor-symbols',type=Path,required=True)
    p.add_argument('--stock-symbols',type=Path,required=True)
    p.add_argument('--stock-rom',type=Path,required=True)
    p.add_argument('--stock-blue-rom',type=Path,required=True)
    p.add_argument('--move-constants',type=Path,required=True)
    p.add_argument('--effect-constants',type=Path,required=True)
    p.add_argument('--donor-pics-source',type=Path,required=True)
    p.add_argument('--out',type=Path,required=True)
    a=p.parse_args();data=a.donor_rom.read_bytes();sy=symbols(a.donor_symbols);stock=symbols(a.stock_symbols);stock_data=a.stock_rom.read_bytes();blue_data=a.stock_blue_rom.read_bytes()
    moves=move_constants(a.move_constants);struggle=moves['STRUGGLE'];assert 165<struggle<255
    dex=data[sy['PokedexOrder']:sy['PokedexOrder']+254]
    assert len(dex)==254
    added=[(i+1,n) for i,n in enumerate(dex) if 152<=n<=251]
    assert len(added)==100 and {n for _,n in added}==set(range(152,252))
    pic_paths={}
    for label,path in re.findall(r'(\w+Pic(?:Front|Back))::\s+INCBIN "([^"]+)"',a.donor_pics_source.read_text()):
        if label in sy:pic_paths[sy[label]]=a.donor_pics_source.parent.parent/path
    blob=[];records=[];picture_sources={};dex_bytes=[];evo_bytes=[]
    def native_move(value):
        return 165 if value==struggle else value+1 if 165<=value<struggle else value
    def evolution(rom,labels,species,remap):
        table=labels['EvosMovesPointerTable'];bank=table//0x4000
        pointer=int.from_bytes(rom[table+2*(species-1):table+2*species],'little')
        assert 0x4000<=pointer<0x8000
        at=bank*0x4000+pointer-0x4000;out=[]
        while rom[at]:
            method=rom[at];size=4 if method==2 else 3
            assert method in (1,2,3)
            row=rom[at:at+size];assert len(row)==size and 1<=row[-2]<=100
            out.extend(row);at+=size
        out.append(0);at+=1;previous=0
        while rom[at]:
            level,move=rom[at:at+2];assert previous<=level<=100 and 1<=move<=struggle
            out.extend((level,native_move(move) if remap else move));at+=2;previous=level
        out.append(0);assert len(out)<128
        return out
    # The donor retains the original internal IDs and original151 behavior.
    # Reject a changed donor profile instead of silently replacing stock data.
    stock_dex=stock_data[stock['PokedexOrder']:stock['PokedexOrder']+190]
    def icon(rom,labels,number):
        value=rom[labels['MonPartyData']+(number-1)//2]
        return (value>>4) if number&1 else value&15
    original_icons={icon(stock_data,stock,n) for n in range(1,152)}
    assert all(icon(data,sy,n)==icon(stock_data,stock,n) for n in range(1,152)),'Donor changes original party icons'
    for index,number in enumerate(stock_dex):
        if not 1<=number<=151:continue
        assert dex[index]==number,'Donor remaps an original species'
        assert evolution(data,sy,index+1,True)==evolution(stock_data,stock,index+1,False),'Donor changes an original learnset/evolution'
        assert data[sy['CryData']+index*3:sy['CryData']+index*3+3]==stock_data[stock['CryData']+index*3:stock['CryData']+index*3+3],'Donor changes an original cry'
    def picture(at):
        path=pic_paths[at];packed=path.read_bytes();raw_path=path.with_suffix('.2bpp');raw=raw_path.read_bytes()
        assert data[at:at+len(packed)]==packed,'Owned compressed picture differs from donor ROM'
        w,h=packed[0]>>4,packed[0]&15;assert 1<=w<=7 and 1<=h<=7 and len(raw)==w*h*16
        expected=bytearray(7*7*16)
        for y in range(h):
            for x in range(w):
                dest=((x+(8-w)//2)*7+y+7-h)*16
                expected[dest:dest+16]=raw[(y*w+x)*16:(y*w+x+1)*16]
        start=len(blob);blob.extend(packed)
        for q in (path,raw_path):picture_sources[str(q)]=hashlib.sha256(q.read_bytes()).hexdigest()
        return dict(offset=start,size=len(packed),dim=packed[0],expected=list(expected))
    for species,n in sorted(added):
        at=sy['BaseStats']+(n-1)*28;header=bytearray(data[at:at+28]);assert len(header)==28 and header[0]==n,(species,n,header[:10])
        name=data[sy['MonsterNames']+(species-1)*10:sy['MonsterNames']+species*10]
        assert len(name)==10 and name[0]!=0x50
        header[0]=species
        # Original move IDs, including Struggle165, remain stable for saves.
        for i in range(15,19):
            header[i]=native_move(header[i])
        bank=data[sy['PicBankByIndex']+species-1];assert bank>0
        pics=[picture(bank*0x4000+int.from_bytes(header[i:i+2],'little')-0x4000) for i in (11,13)]
        assert header[10]==pics[0]['dim']
        # These are adapter-owned data windows. Only the exact stock sprite
        # decoder can read them; neither donor pointers nor opcodes execute.
        header[11:15]=bytes([0,0x40,0,0x60])
        # Flatten only immutable text data, never donor code or TX_ASM. The
        # original stock text/number callers retain their complete execution.
        entry_ptr=int.from_bytes(data[sy['PokedexEntryPointers']+(species-1)*2:sy['PokedexEntryPointers']+species*2],'little')
        entry=16*0x4000+entry_ptr-0x4000;end=data.index(0x50,entry)+1
        category=data[entry:end];assert 1<len(category)<=13
        tail=data[end:end+9];assert len(tail)==9 and tail[4]==0x17 and tail[8]==0x50
        desc=tail[7]*0x4000+int.from_bytes(tail[5:7],'little')-0x4000
        desc_end=data.index(0x50,desc)+1;description=data[desc:desc_end]
        assert description[0]==0 and description[-2:]==bytes([0x5f,0x50]) and all(v>=0x40 for v in description[1:]),'Dex description requires a new native text handler'
        dex_entry=category+tail[:4]+description+bytes([0x50]);assert len(dex_entry)<=512
        dex_offset=len(dex_bytes);dex_bytes.extend(dex_entry)
        evo=evolution(data,sy,species,True);evo_offset=len(evo_bytes)
        # Original stone-menu FarCopyData copies thirteen bytes, even when an
        # evolution record is shorter. Padding is native immutable data only.
        evo_bytes.extend(evo+[0]*13)
        cry=list(data[sy['CryData']+(species-1)*3:sy['CryData']+species*3]);assert len(cry)==3 and cry[0]<38
        menu_icon=icon(data,sy,n);assert menu_icon in original_icons,'New party icon needs its own native art service'
        records.append(dict(species=species,dex=n,header=list(header),name=list(name)+[0x50],front=pics[0],back=pics[1],dex_offset=dex_offset,dex_size=len(dex_entry),evo_offset=evo_offset,evo_size=len(evo),cry=cry,icon=menu_icon))
    assert len(evo_bytes)<0x2000,'Native evolution data exceeds its guarded read window'
    type_names=[]
    for number in (9,27):
        pointer=int.from_bytes(data[sy['TypeNames']+number*2:sy['TypeNames']+number*2+2],'little')
        at=9*0x4000+pointer-0x4000;name=data[at:data.index(0x50,at)+1]
        assert 1<len(name)<=16;type_names.append(list(name)+[0x50]*(16-len(name)))
    # Native-authored optional encounter overlay, not a donor ROM patch.
    # Half of rare8/9 outcomes use these records; the original roll's other
    # parity retains the original species. Rates,levels and RNG draws stay native.
    habitats=[];habitat_index={};habitat_maps=[]
    map_count=(stock['NothingWildMons']-stock['WildDataPointers'])//2;assert map_count==249
    for map_id in range(map_count):
        pointers=[int.from_bytes(rom[stock['WildDataPointers']+map_id*2:stock['WildDataPointers']+map_id*2+2],'little') for rom in (stock_data,blue_data)]
        assert pointers[0]==pointers[1];positions=[3*0x4000+p-0x4000 for p in pointers];row=[]
        for water in (False,True):
            rates=[rom[pos] for rom,pos in zip((stock_data,blue_data),positions)];positions=[pos+1 for pos in positions]
            assert bool(rates[0])==bool(rates[1])
            if not rates[0]:row.append(255);continue
            key=(positions[0],water);assert positions[0]==positions[1]
            if key not in habitat_index:
                habitat_index[key]=len(habitats);habitats.append(dict(address=positions[0]-3*0x4000+0x4000,water=water,species=[0,0],levels=[stock_data[positions[0]+i*2] for i in (8,9)],maps=[]))
            index=habitat_index[key];habitats[index]['maps'].append(map_id);row.append(index);positions=[pos+20 for pos in positions]
        habitat_maps.append(row)
    minimum={x['species']:1 for x in records}
    for mon in records:
        at=mon['evo_offset']
        while evo_bytes[at]:
            method=evo_bytes[at];size=4 if method==2 else 3;target=evo_bytes[at+size-1]
            if method==1 and target in minimum:minimum[target]=max(minimum[target],evo_bytes[at+1])
            at+=size
    slots=sorted([(h['levels'][j],min(h['maps']),i,j) for i,h in enumerate(habitats) for j in range(2)])
    ordered=sorted(records,key=lambda x:(x['dex'] in (243,244,245,249,250,251),max(minimum[x['species']],max(1,(x['header'][9]-80)//5)),x['header'][9],x['dex']))
    assert len(slots)>=len(ordered)
    for i,mon in enumerate(ordered):
        _,_,h,j=slots[i*(len(slots)-1)//(len(ordered)-1)];habitats[h]['species'][j]=mon['species']
    assert sorted(v for h in habitats for v in h['species'] if v)==sorted(x['species'] for x in records)
    assert any(9 in x['header'][6:8] for x in records) and any(27 in x['header'][6:8] for x in records),'Donor must include real Steel/Dark'
    assert data[sy['Moves']:sy['Moves']+164*6]==stock_data[stock['Moves']:stock['Moves']+164*6],'Donor changes original move metadata; resolve that profile before importing'
    names=[];start=sy['MoveNames']
    for i in range(struggle):
        end=data.index(0x50,start)+1;names.append(data[start:end]);start=end
    name_stream=[];native_moves=[];max_effect=move_constants(a.effect_constants)['DISABLE_EFFECT']
    animations=[int.from_bytes(data[sy['AttackAnimationPointers']+i*2:sy['AttackAnimationPointers']+i*2+2],'little') for i in range(struggle)]
    for native_id in range(1,struggle+1):
        donor_id=struggle if native_id==165 else native_id-1 if native_id>165 else native_id
        raw=bytearray(data[sy['Moves']+(donor_id-1)*6:sy['Moves']+donor_id*6]);raw[0]=native_id
        assert raw[1]<=max_effect,'Donor requires a new battle handler; do not silently map it'
        offset=len(name_stream);name_stream.extend(names[donor_id-1])
        if native_id>165:
            assert animations[donor_id-1] in animations[:164],'Donor has a new animation; implement its native presentation before importing'
            native_moves.append(dict(id=native_id,data=list(raw),name_offset=offset,animation=animations[:164].index(animations[donor_id-1])+1))
    assert len(native_moves)==61 and all(len(names[x])<=13 for x in range(164,struggle-1))
    name_stream.extend([0x50]*20)
    for m in native_moves:m['name']=name_stream[m['name_offset']:m['name_offset']+20]
    type_effects=[];at=sy['TypeEffects']
    while data[at]!=255:
        type_effects.extend(data[at:at+3]);at+=3;assert len(type_effects)<1000
    type_effects.append(255)
    sha=lambda b:hashlib.sha256(b).hexdigest()
    asset=sha(data);content=sha(json.dumps(dict(mons=records,sprites=blob,moves=native_moves,names=name_stream,type_effects=type_effects,dex=dex_bytes,evolution=evo_bytes,type_names=type_names,habitats=habitats,habitat_maps=habitat_maps),separators=(',',':')).encode())
    text=['#pragma once','#include <array>','#include <cstdint>','namespace gbcllr::gen1_extended_data {',
          'struct Picture {uint32_t offset,size;std::array<uint8_t,784> expected;};',
          'struct Mon {uint8_t species,dex;std::array<uint8_t,28> header;std::array<uint8_t,11> name;Picture front,back;uint16_t dex_offset,dex_size,evo_offset,evo_size;std::array<uint8_t,3> cry;uint8_t icon;};',
          f'inline constexpr char asset_identity[]="{asset}";',f'inline constexpr char content_identity[]="{content}";',
          'inline constexpr std::array<Mon,100> mons{{']
    for x in records:
        pics=['{'+str(x[k]['offset'])+','+str(x[k]['size'])+',{'+','.join(map(str,x[k]['expected']))+'}}' for k in ('front','back')]
        text.append('{'+f'{x["species"]},{x["dex"]},'+ '{'+','.join(map(str,x['header']))+'},{'+','.join(map(str,x['name']))+'},'+','.join(pics)+','+','.join(str(x[k]) for k in ('dex_offset','dex_size','evo_offset','evo_size'))+',{'+','.join(map(str,x['cry']))+'},'+str(x['icon'])+'},')
    text+=['}};',f'inline constexpr std::array<uint8_t,{len(blob)}> sprite_bytes{{'+','.join(map(str,blob))+'};',
           'struct Move {uint8_t id;std::array<uint8_t,6> data;uint16_t name_offset;uint8_t animation;std::array<uint8_t,20> name;};','inline constexpr std::array<Move,61> moves{{']
    for m in native_moves:text.append('{'+str(m['id'])+',{'+','.join(map(str,m['data']))+'},'+str(m['name_offset'])+','+str(m['animation'])+',{'+','.join(map(str,m['name']))+'}},')
    text+=['}};',f'inline constexpr std::array<uint8_t,{len(name_stream)}> move_names{{'+','.join(map(str,name_stream))+'};',f'inline constexpr std::array<uint8_t,{len(type_effects)}> type_effects{{'+','.join(map(str,type_effects))+'};',f'inline constexpr std::array<uint8_t,{len(dex_bytes)}> dex_bytes{{'+','.join(map(str,dex_bytes))+'};',f'inline constexpr std::array<uint8_t,{len(evo_bytes)}> evolution_bytes{{'+','.join(map(str,evo_bytes))+'};','inline constexpr std::array<std::array<uint8_t,16>,2> type_names{{'+','.join('{'+','.join(map(str,x))+'}' for x in type_names)+'}};',
           'struct Habitat {uint16_t address;bool water;std::array<uint8_t,2> species;};',f'inline constexpr std::array<Habitat,{len(habitats)}> habitats{{{{'+','.join('{'+str(h['address'])+','+str(int(h['water']))+',{'+','.join(map(str,h['species']))+'}}' for h in habitats)+'}};',
           'inline constexpr std::array<std::array<uint8_t,2>,249> habitat_maps{{'+','.join('{'+','.join(map(str,row))+'}' for row in habitat_maps)+'}};','}'];header=('\n'.join(text)+'\n').encode()
    a.out.mkdir(parents=True,exist_ok=False);(a.out/'gen1_extended_data.hpp').write_bytes(header)
    manifest=dict(format=7,scope='Native immutable species/sprite/move/type/Dex/evolution/cry/icon and optional encounter data;full Extended feature not qualified',records=100,moves=61,type_effects=(len(type_effects)-1)//3,effect_profile='Actual owned donor:existing Gen1 effect aliases and generic Pound animation;not full Gen2 battle semantics',sprite_bytes=len(blob),dex_bytes=len(dex_bytes),evolution_bytes=len(evo_bytes),type_names=type_names,habitats=habitats,encounter_profile='Native authored optional overlay:original RNG roll parity keeps original species or rare8/9 added species;unchanged levels/rates/RNG draws. Source evolution/baseEXP order with legendary group last;not original donor encounter content.',asset_identity=asset,content_identity=content,header_sha256=sha(header),source_sha256={str(q):sha(q.read_bytes()) for q in [a.donor_rom,a.donor_symbols,a.stock_rom,a.stock_blue_rom,a.stock_symbols,a.move_constants,a.effect_constants,a.donor_pics_source]},picture_sources=picture_sources)
    (a.out/'gen1_extended_data.json').write_text(json.dumps(manifest,indent=2)+'\n',encoding='utf-8')
    print(json.dumps(dict(records=100,asset=asset,header_sha256=manifest['header_sha256'],stock_picture_template=stock['BaseStats'])))

if __name__=='__main__':main()
