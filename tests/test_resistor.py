from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class ResistorTests(unittest.TestCase):
    def test_real_c_model(self):
        app = ROOT / "firmware/resistor-reader"
        with tempfile.TemporaryDirectory() as temporary:
            binary = Path(temporary) / "resistor-tests"
            result = subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                                     "-I", str(app / "main"), str(app / "tests/test_model.c"),
                                     str(app / "main/resistor_model.c"), "-lm", "-o", str(binary)],
                                    text=True, capture_output=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            subprocess.run([str(binary)], check=True)
