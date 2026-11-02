from pathlib import Path
import subprocess
import tempfile
import unittest
ROOT = Path(__file__).resolve().parents[1]
class BleLabTests(unittest.TestCase):
    def test_binding_and_request_lifecycle(self):
        app = ROOT / 'firmware/ble-signer-lab'
        with tempfile.TemporaryDirectory() as tmp:
            exe = Path(tmp) / 'ble-lab-tests'
            subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-I',str(app/'main'),
                            str(app/'tests/test_model.c'),str(app/'main/lab_model.c'),'-o',str(exe)],check=True)
            subprocess.run([str(exe)],check=True)
