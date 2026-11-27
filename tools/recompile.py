"""Emit conservative, opcode-specialized native ROM entries against timed LLE.

Ordinary entries retain instruction boundaries and bus reads (including
immediates). Qualified native wait operations retain all observable device,
IRQ and host boundaries. Unknown/RAM entries safely use the donor CPU.
Generated output is ROM-derived.
"""
import argparse
import collections
import hashlib
import json
import pathlib
import re
from native_blocks import emit as emit_blocks

ROOT = pathlib.Path(__file__).resolve().parents[1]
LENGTHS = [
    1,3,1,1,1,1,2,1,3,1,1,1,1,1,2,1,
    2,3,1,1,1,1,2,1,2,1,1,1,1,1,2,1,
    2,3,1,1,1,1,2,1,2,1,1,1,1,1,2,1,
    2,3,1,1,1,1,2,1,2,1,1,1,1,1,2,1,
] + [1]*128 + [
    1,1,3,3,3,1,2,1,1,1,3,2,3,3,2,1,
    1,1,3,1,3,1,2,1,1,1,3,1,3,1,2,1,
    2,1,1,1,1,1,2,1,2,1,3,1,1,1,2,1,
    2,1,1,1,1,1,2,1,2,1,3,1,1,1,2,1,
]
ILLEGAL = {0xd3,0xdb,0xdd,0xe3,0xe4,0xeb,0xec,0xed,0xf4,0xfc,0xfd}

def discover(rom, seeds):
    """Follow only direct flow within a known bank; unresolved edges stay safe."""
    seen = set()
    work = list(seeds)
    def physical(bank, address):
        if address < 0x4000:
            return address # bank-0 assumption only affects discovery, never dispatch
        if address < 0x8000:
            return bank * 0x4000 + address - 0x4000
        return len(rom)
    while work:
        pos = work.pop()
        while 0 <= pos < len(rom) and pos not in seen:
            op = rom[pos]
            if op in ILLEGAL or pos + LENGTHS[op] > len(rom):
                break
            seen.add(pos)
            bank, offset = divmod(pos, 0x4000)
            pc = offset + (0x4000 if bank else 0)
            following = pos + LENGTHS[op]
            if offset + LENGTHS[op] >= 0x4000:
                break
            if op in {0xc2,0xc3,0xc4,0xca,0xcc,0xcd,0xd2,0xd4,0xda,0xdc}:
                target = rom[pos+1] | rom[pos+2]<<8
                work.append(physical(bank, target))
            if op in {0x18,0x20,0x28,0x30,0x38}:
                delta = int.from_bytes(rom[pos+1:pos+2], 'little', signed=True)
                work.append(physical(bank, (pc+2+delta)&0xffff))
            if op & 0xc7 == 0xc7:
                work.append(op & 0x38)
            if op in {0xc3,0x18,0xc9,0xd9,0xe9}:
                break
            pos = following
    return seen

