"""Stage a source build's cover and GUI launchers. No ROM or save is copied."""
import argparse
import json
from pathlib import Path
import shutil


def stage(args):
    runner = args.runner.resolve()
    root = runner.parent
    cover = root / 'assets/img/boxart.tga'
    cover.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(args.boxart, cover)
    arguments = ['--rom', str(args.rom.resolve()), '--model', args.model, '--window']
    if getattr(args, 'reference', False):
        arguments.append('--reference')
    if args.boot_rom:
        arguments += ['--boot-rom', str(Path(args.boot_rom).resolve())]
    config = dict(runner=runner.name, arguments=arguments,
                  compiler_dir=str(Path(args.compiler_dir).resolve()))
    (root / 'play.json').write_text(json.dumps(config, indent=2) + '\n')
    (root / 'Play.ps1').write_text(
        "$ErrorActionPreference='Stop'\n"
        "Set-Location -LiteralPath $PSScriptRoot\n"
        "$game=Get-Content -LiteralPath 'play.json' -Raw | ConvertFrom-Json\n"
        "$env:PATH=$game.compiler_dir+';'+$env:PATH\n"
        "$gameArgs=@($game.arguments)+@($args)\n"
        "$previousPreference=$ErrorActionPreference\ntry {\n"
        "  $ErrorActionPreference='Continue'\n"
        "  & (Join-Path $PSScriptRoot $game.runner) @gameArgs 2>&1 | "
        "ForEach-Object { \"$_\" } | Out-File -LiteralPath 'launch.log' -Encoding utf8 -ErrorAction Stop\n"
        "  $gameExit=$LASTEXITCODE\n"
        "} finally { $ErrorActionPreference=$previousPreference }\n"
        "exit $gameExit\n", encoding='utf-8')
    (root / 'Play.cmd').write_bytes(
        b'@echo off\r\npowershell.exe -NoProfile -ExecutionPolicy Bypass '
        b'-WindowStyle Hidden -File "%~dp0Play.ps1" %*\r\n')
    # JSON keeps user paths out of shell command text.
    script = root / 'Play.py'
    script.write_text(
        "import json,os,pathlib,sys\n"
        "r=pathlib.Path(__file__).resolve().parent\n"
        "g=json.loads((r/'play.json').read_text())\n"
        "os.chdir(r)\n"
        "os.execv(str(r/g['runner']),[str(r/g['runner']),*g['arguments'],*sys.argv[1:]])\n")
    sh = root / 'Play.sh'
    sh.write_text('#!/bin/sh\nset -eu\ncd -- "$(dirname -- "$0")"\n'
                  'exec python3 ./Play.py "$@"\n', newline='\n')
    sh.chmod(0o755)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    for key in ['runner', 'rom', 'boxart']:
        parser.add_argument('--' + key, type=Path, required=True)
    parser.add_argument('--boot-rom', default='')
    parser.add_argument('--model', choices=['dmg', 'cgb'], required=True)
    parser.add_argument('--compiler-dir', required=True)
    parser.add_argument('--reference', action='store_true',
                        help='Select the qualified reference instruction decoder')
    stage(parser.parse_args())
