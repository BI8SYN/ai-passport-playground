#!/usr/bin/env python3
"""Render the actual application UI using native LVGL inside the SDK container."""
from pathlib import Path
import struct
import subprocess
import zlib

ROOT = Path(__file__).resolve().parents[1]

def run(*args):
    subprocess.run(list(map(str, args)), check=True)

def chunk(kind, data):
    return struct.pack('!I', len(data)) + kind + data + struct.pack('!I', zlib.crc32(kind + data))

def main():
    app = ROOT / 'firmware/resistor-reader'
    lvgl = app / 'managed_components/lvgl__lvgl'
    if not (lvgl / 'CMakeLists.txt').is_file():
        raise SystemExit('Run ./tools/dev build resistor-reader first to resolve locked LVGL sources')
    build = Path('/build/preview/resistor-reader')
    output = ROOT / 'artifacts/resistor-reader-preview'
    output.mkdir(parents=True, exist_ok=True)
    run('cmake', '-S', app / 'preview', '-B', build, '-G', 'Ninja', f'-DLVGL_DIR={lvgl}')
    run('cmake', '--build', build, '--parallel', '8')
    run(build / 'resistor_preview', output)
    for ppm in output.glob('*-band.ppm'):
        header, size, maximum, pixels = ppm.read_bytes().split(b'\n', 3)
        assert (header, size, maximum, len(pixels)) == (b'P6', b'240 320', b'255', 240 * 320 * 3)
        scanlines = b''.join(b'\0' + pixels[y*720:(y+1)*720] for y in range(320))
        png = b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('!IIBBBBB', 240, 320, 8, 2, 0, 0, 0))
        png += chunk(b'IDAT', zlib.compress(scanlines)) + chunk(b'IEND', b'')
        ppm.with_suffix('.png').write_bytes(png)
    print(f'Preview PNGs: {output}')

if __name__ == '__main__':
    main()
