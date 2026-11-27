"""Package explicitly selected, already qualified Windows/Linux game inputs.

The private JSON spec supplies title, source, model, assets (destination: path),
and runtimes (windows/linux: {destination: path}). Asset names game.rom,
boot.bin, dmg_boot.bin and feature.gbmod are required. Runtime names include
gbcllr-run.exe / gbcllr-run and their dependencies and licenses. No builds,
downloads, save imports or delivery-directory replacement happen here.
"""
import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import shutil
import zipfile


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def copy_inputs(inputs, destination):
    for name, source in inputs.items():
        relative = PurePosixPath(name)
        if relative.is_absolute() or ".." in relative.parts or "\\" in name:
            raise ValueError("Unsafe package destination: " + name)
        target = destination.joinpath(*relative.parts)
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source, target)
        if digest(target) != digest(Path(source)):
            raise RuntimeError("Copied input differs: " + name)


def package(spec, output):
    title = spec["title"]
    if not title or any(c in title for c in '/\\:*?"<>|'):
        raise ValueError("Invalid game title")
    assets = spec["assets"]
    required = {"game.rom", "boot.bin", "dmg_boot.bin", "feature.gbmod"}
    if not required <= assets.keys():
        raise ValueError("Missing required assets: " + str(required - assets.keys()))
    root = output / title
    root.mkdir(parents=True, exist_ok=False)
    copy_inputs(assets, root / "data")
    rom_hash = digest(root / "data/game.rom")
    manifest = (root / "data/feature.gbmod").read_text()
    if 'target "' + rom_hash + '"' not in manifest:
        raise ValueError("Mod package targets a different stock cartridge")
    for platform, executable in [("windows", "gbcllr-run.exe"), ("linux", "gbcllr-run")]:
        runtime = spec["runtimes"][platform]
        if executable not in runtime or "assets/img/boxart.tga" not in runtime:
            raise ValueError("Missing " + platform + " runner or game boxart")
        copy_inputs(runtime, root / "runtime" / platform)
        copy_inputs({Path(assets["feature.gbmod"]).name: assets["feature.gbmod"]},
                    root / "runtime" / platform / "assets/mods")
    model = spec["model"]
    if model not in ("dmg", "cgb"):
        raise ValueError("Invalid hardware model")
    (root / "defaults.cfg").write_text(
        'gbcllr-desktop 1\nfullscreen 0\nvolume 100\nsgb_boot "data/dmg_boot.bin"\n'
    )
    arguments = ["--rom", "data/game.rom", "--boot-rom", "data/boot.bin",
                 "--model", model, "--window",
                 "--mod-package", "data/feature.gbmod", "--profile", "profile"]
    if spec.get("reference_cpu", False):
        arguments.append("--reference")
    ps_args = ",".join("'" + a + "'" for a in arguments)
    (root / "Launch.ps1").write_text(
        "$ErrorActionPreference='Stop'\nSet-Location -LiteralPath $PSScriptRoot\n"
        "[IO.Directory]::CreateDirectory((Join-Path $PSScriptRoot 'profile')) | Out-Null\n"
        "if (!(Test-Path -LiteralPath 'profile/desktop.cfg')) {\n"
        "  Copy-Item -LiteralPath 'defaults.cfg' -Destination 'profile/desktop.cfg'\n}\n"
        "$env:PATH=(Join-Path $PSScriptRoot 'runtime/windows')+';'+$env:PATH\n"
        "Remove-Item Env:SDL_AUDIODRIVER,Env:SDL_VIDEODRIVER -ErrorAction SilentlyContinue\n"
        "$gameArgs=@(" + ps_args + ")\n"
        "$runner=Join-Path $PSScriptRoot 'runtime/windows/gbcllr-run.exe'\n"
        "if (!(Test-Path -LiteralPath $runner -PathType Leaf)) { throw 'Game runtime is missing.' }\n"
        "$previousPreference=$ErrorActionPreference\ntry {\n"
        "  $ErrorActionPreference='Continue'\n"
        "  & $runner @gameArgs @args 2>&1 | ForEach-Object { \"$_\" } | "
        "Out-File -LiteralPath 'profile/launch.log' -Encoding utf8 -ErrorAction Stop\n"
        "  $gameExit=$LASTEXITCODE\n"
        "} finally { $ErrorActionPreference=$previousPreference }\nexit $gameExit\n"
    )
    (root / "Launch Windows.cmd").write_bytes(
        b'@echo off\r\npowershell.exe -NoProfile -ExecutionPolicy Bypass -WindowStyle Hidden '
        b'-File "%~dp0Launch.ps1"\r\n'
    )
    (root / "Launch Linux.sh").write_text(
        '#!/bin/sh\nset -eu\ncd -- "$(dirname -- "$0")"\nmkdir -p profile\n'
        'if [ ! -f profile/desktop.cfg ]; then cp defaults.cfg profile/desktop.cfg; fi\n'
        'unset SDL_AUDIODRIVER SDL_VIDEODRIVER\n'
        'export LD_LIBRARY_PATH="$PWD/runtime/linux${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"\n'
        'exec ./runtime/linux/gbcllr-run ' + " ".join(arguments) + ' "$@"\n',
        newline="\n"
    )
    (root / "README.txt").write_text(
        title + "\n\nLaunch Windows.cmd or Launch Linux.sh opens the shared GB/GBC launcher.\n"
        "The game starts in a normal window; existing saved launcher preferences are preserved.\n"
        "Esc opens settings and Mods. Fit to screen is the fresh-profile default.\n"
        "Native saves and settings live in profile/. Keep that folder when updating.\n"
        "This archive contains no profile and will not replace your existing saves/settings.\n"
        "Save states are specific to this build. Native in-game saves remain supported.\n\n"
        + spec.get("notes", "") + "\n"
    )
    records = {p.relative_to(root).as_posix(): digest(p)
               for p in sorted(root.rglob("*")) if p.is_file()}
    (root / "build.json").write_text(json.dumps(
        {"title": title, "source": spec["source"], "stock_rom_sha256": rom_hash,
         "files": records}, indent=2) + "\n")
    archive = output / (title + ".zip")
    with zipfile.ZipFile(archive, "x", compression=zipfile.ZIP_DEFLATED, compresslevel=1) as z:
        for p in sorted(root.rglob("*")):
            if not p.is_file():
                continue
            info = zipfile.ZipInfo((Path(title) / p.relative_to(root)).as_posix())
            info.create_system = 3
            mode = 0o755 if p.name.endswith(".sh") or p.name == "gbcllr-run" else 0o644
            info.external_attr = (0o100000 | mode) << 16
            info.compress_type = zipfile.ZIP_DEFLATED
            z.writestr(info, p.read_bytes(), compresslevel=1)
    with zipfile.ZipFile(archive) as z:
        if z.testzip() is not None:
            raise RuntimeError("Archive CRC failed")
        for name, expected in records.items():
            if hashlib.sha256(z.read(title + "/" + name)).hexdigest() != expected:
                raise RuntimeError("Archived input differs: " + name)
    print(json.dumps({"archive": str(archive), "sha256": digest(archive),
                      "files": len(records) + 1}))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--spec", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    package(json.loads(args.spec.read_text(encoding="utf-8-sig")), args.output)
