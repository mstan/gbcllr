"""Freeze identified MMW3/4DX presentation data; never construct a patched ROM.

Uses the existing bounded BPS presentation reader. It exports palettes, stage
attributes and explicit actor/body presentation data referenced by the original
resource loaders and pose initializers. Donor instructions are never exported.
Outputs are private generated build inputs, never source-controlled assets.
"""
import argparse
import hashlib
import json
from pathlib import Path
import zipfile
from mm5_dx_data import PresentationSlices

INPUTS = (
    (3, '7344a36fcfc8151098238529218762e44c5a1546fab7e7fd5d32927e06cbf5a8',
     'mmw3dx_v1_2b.bps', 'f850c08a50d80aff4ce4f538253896a3172c7390d9dd202410957e4bdd149e5a', 0x1d, 0x26, 12, 73),
    (4, '4d980ca46a83cd127312dbc445ed1a83e97ed7a8026dce6fdd22a01b1895781b',
     'mmw4dx_v1_1.bps', '7e93963de33f576d612a03f69a83222bf4053585561c3c8b690edb3feb8e9132', 0x32, 0x3d, 23, 128),
)


def stage_select3(original, view, arrays):
    def raw(address, count):
        return original[address:address+count]

    def style(cursor, pixels, layout, callback=False):
        start, commands = cursor, []
        def take(count):
            nonlocal cursor
            out = view.data(0x27, cursor, count)
            cursor += count
            return out
        for _ in range(128):
            at, op = cursor, take(1)[0]
            if not op:
                return [start, cursor-start, commands]
            if op in (1, 2):
                count = take(1)[0]
                if count > 8:
                    raise ValueError('Stage-select palette overflow')
                for index in range(count):
                    layout[(0 if op == 1 else 8)+index] = int.from_bytes(take(2), 'little')
                commands.append([at, op, count])
            elif op in (3, 4):
                h = take(4)
                destination, rows, columns = int.from_bytes(h[:2], 'little'), h[2], h[3]
                if not rows or not columns or rows > 32 or columns > 32 or destination < 0x9800 or destination+32*(rows-1)+columns > 0xa000:
                    raise ValueError('Invalid stage-select attribute rectangle')
                values = take(1 if op == 3 else rows*columns)
                for y in range(rows):
                    for x in range(columns):
                        pixels[destination-0x8000+y*32+x] = values[0 if op == 3 else y*columns+x]
                commands.append([at, op, destination, rows, columns])
            elif op == 5:
                h = take(4)
                destination, count = int.from_bytes(h[:2], 'little'), int.from_bytes(h[2:], 'little')
                if not count or destination < 0x8000 or destination+count > 0xa000:
                    raise ValueError('Invalid stage-select texture copy')
                pixels[destination-0x8000:destination-0x8000+count] = take(count)
                commands.append([at, op, destination, count])
            elif op == 6 and callback and int.from_bytes(take(2), 'little') == 0x4562:
                # This source callback changes eight palette pointers only
                # when the first four masters are owned. Freeze its DATA;
                # the native renderer supplies that explicit condition.
                commands.append([at, op, 0x4562])
                return [start, cursor-start, commands]
            else:
                raise ValueError('Unidentified stage-select style behavior')
        raise ValueError('Unterminated stage-select style')

    arrays['select_original_art'], arrays['select_color_art'] = [], []
    copies = []
    for resource in (1, 2):
        original_art, color_art = bytearray(0x1800), bytearray(0x1800)
        cursor = int.from_bytes(raw(0x1f23+resource*2, 2), 'little')
        for _ in range(16):
            source = int.from_bytes(raw(cursor, 2), 'little')
            if not source:
                break
            tile, count, bank, high = raw(cursor+2, 4)
            destination, count = ((high | 0x80) << 8) | (tile << 4), count*16
            if destination < 0x8000 or destination+count > 0x9800 or not 0x4000 <= source < source+count <= 0x8000:
                raise ValueError('Stage-select raw-art copy escapes its binding')
            at = bank*0x4000+source-0x4000
            original_art[destination-0x8000:destination-0x8000+count] = original[at:at+count]
            color_art[destination-0x8000:destination-0x8000+count] = view.data(bank, source, count)
            copies.append([resource, bank, source, destination, count])
            cursor += 6
        else:
            raise ValueError('Unterminated stage-select raw-art list')
        arrays['select_original_art'].extend(original_art)
        arrays['select_color_art'].extend(color_art)
    # Both board groups retain identical native tile maps, including all
    # four completion overlays. Preserve the live original map and cursor.
    for resource in range(1, 7):
        bank, high, lo, hi = raw(0x206b+resource*4, 4)
        address = lo | hi << 8
        at = bank*0x4000+address-0x4000
        columns, rows = original[at:at+2]
        count = 4+columns*rows
        if original[at:at+count] != view.data(bank, address, count):
            raise ValueError('Stage-select map needs an explicit authored binding')

    pixels, layout = bytearray(arrays['menu_boot_vram']), [0]*16
    record = style(0x4132, pixels, layout, callback=True)
    arrays['select_vram1'], arrays['select_palettes'] = list(pixels), list(layout)
    alternate = list(layout)
    alternate[3:7] = [view.word(0x27, 0x4584+i*2) for i in range(4)]
    alternate[10:14] = [view.word(0x27, 0x458c+i*2) for i in range(4)]
    arrays['select_alternate_palettes'] = alternate
    arrays['select_completed_attributes'] = []
    completed = []
    for resource in range(3, 7):
        copy, colors = bytearray(pixels), list(layout)
        source = view.word(0x27, 0x4000+resource*2)
        completed.append(style(source, copy, colors))
        if colors != layout or copy[:0x1800] != pixels[:0x1800]:
            raise ValueError('Stage-select completion changes more than attributes')
        arrays['select_completed_attributes'].extend(a if a != b else 255 for a, b in zip(copy[0x1800:], pixels[0x1800:]))
    arrays['select_oam'], arrays['select_oam_offsets'] = [], [0]
    arrays['select_oam_destinations'] = [0x38, 0x54, 0x64, 0x74, 0x38, 0x50, 0x58, 0x78, 0x38]
    records = [(0x41a1, 28), (0x41c9, 16), (0x41e5, 16), (0x4201, 36),
               (0x4231, 24), (0x4255, 8), (0x4269, 32), (0x4295, 40), (0x42c9, 36)]
    for index, (address, count) in enumerate(records):
        if arrays['select_oam_destinations'][index]+count > 160 or count % 4:
            raise ValueError('Stage-select authored OAM copy exceeds native buffer')
        arrays['select_oam'].extend(view.data(0x11, address, count))
        arrays['select_oam_offsets'].append(len(arrays['select_oam']))
    # Original022D loads resources5..12 during the eight master introductions.
    # Their native custom-sprite producer is retained; the authored records add
    # one skin overlay to each of its twenty source poses.
    arrays['intro_original_art'], arrays['intro_color_art'] = [], []
    intro_copies = []
    for resource in range(5, 13):
        native, art = bytearray(0x800), bytearray(0x800)
        cursor = int.from_bytes(raw(0x1f23+resource*2, 2), 'little')
        for _ in range(16):
            source = int.from_bytes(raw(cursor, 2), 'little')
            if not source:
                break
            tile, count, bank, high = raw(cursor+2, 4)
            destination, count = ((high | 0x80) << 8) | (tile << 4), count*16
            if not 0x8000 <= destination < destination+count <= 0x8800:
                raise ValueError('Introduction art escapes its original OBJ binding')
            offset = bank*0x4000+source-0x4000
            native[destination-0x8000:destination-0x8000+count] = original[offset:offset+count]
            art[destination-0x8000:destination-0x8000+count] = view.data(bank, source, count)
            intro_copies.append([resource, bank, source, destination, count])
            cursor += 6
        else:
            raise ValueError('Unterminated introduction art list')
        arrays['intro_original_art'].extend(native)
        arrays['intro_color_art'].extend(art)
    arrays['intro_attributes'] = list(view.data(0x11, 0x7100, 0x800))
    if any(value > 7 for value in arrays['intro_attributes']):
        raise ValueError('Introduction sprite attributes need an explicit geometry/bank binding')
    arrays['intro_pose_addresses'], arrays['intro_overlays'] = [], []
    def pose(address, authored=False):
        def read(a, n):
            return view.data(3, a, n) if authored else original[3*0x4000+a-0x4000:3*0x4000+a-0x4000+n]
        header = read(address, 7)
        end = address+7+(header[6] >> 4)*(header[6] & 15)
        for _ in range(64):
            if read(end, 1) == b'\x80':
                return read(address, end+1-address)
            end += 3
        raise ValueError('Unterminated introduction pose')
    for index in range(20):
        source = int.from_bytes(original[3*0x4000+0x3b15+index*2:3*0x4000+0x3b17+index*2], 'little')
        target = view.word(3, 0x7b15+index*2)
        native, art = pose(source), pose(target, True)
        if not art.startswith(native[:-1]) or len(art) != len(native)+3:
            raise ValueError('Introduction changes more than its identified single overlay')
        arrays['intro_pose_addresses'].append(source)
        arrays['intro_overlays'].extend(native[4:6]+art[-4:-1])
    arrays['intro_palettes'] = [0x485c]
    arrays['password_original_art'], arrays['password_color_art'] = list(bytes(0x1800)), list(bytes(0x1800))
    cursor = int.from_bytes(raw(0x1f23+3*2, 2), 'little')
    password_copies = []
    for _ in range(16):
        source = int.from_bytes(raw(cursor, 2), 'little')
        if not source:
            break
        tile, count, bank, high = raw(cursor+2, 4)
        destination, count = ((high | 0x80) << 8) | (tile << 4), count*16
        if not 0x8000 <= destination < destination+count <= 0x9800:
            raise ValueError('Password raw-art range escapes source binding')
        offset = bank*0x4000+source-0x4000
        arrays['password_original_art'][destination-0x8000:destination-0x8000+count] = original[offset:offset+count]
        arrays['password_color_art'][destination-0x8000:destination-0x8000+count] = view.data(bank, source, count)
        password_copies.append([bank, source, destination, count])
        cursor += 6
    else:
        raise ValueError('Unterminated password raw-art list')
    attrs, layout = bytearray(arrays['menu_boot_vram']), [0]*16
    password_style = style(view.word(0x27, 0x4000+7*2), attrs, layout)
    arrays['password_vram1'], arrays['password_palettes'] = list(attrs), layout
    arrays['password_original_map'], arrays['password_color_map'], arrays['password_map_valid'] = [0]*0x800, [0]*0x800, [0]*0x800
    bank, high, lo, hi = raw(0x206b+7*4, 4)
    address = lo | hi << 8
    offset = bank*0x4000+address-0x4000
    header = original[offset:offset+4]
    if header != view.data(bank, address, 4):
        raise ValueError('Password map changes its native rectangle')
    columns, rows, x0, y0 = header
    if x0+columns > 32 or y0+rows > 32 or high not in (0x98, 0x9c):
        raise ValueError('Password map escapes its native map')
    native, art = original[offset+4:offset+4+rows*columns], view.data(bank, address+4, rows*columns)
    for y in range(rows):
        for x in range(columns):
            cell = (high-0x98)*256+(y+y0)*32+x+x0
            arrays['password_original_map'][cell] = native[y*columns+x]
            arrays['password_color_map'][cell] = art[y*columns+x]
            arrays['password_map_valid'][cell] = 1
    return dict(password_raw_art=password_copies, password_style=[0x27, *password_style], password_map=[bank, address, len(native)+4], intro_raw_art=intro_copies, intro_pose_table=[3, 0x7b15, 40], intro_attribute_pages=[0x11, 0x7100, 0x800], stage_select_raw_art=copies, stage_select_style=[0x27, *record],
                stage_select_completion_styles=completed,
                stage_select_palette_condition='DE9A&3C==3C: BG3..6/OBJ2..5 from27:4584/458C',
                stage_select_oam_data=[[0x11, a, n] for a, n in records])


