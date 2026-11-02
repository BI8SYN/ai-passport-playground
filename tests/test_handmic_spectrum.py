from pathlib import Path
import subprocess
import tempfile
import unittest
ROOT = Path(__file__).resolve().parents[1]
class HandmicSpectrumTests(unittest.TestCase):
    def test_real_frequency_analysis(self):
        app = ROOT / 'firmware/rf-link-handmic'
        with tempfile.TemporaryDirectory() as tmp:
            exe = Path(tmp) / 'spectrum'
            subprocess.run(['cc','-std=c11','-O2','-Wall','-Wextra','-Werror','-I',str(app/'main'),str(app/'tests/test_spectrum.c'),str(app/'main/hm_spectrum.c'),'-lm','-o',str(exe)],check=True)
            subprocess.run([str(exe)],check=True)
