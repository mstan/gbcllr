"""Freeze identified Mega Man I/II DX presentation assets from pinned IPS data.

The original cartridges are immutable inputs. This bounded reader exports only
identified palettes, color attributes, level data and graphics transfer assets;
it never creates a patched cartridge and never exports executable guest code.
"""
import argparse
import bisect
import hashlib
import json
from pathlib import Path

ROM_IDS = ('33d16365318411f063edd9100c79458aabb7ea71bb1873e30f38b93814f0ec6e',
           'be3fc7ab2d7eb79a9e2538b090bdecc2694740f3beda933f34cb1e4059b10a11')
PATCH_IDS = ('b1502839b72afa49137a63f4a516c3a70501805d5a1f0385d8b8fa110eed61aa',
             '4d588462e06ef91ea48f564d95fd4ba83a23ec3a36a8dd0ad1ca433a4e76c823')


class Presentation:
    def __init__(self, original, patch, title):
        self.original = original.read_bytes()
        patch = patch.read_bytes()
        if hashlib.sha256(self.original).hexdigest() != ROM_IDS[title-1]:
            raise ValueError('Unidentified original cartridge')
        if hashlib.sha256(patch).hexdigest() != PATCH_IDS[title-1] or patch[:5] != b'PATCH':
            raise ValueError('Unidentified official DX IPS patch')
        self.records = []
        cursor = 5
        while patch[cursor:cursor+3] != b'EOF':
            address = int.from_bytes(patch[cursor:cursor+3], 'big')
            count = int.from_bytes(patch[cursor+3:cursor+5], 'big')
            cursor += 5
            if count:
                value = patch[cursor:cursor+count]
                cursor += count
            else:
                count = int.from_bytes(patch[cursor:cursor+2], 'big')
                value = bytes([patch[cursor+2]])*count
                cursor += 3
            if len(value) != count:
                raise ValueError('Truncated IPS record')
            self.records.append((address, value))
        self.records.sort()
        if any(a+len(v) > b for (a, v), (b, _) in zip(self.records, self.records[1:])):
            raise ValueError('Overlapping IPS records need explicit ordering')
        self.starts = [address for address, _ in self.records]

    @staticmethod
    def physical(bank, address):
        if 0 <= address < 0x4000:
            return address
        if not 0x4000 <= address < 0x8000:
            raise ValueError('Asset outside banked data')
        return bank*0x4000+address-0x4000

    def byte(self, physical):
        index = bisect.bisect_right(self.starts, physical)-1
        if index >= 0:
            start, value = self.records[index]
            if physical < start+len(value):
                return value[physical-start]
        return self.original[physical] if physical < len(self.original) else 0xff

    def data(self, bank, address, count):
        if count < 0 or address+count > 0x8000:
            raise ValueError('Asset crosses bank boundary')
        at = self.physical(bank, address)
        return bytes(self.byte(at+i) for i in range(count))

    def original_data(self, bank, address, count):
        at = self.physical(bank, address)
        if address+count > 0x8000 or at+count > len(self.original):
            raise ValueError('Original asset outside bank')
        return self.original[at:at+count]

    def word(self, bank, address, original=False):
        return int.from_bytes((self.original_data if original else self.data)(bank, address, 2), 'little')


def mm1_levels(view):
    """Decode the original loader's actual checkpoint and connected-room tables."""
    graph = []
    assets = set()
    for stage in range(1, 9):
        at = 0x2f8f+stage*4
        root = int.from_bytes(view.original[at:at+2], 'little')
        bank = view.original[at+2]
        level = view.word(bank, view.word(bank, root, True), True)
        table = view.word(bank, level, True)
        checkpoints = []
        for index in range(8):
            checkpoint = view.word(bank, table+index*2, True)
            if not 0x4000 <= checkpoint < 0x8000:
                break
            checkpoints.append(checkpoint)
        rooms = {}
        todo = [view.word(bank, checkpoint+12, True) for checkpoint in checkpoints]
        for checkpoint in checkpoints:
            metatiles = view.word(bank, checkpoint+1, True)
            # First byte is the original block count. Records are TL,BL,TR,BR.
            blocks = view.original_data(bank, metatiles, 1)[0]
            assets.add((bank, metatiles+1, blocks*4))
        while todo:
            room = todo.pop()
            if room in rooms:
                continue
            layout = view.word(bank, room+3, True)
            columns, rows = view.original_data(bank, layout, 2)
            if not 1 <= columns <= 255 or not 1 <= rows <= 32:
                raise ValueError('Invalid original room dimensions')
            assets.add((bank, layout+2, columns*rows))
            exits = []
            cursor = room+7
            for _ in range(2):
                shape = view.original_data(bank, cursor, 1)[0]
                cursor += 1
                if shape:
                    x, flags = view.original_data(bank, cursor, 2)
                    destination = view.word(bank, cursor+2, True)
                    cursor += 4
                    exits.append(dict(shape=shape, x=x, flags=flags, destination=destination))
                    todo.append(destination)
            rooms[room] = dict(address=room, layout=layout, columns=columns, rows=rows, exits=exits)
        graph.append(dict(stage=stage, bank=bank, checkpoints=checkpoints, rooms=list(rooms.values())))
    return graph, assets


