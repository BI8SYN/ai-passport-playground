#!/usr/bin/env python3
"""Build the read-only Passport product sources with native container LVGL."""
import os
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[2]
SOURCE = Path(__file__).resolve().parent
OUTPUT = Path('/build/handmic-simulator/backend')


def main():
    candidates = [ROOT / 'firmware' / target / 'managed_components/lvgl__lvgl'
                  for target in ('rf-link-handmic', 'ear-trainer', 'resistor-reader', 'ble-signer-lab')]
    lvgl = next((p for p in candidates if (p / 'CMakeLists.txt').is_file()), None)
    if lvgl is None:
        raise SystemExit('Resolve LVGL first with ./tools/dev build rf-link-handmic')
    cjson = Path(os.environ.get('IDF_PATH', '/opt/esp/idf')) / 'components/json/cJSON'
    OUTPUT.mkdir(parents=True, exist_ok=True)
    subprocess.run(['cmake', '-S', str(SOURCE), '-B', str(OUTPUT), '-G', 'Ninja',
                    f'-DLVGL_DIR={lvgl}', f'-DAPP_DIR={ROOT / "firmware/rf-link-handmic/main"}',
                    f'-DCJSON_DIR={cjson}'], check=True)
    subprocess.run(['cmake', '--build', str(OUTPUT), '--parallel', '8'], check=True)
    print(f'Backend: {OUTPUT / "passport_simulator"}')


if __name__ == '__main__':
    main()
