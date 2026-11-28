"""Write a small authored cartridge for native machine contracts."""
from pathlib import Path
import sys

image = bytearray(32768)
image[0x100:0x103] = bytes.fromhex('c3 50 01')
image[0x146] = 3  # The SGB service is advertised, but stays off by default.
image[0x147] = 3
image[0x149] = 2
# Counter, joypad and idle entry points share one generated native image.
image[0x150:0x159] = bytes.fromhex('fa 00 c0 3c ea 00 c0 18 f7')
image[0x180:0x18b] = bytes.fromhex('3e 10 e0 00 f0 00 ea 00 c0 18 f5')
image[0x1c0:0x1c2] = bytes.fromhex('18 fe')
image[0x1d0:0x1d2] = bytes.fromhex('18 fe')
image[0x200:0x20a] = bytes.fromhex('3e c0 e0 46 3e 28 3d 20 fd c9')
image[0x230:0x23c] = bytes.fromhex('21 b7 c4 3e c3 22 3e c0 22 36 01 c9')
path = Path(sys.argv[1])
if not path.exists() or path.read_bytes() != image:
    path.write_bytes(image)
