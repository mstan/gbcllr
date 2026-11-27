"""Verify the exported artifact with the standard ZIP reader."""
import pathlib
import subprocess
import sys
import tempfile
import zipfile

with tempfile.TemporaryDirectory(prefix="gbcllr-diagnostics-contract-") as scratch:
    desktop = sys.argv[1] == "--desktop"
    native = sys.argv[1] == "--native"
    if desktop:
        profile = pathlib.Path(scratch, "profile")
        subprocess.run([sys.argv[2], "--support-shortcuts", str(profile)], check=True)
        folder = profile / "diagnostics"
    elif native:
        subprocess.run([sys.argv[2], scratch, "--native"], check=True)
        folder = pathlib.Path(scratch, "recording")
    else:
        subprocess.run([sys.argv[1], scratch], check=True)
        folder = pathlib.Path(scratch, "recording")
    reports = list(folder.glob("bug-report-*.zip"))
    assert len(reports) == 1
    with zipfile.ZipFile(reports[0]) as report:
        assert report.testzip() is None
        names = set(report.namelist())
        assert {"report.txt", "current.state", "screenshot.ppm", "session.log"} <= names
        assert sum(name.endswith(".replay") for name in names) == (1 if desktop else 2)
        assert report.read("screenshot.ppm").startswith(b"P6\n160 144\n255\n")
        assert "rom_sha256=" in report.read("report.txt").decode()
        assert not any(name.endswith((".gb", ".gbc", ".bin", ".sav", ".cfg")) for name in names)