def freeze(args):
    views = [Presentation(getattr(args, f'original{n}'), getattr(args, f'patch{n}'), n) for n in (1, 2)]
    arrays = {}
    first, second = views
    graph, ranges = mm1_levels(first)
    # Native03C3 loads the finite menu font from fixed-bank041E to VRAM8900.
    ranges.add((0, 0x041e, 0x01e0))
    # Original12:6BAC loads this finite password-input graphics payload.
    ranges.add((12, 0x6e11, 0x0420))
    # Original title graphics loader1A7D: two finite transfer descriptors.
    # Only the identified graphics payloads are exported, never loader code.
    for binding in (0x343, 0x345):
        header = first.word(6, first.word(0, binding, True), True)
        count = first.word(6, header, True)
        source = first.word(6, header+2, True)
        if not count or source+count > 0x8000:
            raise ValueError('Invalid original title graphics transfer')
        ranges.add((6, source, count))
    # Stage-select1AFC/1B6B: original finite direct graphics transfers.
    for binding, bank in ((0x351, 6), (0x305, 4)):
        header = first.word(bank, first.word(0, binding, True), True)
        count = first.word(bank, header, True)
        source = first.word(bank, header+2, True)
        ranges.add((bank, source, count))
    # Original weapon-get20F2 requests these two finite BG art payloads.
    for binding in (0x357, 0x359):
        header = first.word(6, first.word(0, binding, True), True)
        count, source = first.word(6, header, True), first.word(6, header+2, True)
        ranges.add((6, source, count))
    map_header = first.word(6, first.word(0, 0x35b, True), True)
    map_count, map_source = first.word(6, map_header, True), first.word(6, map_header+2, True)
    if first.original_data(6, map_source, map_count) != first.data(6, map_source, map_count):
        raise ValueError('Weapon-get map needs an explicit authored remap')
    # Original1B88 uses four finite indexed tile lists. Each1E9C copy moves
    # exactly16 identified art bytes; FF is an intentional blank tile.
    for binding, graphics in ((0x397, 0x379), (0x399, 0x36d), (0x39b, 0x371), (0x39d, 0x375)):
        header = first.word(7, first.word(0, graphics, True), True)
        if first.data(7, header, 3) != first.original_data(7, header, 3):
            raise ValueError('Stage-select graphics descriptor changed')
        source = first.word(7, header+1, True)
        pointer = first.word(7, first.word(7, first.word(0, binding, True), True), True)
        count = first.original_data(7, pointer, 1)[0]
        for tile in first.original_data(7, pointer+1, count):
            if tile != 255:
                ranges.add((7, source+tile*16, 16))
    # Stage graphics scripts: original23A8 / table2489, each record is tiles,
    # destination tile, source pointer. Only their tile payload is exported.
    for stage in range(1, 9):
        record = int.from_bytes(first.original[0x2489+stage*2:0x248b+stage*2], 'little')
        script = first.word(12, first.word(12, record, True), True)
        for _ in range(32):
            count = first.original_data(12, script, 1)[0]
            if not count:
                break
            ranges.add((12, first.word(12, script+2, True), count*16))
            script += 4
        else:
            raise ValueError('Unterminated original stage graphics script')
    # Player, pickup, weapon and enemy art records referenced by native2412.
    ranges.update(((4, 0x405c, 125*16),))
    for actor in range(1, 62):
        bank = 4 if actor < 30 else 7
        entry = int.from_bytes(first.original[0x2499+actor*2:0x249b+actor*2], 'little')
        if not 0x4000 <= entry < 0x8000:
            continue
        indirect = first.word(bank, entry, True)
        if not 0x4000 <= indirect <= 0x7ffe:
            continue
        header = first.word(bank, indirect, True)
        if not 0x4000 <= header <= 0x7ffd:
            continue
        count = first.original_data(bank, header, 1)[0]
        source = first.word(bank, header+1, True)
        if count and 0x4000 <= source and source+count*16 <= 0x8000:
            ranges.add((bank, source, count*16))
    # Original ending4B06's four finite art requests, including its later
    # queued corridor transfer. They are identified data payloads, not code.
    ranges.update((14, address, count) for address, count in
                  ((0x4806, 0x300), (0x5341, 0x660), (0x59a1, 0x760), (0x6c41, 0x400), (0x77ca, 0x3f0)))
    ranges.add((6, 0x690f, 0x7c0))
    # Native fortress2245 and bank0B direct OBJ transfers.
    ranges.update(((6, 0x5de7, 0x800), (6, 0x65e7, 0x1c0),
                   (11, 0x47f6, 0x110), (11, 0x4b61, 0x100), (11, 0x4ec9, 0x430), (11, 0x5534, 0x30)))
    if first.data(11, 0x4d61, 360) != first.original_data(11, 0x4d61, 360):
        raise ValueError('Unidentified first fortress map replacement')
    if first.data(6, 0x67a7, 360) != first.original_data(6, 0x67a7, 360):
        raise ValueError('Unidentified second fortress map replacement')
    # Paired source/presentation payloads retain exact source identity. Runtime
    # substitutes only at the original native graphics-copy services.
    for title, view, graphics in ((1, first, sorted(ranges)),
                                  (2, second, [(bank, 0x4000, 0x4000) for bank in range(8, 13)])):
        records, original, colored = [], bytearray(), bytearray()
        for bank, address, count in graphics:
            # MM1 room maps share this address space; only unchanged-size
            # presentation replacements are read by the host map renderer.
            records.extend((bank, address, count, len(original)))
            original.extend(view.original_data(bank, address, count))
            colored.extend(view.data(bank, address, count))
        arrays[f'mm{title}_records'] = ('uint32_t', records)
        arrays[f'mm{title}_original'] = ('uint8_t', original)
        arrays[f'mm{title}_colored'] = ('uint8_t', colored)
    arrays['mm1_background_palettes'] = ('uint8_t', first.data(15, 0x52c0, 13*64))
    arrays['mm1_object_palettes'] = ('uint8_t', first.data(15, 0x71c0, 13*64))
    arrays['mm1_stage_attributes'] = ('uint8_t', first.data(15, 0x5f00, 13*256))
    arrays['mm1_object_attributes'] = ('uint8_t', first.data(15, 0x7700, 512))
    arrays['mm1_face_index'] = ('uint8_t', first.data(15, 0x5000, 256))
    arrays['mm1_face_records'] = ('uint8_t', first.data(15, 0x5100, 256))
    arrays['mm1_weapon_palettes'] = ('uint8_t', first.data(15, 0x5d00, 11*8))
    arrays['mm1_extra_art'] = ('uint8_t', first.data(15, 0x4300, 0x1800))
    profiles = [second.byte(17*0x4000+0x100+second.original[0x3bff+stage*2]) for stage in range(10)]
    if max(profiles) > 10:
        raise ValueError('Unknown Mega Man II palette profile')
    arrays['mm2_profiles'] = ('uint8_t', profiles)
    arrays['mm2_background_palettes'] = ('uint8_t', second.data(17, 0x4800, 11*64))
    arrays['mm2_object_palettes'] = ('uint8_t', second.data(17, 0x5000, 11*64))
    arrays['mm2_stage_attributes'] = ('uint8_t', second.data(17, 0x7500, 11*256))
    arrays['mm2_object_attributes'] = ('uint8_t', second.data(17, 0x6000, 128)+bytes(128)+second.data(17, 0x4300, 256))
    arrays['mm2_face_index'] = ('uint8_t', second.data(17, 0x6a00, 256))
    arrays['mm2_face_records'] = ('uint8_t', second.data(17, 0x6b00, 256))
    # The original native art-set requests select these parallel color tables.
    # Addresses are the eight explicitly identified normal enemy resources in
    # bank18's58xx/59xx loaders; no donor instructions are exported/interpreted.
    resources = [(8, 0x4000, 0x7880, 0x7d40), (8, 0x5000, 0x7980, 0x7a50), (8, 0x4800, 0x7180, 0x7100),
                 (8, 0x5800, 0x7a80, 0x7c00), (8, 0x6800, 0x7e80, 0x7f10),
                 (8, 0x7800, 0x7d80, 0x7d10), (9, 0x4800, 0x7c80, 0x7e50),
                 (9, 0x5800, 0x7f80, 0x7f50), (9, 0x6800, 0x7b80, 0x7c50)]
    arrays['mm2_resource_keys'] = ('uint16_t', [bank*256+(address>>8) for bank, address, _, _ in resources])
    arrays['mm2_resource_attributes'] = ('uint8_t', b''.join(second.data(18, attributes, 128) for _, _, attributes, _ in resources))
    arrays['mm2_resource_palettes'] = ('uint8_t', b''.join(second.data(18, palette, 48) for _, _, _, palette in resources))
    # Eight original boss resources, selected through native ActS_GFXSetTbl.
    # Author18's explicit services fill body attributes, upload finite palette
    # tails, clear upper face indices and optionally replace64 face-record bytes.
    bosses = [
        (8, 0x6000, 5, 40, 0x7e20, 0, 0, {0x96:4,0xa6:4,0x9a:4,0xaa:4}),
        (8, 0x7000, 5, 32, 0x7540, 0x7570, 0x7590, {0x8e:4,0x8f:4,0xb1:6,0xbc:6,0xf1:6,0xf5:6,0xa6:6}),
        (9, 0x4000, 5, 40, 0x7e20, 0x7b36, 0x7b60, {}),
        (9, 0x5000, 6, 40, 0x7e20, 0x7b00, 0x7b20, {0xd0:1,0xd1:1,0xc2:5,0xc3:5}),
        (9, 0x6000, 5, 40, 0x7e00, 0x7a00, 0x7a10, {0xc8:6,0xc9:6,0xd8:6,0xe8:6}),
        (9, 0x7000, 5, 40, 0x7e38, 0x7c30, 0x7c40, {tile:6 for tile in [*range(0xc8,0xce),*range(0xd8,0xde)]}),
        (12, 0x7000, 6, 48, 0x5ce0, 0, 0x7a3f, {}),
        (12, 0x7800, 6, 48, 0x5c20, 0x7b70, 0x5c30, {}),
    ]
    boss_attrs, boss_pals, boss_faces, boss_records = bytearray(), bytearray(), bytearray(), bytearray()
    for bank, source, fill, start, palette, face, pairs, changes in bosses:
        attributes = bytearray([fill])*128
        for tile, value in changes.items():
            attributes[tile-128] = value
        boss_attrs.extend(attributes)
        colors = bytearray(64)
        colors[start:] = second.data(18, palette, 64-start)
        boss_pals.extend(colors)
        indices = bytearray([255])*128+bytearray(128)
        if pairs:
            for offset in range(0, 64, 2):
                tile = second.data(18, pairs+offset, 1)[0]
                if tile == 255:
                    break
                value = second.data(18, pairs+offset+1, 1)[0]
                if value+3 >= 256:
                    raise ValueError('Boss face record crosses its finite table')
                indices[tile] = value
            else:
                raise ValueError('Unterminated boss face-index list')
        if (bank, source) == (9, 0x4000):
            indices[0x18] = 0  # Author5F00, original Crash resource tail.
        boss_faces.extend(indices)
        boss_records.extend(second.data(18, face, 64) if face else bytes(64))
    arrays['mm2_boss_resource_keys'] = ('uint16_t', [bank*256+(source>>8) for bank, source, *_ in bosses])
    arrays['mm2_boss_resource_attributes'] = ('uint8_t', boss_attrs)
    arrays['mm2_boss_resource_palette_starts'] = ('uint8_t', [record[3] for record in bosses])
    arrays['mm2_boss_resource_palettes'] = ('uint8_t', boss_pals)
    arrays['mm2_boss_resource_faces'] = ('uint8_t', boss_faces)
    arrays['mm2_boss_resource_record_loads'] = ('uint8_t', [int(bool(record[5])) for record in bosses])
    arrays['mm2_boss_resource_records'] = ('uint8_t', boss_records)
    arrays['mm2_weapon_palettes'] = ('uint8_t', second.data(16, 0x7500, 13*16))
    extra = bytearray(second.data(17, 0x6200, 0x800)+second.data(16, 0x4a00, 0x800)+second.data(16, 0x4200, 0x800))
    # Authored16:4057..4063 explicitly clears bank1 tile5D at95D0 after the
    # graphics transfers. It is the blank frame tile, not raw icon-sheet art.
    extra[0x15d0:0x15e0] = bytes([255])*16
    arrays['mm2_extra_art'] = ('uint8_t', extra)
    # forple's score uses the same original music-driver data format. Prove
    # the small driver delta before exporting data; the native adapter ports
    # the octave rules while continuing to execute only original bank7 code.
    allowed = {0x403d: 0x1a, 0x404d: 3, 0x41a9: 0xc3, 0x41aa: 0x60, 0x41ab: 0x74,
               0x4282: 0xc3, 0x4283: 0x83, 0x4284: 0x74,
               0x439a: 0xc3, 0x439b: 0xaa, 0x439c: 0x74}
    for address in range(0x4000, 0x44db):
        original = second.original_data(7, address, 1)[0]
        remixed = second.data(20, address, 1)[0]
        if remixed != allowed.get(address, original):
            raise ValueError(f'Unidentified remixed driver change at {address:04x}')
    music = bytearray(second.data(20, 0x44db, 0x8000-0x44db))
    # This hole contains the three guest trampolines. Their native rules are
    # explicit C++; the generated asset never contains their instructions.
    music[0x7460-0x44db:0x74d0-0x44db] = bytes(0x70)
    arrays['mm2_remix_score'] = ('uint8_t', music)
    # Identified finite attribute lists from the original DX resource service.
    # Records key=(world_group*64+kind), then each original OBP plane's offset/count.
    # Complex boss services below are exported as finite native style updates.
    colors, records = bytearray(), []
    simple = {12: (0x4370, 0x4340), 15: (0x4460, 0x4440),
              18: (0x4c40, 0x4c50), 19: (0x5640, 0x5620), 24: (0x4550, 0x4540)}
    def finite_list(address):
        value = first.data(16, address, 128)
        end = value.find(bytes([255]))
        if end < 0 or any(color > 7 for color in value[:end]):
            raise ValueError('Invalid identified object attribute list')
        return value[:end]
    for group in range(2):
        for kind in range(62):
            pair = first.data(16, 0x4100+kind*2, 2)
            if pair[0] != 255:
                count = first.data(16, 0x4200+kind*2, 1)[0]
                palettes = [bytes([color])*count for color in pair]
            else:
                if kind in (9, 20):
                    address = 0x6a50 if group else 0x6a40
                    palettes = [finite_list(address)]*2
                elif kind == 16:
                    address = 0x6b50 if group else 0x6b40
                    palettes = [finite_list(address)]*2
                elif kind in simple:
                    palettes = [finite_list(address) for address in simple[kind]]
                else:
                    continue
            record = [group*64+kind]
            for palette in palettes:
                record.extend((len(colors), len(palette)))
                colors.extend(palette)
            records.extend(record)
    # Every complex native resource style is a finite authored data update.
    # Descriptors are kind,target,offset,count,data-offset. Targets identify
    # attributes, face indices/records, OBJ colors, BG buffer/attributes.
    boss_records, boss_data = [], bytearray()
    def update(kind, target, offset, value):
        boss_records.extend((kind, target, offset, len(value), len(boss_data)))
        boss_data.extend(value)
    def finite(address, count=256):
        value = first.data(16, address, count)
        end = value.find(bytes([255]))
        if end < 0:
            raise ValueError('Unterminated authored resource data')
        return value[:end]
    def fill(kind, plane, count, color):
        update(kind, 0, plane*256+0x8b, bytes([color])*count)
    def faces(kind, start, index, records):
        update(kind, 1, start, finite(index))
        update(kind, 2, 0xc0, finite(records))
    fill(32, 0, 0x4a, 6); fill(32, 1, 0x4a, 5)
    faces(32, 0x8b, 0x4780, 0x47c0)
    update(32, 3, 0x28, first.data(16, 0x4770, 8))
    fill(34, 0, 0x4a, 7); fill(34, 1, 0x4a, 6)
    faces(34, 0x8b, 0x4650, 0x4690)
    fill(36, 0, 0x49, 7)
    update(36, 0, 256+0x8b, finite(0x5140))
    faces(36, 0x8b, 0x51a0, 0x51e0)
    update(38, 3, 0x10, first.data(16, 0x53d0, 32))
    update(38, 0, 0x8b, finite(0x5550)); update(38, 0, 256+0x8b, finite(0x5500))
    faces(38, 0x8b, 0x55a0, 0x55d0)
    fill(40, 0, 0x58, 6); fill(40, 1, 0x58, 5)
    fill(42, 0, 0x5e, 6); fill(42, 1, 0x5e, 3)
    faces(42, 0x8b, 0x4f50, 0x4fc0)
    update(42, 3, 0x10, first.data(16, 0x7560, 32))
    fill(44, 0, 0x5e, 7); fill(44, 1, 0x5e, 6)
    faces(44, 0x8f, 0x4e60, 0x4ec0)
    fill(46, 0, 0x5e, 7); fill(46, 1, 0x5e, 6)
    faces(46, 0x8e, 0x5060, 0x50c0)
    for kind in (38, 40, 42, 44, 46):
        update(kind, 4, 0x28, finite(0x5050 if kind==40 else 0x5040))
    update(48, 5, 0, first.data(16, 0x6e00, 256))
    update(48, 4, 0, first.data(16, 0x6f00, 256))
    update(48, 3, 0x10, first.data(16, 0x7000, 32))
    update(48, 0, 0x8b, first.data(16, 0x708b, 0x74))
    update(48, 0, 256+0x8b, first.data(16, 0x718b, 0x74))
    update(55, 3, 0x28, first.data(16, 0x47f0, 8))
    update(55, 0, 256+0x8b, bytes([5,5]))
    update(56, 5, 0, first.data(16, 0x4a00, 255))
    update(56, 4, 0, first.data(16, 0x4b00, 255))
    update(56, 0, 256+0x8b, bytes([6])+bytes([7])*20)
    arrays['mm1_boss_style_records'] = ('uint16_t', boss_records)
    arrays['mm1_boss_style_data'] = ('uint8_t', boss_data)
    arrays['mm1_boss_fade_modes'] = ('uint8_t', first.data(21, 0x5d00, 256)+first.data(21, 0x5200, 256))
    arrays['mm1_boss_fixed_background_palettes'] = ('uint8_t', first.data(21, 0x4980, 64)+first.data(21, 0x49c0, 64)+first.data(21, 0x4a80, 64))
    arrays['mm1_stage_animation_delays'] = ('uint8_t', first.data(21, 0x4100, 13))
    arrays['mm1_stage_animation_palettes'] = ('uint8_t', b''.join(first.data(21, 0x4300+profile*256, 128) for profile in range(13)))
    arrays['mm1_weapon6_color_offsets'] = ('uint8_t', first.data(21, 0x7e00, 256))
    arrays['mm1_weapon6_colors'] = ('uint8_t', first.data(21, 0x7d00, max(first.data(21, 0x7e00, 256))+8))
    arrays['mm1_resource_records'] = ('uint16_t', records)
    arrays['mm1_resource_attributes'] = ('uint8_t', colors)
    arrays['mm1_hud_weapon_palettes'] = ('uint8_t', first.data(15, 0x5e00, 11*8))
    arrays['mm1_hud_icons'] = ('uint8_t', first.data(19, 0x7000, 11*6))
    # Original ending4B2F calls authored1A:4000. Role4C's exact BGP
    # lookup selects base/dim/light or finite fixed palettes, not interpolation.
    base = first.data(26, 0x4100, 64)
    palettes = (base + bytes(x & 0xcc for x in base) + bytes(x | 0xcc for x in base)
                + first.data(21, 0x4980, 64) + first.data(21, 0x5600, 64)
                + first.data(21, 0x4a00, 64) + first.data(21, 0x4480, 64))
    # Original ending455B/0192 requests the ending BG header0363.
    header = first.word(6, first.word(0, 0x363, True), True)
    count, source = first.word(6, header, True), first.word(6, header+2, True)
    if (count, source) != (0x7c0, 0x690f):
        raise ValueError('Unidentified original ship-fall background request')
    # Its art range must be inserted above before the paired arrays freeze.
    arrays['mm1_credits_background_palettes'] = ('uint8_t', palettes)
    arrays['mm1_credits_fade_modes'] = ('uint8_t', first.data(21, 0x5c00, 256))
    arrays['mm1_credits_background_attributes'] = ('uint8_t', first.data(26, 0x4300, 256))
    arrays['mm1_credits_object_palettes'] = ('uint8_t', first.data(26, 0x4200, 64))
    arrays['mm1_credits_object_modes'] = ('uint8_t', first.data(21, 0x5a00, 256)+first.data(21, 0x6000, 256))
    arrays['mm1_credits_object_attributes'] = ('uint8_t', first.data(21, 0x5f00, 256))
    arrays['mm1_credits_face_index'] = ('uint8_t', first.data(26, 0x4400, 256))
    arrays['mm1_credits_face_records'] = ('uint8_t', first.data(26, 0x4500, 256))
    # Authored17:7000 supplies ship-fall BG attributes and role50 fades.
    ship = first.data(23, 0x7700, 64)
    arrays['mm1_ship_background_palettes'] = ('uint8_t', ship + bytes(x & 0xcc for x in ship)
        + bytes(x | 0xcc for x in ship) + first.data(21, 0x4980, 64)
        + first.data(21, 0x49c0, 64) + first.data(21, 0x4a00, 64))
    arrays['mm1_ship_fade_modes'] = ('uint8_t', first.data(21, 0x5000, 256))
    arrays['mm1_ship_background_attributes'] = ('uint8_t', first.data(23, 0x7100, 768))
    arrays['mm1_ship_object_palettes'] = ('uint8_t', first.data(23, 0x6d00, 64))
    arrays['mm1_ship_object_attributes'] = ('uint8_t', first.data(23, 0x6e00, 512))
    # Native20F2 calls authored18:4000 after the art/map uploads.
    arrays['mm1_getweapon_background_palettes'] = ('uint8_t', first.data(24, 0x4600, 64))
    arrays['mm1_getweapon_background_attributes'] = ('uint8_t', first.data(24, 0x4100, 768))
    arrays['mm2_hud_icons'] = ('uint8_t', second.data(16, 0x74e0, 14))
    # Authored native pause services7300/7400/7600: finite row/slot tables,
    # plus all14 selection palettes including the E-Tank entry.
    arrays['mm2_menu_attributes'] = ('uint8_t', second.data(16, 0x74b0, 20))
    arrays['mm2_menu_slot_steps'] = ('uint8_t', second.data(16, 0x74d0, 14))
    arrays['mm2_menu_palettes'] = ('uint8_t', second.data(16, 0x7500, 14*16))
    arrays['mm2_menu_original_cells'] = ('uint8_t', second.original_data(0, 0x3b2b, 14*4))
    arrays['mm2_menu_cells'] = ('uint8_t', second.data(0, 0x3b2b, 14*4))
    # Native06F2's finite tile-definition format, including row/column runs.
    # Only decoded presentation maps are exported, never the loader code.
    def mm2_scene_map(address, original, size=1024, bank=4):
        read = second.original_data if original else second.data
        result = bytearray(size)
        for _ in range(128):
            high = read(bank, address, 1)[0]
            if not high:
                return result
            low, command = read(bank, address+1, 2)
            address += 3
            destination = high*256+low
            count, mode = command & 63, command >> 6
            if not count:
                raise ValueError('Unidentified zero-length tile run')
            if mode & 1:
                values = read(bank, address, 1)*count
                address += 1
            else:
                values = read(bank, address, count)
                address += count
            for tile in values:
                if not 0x9800 <= destination < 0x9800+size:
                    raise ValueError('Fixed-scene map crosses the native map')
                result[destination-0x9800] = tile
                destination += 32 if mode & 2 else 1
        raise ValueError('Unterminated fixed-scene tile definition')
    arrays['mm2_title_original_map'] = ('uint8_t', mm2_scene_map(0x4000, True))
    arrays['mm2_title_map'] = ('uint8_t', mm2_scene_map(0x4000, False))
    arrays['mm2_title_palettes'] = ('uint8_t', second.data(19, 0x7000, 64))
    arrays['mm2_title_object_palettes'] = ('uint8_t', second.data(19, 0x7040, 64))
    arrays['mm2_title_flash_palettes'] = ('uint8_t', second.data(19, 0x7080, 64))
    arrays['mm2_title_attributes'] = ('uint8_t', second.data(19, 0x7100, 256))
    arrays['mm2_title_music_labels'] = ('uint8_t', second.data(16, 0x7eb0, 8))
    # Author19:5A40 initializes the original first-quad selector. Its two
    # native map halves remain separate for the real BG/window split at72.
    arrays['mm2_select_original_map'] = ('uint8_t', mm2_scene_map(0x5000, True, 2048))
    arrays['mm2_select_map'] = ('uint8_t', mm2_scene_map(0x5000, False, 2048))
    arrays['mm2_select_palettes'] = ('uint8_t', second.data(19, 0x6e00, 64))
    arrays['mm2_select_object_palettes'] = ('uint8_t', second.data(19, 0x6e80, 64))
    arrays['mm2_select_flash_palettes'] = ('uint8_t', second.data(19, 0x6ec0, 64))
    arrays['mm2_select_attributes'] = ('uint8_t', second.data(19, 0x6f00, 256))
    # Original GFXSET_SPACE05D2 calls the authored AC00 service, copied
    # from16:6C00. These are finite palette/attribute/face records only.
    arrays['mm2_space_background_palettes'] = ('uint8_t', second.data(16, 0x6cc0, 64))
    arrays['mm2_space_object_palettes'] = ('uint8_t', second.data(16, 0x6c80, 64))
    arrays['mm2_space_background_attributes'] = ('uint8_t', second.data(16, 0x6b00, 256))
    arrays['mm2_space_object_attributes'] = ('uint8_t', second.data(16, 0x6d00, 256))
    arrays['mm2_space_face_records'] = ('uint8_t', second.data(16, 0x7c20, 24))
    if any(value > 7 for name in ('mm2_space_background_attributes', 'mm2_space_object_attributes') for value in arrays[name][1]):
        raise ValueError('Unidentified authored space palette attribute')
    # Original Sc_DrawSprMap1:5A39 consumes these finite sprite-map records.
    # The authored credits move large Rockman's tiles70..7D to60..6D and
    # align the leg pieces with the revised graphics payload.
    for original, name in ((True, 'mm2_credits_original_map'), (False, 'mm2_credits_map')):
        read = second.original_data if original else second.data
        pointer = second.word(1, 0x62b1, original)
        count = read(1, pointer, 1)[0]
        if count != 14:
            raise ValueError('Unidentified authored credits sprite-map allocation')
        arrays[name] = ('uint8_t', read(1, pointer, 1+count*4))
    # Original LoadGFX_GetWpn0508 calls authored0B:7800 before its
    # unchanged5800 picture upload. Freeze only its finite presentation data.
    arrays['mm2_getweapon_original_map'] = ('uint8_t', mm2_scene_map(0x5180, True))
    arrays['mm2_getweapon_map'] = ('uint8_t', mm2_scene_map(0x5180, False))
    arrays['mm2_getweapon_palettes'] = ('uint8_t', second.data(11, 0x7840, 64))
    arrays['mm2_getweapon_object_palettes'] = ('uint8_t', second.data(11, 0x7880, 64))
    arrays['mm2_getweapon_attributes'] = ('uint8_t', second.data(11, 0x7900, 256))
    # Original GFXSet5/6: the castle and station retain native scripted
    # actor movement. Only their identified maps/palettes/attributes differ.
    arrays['mm2_castle_original_map'] = ('uint8_t', mm2_scene_map(0x53c0, True))
    arrays['mm2_castle_map'] = ('uint8_t', mm2_scene_map(0x53c0, False))
    arrays['mm2_castle_palettes'] = ('uint8_t', second.data(12, 0x5580, 128))
    arrays['mm2_castle_object_palettes'] = ('uint8_t', second.data(12, 0x5540, 64))
    arrays['mm2_castle_attributes'] = ('uint8_t', second.data(12, 0x5600, 256))
    arrays['mm2_castle_object_attributes'] = ('uint8_t', second.data(12, 0x5700, 128))
    arrays['mm2_station_original_map'] = ('uint8_t', mm2_scene_map(0x5540, True))
    arrays['mm2_station_map'] = ('uint8_t', mm2_scene_map(0x5540, False))
    arrays['mm2_station_palettes'] = ('uint8_t', second.data(12, 0x7cc0, 64))
    arrays['mm2_station_attributes'] = ('uint8_t', second.data(12, 0x7d80, 128)+second.data(12, 0x7e80, 128))
    # Original CGB VBlank4: handler table AF20 selects only these authored
    # finite stage effects. No guest handler bytes are exported or interpreted.
    if second.data(16, 0x6f20, 16) != bytes.fromhex('1e b2 5a 8b 71 a5 7e 1e 1e e3 1e 1e 1e 1e 1e 1e'):
        raise ValueError('Unidentified authored stage-effect dispatch')
    # Original5:7600 holds all10 native64-block definitions. Authored
    # Needle/station changes are presentation tile indices only; retain the
    # original writable definitions for gameplay and match before remapping.
    arrays['mm2_original_blocks'] = ('uint8_t', second.original_data(5, 0x7600, 10*256))
    arrays['mm2_blocks'] = ('uint8_t', second.data(5, 0x7600, 10*256))
    def layout2(stage, original):
        read = second.original_data if original else second.data
        cursor = second.word(5, 0x4000+stage*2, original)
        value = bytearray()
        while len(value) < 0xa00:
            count = read(5, cursor, 1)[0]
            cursor += 1
            if count & 128:
                value.extend(read(5, cursor, 1)*(count & 127 or 256))
                cursor += 1
            else:
                value.append(count)
        if len(value) != 0xa00:
            raise ValueError('Original native layout decode overrun')
        return value
    if any(layout2(stage, True) != layout2(stage, False) for stage in range(10)):
        raise ValueError('Authored layout/actor changes require an explicit native gameplay decision')
    arrays['mm2_stage_effect_palettes'] = ('uint8_t', b''.join(second.data(16, address, 64) for address in (0x7800, 0x7840, 0x7880, 0x7940)))
    arrays['mm2_stage_effect_tiles'] = ('uint8_t', second.data(16, 0x78c0, 64)+second.data(16, 0x7980, 64))
    arrays['mm2_crash_room_colors'] = ('uint8_t', second.data(16, 0x7bc0, 64))
    arrays['mm2_wily_object_attributes'] = ('uint8_t', second.data(18, 0x7380, 128))
    arrays['mm2_wily_object_palettes'] = ('uint8_t', second.data(18, 0x7850, 48))
    arrays['mm2_wily_face_records'] = ('uint8_t', second.data(18, 0x56c0, 16))
    arrays['mm2_wily_fade_colors'] = ('uint8_t', second.data(18, 0x5ca0, 64))
    arrays['mm2_wily_background_palettes'] = ('uint8_t', second.data(18, 0x4fc0, 40))
    arrays['mm2_wily_background_attributes'] = ('uint8_t', second.data(18, 0x5000, 256))
    arrays['mm2_quint_object_attributes'] = ('uint8_t', second.data(18, 0x7780, 128))
    arrays['mm2_quint_object_palettes'] = ('uint8_t', second.data(18, 0x7500, 48))
    arrays['mm2_quint_face_indices'] = ('uint8_t', second.data(18, 0x7680, 128))
    arrays['mm2_password_original_map'] = ('uint8_t', mm2_scene_map(0x5086, True, bank=1))
    arrays['mm2_password_map'] = ('uint8_t', mm2_scene_map(0x5086, False, bank=1))
    if second.original_data(1, 0x4f55, 3) != bytes([0x80,0x00,0x10]):
        raise ValueError('Unidentified native password cursor graphics definition')
    arrays['mm2_password_original_cursor'] = ('uint8_t', second.original_data(1, 0x4f58, 16))
    arrays['mm2_password_cursor'] = ('uint8_t', second.data(1, 0x4f58, 16))
    # Original1A7D/1AE3's static title map and finite text records. The native
    # renderer replaces only still-matching original cells, preserving actual
    # original blinking/dynamic writes. Author bank17:4000 selects these exact
    # color/attribute payloads; its instructions are not exported or executed.
    def title_map(original):
        read = first.original_data if original else first.data
        pointer = first.word(6, first.word(6, first.word(0, 0x347, original), original), original)
        width, height = read(6, pointer, 2)
        if width > 32 or height > 32:
            raise ValueError('Invalid title map dimensions')
        result = bytearray([0x6c])*1024
        for row in range(height):
            result[row*32:row*32+width] = read(6, pointer+2+row*width, width)
        cursor = first.word(6, first.word(6, first.word(0, 0x349, original), original), original)
        for _ in range(32):
            destination = first.word(6, cursor, original)
            cursor += 2
            if destination == 0:
                return result
            if not 0x9800 <= destination < 0x9c00:
                raise ValueError('Title text destination outside map')
            for _ in range(128):
                tile = read(6, cursor, 1)[0]
                cursor += 1
                if tile == 0:
                    break
                result[destination-0x9800] = tile
                destination += 1
            else:
                raise ValueError('Unterminated title text')
        raise ValueError('Unterminated title text list')
    arrays['mm1_title_original_map'] = ('uint8_t', title_map(True))
    arrays['mm1_title_map'] = ('uint8_t', title_map(False))
    arrays['mm1_title_attributes'] = ('uint8_t', first.data(17, 0x4100, 1024))
    arrays['mm1_title_palettes'] = ('uint8_t', first.data(17, 0x4080, 64))
    arrays['mm1_title_object_palettes'] = ('uint8_t', first.data(17, 0x7200, 64))
    def select_map(original):
        read = first.original_data if original else first.data
        result = bytearray(2048)
        for half, binding in enumerate((0x353, 0x355)):
            pointer = first.word(6, first.word(6, first.word(0, binding, original), original), original)
            for row in range(9):
                result[half*1024+row*32:half*1024+row*32+20] = read(6, pointer+row*20, 20)
        return result
    def role50(base):
        return (base + bytes(x & 0xcc for x in base) + bytes(x | 0xcc for x in base)
                + first.data(21, 0x4980, 64) + first.data(21, 0x49c0, 64)
                + first.data(21, 0x4a00, 64))
    arrays['mm1_select_background_fades'] = ('uint8_t', role50(first.data(18, 0x4100, 64)))
    arrays['mm1_fortress_background_palettes'] = ('uint8_t', role50(first.data(23, 0x4200, 64)))
    arrays['mm1_first_castle_object_palettes'] = ('uint8_t', first.data(23, 0x4240, 64))
    arrays['mm1_fortress_object_palettes'] = ('uint8_t', first.data(23, 0x4280, 64))
    arrays['mm1_fortress_background_attributes'] = ('uint8_t', first.data(23, 0x4300, 768))
    arrays['mm1_select_original_map'] = ('uint8_t', select_map(True))
    arrays['mm1_select_map'] = ('uint8_t', select_map(False))
    arrays['mm1_select_attributes'] = ('uint8_t', first.data(18, 0x4200, 256))
    arrays['mm1_select_palettes'] = ('uint8_t', first.data(18, 0x4100, 64))
    arrays['mm1_select_object_palettes'] = ('uint8_t', first.data(18, 0x4140, 64))
    arrays['mm1_select_object_attributes'] = ('uint8_t', first.data(18, 0x4600, 512))
    arrays['mm1_select_face_index'] = ('uint8_t', first.data(18, 0x4800, 256))
    arrays['mm1_select_face_records'] = ('uint8_t', first.data(18, 0x4900, 256))
    def password1_map(original):
        read = first.original_data if original else first.data
        width, height = read(12, 0x6ca7, 2)
        if width != 20 or height != 18:
            raise ValueError('Unidentified native password map dimensions')
        result = bytearray(1024)
        for row in range(height):
            result[row*32:row*32+width] = read(12, 0x6ca9+row*width, width)
        return result
    arrays['mm1_password_original_map'] = ('uint8_t', password1_map(True))
    arrays['mm1_password_map'] = ('uint8_t', password1_map(False))
    lines = ['#pragma once', '#include <cstdint>', 'namespace gbcllr::mm12_dx {']
    for name, (kind, values) in arrays.items():
        lines.append(f'inline constexpr {kind} {name}[{len(values)}]={{')
        lines.extend(','.join(map(str, values[i:i+32]))+',' for i in range(0, len(values), 32))
        lines.append('};')
    lines.append('}')
    header = ('\n'.join(lines)+'\n').encode()
    args.output.mkdir(parents=True, exist_ok=True)
    (args.output/'mm12_dx_data.hpp').write_bytes(header)
    manifest = dict(format=1, header_sha256=hashlib.sha256(header).hexdigest(), original_rom_sha256=ROM_IDS,
                    patch_sha256=PATCH_IDS, versions=['1.02', '1.01'],
                    source_urls=['https://www.romhacking.net/hacks/7492/', 'https://www.romhacking.net/hacks/8688/'],
                    credits=[['SpecialAgentApe: hacking/colors', 'Superjustinbros: weapon icons',
                              'SuxMenner: title logo (modified)', 'Kensuyjin33: Enker graphics/palettes',
                              'Tails Gaming Power: cutting wheel graphics/misc', 'marc_max: special thanks'],
                             ['SpecialAgentApe: hack', 'forple: remixed OST/SFX', 'Kensuyjin33: Wily graphics',
                              'marc_max and GB colorization group: special thanks']],
                    ranges={name: len(values) for name, (_, values) in arrays.items()},
                    scope='Read-only presentation data. No patched cartridge, guest code execution or ROM mutation.')
    (args.output/'mm12_dx_data.json').write_text(json.dumps(manifest, indent=2)+'\n')
    (args.output/'mm1-room-graph.json').write_text(json.dumps(graph, indent=2)+'\n')
    print(json.dumps(dict(header_sha256=manifest['header_sha256'], header_bytes=len(header), rooms=sum(len(x['rooms']) for x in graph))))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    for title in (1, 2):
        parser.add_argument(f'--original{title}', type=Path, required=True)
        parser.add_argument(f'--patch{title}', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    freeze(parser.parse_args())
