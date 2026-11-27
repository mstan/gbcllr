"""Generate resumable linear native blocks using the donor's timed semantics.

Each guest instruction commits its device work before the next one begins.
The machine keeps all continuations in guest PC/register/device state. Blocks
exit on the caller's deadline, control flow, bank changes, or pending interrupts;
they do not retain guest return addresses on the host C stack.

The runtime helpers are provided by sm83_cpu.c. This emitter is deliberately
separate from discovery so qualification can compare the instruction and block
tiers using exactly the same discovered sites and hardware implementation.
"""

CONTROL = {
    0x10, 0x18, 0x20, 0x28, 0x30, 0x38, 0x76,
    0xc0, 0xc2, 0xc3, 0xc4, 0xc7, 0xc8, 0xc9, 0xca, 0xcc, 0xcd, 0xcf,
    0xd0, 0xd2, 0xd4, 0xd7, 0xd8, 0xd9, 0xda, 0xdc, 0xdf,
    0xe7, 0xe9, 0xef, 0xf3, 0xf7, 0xfb, 0xff,
}


def partition(rom, entries, lengths, maximum=16):
    following = {}
    predecessors = {}
    for pos in entries:
        nxt = pos + lengths[rom[pos]]
        if rom[pos] not in CONTROL and nxt in entries and pos//0x4000 == nxt//0x4000:
            following[pos] = nxt
            predecessors[nxt] = predecessors.get(nxt, 0) + 1
    blocks = []
    visited = set()
    # Sorting ensures an earlier fall-through instruction owns its successor.
    # Entry labels still permit dispatch into any instruction within the block.
    for start in sorted(entries):
        if start in visited:
            continue
        block = []
        pos = start
        while pos not in visited:
            block.append(pos)
            visited.add(pos)
            nxt = following.get(pos)
            if len(block) == maximum or nxt is None or predecessors.get(nxt) != 1:
                break
            pos = nxt
        blocks.append(block)
    return blocks


def emit(rom, entries, lengths, handlers, prefix='llr'):
    blocks = partition(rom, entries, lengths)
    out = ['/* ROM-derived resumable native blocks. */']
    for index, block in enumerate(blocks):
        out += [f'static noinline unsigned {prefix}_block_{index}(GB_gameboy_t *gb, uint32_t offset, uint8_t opcode) {{',
                'unsigned executed = 0;', 'switch (offset) {']
        for pos in block:
            out += [f'case {pos}: goto site_{pos:x};']
        out += ['default: return 0;', '}']
        for i, pos in enumerate(block):
            op = rom[pos]
            out += [f'site_{pos:x}:', f'if (opcode != 0x{op:02x}) goto unexpected_opcode;',
                    f'{handlers[op]}(gb, 0x{op:02x});', '++executed;',
                    'flush_pending_cycles(gb);']
            if i + 1 < len(block):
                nxt = block[i+1]
                pc = (nxt & 0x3fff) + (0x4000 if nxt >= 0x4000 else 0)
                out += [f'if (!llr_can_chain(gb, 0x{pc:04x}, {nxt})) return executed;',
                        '++gb->llr_chained_count;',
                        'GB_instruction_end(gb);', 'GB_set_running_thread(gb);',
                        'gb->vblank_just_occured = false;',
                        'opcode = llr_fetch_opcode(gb);']
            else:
                out += ['return executed;']
        # A previously fetched opcode can differ because of DMA/open bus. It
        # must be executed once, not fetched again or silently skipped.
        out += ['unexpected_opcode:',
                'if (!executed) return 0;',
                'opcodes[opcode](gb, opcode);', '++gb->llr_fallback_count;',
                'flush_pending_cycles(gb);', 'return executed;', '}']
    out += [f'static unsigned (*const {prefix}_blocks[])(GB_gameboy_t*, uint32_t, uint8_t) = {{',
            '0,']
    out += [f'{prefix}_block_{i},' for i in range(len(blocks))]
    out += ['};', f'static const uint32_t {prefix}_block_map[{len(rom)}] = {{']
    for index, block in enumerate(blocks):
        out += [f'[{pos}] = {index+1},' for pos in block]
    out += ['};',
            f'static unsigned {prefix}_native_dispatch(GB_gameboy_t *gb, uint32_t offset, uint8_t opcode) {{',
            f'if (offset >= {len(rom)} || !{prefix}_block_map[offset]) return 0;',
            f'return {prefix}_blocks[{prefix}_block_map[offset]](gb, offset, opcode);', '}']
    return out, len(blocks)
