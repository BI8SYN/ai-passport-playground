from pathlib import Path
import json
import os
import subprocess
import tempfile
import unittest
ROOT = Path(__file__).resolve().parents[1]

class HandmicCoreTests(unittest.TestCase):
    def test_protocol_core(self):
        app = ROOT / 'firmware/rf-link-handmic'
        cjson = Path(os.environ.get('IDF_PATH', '/opt/esp/idf')) / 'components/json/cJSON'
        self.assertTrue((cjson / 'cJSON.c').is_file(), 'run via ./tools/dev test in SDK container')
        with tempfile.TemporaryDirectory() as tmp:
            vectors = json.loads((ROOT / 'docs/spec/rf-link-handmic/vectors.json').read_text())
            header = Path(tmp) / 'vectors.h'
            lines = []
            for name in ['audio', 'app_flow_mic_credit', 'device_flow_ptt_down']:
                data = bytes.fromhex(vectors[name]['hex'])
                lines.append(f'static const unsigned char vector_{name}[] = {{' + ','.join(map(str, data)) + '};')
            for group in ['station_metadata', 'station_metadata_boundary']:
                lines.append(f'static const char *vector_{group}[] = {{' + ','.join(json.dumps(json.dumps(v['json'], ensure_ascii=False, separators=(",", ":")), ensure_ascii=False) for v in vectors[group]) + '};')
            header.write_text('\n'.join(lines))
            exe = Path(tmp) / 'handmic-core'
            subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined', '-fno-omit-frame-pointer', '-I', str(app/'main'), '-I', str(cjson), '-I', tmp,
                            str(app/'tests/test_core.c'), str(app/'main/hm_core.c'), str(app/'main/hm_adpcm.c'), str(cjson/'cJSON.c'), '-lm', '-o', str(exe)], check=True)
            subprocess.run([str(exe)], check=True)