def generate(rom, seeds, output, blocks=False, prefix='llr', complete=False):
    source = (ROOT/'third_party/sameboy/Core/sm83_cpu.c').read_text()
    table = source.split('static opcode_t *opcodes[256] = {',1)[1].split('};',1)[0]
    table = re.sub(r'/\*.*?\*/', '', table, flags=re.S)
    handlers = re.findall(r'\b[a-z_][a-z_0-9]*\b', table)
    assert len(handlers) == 256
    entries = discover(rom, seeds)
    banks = {pos//0x4000 for pos in entries}
    # A compact address-to-specialization map avoids a second giant branch tree
    # after the opcode fetch. Each native body sees a constant opcode, letting
    # the compiler fold register/flag selection while retaining timed bus reads.
    out = ['/* ROM-derived generated code; do not redistribute with source. */']
    used = list(range(256)) if complete else sorted({rom[pos] for pos in entries})
    for op in used:
        out += [f'static void {prefix}_op_{op:02x}(GB_gameboy_t *gb) {{ {handlers[op]}(gb, 0x{op:02x}); }}']
    out += [f'static void (*const {prefix}_bodies[256])(GB_gameboy_t*) = {{']
    out += [f'[{op}] = {prefix}_op_{op:02x},' for op in used]
    if complete:
        out += ['};', f'static const uint8_t {prefix}_expected[{len(rom)}] = {{']
        out += [','.join(str(v) for v in rom[i:i+32])+',' for i in range(0,len(rom),32)]
        out += ['};', f'static unsigned {prefix}_native_dispatch(GB_gameboy_t *gb, uint32_t offset, uint8_t opcode) {{']
        # Crystal copies this original ROM routine into HRAM at startup.
        # Bind the copied native entries to their exact original source bytes.
        # Operands remain live timed reads, and writes to either space are valid.
        copied_sources={
            'fdcc3c8c43813cf8731fc037d2a6d191bac75439c34b24ba1c27526e6acdc8a2':(0x403f,0x4049,0xff80),
            '5ca7ba01642a3b27b0cc0b5349b52792795b62d3ed977e98a09390659af96b7b':(0x4bfb,0x4c05,0xff80),
        }
        copied=copied_sources.get(hashlib.sha256(rom).hexdigest())
        if copied:
            out += ['if (offset == UINT32_MAX) {', 'switch ((uint16_t)(gb->pc - 1)) {']
            start,end,destination=copied
            pos=start
            while pos<end:
                out += [f'case 0x{destination+pos-start:04x}: if (opcode != 0x{rom[pos]:02x}) return 0; {prefix}_op_{rom[pos]:02x}(gb); return 1;']
                pos+=LENGTHS[rom[pos]]
            out += ['default: return 0;', '}', '}']
        out += [f'if (offset >= {len(rom)} || {prefix}_expected[offset] != opcode) return 0;']
    else:
        out += ['};', f'static const uint16_t {prefix}_map[{len(rom)}] = {{']
        out += [f'[{pos}] = {rom[pos]+1},' for pos in sorted(entries)]
        out += ['};', f'static unsigned {prefix}_native_dispatch(GB_gameboy_t *gb, uint32_t offset, uint8_t opcode) {{',
                f'if (offset >= {len(rom)} || {prefix}_map[offset] != (unsigned)opcode + 1) return 0;']
    polls=[pos for pos in sorted(entries) if pos%0x4000<=0x4000-6 and rom[pos:pos+6]==bytes([0xf0,0x41,0xe6,2,0x20,0xfa])]
    if polls:
        out += ['#if GBCLLR_NATIVE_WAIT && GBCLLR_FRAME_AUDIO',
                'if (opcode == 0xf0 && ('+' || '.join(f'offset == {pos}' for pos in polls)+')) {',
                'unsigned retired=GB_native_stat_poll_wait(gb); if(retired)return retired;', '}', '#endif']
    out += [f'{prefix}_bodies[opcode](gb); return 1;', '}']
    block_count=0
    if blocks and not complete:
        out,block_count=emit_blocks(rom,entries,LENGTHS,handlers,prefix)
    output.mkdir(parents=True, exist_ok=True)
    (output/'llr_program.inc').write_text('\n'.join(out)+'\n')
    digest = hashlib.sha256(rom).hexdigest()
    write_identities(output,[digest])
    metadata = {'sha256':digest, 'rom_bytes':len(rom), 'native_entries':len(rom) if complete else len(entries),
                'tier':'complete-original-entry-specialization' if complete else 'resumable-native-blocks' if blocks else 'instruction-specialization',
                'banks':len(banks), 'blocks':block_count}
    (output/'program.json').write_text(json.dumps(metadata, indent=2)+'\n')
    return metadata

def write_identities(output, identities):
    (output/'identity.h').write_text(f'#define GBCLLR_PROGRAM_COUNT {len(identities)}\n'
        'static const char *const gbcllr_program_sha256[] = {\n'+
        ''.join(f'"{digest}",\n' for digest in identities)+'};\n')

def symbol_seeds(path):
    seeds=set()
    for line in path.read_text().splitlines():
        match=re.match(r'^([0-9a-fA-F]+):([0-9a-fA-F]{4})\s',line)
        if match:
            bank,address=(int(x,16) for x in match.groups())
            if address<0x8000:
                seeds.add(bank*0x4000+(address&0x3fff))
    return seeds

def generate_catalog(cartridges, output, blocks=False, complete=False):
    """Bind each machine once by exact hash; dispatch keeps its own ROM guard."""
    identities=[hashlib.sha256(rom).hexdigest() for rom,seeds in cartridges]
    if len(set(identities))!=len(identities):
        raise ValueError('duplicate cartridge identity in generated catalogue')
    if len(cartridges)==1:
        return generate(*cartridges[0],output,blocks,complete=complete)
    metadata=[];out=[]
    for i,(rom,seeds) in enumerate(cartridges):
        metadata.append(generate(rom,seeds,output/f'cart_{i}',blocks,f'llr_cart_{i}',complete))
        out.append(f'#include "cart_{i}/llr_program.inc"')
    out+=['static unsigned llr_native_dispatch(GB_gameboy_t *gb, uint32_t offset, uint8_t opcode) {',
          'switch (gb->llr_program_index) {']
    out += [f'case {i}: return llr_cart_{i}_native_dispatch(gb, offset, opcode);' for i in range(len(cartridges))]
    out+=['default: return 0;','}','}']
    (output/'llr_program.inc').write_text('\n'.join(out)+'\n')
    write_identities(output,identities)
    result={'cartridges':metadata}
    (output/'program.json').write_text(json.dumps(result,indent=2)+'\n')
    return result

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--rom', type=pathlib.Path, required=True)
    p.add_argument('--additional-rom', type=pathlib.Path, action='append', default=[],
                   help='Additional exact cartridge; default control-flow discovery')
    p.add_argument('--output', type=pathlib.Path, required=True)
    p.add_argument('--symbols', type=pathlib.Path)
    p.add_argument('--cartridge-symbols', action='append', default=[], metavar='SHA256=PATH',
                   help='RGBDS discovery symbols bound to an exact catalogue ROM hash')
    p.add_argument('--trace', type=pathlib.Path)
    p.add_argument('--blocks', action='store_true')
    p.add_argument('--complete-rom-entries', action='store_true')
    args = p.parse_args()
    rom = args.rom.read_bytes()
    if len(rom)<0x8000 or len(rom)&(len(rom)-1):
        p.error('initial tier requires a power-of-two ROM size of at least 32 KiB')
    seeds = {0x100, *range(0,0x40,8), *range(0x40,0x61,8)}
    if args.symbols:
        seeds.update(symbol_seeds(args.symbols))
    if args.trace:
        seeds.update(int(line,16) for line in args.trace.read_text().splitlines() if line.strip())
    cartridges=[(rom,seeds)]
    for path in args.additional_rom:
        extra=path.read_bytes()
        if len(extra)<0x8000 or len(extra)&(len(extra)-1):
            p.error('additional cartridges require a power-of-two ROM size of at least 32 KiB')
        cartridges.append((extra,{0x100,*range(0,0x40,8),*range(0x40,0x61,8)}))
    by_hash={hashlib.sha256(rom).hexdigest():seeds for rom,seeds in cartridges}
    assigned=set()
    for binding in args.cartridge_symbols:
        digest,separator,path=binding.partition('=')
        digest=digest.lower()
        if not separator or not re.fullmatch('[0-9a-f]{64}',digest) or not path:
            p.error('--cartridge-symbols requires SHA256=PATH')
        if digest not in by_hash:
            p.error('symbol binding does not match a catalogue cartridge: '+digest)
        if digest in assigned:
            p.error('duplicate cartridge symbol binding: '+digest)
        assigned.add(digest)
        by_hash[digest].update(symbol_seeds(pathlib.Path(path)))
    try:
        print(json.dumps(generate_catalog(cartridges,args.output,args.blocks,args.complete_rom_entries)))
    except ValueError as error:
        p.error(str(error))

if __name__ == '__main__':
    main()
