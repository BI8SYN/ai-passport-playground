from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class HandmicProductTests(unittest.TestCase):
    def test_product_state_machine(self):
        app = ROOT / 'firmware/rf-link-handmic'
        with tempfile.TemporaryDirectory() as tmp:
            exe = Path(tmp) / 'handmic-product'
            subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror',
                            '-I', str(app / 'main'), str(app / 'tests/test_product.c'),
                            str(app / 'main/hm_product.c'), '-o', str(exe)], check=True)
            subprocess.run([str(exe)], check=True)