def actor_data(title, original, view, arrays):
    """Resolve DATA referenced by the original resource and pose services."""
    def raw(bank, address, size):
        at = address if address < 0x4000 else bank*0x4000+address-0x4000
        return original[at:at+size]

    def authored(bank, address, size):
        return bytes(view.byte(address+i) for i in range(size)) if address < 0x4000 else view.data(bank, address, size)

    resource_count = 71 if title == 3 else 114
    arrays['actor_art_offsets'] = [0]*resource_count
    arrays['actor_art_counts'] = [0]*resource_count
    arrays['actor_art'] = []
    art_ranges = []
    for resource in range(1, resource_count):
        table = (0x3bed if title == 3 else 0x41fc)+resource*3
        entry = authored(0 if title == 3 else 1, table, 3)
        bank, pointer = entry[0], int.from_bytes(entry[1:], 'little')
        pointer = view.word(bank, pointer)
        header = view.data(bank, pointer, 3)
        count, source = header[0], int.from_bytes(header[1:], 'little')
        arrays['actor_art_offsets'][resource] = len(arrays['actor_art'])
        arrays['actor_art_counts'][resource] = count
        if count:
            arrays['actor_art'].extend(view.data(bank, source, count*16))
            art_ranges.append([resource, bank, source, count*16])

    def frame(read, bank, address, kind):
        if not 0x4000 <= address <= 0x7ff8:
            return None
        header = read(bank, address, 7)
        bounds = [value if value < 128 else value-256 for value in header[:4]]
        columns, rows = header[6] >> 4, header[6] & 15
        if bounds[0] > 0 or bounds[1] < 0 or bounds[2] > 0 or bounds[3] < 0:
            return None
        if not kind and (columns > 8 or rows > 12 or columns*rows > 64):
            return None
        at = address+4 if kind else address+7+columns*rows
        for _ in range(128):
            if at >= 0x8000:
                return None
            if read(bank, at, 1) == b'\x80':
                return read(bank, address+4, at+1-address-4)
            if kind and (at+4 > 0x8000 or (read is raw and read(bank, at+3, 1)[0] & 15)):
                return None
            at += 4 if kind else 3
        return None

    frames, initializers = {}, []
    service = bytes((0xe2 if title == 3 else 0xdd, 1))
    for bank in range(1, len(original)//0x4000):
        data = original[bank*0x4000:(bank+1)*0x4000]
        for offset in range(len(data)-6):
            if data[offset] != 0x11 or data[offset+3] not in (0xcd, 0xc3) or data[offset+4:offset+6] != service:
                continue
            table = int.from_bytes(data[offset+1:offset+3], 'little')
            if not 0x4000 <= table < 0x7fff:
                continue
            code = view.data(bank, offset+0x4000, 6)
            if code[0] != 0x11 or code[3:] != data[offset+3:offset+6]:
                raise ValueError('Changed original pose initializer requires explicit native handling')
            target = int.from_bytes(code[1:3], 'little')
            count = 0
            for index in range(128):
                if table+index*2+2 > 0x8000 or target+index*2+2 > 0x8000:
                    break
                source = int.from_bytes(raw(bank, table+index*2, 2), 'little')
                originals = [frame(raw, bank, source, kind) for kind in range(1 if title == 3 else 2)]
                if not any(record is not None for record in originals):
                    break
                destination = view.word(bank, target+index*2)
                paired = False
                for kind, original_frame in enumerate(originals):
                    record = frame(authored, bank, destination, kind) if original_frame is not None else None
                    if record is None:
                        continue
                    key = kind << 24 | bank << 16 | source+4
                    value = destination+4, record
                    if key in frames and frames[key] != value:
                        raise ValueError('Conflicting authored pose binding')
                    frames[key] = value
                    paired = True
                if not paired:
                    raise ValueError('Invalid authored pose referenced by an original pose initializer')
                count += 1
            if count:
                initializers.append([bank, offset+0x4000, table, target, count])
    ending_bindings = []
    if title == 3:
        # The ending uses015F directly, not the ordinary1E2 initializer.
        # Its old eight-entry walking table repeats four poses; the authored
        # table contains four unique poses followed by the static final pose.
        # Keep native timing here and bind each logical pose to its actual
        # relocated DATA/parallel attributes. Earlier animation speed is a
        # separate behavior contract.
        if raw(6, 0x6b9a, 5) != bytes.fromhex('9f6b0a08ff') or view.data(6, 0x6b9a, 5) != bytes.fromhex('9f6b0a04ff'):
            raise ValueError('Unidentified ending pose table')
        for source, destination in [(0x6c56,0x7ff0),(0x6c65,0x6c52),(0x6c88,0x6c75)]:
            before, after = frame(raw,6,source,0), frame(authored,6,destination,0)
            if before is None or after is None:
                raise ValueError('Unidentified credits special actor record')
            frames[6 << 16 | source+4] = destination+4, after
        for old_index, new_index in [(0, 0), (2, 1), (4, 2), (6, 3), (8, 4)]:
            source = int.from_bytes(raw(6, 0x6b9f+old_index*2, 2), 'little')
            destination = view.word(6, 0x6b9f+new_index*2)
            original_record, record = frame(raw, 6, source, 0), frame(authored, 6, destination, 0)
            if original_record is None or record is None:
                raise ValueError('Invalid explicit ending pose binding')
            key, value = 6 << 16 | source+4, (destination+4, record)
            if key in frames and frames[key] != value:
                raise ValueError('Conflicting ending pose binding')
            frames[key] = value
            ending_bindings.append([6, source+4, destination+4, len(original_record), len(record)])
    arrays['actor_pose_keys'] = sorted(frames)
    arrays['actor_pose_addresses'] = []
    arrays['actor_pose_offsets'] = []
    arrays['actor_pose_records'] = []
    for key in arrays['actor_pose_keys']:
        address, record = frames[key]
        arrays['actor_pose_addresses'].append(address)
        arrays['actor_pose_offsets'].append(len(arrays['actor_pose_records']))
        arrays['actor_pose_records'].extend(record)
    return dict(actor_raw_art_ranges=art_ranges, original_pose_initializers=initializers, ending_pose_bindings=ending_bindings,
                original_pose_data_keys=arrays['actor_pose_keys'], authored_pose_data_addresses=arrays['actor_pose_addresses'])


def menu_data(title, original, view, arrays):
    """Export authored NES menu DATA and the six/seven boot VRAM1 art copies.

    These are the literal texture ranges named by 10:402A..406F (III) and
    20:402A..407B (IV), not their enclosing executable banks.
    """
    bank = 0x10 if title == 3 else 0x20
    copies = ([(0x4099, 0x8000, 0xe0), (0x4179, 0x86e0, 0x10),
               (0x4189, 0x8660, 0x40), (0x41c9, 0x8720, 0x20),
               (0x41e9, 0x9010, 0x300), (0x44e9, 0x96c0, 0x50)] if title == 3 else
              [(0x40a4, 0x8000, 0xe0), (0x4184, 0x86c0, 0x40),
               (0x41c4, 0x8760, 0x40), (0x49e4, 0x9010, 0x340),
               (0x4d24, 0x96b0, 0x10), (0x4204, 0x8100, 0x420),
               (0x4624, 0x8900, 0x3c0)])
    pixels = bytearray(8192)
    for source, destination, count in copies:
        pixels[destination-0x8000:destination-0x8000+count] = view.data(bank, source, count)
    arrays['menu_boot_vram'] = list(pixels)
    font_bank, font = (0, 0x0339) if title == 3 else (4, 0x56e7)
    arrays['menu_font'] = list(bytes(view.byte(font+i) for i in range(0x1f0)) if not font_bank else view.data(font_bank, font, 0x1f0))
    count = 13 if title == 3 else 14
    # III 1:7E9D sends D900's energy/lower-icon row to 7E6E, then sends
    # D908's upper-icon/label row to the old 6417 destinations.
    arrays['menu_bottom'] = [view.word(1, (0x7e6e if title == 3 else 0x53ba)+i*2) for i in range(count)]
    arrays['menu_top'], arrays['menu_labels'] = [], []
    for i in range(count):
        record = view.word(1, (0x7e20 if title == 3 else 0x7a80)+i*2)
        source = record if title == 3 else record+2
        arrays['menu_labels'].extend(view.data(1, source, 4))
        arrays['menu_top'].append(view.word(1, 0x6417+i*2) if title == 3 else view.word(1, record))
    arrays['menu_lower'] = list(view.data(1, 0x6572 if title == 3 else 0x539c, (count-1)*2)) + ([0xe4, 0xed] if title == 3 else [0, 0])
    arrays['menu_energy'] = list(view.data(1, 0x654b if title == 3 else 0x5351, 5))
    # Original reserved HUD tiles72..7F are one contiguous raw-art range.
    # Their authored bitplanes differ (including the orange energy stripes).
    hud = 0x4dd3 if title == 3 else 0x55fb
    at = 4*0x4000+hud-0x4000
    arrays['hud_original_art'] = list(original[at:at+0xe0])
    arrays['hud_color_art'] = list(view.data(4, hud, 0xe0))
    hud_bank, hud_attributes = (0x27, 0x4be9) if title == 3 else (0x3e, 0x4000)
    if view.data(hud_bank, hud_attributes, 5) != bytes.fromhex('04009c0214'):
        raise ValueError('Unrecognized authored two-row HUD attribute rectangle')
    arrays['hud_attributes'] = list(view.data(hud_bank, hud_attributes+5, 40))
    return dict(menu_boot_texture_bank=bank, menu_boot_texture_copies=copies,
                menu_font_range=[font_bank, font, 0x1f0], menu_item_count=count,
                reserved_hud_art=[4, hud, 0xe0], hud_attributes=[hud_bank, hud_attributes+5, 40])


def fixed_title(title, original, view, arrays):
    bank, cursor = (0x27, 0x4c78) if title == 3 else (0x3e, 0x40a2)
    start = cursor
    vram1 = bytearray(arrays['menu_boot_vram'])
    layout = [0]*16
    commands = []
    def take(n):
        nonlocal cursor
        out = view.data(bank, cursor, n)
        cursor += n
        return out
    for _ in range(128):
        command_start = cursor
        op = take(1)[0]
        if not op:
            break
        if op in (1, 2):
            count = take(1)[0]
            if count > 8:
                raise ValueError('Title palette count exceeds hardware slots')
            values = take(count*2)
            for i in range(count):
                layout[(0 if op == 1 else 8)+i] = int.from_bytes(values[i*2:i*2+2], 'little')
            commands.append([command_start, op, count])
        elif op in (3, 4):
            header = take(4)
            destination, rows, cols = int.from_bytes(header[:2], 'little'), header[2], header[3]
            payload = take(1 if op == 3 else rows*cols)
            if not rows or not cols or cols > 32 or rows > 32 or destination < 0x9800 or destination+32*(rows-1)+cols > 0xa000:
                raise ValueError('Title attribute rectangle exceeds PPU map')
            for y in range(rows):
                for x in range(cols):
                    vram1[destination-0x8000+y*32+x] = payload[0 if op == 3 else y*cols+x]
            commands.append([command_start, op, destination, rows, cols])
        elif op in (5, 7):
            header = take(4)
            destination, count = int.from_bytes(header[:2], 'little'), int.from_bytes(header[2:], 'little')
            if not count or destination < 0x8000 or destination+count > 0xa000:
                raise ValueError('Title VRAM1 data exceeds PPU memory')
            payload = take(count) if op == 5 else bytes(count)
            vram1[destination-0x8000:destination-0x8000+count] = payload
            commands.append([command_start, op, destination, count])
        else:
            raise ValueError('Title style script requires an explicit native service')
    else:
        raise ValueError('Unterminated title style data')
    native, art = bytearray(8192), bytearray(8192)
    art_bank = 6 if title == 3 else 0x10
    map_bank, map_source, blank = (4, 0x7bcf, 0x6c) if title == 3 else (0x10, 0x4480, 0x62)
    oam_bank, oam_source = (0x27, 0x4f34) if title == 3 else (0x10, 0x7d9d)
    regions = ([(0x404c, 0x9000, 0x670), (0x46bc, 0x8800, 0x400), (0x403c, 0x8000, 0x10)] if title == 3 else
               [(0x4629, 0x9000, 0x630), (0x4080, 0x8800, 0x400), (0x4619, 0x8000, 0x10)])
    for source, destination, count in regions:
        at = art_bank*0x4000+source-0x4000
        native[destination-0x8000:destination-0x8000+count] = original[at:at+count]
        art[destination-0x8000:destination-0x8000+count] = view.data(art_bank, source, count)
    header = view.data(map_bank, map_source, 2)
    cols, rows = header
    if (cols, rows) != (20, 18):
        raise ValueError('Unexpected authored title dimensions')
    cells = view.data(map_bank, map_source+2, cols*rows)
    titlemap = bytearray([blank])*1024
    for y in range(rows):
        titlemap[y*32:y*32+cols] = cells[y*cols:y*cols+cols]
    native_map = bytearray([blank])*1024
    at = map_bank*0x4000+map_source+2-0x4000
    original_cells = original[at:at+360]
    for y in range(18):
        native_map[y*32:y*32+20] = original_cells[y*20:y*20+20]
    native[0x1800:0x1c00] = native_map
    art[0x1800:0x1c00] = titlemap
    arrays['title_original_vram'] = list(native)
    arrays['title_color_vram'] = list(art)
    arrays['title_vram1'] = list(vram1)
    arrays['title_map'] = list(titlemap)
    arrays['title_palettes'] = layout
    arrays['title_extra_oam'] = list(view.data(oam_bank, oam_source, 44))
    return dict(title_style_script=[bank, start, cursor-start], title_style_commands=commands,
                title_art_bank=art_bank, title_art_copies=regions, title_map_range=[map_bank, map_source, 362],
                title_extra_oam_range=[oam_bank, oam_source, 44])


def final_thanks3(original, view, arrays):
    native, art, attrs = bytearray(8192), bytearray(8192), bytearray(8192)
    # Original ending loaders plus the native walking/meteor resources.
    regions = [(6, 0x6772, 0x8000, 0x420), (6, 0x46bc, 0x9200, 0x400),
               (0, 0x00c0, 0x9000, 0x40), (14, 0x655f, 0x8800, 0x120),
               (4, 0x48a6, 0x8920, 0xd0), (15, 0x7c40, 0x8780, 0x30)]
    for bank, source, destination, count in regions:
        at = source if source < 0x4000 else bank*0x4000+source-0x4000
        native[destination-0x8000:destination-0x8000+count] = original[at:at+count]
        payload = bytes(view.byte(source+i) for i in range(count)) if bank == 0 else view.data(bank, source, count)
        art[destination-0x8000:destination-0x8000+count] = payload
    extra_art = [(0x11, 0x447b, 0x8fd0, 0x40), (0x11, 0x44bb, 0x8420, 0x20)]
    for bank, source, destination, count in extra_art:
        art[destination-0x8000:destination-0x8000+count] = view.data(bank, source, count)
    # Authored6:7C36 fills the final map with tile3, then keeps the original
    # four text-row loader.11:4556 assigns the six CAPCOM cells palette1.
    art[0x1800:0x1a34] = bytes([3])*0x234
    cursor = 0x776b
    rows = view.data(6, cursor, 1)[0]
    cursor += 1
    if rows != 4:
        raise ValueError('Unexpected final thank-you row count')
    records = []
    for _ in range(rows):
        count = view.data(6, cursor, 1)[0]
        destination = view.word(6, cursor+1)
        cursor += 3
        if not count or not 0x9800 <= destination < destination+count <= 0x9c00:
            raise ValueError('Invalid final thank-you text row')
        art[destination-0x8000:destination-0x8000+count] = view.data(6, cursor, count)
        records.append([destination, count])
        cursor += count
    attrs[0x1984:0x198a] = bytes([1])*6
    layout = [view.word(0x11, 0x44fb+i*2) if i < 13 else 0 for i in range(16)]
    layout[:8] = [view.word(0x11, 0x4578+i*2) for i in range(8)]
    layout[8] = view.word(6, 0x7fb9)  # final walking scene6:7F91 table OBJ0
    if layout[8] != 0x4880 or layout[9] != 0x488c:
        raise ValueError('Unidentified final ending object palette')
    # Earlier large-Wily animation changes are outside this final scene.
    # The final actor and meteor texture ranges above are bound to their
    # actual original final-scene VRAM copies, not a color-by-tile heuristic.
    for name, values in [('ending_original' , native), ('ending_art', art), ('ending_attrs', attrs), ('ending_palettes', layout)]:
        arrays[name] = list(values)
    return dict(final_thanks_art=regions, final_thanks_extra_art=extra_art, final_thanks_rows=records,
                final_thanks_palette_sources=[[0x11, 0x44fb, 26], [0x11, 0x4578, 16], [6, 0x7fb9, 2]],
                final_thanks_attributes=[0x9984, 6, 1])


def credits3(original, view, arrays):
    """Finite data used by the original6:779E credits controller hooks."""
    arrays['credits_palettes'] = []
    for bank, address, count in [(0x11,0x4467,11),(0x11,0x44fb,13),(6,0x7fa9,9),
                                  (0x11,0x4536,8),(0x11,0x4546,8)]:
        arrays['credits_palettes'].extend([view.word(bank,address+n*2) if n<count else 0 for n in range(16)])
    arrays['credits_map_sources'], arrays['credits_map_attributes'] = [], []
    arrays['credits_map_original'], arrays['credits_map_art'] = [], []
    for source in (0x6cab,0x7603):
        arrays['credits_map_sources'].extend([source,0x168,len(arrays['credits_map_attributes'])])
        arrays['credits_map_attributes'].extend(view.data(0x14,source,0x168))
        physical=6*0x4000+source-0x4000
        arrays['credits_map_original'].extend(original[physical:physical+0x168])
        arrays['credits_map_art'].extend(view.data(6,source,0x168))
    return dict(credits_controller=[6,0x779e],credits_palette_tables=[[0x11,0x4467,11],[0x11,0x44fb,13],[6,0x7fa9,9],[0x11,0x4536,8],[0x11,0x4546,8]],credits_parallel_map_attributes=[[0x14,source,0x168] for source in (0x6cab,0x7603)])


def final_thanks4(original, view, arrays):
    native, art = bytearray(8192), bytearray(8192)
    regions = [(0x6e1c, 0x9000, 0x410), (0x6818, 0x8000, 0xd0),
               (0x66fe, 0x80d0, 0x80), (0x6995, 0x8150, 0xd0)]
    for source, destination, count in regions:
        at = 0x14*0x4000+source-0x4000
        native[destination-0x8000:destination-0x8000+count] = original[at:at+count]
        art[destination-0x8000:destination-0x8000+count] = view.data(0x14, source, count)
    attrs, layout, cursor = bytearray(arrays['menu_boot_vram']), [0]*16, 0x5d79
    commands = []
    def take(count):
        nonlocal cursor
        out = view.data(0x3e, cursor, count)
        cursor += count
        return out
    for _ in range(128):
        at, op = cursor, take(1)[0]
        if not op:
            break
        if op in (1, 2):
            count = take(1)[0]
            if count > 8:
                raise ValueError('Final-screen palette count exceeds hardware slots')
            for i in range(count):
                layout[(0 if op == 1 else 8)+i] = int.from_bytes(take(2), 'little')
            commands.append([at, op, count])
        elif op in (3, 4):
            header = take(4)
            destination, rows, cols = int.from_bytes(header[:2], 'little'), header[2], header[3]
            if not rows or not cols or rows > 32 or cols > 32 or destination < 0x9800 or destination+32*(rows-1)+cols > 0xa000:
                raise ValueError('Final-screen attribute rectangle escapes native map')
            payload = take(1 if op == 3 else rows*cols)
            for y in range(rows):
                for x in range(cols):
                    attrs[destination-0x8000+y*32+x] = payload[0 if op == 3 else y*cols+x]
            commands.append([at, op, destination, rows, cols])
        elif op in (5, 7):
            header = take(4)
            destination, count = int.from_bytes(header[:2], 'little'), int.from_bytes(header[2:], 'little')
            if not count or destination < 0x8000 or destination+count > 0xa000:
                raise ValueError('Final-screen texture copy escapes native VRAM')
            attrs[destination-0x8000:destination-0x8000+count] = take(count) if op == 5 else bytes(count)
            commands.append([at, op, destination, count])
        else:
            raise ValueError('Final-screen style requires an explicit native service')
    else:
        raise ValueError('Unterminated final-screen style')
    old_map = original[0x14*0x4000+0x28ec:0x14*0x4000+0x28ec+53]
    new_map = view.data(0x14, 0x7d98, 57)
    if old_map[-1] != 128 or new_map[-1] != 128:
        raise ValueError('Unidentified final Rockman source map')
    arrays['ending_original'], arrays['ending_art'], arrays['ending_attrs'] = list(native), list(art), list(attrs)
    arrays['ending_palettes'], arrays['ending_actor_native'], arrays['ending_actor_map'] = layout, list(old_map), list(new_map)
    return dict(final_thanks_art=[[0x14, *r] for r in regions], final_thanks_style=[0x3e, 0x5d79, cursor-0x5d79], final_thanks_style_commands=commands,
                final_thanks_actor_native=[0x14, 0x68ec, 53], final_thanks_actor_map=[0x14, 0x7d98, 57], final_thanks_actor_origin=[128, 88])


def iv_cast_data(original, view, arrays):
    # Exact DATA at the two authored ending callbacks; no runtime script VM.
    portrait = view.data(0x3e, 0x5d15, 36)
    if portrait[:5] != bytes.fromhex('04009a0506') or portrait[-1] != 0 or set(portrait[5:-1]) != {3}:
        raise ValueError('Unidentified IV portrait attribute rectangle')
    staff = view.data(0x3e, 0x5d6c, 13)
    if staff != bytes.fromhex('01021049104903009c03200000'):
        raise ValueError('Unidentified IV cast-name style')
    arrays['iv_charge_palette_lows'] = list(view.data(6,0x5a63,8))
    if arrays['iv_charge_palette_lows'] != [0x68,0x6e,0x74,0,0,0x7a,0x80,0x86]:
        raise ValueError('IV charged OBJ1 palette table changed')
    arrays['iv_portrait_attributes'] = list(portrait[5:-1])
    arrays['iv_cast_palettes'] = [view.word(0x3e, 0x5d6e+i*2) for i in range(2)]
    arrays['iv_cast_palettes'] += [view.word(0x3e, 0x5e5a+i*2) for i in range(6)]
    return dict(iv_portrait_style=[0x3e,0x5d15,36], iv_cast_name_style=[0x3e,0x5d6c,13],
                iv_native_cast_initializer=[0x14,0x5590], iv_native_portrait_callback=[0x12,0x71b3],
                iv_native_name_callback=[0x14,0x56b1], iv_cast_font=[0x14,0x5c66,0x1a0])


def background_data(title, original, view, arrays):
    """Pair the original graphics packets with their authored destination tiles.

    The final-machine graphics move banks, so matching the same donor source
    address is insufficient. Records retain the original copy-service inputs.
    """
    table, stages = (0x315f, 12) if title == 3 else (0x337a, 23)
    def raw(bank, address, count):
        at = address if address < 0x4000 else bank*0x4000+address-0x4000
        return original[at:at+count]
    def authored(bank, address, count):
        return bytes(view.byte(address+i) for i in range(count)) if address < 0x4000 else view.data(bank, address, count)
    def packets(stage, read):
        entry = read(0, table+stage*6, 6)
        if entry[0] == 255:
            return [], {}
        bank, descriptor = entry[0], int.from_bytes(entry[1:3], 'little')
        address = int.from_bytes(read(bank, descriptor, 2), 'little')
        records, tiles = [], {}
        for _ in range(32):
            count, tile, lo, hi = read(bank, address, 4)
            if not count:
                return records, tiles
            source = lo | hi << 8
            if not 0x4000 <= source < source+count*16 <= 0x8000 or tile+count > 128:
                raise ValueError('Stage graphics packet escapes identified BG art')
            pixels = read(bank, source, count*16)
            records.append((bank, source, tile, count))
            for n in range(count):
                tiles[tile+n] = pixels[n*16:(n+1)*16]
            address += 4
        raise ValueError('Unterminated stage graphics packets')

    pairs, stage_records = {}, []
    arrays['background_extra_keys'], arrays['background_extra_art'] = [], []
    for stage in range(1, stages):
        native, old_tiles = packets(stage, raw)
        _, new_tiles = packets(stage, authored)
        if old_tiles.keys()-new_tiles.keys():
            raise ValueError('Authored stage removes a native BG slot')
        for tile in sorted(new_tiles.keys()-old_tiles.keys()):
            # These authored-only slots may replace a clear native tile only.
            # The renderer checks all16 original VRAM bytes before using them.
            arrays['background_extra_keys'].append(stage*256+tile)
            arrays['background_extra_art'].extend(new_tiles[tile])
        for bank, source, tile, count in native:
            before = raw(bank, source, count*16)
            after = b''.join(new_tiles[tile+n] for n in range(count))
            key = bank, source, count*16
            if key in pairs and pairs[key] != (before, after):
                raise ValueError('Ambiguous original stage graphics copy')
            pairs[key] = before, after
            stage_records.append([stage, bank, source, tile, count])
    # Both original loaders also copy these eleven common BG/HUD tiles.
    common_source = int.from_bytes(raw(0x0c, 0x4000, 2), 'little')
    target = view.word(0x0c, 0x4000)
    pairs[0x0c, common_source, 0xb0] = raw(0x0c, common_source, 0xb0), view.data(0x0c, target, 0xb0)
    animations = []
    if title == 3:
        # Exact raw copies made by6:77F8/7BD1 and the fixed-bank blank glyph.
        # Original source identity and completed VRAM bytes fence each binding.
        for bank, source, count in [(6,0x6772,0x420),(6,0x46bc,0x400),(6,0x6e13,0xb0),(6,0x6ec3,0x740),(0,0xc0,0x40),(15,0x4da2,0x700),(15,0x7c40,0x30)]:
            pairs[bank,source,count] = raw(bank,source,count), authored(bank,source,count)
        # Original Spark-room graphics animation: the actual native178B
        # producer cycles these eight64-byte copies during display. Freeze
        # each paired source, retaining its original destination and timer.
        bank, pointer = 8, 0x7927
        animation_table = int.from_bytes(raw(bank, pointer, 2), 'little')
        target_table = view.word(bank, pointer)
        for index in range(64):
            at, target_at = animation_table+index*7, target_table+index*7
            if raw(bank, at, 1) == b'\xff':
                if view.data(bank, target_at, 1) != b'\xff':
                    raise ValueError('Authored BG animation changes its record count')
                break
            record, color_record = raw(bank, at, 7), view.data(bank, target_at, 7)
            destination = int.from_bytes(record[:2], 'little')
            count = int.from_bytes(record[2:4], 'little')
            old_source, new_source = int.from_bytes(record[4:6], 'little'), int.from_bytes(color_record[4:6], 'little')
            if record[:4] != color_record[:4] or record[6] != color_record[6] or not 0x9000 <= destination < destination+count <= 0x9800 or count&15 or not 0x4000 <= old_source < old_source+count <= 0x8000:
                raise ValueError('Unidentified authored BG animation behavior')
            pairs[bank, old_source, count] = raw(bank, old_source, count), view.data(bank, new_source, count)
            animations.append([bank, at, old_source, new_source, destination, count, record[6]])
        else:
            raise ValueError('Unterminated original BG animation')
    else:
        # Original final14:6B3B loads the background, while722C cycles the
        # last seven tiles. The original copy services/timers remain active.
        for source, count in [(0x6e1c, 0x410), (0x7242, 0x70), (0x72b2, 0x70), (0x7322, 0x70)]:
            pairs[0x14, source, count] = raw(0x14, source, count), view.data(0x14, source, count)
        # Original0F:4064 loads the weapon-award picture through0183.
        # This raw BG payload is not a generic actor resource.
        if raw(0x0f,0x4064,12) != view.data(0x0f,0x4064,12):
            raise ValueError('Weapon-award raw loader needs an explicit native binding')
        pairs[0x0f,0x40c5,0x6d0] = raw(0x0f,0x40c5,0x6d0),view.data(0x0f,0x40c5,0x6d0)
        # Original0B:5650/5665/5671 space-escape display copies. Its native
        # 5808 map is unchanged; only these three source payloads differ.
        for source,count in [(0x5972,0x70),(0x4e36,0x3b0),(0x59e2,0x130)]:
            pairs[0x0b,source,count] = raw(0x0b,source,count),view.data(0x0b,source,count)
    # Native background-effect initializers use0171 to select a word from
    # their frame-stream animation_lookup, then store timer/pointer/index at actor+28.
    # Bind that existing finite service contract, including relocated art;
    # original controllers, destinations, frame counts and timers still run.
    animation_initializers = []
    suffix = bytes.fromhex('cd71013e28816f36002c732c72')
    def animation_stream(bank, address):
        if not 0x4000 <= address < 0x7ff8:
            return None
        records = []
        for index in range(128):
            at = address+index*7
            if at+7 > 0x8000:
                return None
            if raw(bank, at, 1) == b'\xff':
                return records if records else None
            record = raw(bank, at, 7)
            destination, count, source = [int.from_bytes(record[i:i+2], 'little') for i in (0, 2, 4)]
            if not 0x9000 <= destination < destination+count <= 0x9800 or count&15 or not 0x4000 <= source < source+count <= 0x8000:
                return None
            records.append(record)
        return None
    for bank in range(1, len(original)//0x4000):
        code = raw(bank, 0x4000, 0x4000)
        for offset in range(len(code)-len(suffix)-3):
            if code[offset] != 0x11 or code[offset+3:offset+3+len(suffix)] != suffix:
                continue
            animation_lookup = int.from_bytes(code[offset+1:offset+3], 'little')
            authored_code = view.data(bank, offset+0x4000, 6)
            if authored_code[0] != 0x11 or authored_code[3:] != code[offset+3:offset+6]:
                raise ValueError('Background-animation initializer needs an explicit native semantic port')
            target_table = int.from_bytes(authored_code[1:3], 'little')
            entries = next((n for n in range(1, 17) if raw(bank, animation_lookup+n*2, 4) == bytes((0x7a, 0xfe, n, 0xd0))), None)
            if entries is None:
                raise ValueError('Background-animation lookup has no identified native bound')
            for entry in range(entries):
                address = int.from_bytes(raw(bank, animation_lookup+entry*2, 2), 'little')
                records = animation_stream(bank, address)
                if records is None:
                    raise ValueError('Native bounded background-animation stream is invalid')
                target = view.word(bank, target_table+entry*2)
                for index, record in enumerate(records):
                    color_record = view.data(bank, target+index*7, 7)
                    if record[:4] != color_record[:4] or record[6] != color_record[6]:
                        raise ValueError(f'Background-animation destination/count/timer needs native handling: title={title} bank={bank} pc={offset+0x4000:04x} animation_lookup={animation_lookup:04x} index={index}')
                    source, color_source = int.from_bytes(record[4:6], 'little'), int.from_bytes(color_record[4:6], 'little')
                    count, destination = int.from_bytes(record[2:4], 'little'), int.from_bytes(record[:2], 'little')
                    if not 0x4000 <= color_source < color_source+count <= 0x8000:
                        raise ValueError('Background-animation authored art escapes its bank')
                    value = raw(bank, source, count), view.data(bank, color_source, count)
                    key = bank, source, count
                    if key in pairs and pairs[key] != value:
                        raise ValueError('Conflicting authored background-animation texture')
                    pairs[key] = value
                    binding = [bank, address+index*7, source, color_source, destination, count, record[6]]
                    if binding not in animations:
                        animations.append(binding)
                if view.data(bank, target+len(records)*7, 1) != b'\xff':
                    raise ValueError('Background-animation authored stream changes its native length')
                animation_initializers.append([bank, offset+0x4000, animation_lookup, target_table, entry, len(records)])
    # Resource2162/1ABF copies share the same native VRAM-copy service.
    # Pair their DATA by original resource ID even when a lab/room uses that
    # resource as BG art. The native loader publishes appended authored tiles
    # from actor_art after its original prefix has finished copying.
    resource_background = []
    arrays['resource_native_counts'] = [0]*(71 if title == 3 else 114)
    resource_table, resource_bank, resource_count = (0x3bed, 0, 71) if title == 3 else (0x41fc, 1, 114)
    for resource in range(1, resource_count):
        header = raw(resource_bank, resource_table+resource*3, 3)
        bank, address = header[0], int.from_bytes(header[1:], 'little')
        address = int.from_bytes(raw(bank, address, 2), 'little')
        count, source = raw(bank, address, 1)[0], int.from_bytes(raw(bank, address+1, 2), 'little')
        target = authored(resource_bank, resource_table+resource*3, 3)
        target_bank, target_address = target[0], int.from_bytes(target[1:], 'little')
        target_address = view.word(target_bank, target_address)
        target_count, target_source = view.data(target_bank, target_address, 1)[0], view.word(target_bank, target_address+1)
        if target_count < count:
            raise ValueError('Resource tile-count reduction needs an explicit native presentation binding')
        arrays['resource_native_counts'][resource] = count
        shared = count*16
        if not shared:
            continue
        if not 0 <= source < source+shared <= (0x4000 if source<0x4000 else 0x8000) or not 0 <= target_source < target_source+shared <= (0x4000 if target_source<0x4000 else 0x8000):
            raise ValueError(f'Resource presentation art escapes its original bank: title={title} resource={resource} native={bank}:{source:04x}/{count} authored={target_bank}:{target_source:04x}/{target_count}')
        value = raw(bank, source, shared), authored(target_bank, target_source, shared)
        key = (0 if source<0x4000 else bank), source, shared
        if key in pairs and pairs[key] != value:
            raise ValueError(f'Resource BG art needs contextual binding: title={title} resource={resource}')
        pairs[key] = value
        resource_background.append([resource, key[0], source, shared, count*16, target_bank, target_source])
    if title == 3:
        # Raw1C55/1C93 packet lists referenced by the exact story/award
        # controllers. Pair by resource and destination, including relocations.
        for resource in (4,13,14,15,16):
            old=int.from_bytes(raw(0,0x1f23+resource*2,2),'little')
            new=int.from_bytes(authored(0,0x1f23+resource*2,2),'little')
            for _ in range(16):
                a,b=raw(0,old,6),authored(0,new,6)
                source,target=int.from_bytes(a[:2],'little'),int.from_bytes(b[:2],'little')
                if not source:
                    if target:raise ValueError('Story raw graphics stream grew')
                    break
                if a[2:4]!=b[2:4] or a[5]!=b[5]:raise ValueError('Story raw graphics destination/count changed')
                count=a[3]*16
                if not count or source+count>0x8000 or target+count>0x8000:raise ValueError('Story raw graphics payload range')
                key=a[4],source,count
                value=raw(a[4],source,count),authored(b[4],target,count)
                if key in pairs and pairs[key]!=value:raise ValueError('Story raw graphics source needs contextual binding')
                pairs[key]=value
                old+=6;new+=6
            else:raise ValueError('Story raw graphics stream has no terminator')
    # The cache can match containing source ranges. Require every overlap to
    # name identical original/authored bytes, so lookup order cannot choose
    # a different texture for any native copy.
    keys = sorted(pairs)
    for index, left in enumerate(keys):
        for right in keys[index+1:]:
            if left[0] != right[0]:
                continue
            first, end = max(left[1], right[1]), min(left[1]+left[2], right[1]+right[2])
            if first < end:
                for image in (0, 1):
                    if pairs[left][image][first-left[1]:end-left[1]] != pairs[right][image][first-right[1]:end-right[1]]:
                        raise ValueError('Overlapping graphics source ranges need contextual native binding')
    if title == 4:
        pairs[0x14,0x5c66,0x1a0] = raw(0x14,0x5c66,0x1a0), view.data(0x14,0x5c66,0x1a0)
    arrays['background_records'], arrays['background_original'], arrays['background_art'] = [], [], []
    for (bank, source, count), (before, after) in sorted(pairs.items()):
        arrays['background_records'].extend([bank, source, count, len(arrays['background_art'])])
        arrays['background_original'].extend(before)
        arrays['background_art'].extend(after)
    if not arrays['background_extra_keys']:
        arrays['background_extra_keys'], arrays['background_extra_art'] = [0], [0]*16

    # Counts are the original content-table boundaries already used by the
    # systematic stage/checkpoint audit, rather than a scan for plausible data.
    checkpoints = ({**{n: 3 for n in range(1, 11)}, 11: 4} if title == 3 else
                   {**{n: 3 for n in range(1, 11)}, 11: 1, 12: 3, 13: 2,
                    14: 3, 15: 3, 16: 1, 17: 3, 18: 1, 19: 3, 21: 3})
    metatiles = {}
    def blocks(stage, checkpoint, read):
        entry = read(0, table+stage*6, 6)
        bank, descriptor = entry[3], int.from_bytes(entry[4:6], 'little')
        header = int.from_bytes(read(bank, descriptor, 2), 'little')
        check_table = int.from_bytes(read(bank, header, 2), 'little')
        record = int.from_bytes(read(bank, check_table+checkpoint*2, 2), 'little')
        address = int.from_bytes(read(bank, record+1, 2), 'little')
        count = read(bank, address, 1)[0]
        if not 0x4000 <= address < address+1+count*4 <= 0x8000:
            raise ValueError('Metatile table escapes identified level data')
        return address+1, read(bank, address+1, count*4)
    for stage, count in checkpoints.items():
        for checkpoint in range(count):
            address, before = blocks(stage, checkpoint, raw)
            _, after = blocks(stage, checkpoint, authored)
            if len(before) != len(after):
                raise ValueError('Authored metatile count needs an explicit binding')
            key = stage, address
            if key in metatiles and metatiles[key] != (before, after):
                raise ValueError('Ambiguous original metatile table')
            metatiles[key] = before, after
    arrays['metatile_records'], arrays['metatile_original'], arrays['metatile_art'] = [], [], []
    for (stage, address), (before, after) in sorted(metatiles.items()):
        arrays['metatile_records'].extend([stage, address, len(before), len(arrays['metatile_art'])])
        arrays['metatile_original'].extend(before)
        arrays['metatile_art'].extend(after)
    return dict(original_stage_graphics_table=table, background_packet_bindings=stage_records,
                common_background_copy=[0x0c, common_source, 0x9040, 0xb0],
                background_copy_records=arrays['background_records'],
                background_animation_bindings=animations, background_animation_initializers=animation_initializers, background_resource_bindings=resource_background,
                background_extra_keys=arrays['background_extra_keys'],
                metatile_bindings=arrays['metatile_records'])


def managed_styles4(original, view, arrays):
    # These are the finite data streams named by original-controller wrappers
    # which call fixed0670 ->3E:5E66. Ending5D15/5D6C and final5D79 retain their
    # explicit implementations. The original game keeps every controller.
    styles = [0x402e,0x4083,0x40a2,0x4332,0x43f6,0x4591,0x4751,0x47b4,0x47bb,0x4935,0x4986,0x4a66,0x4bad,0x4d31,0x4df0,0x4e64,0x4ef2,0x4f0d,0x5123,0x512a,0x5335,0x54bd,0x5658,0x5981,0x5ae3,0x5b69,0x5bab,0x5bed,0x5cb5,0x5cd3,0x5cf1,0x5d39,0x5e1d]
    arrays['iv_style_addresses'] = styles
    old=original[0x0f*0x4000+0x4795-0x4000:0x0f*0x4000+0x4795-0x4000+382]
    new=view.data(0x0f,0x4795,382)
    if old[:2]!=bytes([20,19]) or new[:2]!=old[:2]:raise ValueError('Weapon-award picture map dimensions')
    arrays['iv_award_map_original'],arrays['iv_award_map_art']=list(old[2:]),list(new[2:])
    arrays['iv_award_object_palettes']=[view.word(0x20,0x61be+stage*4+slot*2) for stage in range(8) for slot in range(2)]+[view.word(0x20,0x61de+slot*2) for slot in range(4)]
    arrays['iv_style_palettes'],arrays['iv_style_attributes'] = [],[]
    arrays['iv_style_texture_records'],arrays['iv_style_textures'] = [],[]
    source=[]
    for index, style in enumerate(styles):
        cursor=style
        def take(count):
            nonlocal cursor
            if not 0x4000 <= cursor <= cursor+count <= 0x8000:
                raise ValueError('Managed style escapes its identified source bank')
            payload=view.data(0x3e,cursor,count);cursor+=count;return payload
        palettes,attrs=[65535]*16,[255]*2048
        for _ in range(128):
            op=take(1)[0]
            if not op:
                break
            if op in (1,2):
                count=take(1)[0]
                if not 0<count<=8:raise ValueError('Managed style palette count')
                for n in range(count):palettes[(8 if op==2 else 0)+n]=int.from_bytes(take(2),'little')
            elif op in (3,4):
                header=take(4);dest=int.from_bytes(header[:2],'little');rows,cols=header[2:]
                if not rows or not cols or rows>32 or cols>32 or dest<0x9800 or dest+(rows-1)*32+cols>0xa000:raise ValueError('Managed style attribute rectangle')
                payload=take(1 if op==3 else rows*cols)
                if 255 in payload:raise ValueError('Managed style uses the attribute sentinel')
                for y in range(rows):
                    for x in range(cols):attrs[dest-0x9800+y*32+x]=payload[0 if op==3 else y*cols+x]
            elif op in (5,7):
                header=take(4);dest,count=int.from_bytes(header[:2],'little'),int.from_bytes(header[2:],'little')
                if not count or dest<0x8000 or dest+count>0x9800:raise ValueError('Managed style texture range')
                payload=take(count) if op==5 else bytes(count)
                arrays['iv_style_texture_records'].extend([index,dest-0x8000,count,len(arrays['iv_style_textures'])]);arrays['iv_style_textures'].extend(payload)
            else:raise ValueError(f'Managed style{style:04x} requires an explicit native behavior for op{op}')
        else:raise ValueError('Managed style source has no terminator')
        arrays['iv_style_palettes'].extend(palettes);arrays['iv_style_attributes'].extend(attrs);source.append([style,cursor-style])
    return dict(managed_style_source=[0x3e,0x5e66],managed_style_records=source)


def story_styles3(original, view, arrays):
    # Red trap's next closing pose retains one more solid standing frame.
    # Original native2822 resolves the pose header;288C/2895/289F read its
    # first three bounds before unchanged native rectangle arithmetic.
    at=9*0x4000+0x3419
    if original[at:at+4] != bytes.fromhex('00000000') or view.data(9,0x7419,4) != bytes.fromhex('fa11f800'):
        raise ValueError('Red-trap standing rectangle source contract changed')
    if original[9*0x4000+0x33df:9*0x4000+0x33ec] != bytes.fromhex('0400020406080a0a08060402ff') or view.data(9,0x73df,13) != bytes.fromhex('0400020406080a0a08060402ff'):
        raise ValueError('Red-trap original timer sequence changed')
    if original[9*0x4000+0x33f0:9*0x4000+0x33f4] != bytes.fromhex('19741974') or view.data(9,0x73f0,4) != bytes.fromhex('19741974'):
        raise ValueError('Red-trap standing pose binding changed')
    if any(original[pc] != 0x2a for pc in (0x288c,0x2895,0x289f)):
        raise ValueError('Red-trap native consumed-register boundary changed')

    # Finite Packatto24 animator data consumed by the exact native service.
    if original[9*0x4000+0x22b5:9*0x4000+0x22ba] != bytes.fromhex('08020404ff') or view.data(9,0x62b5,5) != bytes.fromhex('0a020402ff'):
        raise ValueError('Packatto24 animation source contract changed')

    # Fixed0663 preserves the original map-resource A around1CEE, then
    # dispatches27:4000 through4FEF. Board/password input keep their explicit
    # implementations; these six finite records cover the other native UI maps.
    # Native Dust room copy keeps all512 original reads; only these exact
    # authored map-cell differences are consumed after the copy completes.
    before=original[10*0x4000+0x2a9d:10*0x4000+0x2c9d];after=view.data(10,0x6a9d,512)
    offsets=[64,65,92,93,124,125,152,153,212,213]
    if [i for i,(a,b) in enumerate(zip(before,after)) if a!=b] != offsets or [before[i] for i in offsets] != [42,43,42,43,44,45,42,43,44,45] or [after[i] for i in offsets] != [44,45,44,45,0,0,0,0,0,0]:
        raise ValueError('Dust room authored map source contract changed')
    arrays['story_overlay_oam']=list(view.data(0x11,0x4129,28))
    if any(not arrays['story_overlay_oam'][n*4+3]&8 for n in range(7)):
        raise ValueError('Story award skin overlay no longer selects source bank1')
    resources=(8,10,11,12,14,15)
    arrays['story_map_records'],arrays['story_map_original'],arrays['story_map_art']=[],[],[]
    for resource in resources:
        entry=original[0x206b+resource*4:0x206f+resource*4];target=bytes(view.byte(0x206b+resource*4+i) for i in range(4))
        bank,address=entry[0],int.from_bytes(entry[2:],'little');targetbank,targetaddress=target[0],int.from_bytes(target[2:],'little');at=bank*0x4000+address-0x4000
        before=original[at:at+4];after=view.data(targetbank,targetaddress,4)
        if before!=after or entry[1]!=target[1]:raise ValueError('Story map destination/dimensions changed')
        count=before[0]*before[1];a,b=original[at+4:at+4+count],view.data(targetbank,targetaddress+4,count)
        if resource in (10,12):
            destination=(entry[1]<<8)+before[3]*32+before[2]-0x9800
            if destination<0 or destination+(before[1]-1)*32+before[0]>2048:raise ValueError('Story map region exceeds native PPU map')
            arrays['story_map_records'].extend([destination,before[0],before[1],len(arrays['story_map_original'])]);arrays['story_map_original'].extend(a);arrays['story_map_art'].extend(b)
        elif a!=b:raise ValueError('Additional story map needs an explicit source binding')
    styles=sorted({view.word(0x27,0x4000+n*2) for n in resources})
    arrays['story_map_styles']=[view.word(0x27,0x4000+n*2) if n in resources else 0 for n in range(16)]
    arrays['story_style_addresses'] = styles
    arrays['story_style_palettes'],arrays['story_style_attributes'] = [],[]
    arrays['story_style_texture_records'],arrays['story_style_textures'] = [],[]
    source=[]
    for index, style in enumerate(styles):
        cursor=style
        def take(count):
            nonlocal cursor
            if not 0x4000 <= cursor <= cursor+count <= 0x8000:
                raise ValueError('Managed style escapes its identified source bank')
            payload=view.data(0x27,cursor,count);cursor+=count;return payload
        palettes,attrs=[65535]*16,[255]*2048
        for _ in range(128):
            op=take(1)[0]
            if not op:
                break
            if op in (1,2):
                count=take(1)[0]
                if not 0<count<=8:raise ValueError('Managed style palette count')
                for n in range(count):palettes[(8 if op==2 else 0)+n]=int.from_bytes(take(2),'little')
            elif op in (3,4):
                header=take(4);dest=int.from_bytes(header[:2],'little');rows,cols=header[2:]
                if not rows or not cols or rows>32 or cols>32 or dest<0x9800 or dest+(rows-1)*32+cols>0xa000:raise ValueError('Managed style attribute rectangle')
                payload=take(1 if op==3 else rows*cols)
                if 255 in payload:raise ValueError('Managed style uses the attribute sentinel')
                for y in range(rows):
                    for x in range(cols):attrs[dest-0x9800+y*32+x]=payload[0 if op==3 else y*cols+x]
            elif op == 5:
                header=take(4);dest,count=int.from_bytes(header[:2],'little'),int.from_bytes(header[2:],'little')
                if not count or dest<0x8000 or dest+count>0x9800:raise ValueError('Managed style texture range')
                payload=take(count) if op==5 else bytes(count)
                arrays['story_style_texture_records'].extend([index,dest-0x8000,count,len(arrays['story_style_textures'])]);arrays['story_style_textures'].extend(payload)
            elif op == 6 and style == 0x4066 and int.from_bytes(take(2),'little') == 0x4120:
                # Finite callback27:4120 selects four BG pointers by stage.
                arrays['story_award_palettes']=[view.word(0x27,view.word(0x27,0x4f60+stage*2)+slot*2) for stage in range(11) for slot in range(4)]
                break
            else:raise ValueError(f'Story style{style:04x} requires an explicit native behavior for op{op}')
        else:raise ValueError('Managed style source has no terminator')
        arrays['story_style_palettes'].extend(palettes);arrays['story_style_attributes'].extend(attrs);source.append([style,cursor-style])
    return dict(story_style_source=[0x27,0x4ff6],story_style_records=source,story_native_map_wrapper=[0,0x0663,0x1cee])


def freeze(args):
    lines = ['#pragma once', '#include <cstdint>', 'namespace gbcllr::mm34_dx {']
    provenance = []
    for title, identity, member, patch_id, base, attributes, scenes, resources in INPUTS:
        original = getattr(args, f'original{title}').read_bytes()
        if hashlib.sha256(original).hexdigest() != identity:
            raise ValueError(f'Wrong original Mega Man {title} USA cartridge')
        with zipfile.ZipFile(getattr(args, f'patch{title}')) as archive:
            patch = archive.read(member)
        if hashlib.sha256(patch).hexdigest() != patch_id:
            raise ValueError(f'Wrong official MMW{title}DX patch')
        view = PresentationSlices(original, patch)
        arrays = {}
        arrays['fade_banks'] = list(view.data(base, 0x4000, 256))
        arrays['stage_attributes'] = list(view.data(attributes, 0x4000, scenes*256))
        arrays['stage_layouts'] = []
        pointers = set()
        for stage in range(scenes):
            record = view.word(base, 0x7800+stage*2)
            layout = [view.word(base, record+slot*2) for slot in range(10)]+[0]*6
            pointers.update(layout)
            arrays['stage_layouts'].extend(layout)
        # The original DX loader adjusts the low byte of BG1 and OBJ1 to the
        # current weapon*8. OBJ records contain three visible colors (6 bytes).
        pointers.update(0x4100+weapon*8 for weapon in range(12 if title == 3 else 13))
        pointers.update((0x4700 if title == 3 else 0x4a00)+weapon*8 for weapon in range(12 if title == 3 else 13))
        pointers.add(0x4168 if title == 3 else 0x4170)
        arrays['resource_layouts'] = []
        for resource in range(resources):
            at = view.word(base, 0x7000+resource*2)
            layout = [0]*16
            for _ in range(32):
                destination = view.word(base, at)
                if not destination & 255:
                    break
                if not 0xdf80 <= destination <= 0xdf9e or destination & 1:
                    raise ValueError('Unidentified dynamic palette destination')
                layout[(destination-0xdf80)//2] = view.word(base, at+2)
                at += 4
            else:
                raise ValueError('Unterminated dynamic palette record')
            pointers.update(layout)
            arrays['resource_layouts'].extend(layout)
        menu = menu_data(title, original, view, arrays)
        fixed = fixed_title(title, original, view, arrays)
        select = stage_select3(original, view, arrays) if title == 3 else {}
        ending = final_thanks3(original, view, arrays) if title == 3 else final_thanks4(original, view, arrays)
        if title == 3:
            ending.update(credits3(original,view,arrays))
            pointers.update(p for p in arrays['credits_palettes'] if 0x4100 <= p <= 0x6ff8)
        if title == 4:
            ending.update(iv_cast_data(original,view,arrays))
            pointers.update(arrays['iv_cast_palettes'])
            pointers.update(0x4a00+low for low in arrays['iv_charge_palette_lows'])
        pointers.update(arrays["ending_palettes"])
        if title == 3:
            pointers.update(arrays["select_palettes"])
            pointers.update(arrays["intro_palettes"])
            pointers.update(arrays["password_palettes"])
            pointers.update(arrays["select_alternate_palettes"])
        pointers.update(arrays['title_palettes'])
        story = story_styles3(original, view, arrays) if title == 3 else {}
        if title == 3:
            pointers.update(p for p in arrays['story_style_palettes'] if p != 65535)
            pointers.update(arrays['story_award_palettes'])
        managed = managed_styles4(original, view, arrays) if title == 4 else {}
        if title == 4:
            pointers.update(p for p in arrays['iv_style_palettes'] if p != 65535)
            pointers.update(arrays['iv_award_object_palettes'])
        pointers.discard(0)
        if any(pointer < 0x4100 or pointer > 0x6ff8 for pointer in pointers):
            raise ValueError('Palette pointer escapes identified presentation data')
        arrays['palette_pointers'] = sorted(pointers)
        arrays['palette_banks'] = sorted(set(arrays['fade_banks']))
        if any(not base <= bank < attributes for bank in arrays['palette_banks']):
            raise ValueError('Palette fade selects an unidentified bank')
        arrays['palette_words'] = [view.word(bank, pointer+color*2)
                                   for bank in arrays['palette_banks']
                                   for pointer in arrays['palette_pointers'] for color in range(4)]
        # RST28's native remaster implementation selects the read-only parallel
        # attribute bank through [current-bank:7FFF], then indexes the original
        # grid/extra tile byte address. These banks are attribute data, not code.
        pairs = [(bank, view.byte(bank*0x4000+0x3fff)) for bank in range(1, len(original)//0x4000)]
        pairs = [(bank, pair) for bank, pair in pairs if pair != 255]
        arrays['object_banks'] = [bank for bank, _ in pairs]
        arrays['object_attributes'] = []
        for bank, pair in pairs:
            if view.byte(pair*0x4000+0x3fff) != bank:
                raise ValueError('Parallel object attribute bank has wrong return binding')
            arrays['object_attributes'].extend(view.data(pair, 0x4000, 0x3fff)+b'\0')
        # The reserved player producer keeps executing original code. Its DX
        # data table selects a revised grid/extra-piece record; each tile-byte
        # address selects the corresponding parallel-bank color attribute.
        body_table, body_count = (0x7783, 52) if title == 3 else (0x6f83, 29)
        arrays['body_addresses'] = []
        arrays['body_offsets'] = []
        arrays['body_records'] = []
        body_fallbacks = []
        for frame in range(body_count):
            source = view.word(1, body_table+frame*2)
            fallback = title == 4 and frame == 28
            if fallback:
                # The authored long-beam data moved over the table's last
                # original short-beam record. Retain that original geometry
                # and use the authored long beam's identical 5A tiles/colors.
                if source != 0x7153 or original[0x7153:0x715a] != bytes.fromhex('fcf4135a5a5a80'):
                    raise ValueError('Unrecognized original short-beam fallback')
                body_fallbacks.append(dict(frame=frame, original_record=0x7153, authored_color_record=0x714f))
                source = 0x714f
            def body_bytes(address, count):
                return original[address:address+count] if fallback else view.data(1, address, count)
            body_source = 0x7153 if fallback else source
            arrays['body_addresses'].append(source)
            arrays['body_offsets'].append(len(arrays['body_records']))
            record = bytearray(body_bytes(body_source, 3))
            size = (record[2] >> 4)*(record[2] & 15)
            record.extend(body_bytes(body_source+3, size))
            at = body_source+3+size
            for _ in range(32):
                value = body_bytes(at, 1)[0]
                record.append(value)
                at += 1
                if value == 128:
                    break
                record.extend(body_bytes(at, 2))
                at += 2
            else:
                raise ValueError(f'Unterminated reserved player frame: title={title} frame={frame} source={source:04x} header={record[:3].hex()}')
            arrays['body_records'].extend(record)
        art_source, art_size = (0x40c0, 0x720) if title == 3 else (0x40db, 0x6c0)
        arrays['body_original_art'] = list(original[4*0x4000+art_source-0x4000:4*0x4000+art_source-0x4000+art_size])
        arrays['body_color_art'] = list(view.data(4, art_source, art_size))
        actors = actor_data(title, original, view, arrays)
        background = background_data(title, original, view, arrays)
        for name, values in arrays.items():
            typ = 'uint32_t' if name in ('actor_art_offsets', 'actor_pose_keys', 'actor_pose_offsets', 'background_records', 'metatile_records', 'iv_style_texture_records', 'story_style_texture_records') else 'uint16_t' if name in ('stage_layouts', 'resource_layouts', 'palette_pointers', 'palette_words', 'body_addresses', 'body_offsets', 'actor_pose_addresses', 'menu_bottom', 'menu_top', 'title_palettes', 'select_palettes', 'select_alternate_palettes', 'select_oam_offsets', 'ending_palettes', 'background_extra_keys', 'intro_pose_addresses', 'intro_palettes', 'password_palettes', 'credits_palettes', 'credits_map_sources', 'iv_cast_palettes', 'iv_style_addresses', 'iv_style_palettes', 'story_style_addresses', 'story_style_palettes', 'story_map_styles', 'story_award_palettes', 'iv_award_object_palettes', 'story_map_records') else 'uint8_t'
            lines.append(f'inline constexpr {typ} mm{title}_{name}[{len(values)}]={{')
            lines.extend(','.join(map(str, values[start:start+32]))+',' for start in range(0, len(values), 32))
            lines.append('};')
        provenance.append(dict(title=title, patch_sha256=patch_id, palette_bank_selector=[base, 0x4000, 256],
                               stage_layout_pointer_table=[base, 0x7800, scenes*2],
                               resource_layout_pointer_table=[base, 0x7000, resources*2],
                               stage_attribute_table=[attributes, 0x4000, scenes*256],
                               palette_record_pointers=arrays['palette_pointers'], palette_banks=arrays['palette_banks'],
                               parallel_object_attribute_banks=pairs,
                               reserved_body_table=[1, body_table, body_count*2],
                               reserved_body_data_addresses=arrays['body_addresses'],
                               reserved_body_art=[4, art_source, art_size], reserved_body_fallbacks=body_fallbacks, **actors, **menu, **fixed, **select, **ending, **background, **managed, **story))
    lines.append('}')
    header = ('\n'.join(lines)+'\n').encode()
    args.output.mkdir(parents=True, exist_ok=True)
    (args.output/'mm34_dx_data.hpp').write_bytes(header)
    manifest = dict(format=1, header_sha256=hashlib.sha256(header).hexdigest(),
                    original_rom_sha256=[item[1] for item in INPUTS], versions=['1.2b', '1.1'],
                    credits=['Kensu William (kensuyjin33): graphics', 'Marc Max (marc_max): reverse engineering and coding'],
                    source_urls=[f'https://www.marcrobledo.com/game-boy/mega-man-world-{title}-dx/' for title in (3, 4)],
                    ranges=provenance,
                    scope='Presentation data only; original cartridges opened read-only; no complete patched image, guest code, or donor execution.')
    (args.output/'mm34_dx_data.json').write_text(json.dumps(manifest, indent=2)+'\n')
    print(json.dumps(dict(header_sha256=manifest['header_sha256'], header_bytes=len(header))))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    for title in (3, 4):
        parser.add_argument(f'--original{title}', type=Path, required=True)
        parser.add_argument(f'--patch{title}', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    freeze(parser.parse_args())
