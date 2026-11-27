"""Freeze MMW5DX presentation tables without creating or executing a patched ROM.

Only explicitly identified palette/layout/attribute records are read through a
lazy BPS data view. The owned original is opened read-only. Generated output is
private cartridge-derived build input and must stay outside source control.
"""
import argparse
import bisect
import hashlib
import json
import re
from pathlib import Path
import struct
import zipfile
import zlib

ORIGINAL = '7a108770a7c1ad592b52d0c46d7ead422d0a20961abaafca3d40086e3f2f588f'
ARCHIVE = '0929ea574a016b1485ec2f75fa656008f098b4b6eb78e69438c2212fe61da8f8'
SOURCE = 'https://www.marcrobledo.com/game-boy/mega-man-world-5-dx/'


class PresentationSlices:
    def __init__(self, original, patch):
        self.original, self.patch = original, patch
        if patch[:4] != b'BPS1' or zlib.crc32(patch[:-4]) != struct.unpack('<I', patch[-4:])[0]:
            raise ValueError('Invalid BPS patch checksum')
        if zlib.crc32(original) != struct.unpack('<I', patch[-12:-8])[0]:
            raise ValueError('Wrong original cartridge for this presentation patch')
        self.position = 4
        if self.number() != len(original) or self.number() != 1048576:
            raise ValueError('Unexpected source/target size')
        metadata_size = self.number()
        self.position += metadata_size
        self.runs = []
        at = source = target = 0
        while self.position < len(patch)-12:
            value = self.number()
            kind, count = value & 3, (value >> 2)+1
            if kind == 0:
                origin = at
            elif kind == 1:
                origin = self.position
                self.position += count
            elif kind == 2:
                offset = self.number()
                source += -(offset >> 1) if offset & 1 else offset >> 1
                origin, source = source, source+count
            else:
                offset = self.number()
                target += -(offset >> 1) if offset & 1 else offset >> 1
                origin, target = target, target+count
                if origin >= at or origin < 0:
                    raise ValueError('Invalid target-copy source')
            if count > 1048576-at or self.position > len(patch)-12:
                raise ValueError('BPS record exceeds patch bounds')
            if kind in (0, 2) and (origin < 0 or origin+count > len(original)):
                raise ValueError('BPS record exceeds source bounds')
            self.runs.append((at, count, kind, origin))
            at += count
        if at != 1048576 or self.position != len(patch)-12:
            raise ValueError('Incomplete BPS record stream')
        self.starts = [r[0] for r in self.runs]

    def number(self):
        result, shift = 0, 1
        for _ in range(10):
            value = self.patch[self.position]
            self.position += 1
            result += (value & 127)*shift
            if value & 128:
                return result
            shift <<= 7
            result += shift
        raise ValueError('Invalid BPS integer')

    def byte(self, position):
        for _ in range(512):
            at, count, kind, origin = self.runs[bisect.bisect_right(self.starts, position)-1]
            offset = position-at
            if not 0 <= offset < count:
                raise ValueError('Presentation slice outside target records')
            if kind in (0, 2):
                return self.original[origin+offset]
            if kind == 1:
                return self.patch[origin+offset]
            position = origin + offset % (at-origin)
        raise ValueError('Excessively nested BPS target copy')

    def data(self, bank, address, count):
        if not 0x4000 <= address < address+count <= 0x8000:
            raise ValueError('Presentation table crosses its bank')
        return bytes(self.byte(bank*0x4000+(address-0x4000)+n) for n in range(count))

    def word(self, bank, address):
        return int.from_bytes(self.data(bank, address, 2), 'little')


