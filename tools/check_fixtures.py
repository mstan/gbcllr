"""Bounded qualification of the concrete timing gaps recorded by the old fork."""
import argparse
import hashlib
import json
import pathlib
import subprocess
import tempfile

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--exe',type=pathlib.Path,required=True)
    p.add_argument('--mooneye',type=pathlib.Path,required=True)
    p.add_argument('--blargg',type=pathlib.Path,required=True)
    p.add_argument('--boot-rom',type=pathlib.Path,required=True)
    p.add_argument('--output',type=pathlib.Path,required=True)
    args=p.parse_args()
    args.output.parent.mkdir(parents=True,exist_ok=True)
    cases=[]
    for name in ['div_write','rapid_toggle','tim00','tim01','tim10','tim11',
                 'tima_reload','tima_write_reloading','tma_write_reloading']:
        cases.append(('mooneye',args.mooneye/'acceptance/timer'/f'{name}.gb',600))
    for name in ['ei_sequence','ei_timing','rapid_di_ei','di_timing-GS','reti_timing']:
        cases.append(('mooneye',args.mooneye/'acceptance'/f'{name}.gb',600))
    for name in ['intr_2_0_timing','intr_2_mode0_timing','stat_irq_blocking']:
        cases.append(('mooneye',args.mooneye/'acceptance/ppu'/f'{name}.gb',600))
    cases += [('blargg',args.blargg/relative,6000) for relative in
              ['cpu_instrs/cpu_instrs.gb','instr_timing/instr_timing.gb',
               'mem_timing/mem_timing.gb','mem_timing-2/mem_timing.gb','halt_bug.gb']]
    results=[]
    for kind,rom,frames in cases:
        row={'fixture':str(rom),'suite':kind}
        if not rom.is_file():
            row['result']='missing'
        else:
            row['sha256']=hashlib.sha256(rom.read_bytes()).hexdigest()
            command=[str(args.exe.resolve()),'--rom',str(rom),'--frames',str(frames),
                     '--boot-rom',str(args.boot_rom),'--reference']
            row['argv']=command
            try:
                # This screen-only fixture uses ASCII tile IDs. Capture its
                # actual verdict rather than infer it from process exit or a
                # misread 160-pixel screenshot. Other fixtures retain their
                # existing serial/cartridge-RAM result protocols.
                screen_only=rom.name=='halt_bug.gb'
                with tempfile.TemporaryDirectory(prefix='gbcllr-fixture-',dir=args.output.parent) as scratch:
                    tilemap=None
                    if screen_only:
                        commands=pathlib.Path(scratch).resolve()/'fixture.commands'
                        commands.write_text(f'frames {frames} 0\ndump 0x9800 1024 "tilemap.bin"\n')
                        command=[str(args.exe.resolve()),'--rom',str(rom),'--frames','0',
                                 '--boot-rom',str(args.boot_rom),'--reference','--commands',str(commands)]
                        row['argv']=command
                    run=subprocess.run(command,text=True,capture_output=True,timeout=45)
                    if screen_only and (pathlib.Path(scratch)/'tilemap.bin').exists():
                        tilemap=(pathlib.Path(scratch)/'tilemap.bin').read_bytes()
                row['stdout']=run.stdout;row['stderr']=run.stderr;row['exit_code']=run.returncode
                data=json.loads(run.stdout.splitlines()[-1])
                row['observation']=data
                if run.returncode:
                    row['result']='error'
                elif kind=='mooneye':
                    row['result']='pass' if data['registers'][2:]==[3,5,8,13,21,34] else 'fail'
                elif screen_only and tilemap is not None:
                    text='\n'.join(''.join(chr(c) if 32<=c<127 else '.' for c in tilemap[y*32:y*32+20]) for y in range(18))
                    row['tilemap_text']=text
                    row['result']='fail' if 'Failed' in text else 'pass' if 'Passed' in text else 'unverified'
                else:
                    serial=bytes.fromhex(data['serial_hex']).decode('ascii',errors='replace')
                    memory=bytes.fromhex(data['cartridge_ram_hex'])
                    if len(memory)>=4 and memory[1:4]==bytes([0xde,0xb0,0x61]):
                        row['result']='pass' if memory[0]==0 else 'fail' if memory[0]<0x80 else 'unverified'
                    else:
                        row['result']='pass' if 'passed' in serial.lower() else 'fail' if 'failed' in serial.lower() else 'unverified'
            except (subprocess.TimeoutExpired,ValueError,IndexError) as e:
                row['result']='error';row['error']=str(e)
        print(rom.name,row['result'],flush=True);results.append(row)
    evidence={'exe_sha256':hashlib.sha256(args.exe.read_bytes()).hexdigest(),
              'boot_sha256':hashlib.sha256(args.boot_rom.read_bytes()).hexdigest(),
              'backend':'reference CPU + adapted SameBoy LLE hardware', 'results':results}
    args.output.parent.mkdir(parents=True,exist_ok=True)
    args.output.write_text(json.dumps(evidence,indent=2)+'\n')
    return int(any(r['result']!='pass' for r in results))

if __name__=='__main__':raise SystemExit(main())
