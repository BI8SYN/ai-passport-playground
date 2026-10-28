#!/usr/bin/env python3
"""Bake the procedural reference art into native little-endian RGB565 flash assets."""
from pathlib import Path
import hashlib
import json
import subprocess
import tempfile
ROOT = Path(__file__).resolve().parents[1]
app = ROOT / 'firmware/resistor-reader/main'
with tempfile.TemporaryDirectory() as tmp:
    executable = Path(tmp) / 'generate'
    subprocess.run(['cc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-I', str(app),
                    str(ROOT / 'tools/renderer/generate.c'), str(ROOT / 'tools/renderer/resistor_art.c'),
                    str(app / 'resistor_model.c'), str(app / 'resistor_layout.c'), '-lm', '-o', str(executable)], check=True)
    output = ROOT / 'assets/resistor/resistor_textures.bin'
    subprocess.run([str(executable), str(output)], check=True)
    metadata = {'format': 'little-endian RGB565', 'body_images': 2, 'size': [240, 76],
                'tile_centers': [49,70,72,78,91,95,107,112,118,133,186,190], 'tile_size': [11,53],
                'tile_y': 10, 'colors': 12, 'bytes': output.stat().st_size,
                'sha256': hashlib.sha256(output.read_bytes()).hexdigest()}
    output.with_suffix('.json').write_text(json.dumps(metadata, indent=2) + '\n')
    print(metadata)