def freeze(original, archive, output):
    owned, packed = original.read_bytes(), archive.read_bytes()
    if hashlib.sha256(owned).hexdigest() != ORIGINAL or hashlib.sha256(packed).hexdigest() != ARCHIVE:
        raise ValueError('Expected the original USA cartridge and official v1.6.2 archive')
    with zipfile.ZipFile(archive) as z:
        patch = z.read('mmw5dx_v1_6_2.bps')
    slices = PresentationSlices(owned, patch)
    arrays = {}
    arrays['fade_banks'] = list(slices.data(0x20, 0x7b00, 256))
    arrays['bg_attributes'] = [v for scene in range(60) for v in slices.data(0x20, (scene+0x40)*256, 256)]
    arrays['scene_layouts'] = [slices.word(0x22, slices.word(0x22, 0x4000+scene*2)+slot*2) for scene in range(42) for slot in range(16)]
    # Ending scripts use explicit palette packets rather than a level header.
    # Freeze their declarative pointer copies; no donor instructions execute.
    def layout(scene):
        return arrays['scene_layouts'][scene*16:scene*16+16]
    def words(bank,address,count):
        return [slices.word(bank,address+n*2) for n in range(count)]
    ship = layout(28)
    ship[:11] = words(0x3c,0x412c,11)
    ship[9] = (ship[9]&0xff00)|8
    rocket = layout(29)
    rocket[8:14] = words(0x1b,0x74ff,6)
    credits = layout(30)
    gradient = words(0x1b,0x750b,16)
    daylight = gradient.copy()
    daylight[2:6] = words(0x1b,0x752f,4)
    thanks = layout(31)
    thanks[:8] = words(0x1b,0x755b,8)
    for values in (ship,rocket,credits,gradient,daylight,thanks,words(0x1b,0x7537,16),words(0x1b,0x7387,9)+[0]*7,layout(33)):
        arrays['scene_layouts'].extend(values)
    # Original19:457E enters the selector; authored7EF0 uses the item
    # palette header plus explicit stage BG/portrait palette updates.
    arrays['selector_stage_palettes']=words(0x19,0x7f94,22)
    arrays['selector_resources']=list(slices.data(0x19,0x7ff0,10))
    arrays['selector_face_lows']=list(slices.data(0x19,0x7ff7,9))
    rows=[]
    for count in (4,3,3,1,4,3):rows.extend(slices.data(0x3c,0x40ca,count*12))
    arrays['selector_window_attributes']=rows
    arrays['absorption_flash_palettes']=words(0x1b,0x7399,9)
    arrays['absorption_restore_palette']=words(0x1b,0x7397,1)
    arrays['ending_window_attributes'] = list(slices.data(0x3c,0x4142,18*6))
    # Every dynamic palette record is copied as semantics (palette slot and
    # RGB555 data pointer), excluding all guest instruction bytes.
    arrays['actor_layouts'] = []
    for resource in range(145):
        address = slices.word(0x22, (0x4f00 if resource < 128 else 0x5000)+(resource & 127)*2)
        palettes = [0]*16
        for _ in range(32):
            destination = slices.word(0x22, address)
            if not destination & 255:
                break
            if destination < 0xdfc0 or destination > 0xdfde or destination & 1:
                raise ValueError(f'Unidentified dynamic palette destination {resource:02x}:{destination:04x}')
            palettes[(destination-0xdfc0)//2] = slices.word(0x22, address+2)
            address += 4
        else:
            raise ValueError('Unterminated dynamic palette data')
        arrays['actor_layouts'].extend(palettes)
    arrays['body_counts'], arrays['body_pieces'] = [], []
    arrays['hero_tiles'] = list(slices.data(4, 0x4075, 0x6a0))
    arrays['menu_glyphs'] = list(slices.data(4, 0x51e7, 0x1f0))
    arrays['menu_icons'] = list(slices.data(4, 0x7bfa, 14*64))
    arrays['hud_tiles'] = list(slices.data(4, 0x511b, 0xc0))
    # Original57EC selects one of these twelve counter glyphs and copies it
    # into96E0/96F0. Keep the native counter selection and bind its exact art.
    arrays['hud_digit_original'] = list(owned[0x4000+0x170a:0x4000+0x17ca])
    arrays['hud_digits'] = list(slices.data(1,0x570a,0xc0))
    digit_bindings = {}
    for offset in range(0,0xc0,16):
        before = bytes(arrays['hud_digit_original'][offset:offset+16])
        after = bytes(arrays['hud_digits'][offset:offset+16])
        if before in digit_bindings and digit_bindings[before] != after:
            raise ValueError('Ambiguous authored HUD counter glyph')
        digit_bindings[before] = after
    # DX's boot service installs these immutable tiles in CGB VRAM bank1.
    # The NES menu cells select that bank; the separate9700 preview art is
    # not the authored art for the fourteen equipment cells.
    arrays['shared_tiles'] = [0]*0x1800
    for source_address, destination, count in ((0x4200,0x8000,0x100),
            (0x4820,0x86a0,0x50),(0x4df0,0x87e0,0x10),
            (0x4960,0x87f0,0x10),(0x4870,0x8800,0x580),
            (0x4300,0x9010,0x2f0),(0x45f0,0x95d0,0x230)):
        start = destination-0x8000
        arrays['shared_tiles'][start:start+count] = slices.data(0x2f,source_address,count)
    arrays['menu_item_tiles'] = list(slices.data(0x30,0x4233,14*4))
    arrays['menu_attributes'] = list(slices.data(0x30, 0x4200, 360))
    arrays['menu_normal_attributes'] = list(slices.data(0x30, 0x4287, 14))
    arrays['menu_selected_attributes'] = list(slices.data(0x30, 0x4295, 14))
    arrays['menu_cells'] = [slices.word(0x30, 0x426b+n*2) for n in range(14)]
    for frame in range(47):
        address = slices.word(1, 0x71f4+frame*2)
        pieces = []
        for _ in range(10):
            if slices.data(1, address, 1)[0] == 0x80:
                break
            pieces.extend(slices.data(1, address, 4))
            address += 4
        arrays['body_counts'].append(len(pieces)//4)
        arrays['body_pieces'].extend(pieces+[0]*(40-len(pieces)))
    # Stage graphics are declarative original-game packets: tile count,
    # destination tile relative to9000, and a source address. Read the DX
    # packet's own pointers; authored art can move without moving guest code.
    def source_data(bank, address, count):
        at = bank*0x4000+(address & 0x3fff)
        return owned[at:at+count]

    # Original fixed-scene copy services identify the exact source, destination
    # and length. Bind their original textures to authored textures without
    # altering VRAM or copying any donor guest instructions.
    fixed_sources = {
        32: ((0x10,0x4fb0,0x8010,0x20),(0x10,0x4ab0,0x9000,0x2a0),
             (0x10,0x4d50,0x8c00,0x260)),
        35: ((0x10,0x65a7,0x9000,0x630),(0x10,0x5ffe,0x8800,0x400),
             (0x10,0x6597,0x8000,0x10)),
        33: ((0x14,0x44ef,0x8000,0x60),),
        37: ((0x14,0x454f,0x9000,0x270),),
        38: ((0x14,0x48c9,0x9000,0x4a0),),
        39: ((0x14,0x53fd,0x9000,0x250),),
        40: ((0x14,0x4e87,0x9000,0x480),),
        41: ((0x14,0x57d3,0x9000,0x520),),
    }
    def resource_bitmap(resource,authored):
        reader = slices.data if authored else source_data
        entry = reader(1,0x421f+resource*3,3)
        bank,descriptor = entry[0],int.from_bytes(entry[1:],'little')
        header = int.from_bytes(reader(bank,descriptor,2),'little')
        packet = reader(bank,header,3)
        return bank,int.from_bytes(packet[1:],'little'),packet[0]*16
    ship_sources = [(0x1c,0x4d0e,0x9040,0x6a0)]
    for resource,destination in ((0x8e,0x8000),(0x54,0x86a0),(0x8a,0x8b00)):
        bank,address,count = resource_bitmap(resource,False)
        authored_bank,authored_address,authored_count = resource_bitmap(resource,True)
        ship_sources.append((bank,address,destination,min(count,authored_count),authored_bank,authored_address))
    fixed_sources[42] = tuple(ship_sources)
    fixed_sources[43] = ((0x1b,0x46a9,0x9000,0x160),(0x1b,0x4ae1,0x8000,0x290,0x1b,0x7d40))
    fixed_sources[50]=((0x19,0x4c24,0x9000,0x800),(0x19,0x5424,0x8800,0x1d0),(0x19,0x7d70,0x8000,0x30))
    fixed_sources[49]=((0x1b,0x66e7,0x9000,0x620),(0x1b,0x6e71,0x8000,0x200))
    for scene in (44,45,46,48):
        fixed_sources[scene]=((0x1b,0x54f1,0x8700,0x70),)
    arrays['fixed_tiles'] = [0]*(51*384)
    arrays['fixed_original'], arrays['fixed_authored'] = [], []
    for scene, sources in fixed_sources.items():
        for item in sources:
            bank,source,destination,count = item[:4]
            authored_bank,authored_source = item[4:] if len(item)>4 else (bank,source)
            before,after = source_data(bank,source,count),slices.data(authored_bank,authored_source,count)
            for offset in range(0,count,16):
                arrays['fixed_original'].extend(before[offset:offset+16])
                arrays['fixed_authored'].extend(after[offset:offset+16])
                arrays['fixed_tiles'][scene*384+(destination-0x8000+offset)//16] = len(arrays['fixed_original'])//16
    arrays['fixed_map_original'], arrays['fixed_map_authored'] = [256]*(51*360), [256]*(51*360)
    for scene,bank,address,prefix in ((35,0x10,0x63fe,2),(47,0x1b,0x6174,4),(49,0x1b,0x6d07,2)):
        for reader,name in ((source_data,'fixed_map_original'),(slices.data,'fixed_map_authored')):
            width,height = reader(bank,address+prefix-2,2)
            if (width,height)!=(20,18):
                raise ValueError('Fixed-scene map geometry changed')
            arrays[name][scene*360:scene*360+360] = reader(bank,address+prefix,360)
    pointer=slices.word(0x1b,0x556e)
    objects=bytearray()
    while slices.data(0x1b,pointer,1)[0]!=0x80:
        objects.extend(slices.data(0x1b,pointer,4));pointer+=4
        if len(objects)>40:raise ValueError('Credits custom pose exceeds reserved native slots')
    if len(objects)!=32:raise ValueError('Authored credits pose changed')
    arrays['credits_objects']=list(objects)
    # Native55AB selects one of ten static cast cards;55EA copies that
    # source header's cumulative list intoCBC0. The authored service tables
    # select corresponding lists and7492 installs three OBJ palettes.
    arrays['credits_cast_original_counts'],arrays['credits_cast_counts']=[],[]
    arrays['credits_cast_original'],arrays['credits_cast_objects'],arrays['credits_cast_palettes']=[],[],[]
    for index in range(1,11):
        for authored,reader,name,count_name in ((False,source_data,'credits_cast_original','credits_cast_original_counts'),(True,slices.data,'credits_cast_objects','credits_cast_counts')):
            bank,service,resource=reader(0x1b,0x56a4+(index-1)*3,3)
            if bank==3 and service==0x14:
                address=int.from_bytes(reader(3,0x7a99+(index-1)*2,2),'little')
            elif bank==9 and service==0x15:
                address=int.from_bytes(reader(9,0x7edc+(index-6)*2,2),'little')
            elif bank==0x1e and service==0x16 and index==10:
                address=int.from_bytes(reader(0x1e,0x4024,2),'little')
            else:raise ValueError('Credits cast service identity changed')
            objects=bytearray();address+=4
            while reader(bank,address,1)[0]!=0x80:
                objects.extend(reader(bank,address,4));address+=4
                if len(objects)>120:raise ValueError('Credits cast exceeds thirty native reserved slots')
            arrays[count_name].append(len(objects)//4)
            arrays[name].extend(objects+bytes(120-len(objects)))
        pointer=slices.word(0x1b,0x756b+index*2)
        arrays['credits_cast_palettes'].extend(words(0x1b,pointer,3))
    arrays['title_obj_tiles'] = list(slices.data(0x10,0x7f60,0xa0))
    arrays['title_objects'] = list(slices.data(0x10,0x7a32,44))

    def graphics(stage, authored):
        reader = slices.data if authored else source_data
        if authored:
            entry = bytes(slices.byte(0x3299+(stage-1)*6+n) for n in range(6))
        else:
            entry = owned[0x3299+(stage-1)*6:0x3299+stage*6]
        result = {}
        if entry[0] == 255:
            return result
        bank, table = entry[0], int.from_bytes(entry[1:3], 'little')
        address = int.from_bytes(reader(bank, table, 2), 'little')
        for _ in range(32):
            packet = reader(bank, address, 4)
            if not packet[0]:
                break
            count, tile, source_address = packet[0], packet[1], int.from_bytes(packet[2:4], 'little')
            pixels = reader(bank, source_address, count*16)
            for n in range(count):
                if tile+n < 128:
                    result[tile+n] = pixels[n*16:(n+1)*16]
            address += 4
        else:
            raise ValueError('Unterminated stage graphics packets')
        return result

    arrays['stage_tiles'] = [0]*(32*128)
    arrays['stage_authored_tiles'] = [0]*(32*128)
    arrays['tile_original'], arrays['tile_authored'] = [], []
    unique = {}
    for stage in range(1, 32):
        original_tiles, authored_tiles = graphics(stage, False), graphics(stage, True)
        for tile, pixels in original_tiles.items():
            if tile not in authored_tiles:
                continue
            pair = pixels, authored_tiles[tile]
            if pair not in unique:
                unique[pair] = len(unique)+1
                arrays['tile_original'].extend(pair[0])
                arrays['tile_authored'].extend(pair[1])
            arrays['stage_tiles'][stage*128+tile] = unique[pair]
        for tile, pixels in authored_tiles.items():
            pair = original_tiles.get(tile, bytes(16)), pixels
            if pair not in unique:
                unique[pair] = len(unique)+1
                arrays['tile_original'].extend(pair[0])
                arrays['tile_authored'].extend(pair[1])
            arrays['stage_authored_tiles'][stage*128+tile] = unique[pair]
    arrays['metatile_keys'], arrays['metatile_offsets'], arrays['metatile_counts'], arrays['metatile_data'] = [], [], [], []
    checkpoint_counts = {**{n: 3 for n in range(1, 12)}, 13: 15, 14: 3, 15: 10, 16: 5, 17: 3, 18: 3, 22: 3, 23: 3, 25: 3, 27: 3, 30: 3}
    metatiles = {}

    def checkpoint_record(stage, checkpoint, authored):
        if stage == 4 and checkpoint:
            stage = 17
        reader = slices.data if authored else source_data
        entry = bytes(slices.byte(0x3299+(stage-1)*6+n) for n in range(6)) if authored else owned[0x3299+(stage-1)*6:0x3299+stage*6]
        bank, descriptor = entry[3], int.from_bytes(entry[4:6], 'little')
        header = int.from_bytes(reader(bank, descriptor, 2), 'little')
        table = int.from_bytes(reader(bank, header, 2), 'little')
        record = int.from_bytes(reader(bank, table+checkpoint*2, 2), 'little')
        return reader, bank, record

    def metatile(stage, checkpoint, authored):
        reader, bank, record = checkpoint_record(stage, checkpoint, authored)
        address = int.from_bytes(reader(bank, record+1, 2), 'little')
        count = reader(bank, address, 1)[0]
        return address+1, count, reader(bank, address+1, count*4)

    for stage, count in checkpoint_counts.items():
        for checkpoint in range(count):
            original_address, _, _ = metatile(stage, checkpoint, False)
            _, authored_count, pixels = metatile(stage, checkpoint, True)
            key = stage*65536+original_address
            if key in metatiles and metatiles[key] != pixels:
                raise ValueError('Ambiguous authored metatile data for an original stage table')
            metatiles[key] = pixels
    for key, pixels in sorted(metatiles.items()):
        arrays['metatile_keys'].append(key)
        arrays['metatile_offsets'].append(len(arrays['metatile_data']))
        arrays['metatile_counts'].append(len(pixels)//4)
        arrays['metatile_data'].extend(pixels)
    # Native checkpoint +12 selects the first room descriptor (3466), whose
    # +3 word owns the width/height and row cells (3814..38D8). Two transitions
    # follow the seven-byte descriptor; each is absent(0) or a five-byte
    # trigger plus target word (3921..39B7). Pair this finite original/authored
    # graph by transition ordinal, keeping native terrain and dynamic writes.
    room_maps = {}
    for stage, count in checkpoint_counts.items():
        for checkpoint in range(count):
            original_reader, bank, original_record = checkpoint_record(stage, checkpoint, False)
            authored_reader, authored_bank, authored_record = checkpoint_record(stage, checkpoint, True)
            original_table, _, _ = metatile(stage, checkpoint, False)
            pending = [(int.from_bytes(original_reader(bank, original_record+12, 2), 'little'), int.from_bytes(authored_reader(authored_bank, authored_record+12, 2), 'little'))]
            seen = set()
            while pending:
                original_room, authored_room = pending.pop()
                if (original_room, authored_room) in seen:
                    continue
                seen.add((original_room, authored_room))
                if len(seen)>128:
                    raise ValueError('Unbounded source room transition graph')
                def room(reader, source_bank, address):
                    if not 0x4000<=address<0x7ff8:
                        raise ValueError('Invalid native room descriptor')
                    header = int.from_bytes(reader(source_bank, address+3, 2), 'little')
                    columns, rows = reader(source_bank, header, 2)
                    if not columns or not 1<=rows<=9 or header+2+columns*rows>0x8000:
                        raise ValueError('Invalid native room dimensions')
                    targets=[]
                    cursor=address+7
                    for _ in range(2):
                        if reader(source_bank,cursor,1)[0]:
                            targets.append(int.from_bytes(reader(source_bank,cursor+3,2),'little'));cursor+=5
                        else:
                            targets.append(0);cursor+=1
                    return header, columns, rows, reader(source_bank,header+2,columns*rows), targets
                old_header, columns, rows, original_cells, old_targets = room(original_reader,bank,original_room)
                _, authored_columns, authored_rows, authored_cells, new_targets = room(authored_reader,authored_bank,authored_room)
                if (columns,rows)!=(authored_columns,authored_rows):
                    raise ValueError('Authored visual map changes native room dimensions')
                key=(stage<<32)|(original_table<<16)|old_header
                pair=(columns,rows,original_cells,authored_cells)
                if key in room_maps and room_maps[key]!=pair:
                    raise ValueError('Ambiguous authored visual map for native room')
                room_maps[key]=pair
                for old_target,new_target in zip(old_targets,new_targets):
                    if bool(old_target)!=bool(new_target):
                        raise ValueError('Authored room changes native transition ownership')
                    if old_target:pending.append((old_target,new_target))
    arrays['room_keys'], arrays['room_offsets'], arrays['room_columns'], arrays['room_rows'], arrays['room_original'], arrays['room_authored'] = [], [], [], [], [], []
    for key,(columns,rows,original_cells,authored_cells) in sorted(room_maps.items()):
        arrays['room_keys'].append(key)
        arrays['room_offsets'].append(len(arrays['room_original']))
        arrays['room_columns'].append(columns)
        arrays['room_rows'].append(rows)
        arrays['room_original'].extend(original_cells)
        arrays['room_authored'].extend(authored_cells)
    # Identified original animation tables from the existing original-game
    # checkpoint fixtures. Their native LD DE / animation-setter call sites
    # pin the corresponding DX table, whose own frame pointers may move.
    list_tables = ((3,0x4e48),(3,0x5795),(3,0x620a),(3,0x6eb9),(3,0x760f),
                   (9,0x445c),(9,0x526a),(9,0x5965),(9,0x6352),(9,0x6c22),
                   (11,0x687b),(13,0x5ac2),(13,0x5fac),(13,0x76f1),(13,0x7cca),
                   (14,0x59a7),(14,0x7477),(18,0x4d98),(29,0x52fe),(30,0x6867),(6,0x626d))
    # Canonical original animation setters identify additional cumulative
    # tables. Admit only bounded pointer tables whose original and authored
    # frames both terminate as native four-byte pieces. This extends the same
    # typed binding used above; arbitrary ROM bytes are never presentation data.
    setters=(bytes.fromhex('c3dd01'),bytes.fromhex('cddd01'),bytes.fromhex('c37a2a'),bytes.fromhex('cd7a2a'))
    def cumulative(reader,bank,table,frames):
        result=[]
        for frame in range(frames):
            address=int.from_bytes(reader(bank,table+frame*2,2),'little')+4
            if not 0x4004<=address<0x8000:return None
            pieces=bytearray()
            for n in range(128):
                if address+len(pieces)>=0x8000:return None
                if reader(bank,address+len(pieces),1)[0]==0x80:break
                if address+len(pieces)+4>0x8000:return None
                piece=reader(bank,address+len(pieces),4)
                if reader is source_data and piece[3]&15:return None
                pieces.extend(piece)
            else:return None
            result.append(bytes(pieces))
        return result
    extra=set()
    for bank in range(1,32):
        native=owned[bank*0x4000:(bank+1)*0x4000]
        for match in re.finditer(b'\x11(..)[\xcd\xc3](?:\xdd\x01|\x7a\x2a)',native,re.DOTALL):
            table=int.from_bytes(match[1],'little');at=match.start();address=0x4000+at
            if not 0x4000<=table<0x7ffe:continue
            first=int.from_bytes(source_data(bank,table,2),'little')
            if not table<first<=table+256 or (first-table)&1:continue
            frames=(first-table)//2
            if cumulative(source_data,bank,table,frames) is None:continue
            if slices.data(bank,address,1)!=b'\x11' or slices.data(bank,address+3,3) not in setters:continue
            target=slices.word(bank,address+1)
            if cumulative(slices.data,bank,target,frames) is None:continue
            targets={slices.word(bank,0x4001+m.start()) for m in re.finditer(re.escape(bytes((0x11,table&255,table>>8))),native) if m.start()+6<=0x4000 and native[m.start()+3:m.start()+6] in setters}
            if targets=={target}:extra.add((bank,table))
    list_tables=tuple(sorted(set(list_tables)|extra))
    lists = {}
    for bank, table in list_tables:
        first = int.from_bytes(source_data(bank, table, 2), 'little')
        if not table < first <= table+256 or (first-table)&1:
            raise ValueError('Unidentified original animation-table boundary')
        source_bank = owned[bank*0x4000:(bank+1)*0x4000]
        setter = bytes((0x11,table&255,table>>8))
        targets = set()
        at = 0
        while (at := source_bank.find(setter,at)) >= 0:
            if source_bank[at+3:at+6] in (bytes.fromhex('c3dd01'),bytes.fromhex('cddd01'),bytes.fromhex('c37a2a'),bytes.fromhex('cd7a2a')):
                targets.add(slices.word(bank,0x4001+at))
            at += 1
        if (bank,table)==(6,0x626d):
            # Actual Salt Water initializer614D selects animation624A via
            # the native weapon wrapper4DFF. Its first word is the frame table.
            if source_data(6,0x614d,6)!=bytes.fromhex('114a62c3ff4d') or int.from_bytes(source_data(6,0x624a,2),'little')!=table:
                raise ValueError('Salt Water animation producer changed')
            authored_table=slices.word(6,slices.word(6,0x614e))
            # The raw native weapon uploader bypasses resource1FE8. Pin its
            # original/authored packet to the resource used for presentation.
            for authored,reader in ((False,source_data),(True,slices.data)):
                packet=int.from_bytes(reader(4,0x5409+5*2,2),'little')
                count,address=reader(4,packet,1)[0],int.from_bytes(reader(4,packet+1,2),'little')
                resource_bank,resource_address,resource_size=resource_bitmap(0x4b,authored)
                if (packet,count,address)!=(0x59c5,0x16,0x59c8) or resource_size!=count*16 or reader(4,address,count*16)!=reader(resource_bank,resource_address,resource_size):
                    raise ValueError('Salt Water raw/resource presentation identity differs')
        else:
            if len(targets) != 1:
                raise ValueError('Missing or ambiguous authored animation-table binding')
            authored_table = targets.pop()
        for frame in range((first-table)//2):
            original_pointer = int.from_bytes(source_data(bank,table+frame*2,2),'little')+4
            authored_pointer = slices.word(bank,authored_table+frame*2)+4
            pieces = bytearray()
            for n in range(128):
                value = slices.data(bank,authored_pointer+n*4,1)[0]
                if value == 0x80:
                    break
                pieces.extend(slices.data(bank,authored_pointer+n*4,4))
            else:
                raise ValueError('Unterminated identified authored sprite list')
            key = bank*65536+original_pointer
            if key in lists and lists[key] != pieces:
                raise ValueError('Ambiguous authored sprite-list binding')
            lists[key] = pieces
    arrays['list_keys'], arrays['list_offsets'], arrays['list_counts'], arrays['list_data'] = [], [], [], []
    for key, pieces in sorted(lists.items()):
        arrays['list_keys'].append(key)
        arrays['list_offsets'].append(len(arrays['list_data']))
        arrays['list_counts'].append(len(pieces)//4)
        arrays['list_data'].extend(pieces)
    # Original service2010 resolves these declarative resource descriptors;
    # service2074 copies tile_count*16 bytes into its assigned OBJ tile base.
    arrays['resource_counts'], arrays['resource_offsets'], arrays['resource_pixels'] = [0]*146, [0]*146, []
    for resource in range(1,146):
        entry = slices.data(1,0x421f+resource*3,3)
        bank, descriptor = entry[0], int.from_bytes(entry[1:],'little')
        header = slices.word(bank,descriptor)
        packet = slices.data(bank,header,3)
        count, address = packet[0], int.from_bytes(packet[1:],'little')
        arrays['resource_counts'][resource] = count
        arrays['resource_offsets'][resource] = len(arrays['resource_pixels'])
        if count:
            arrays['resource_pixels'].extend(slices.data(bank,address,count*16))
    arrays['palette_words'] = [int.from_bytes(slices.data(bank, address, 2), 'little') for bank in range(0x22, 0x2e) for address in range(0x4400, 0x6400, 2)]
    lines = ['#pragma once', '#include <cstdint>', 'namespace gbcllr::mm5_dx {']
    for name, values in arrays.items():
        typ = 'uint8_t' if name in ('room_columns', 'room_rows', 'room_original', 'room_authored', 'selector_resources', 'selector_face_lows', 'selector_window_attributes', 'credits_cast_original_counts', 'credits_cast_counts', 'credits_cast_original', 'credits_cast_objects', 'credits_objects', 'ending_window_attributes', 'fade_banks', 'bg_attributes', 'body_counts', 'body_pieces', 'hero_tiles', 'tile_original', 'tile_authored', 'metatile_data', 'menu_glyphs', 'menu_icons', 'hud_tiles', 'hud_digit_original', 'hud_digits', 'shared_tiles', 'menu_item_tiles', 'fixed_original', 'fixed_authored', 'title_obj_tiles', 'title_objects', 'menu_attributes', 'menu_normal_attributes', 'menu_selected_attributes', 'list_counts', 'list_data', 'resource_counts', 'resource_pixels') else ('uint32_t' if name in ('room_offsets', 'metatile_keys', 'metatile_offsets', 'list_keys', 'list_offsets', 'resource_offsets') else ('uint64_t' if name=='room_keys' else 'uint16_t'))
        lines.append(f'inline constexpr {typ} {name}[{len(values)}] = {{')
        lines.extend(','.join(str(v) for v in values[n:n+32])+',' for n in range(0, len(values), 32))
        lines.append('};')
    lines.append('}')
    header = ('\n'.join(lines)+'\n').encode()
    output.mkdir(parents=True, exist_ok=True)
    (output/'mm5_dx_data.hpp').write_bytes(header)
    manifest = dict(format=1, original_rom_sha256=ORIGINAL, header_sha256=hashlib.sha256(header).hexdigest(), patch_sha256=hashlib.sha256(patch).hexdigest(), archive_sha256=ARCHIVE, version='1.6.2', source=SOURCE, credits=['Kensu William (kensuyjin33): graphics', 'Marc Max (marc_max): reverse engineering and coding', 'kindlypatrick: retained initial stage palettes'], scope='Native palettes, scene tiles/metatiles, hero, NES pause, identified cumulative sprite lists and declarative OBJ resources; no patched cartridge is created or executed.')
    (output/'mm5_dx_data.json').write_text(json.dumps(manifest, indent=2)+'\n')
    print(json.dumps(manifest))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--original', type=Path, required=True)
    parser.add_argument('--patch-archive', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    freeze(args.original, args.patch_archive, args.output)
