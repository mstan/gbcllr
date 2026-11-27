import importlib.util
import pathlib
import tempfile
import unittest
import sys
import os
import subprocess
import hashlib
import json
import re

sys.path.insert(0,str(pathlib.Path(__file__).resolve().parents[1]/'tools'))

spec=importlib.util.spec_from_file_location('recompiler',pathlib.Path(__file__).resolve().parents[1]/'tools/recompile.py')
r=importlib.util.module_from_spec(spec)
spec.loader.exec_module(r)

class RecompilerTest(unittest.TestCase):
    @unittest.skipUnless(os.environ.get('GBCLLR_TEST_C_COMPILER'), 'CTest supplies the configured C compiler')
    def test_stat_operation_preserves_retirement_and_guarded_fallback(self):
        rom=bytearray([0xd3])*32768;rom[0x100:0x107]=bytes([0xf0,0x41,0xe6,2,0x20,0xfa,0xc9])
        with tempfile.TemporaryDirectory() as d:
            root=pathlib.Path(d);r.generate(rom,{0x100},root)
            generated=(root/'llr_program.inc').read_text()
            names=set(re.findall(r'static void llr_op_[0-9a-f]+\(GB_gameboy_t \*gb\) \{ (\w+)\(',generated))
            harness='''#include <stdint.h>
#define GBCLLR_NATIVE_WAIT 1
#define GBCLLR_FRAME_AUDIO 1
typedef struct {unsigned ordinary,calls,retired;} GB_gameboy_t;
static unsigned GB_native_stat_poll_wait(GB_gameboy_t *g){g->calls++;return g->retired;}
'''
            harness+='\n'.join('static void '+n+'(GB_gameboy_t *g,uint8_t op){(void)op;g->ordinary++;}' for n in names)
            harness+='''\n#include "llr_program.inc"
int main(void){GB_gameboy_t g={0,0,9};
if(llr_native_dispatch(&g,256,0xf0)!=9||g.calls!=1||g.ordinary)return 1;
g.retired=0;if(llr_native_dispatch(&g,256,0xf0)!=1||g.calls!=2||g.ordinary!=1)return 2;
if(llr_native_dispatch(&g,256,0xe6)||llr_native_dispatch(&g,257,0xf0)||g.calls!=2)return 3;
return 0;}
'''
            (root/'test.c').write_text(harness)
            exe=root/('test.exe' if os.name=='nt' else 'test')
            subprocess.run([os.environ['GBCLLR_TEST_C_COMPILER'],'-std=c11',str(root/'test.c'),'-o',str(exe)],check=True,capture_output=True)
            subprocess.run([str(exe)],check=True,capture_output=True)
    def test_banked_direct_flow(self):
        rom=bytearray(65536)
        rom[0x8000:0x8006]=bytes([0xcd,0x10,0x40,0x18,0xfb,0xd3])
        rom[0x8010]=0xc9
        entries=r.discover(rom,{0x8000})
        self.assertEqual(entries,{0x8000,0x8003,0x8010})
    def test_native_uses_timed_semantics_and_opcode_guard(self):
        rom=bytearray(32768);rom[0x100:0x103]=bytes([0x3e,0x12,0xc9])
        with tempfile.TemporaryDirectory() as d:
            meta=r.generate(rom,{0x100},pathlib.Path(d))
            source=(pathlib.Path(d)/'llr_program.inc').read_text()
            self.assertEqual(meta['native_entries'],2)
            self.assertIn('llr_map[offset] != (unsigned)opcode + 1',source)
            self.assertIn('ld_hr_d8(gb, 0x3e)',source)
            self.assertNotIn('0x12);',source) # immediates remain timed bus reads
    def test_blocks_exit_at_control_flow_and_cover_entry_sites(self):
        from native_blocks import partition
        rom=bytearray(32768)
        rom[0x100:0x107]=bytes([0x3e,0x12,0x04,0xfb,0x0c,0x18,0xf9])
        entries=r.discover(rom,{0x100})
        blocks=partition(rom,entries,r.LENGTHS)
        self.assertEqual(blocks,[[0x100,0x102,0x103],[0x104,0x105]])
        self.assertEqual({site for block in blocks for site in block},entries)

    def test_catalogue_rejects_duplicate_rom_identity(self):
        rom=bytearray(32768)
        with tempfile.TemporaryDirectory() as d:
            with self.assertRaises(ValueError):
                r.generate_catalog([(rom,{0x100}),(rom,{0x200})],pathlib.Path(d))

    def test_cli_symbol_binding_follows_exact_cartridge_not_catalogue_order(self):
        with tempfile.TemporaryDirectory() as d:
            root=pathlib.Path(d)
            a=bytearray([0xd3])*32768; a[0x4100:0x4102]=bytes([0x04,0xc9])
            b=bytearray([0xd3])*32768; b[0x4200:0x4202]=bytes([0x0c,0xc9])
            (root/'a.gb').write_bytes(a); (root/'b.gb').write_bytes(b)
            symbols=root/'symbols with spaces.sym'
            symbols.write_text('01:4100 Primary\n01:4200 Additional\n')
            digest=hashlib.sha256(b).hexdigest()
            command=[sys.executable,str(r.ROOT/'tools/recompile.py'),
                '--cartridge-symbols',digest+'='+str(symbols)]
            for primary,extra in [('a','b'),('b','a')]:
                output=root/(primary+'-first')
                result=subprocess.run(command+['--rom',str(root/(primary+'.gb')),
                    '--additional-rom',str(root/(extra+'.gb')),'--output',str(output)],
                    text=True,capture_output=True)
                self.assertEqual(result.returncode,0,result.stdout+result.stderr)
                carts=json.loads((output/'program.json').read_text())['cartridges']
                self.assertEqual({c['sha256']:c['native_entries'] for c in carts},
                    {hashlib.sha256(a).hexdigest():0,digest:2})
                index=0 if primary=='b' else 1
                source=(output/f'cart_{index}/llr_program.inc').read_text()
                self.assertIn('[16896] = 13,',source) # additional ROM's INC C
                self.assertNotIn('[16640] = 5,',source) # primary's INC B stays isolated
            for bindings,message in [(['0'*64+'='+str(symbols)],'does not match'),
                    ([digest+'='+str(symbols)]*2,'duplicate cartridge symbol binding')]:
                output=root/'rejected'
                argv=[sys.executable,str(r.ROOT/'tools/recompile.py'),'--rom',str(root/'b.gb'),
                      '--output',str(output)]
                for binding in bindings:argv+=['--cartridge-symbols',binding]
                result=subprocess.run(argv,text=True,capture_output=True)
                self.assertNotEqual(result.returncode,0)
                self.assertIn(message,result.stderr)
                self.assertFalse(output.exists())

    @unittest.skipUnless(os.environ.get('GBCLLR_TEST_C_COMPILER'), 'CTest supplies the configured C compiler')
    def test_two_cartridge_dispatch_is_isolated_and_opcode_guarded(self):
        a=bytearray(32768);a[0x100:0x102]=bytes([0x04,0xc9])
        b=bytearray(32768);b[0x200:0x202]=bytes([0x0c,0xc9])
        harness=r'''
#include <stdint.h>
#include <stdbool.h>
#define noinline
typedef struct { unsigned llr_program_index, llr_chained_count, llr_fallback_count;
                 bool vblank_just_occured; unsigned r[3]; } GB_gameboy_t;
static void inc_hr(GB_gameboy_t *gb, uint8_t op) { (void)op; ++gb->r[0]; }
static void inc_lr(GB_gameboy_t *gb, uint8_t op) { (void)op; ++gb->r[1]; }
static void ret(GB_gameboy_t *gb, uint8_t op) { (void)op; ++gb->r[2]; }
static void flush_pending_cycles(GB_gameboy_t *gb) { (void)gb; }
static bool llr_can_chain(GB_gameboy_t *gb, uint16_t pc, uint32_t offset)
{ (void)gb; (void)pc; (void)offset; return false; }
static void GB_instruction_end(GB_gameboy_t *gb) { (void)gb; }
static void GB_set_running_thread(GB_gameboy_t *gb) { (void)gb; }
static uint8_t llr_fetch_opcode(GB_gameboy_t *gb) { (void)gb; return 0; }
static void (*opcodes[256])(GB_gameboy_t*,uint8_t);
#include "llr_program.inc"
int main(void) {
    GB_gameboy_t gb={0};
    if (llr_native_dispatch(&gb,0x100,0x04)!=1 || gb.r[0]!=1) return 1;
    if (llr_native_dispatch(&gb,0x200,0x0c) || gb.r[1]) return 2;
    if (llr_native_dispatch(&gb,0x100,0x0c) || gb.r[0]!=1) return 3;
    gb.llr_program_index=1;
    if (llr_native_dispatch(&gb,0x200,0x0c)!=1 || gb.r[1]!=1) return 4;
    if (llr_native_dispatch(&gb,0x100,0x04) || gb.r[0]!=1) return 5;
    gb.llr_program_index=2;
    if (llr_native_dispatch(&gb,0x100,0x04) || gb.r[0]!=1) return 6;
    return 0;
}
'''
        for blocks in [False,True]:
            with self.subTest(blocks=blocks),tempfile.TemporaryDirectory() as d:
                output=pathlib.Path(d)
                r.generate_catalog([(a,{0x100}),(b,{0x200})],output,blocks)
                (output/'harness.c').write_text(harness)
                binary=output/('harness.exe' if os.name=='nt' else 'harness')
                compile=subprocess.run([os.environ['GBCLLR_TEST_C_COMPILER'],'-std=c11',
                    str(output/'harness.c'),'-o',str(binary)],text=True,capture_output=True)
                self.assertEqual(compile.returncode,0,compile.stdout+compile.stderr)
                subprocess.run([str(binary)],check=True,capture_output=True)

if __name__=='__main__': unittest.main()
