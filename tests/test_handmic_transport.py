from pathlib import Path
import subprocess
import tempfile
import unittest
ROOT = Path(__file__).resolve().parents[1]

class HandmicTransportTests(unittest.TestCase):
    def test_fragment_reassembly_adversarial(self):
        app = ROOT / 'firmware/rf-link-handmic'
        with tempfile.TemporaryDirectory() as tmp:
            exe = Path(tmp) / 'transport-tests'
            stub = Path(tmp) / 'host'
            stub.mkdir()
            (stub / 'ble_uuid.h').write_text('#pragma once\n#include <stdint.h>\ntypedef struct {uint16_t value;} ble_uuid_t;\nuint16_t ble_uuid_u16(const ble_uuid_t *);\n')
            (stub / 'ble_att.h').write_text('#pragma once\n#define BLE_ATT_F_READ_ENC 0x04\n#define BLE_ATT_F_READ_AUTHEN 0x08\n#define BLE_ATT_F_READ_AUTHOR 0x10\n')
            (Path(tmp) / 'esp_idf_version.h').write_text('#define ESP_IDF_VERSION_VAL(a,b,c) ((a)*10000+(b)*100+(c))\n#define ESP_IDF_VERSION ESP_IDF_VERSION_VAL(5,5,3)\n')
            subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror',
                            '-DHM_CCCD_GUARD_TEST', '-I', tmp, '-I', str(app / 'main'), str(app / 'tests/test_transport.c'),
                            str(app / 'main/hm_fragment.c'), str(app / 'main/hm_cccd_guard.c'), '-o', str(exe)], check=True)
            subprocess.run([str(exe)], check=True)
