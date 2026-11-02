from pathlib import Path
import subprocess
import tempfile
import unittest
ROOT = Path(__file__).resolve().parents[1]
class HandmicIngressTests(unittest.TestCase):
    def test_bounded_ingress(self):
        app = ROOT / 'firmware/rf-link-handmic'
        with tempfile.TemporaryDirectory() as tmp:
            exe = Path(tmp) / 'ingress'
            subprocess.run(['cc','-std=c11','-O2','-Wall','-Wextra','-Werror','-I',str(app/'main'),str(app/'tests/test_ingress.c'),'-o',str(exe)],check=True)
            subprocess.run([str(exe)],check=True)

    def test_actual_audio_runtime(self):
        app = ROOT / 'firmware/rf-link-handmic'
        with tempfile.TemporaryDirectory() as tmp:
            exe = Path(tmp) / 'runtime'
            subprocess.run(['cc','-std=c11','-O2','-Wall','-Wextra','-Werror','-ffunction-sections','-fdata-sections','-Wl,--gc-sections','-I',str(app/'tests/audio_stubs'),'-I',str(app/'main'),str(app/'tests/test_audio_runtime.c'),'-o',str(exe)],check=True)
            subprocess.run([str(exe)],check=True)
