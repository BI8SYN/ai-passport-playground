from pathlib import Path
import subprocess
import tempfile
import unittest
ROOT = Path(__file__).resolve().parents[1]
class EarTrainerTests(unittest.TestCase):
    def test_model_and_audio(self):
        app = ROOT / 'firmware/ear-trainer'
        with tempfile.TemporaryDirectory() as tmp:
            exe = Path(tmp) / 'ear-tests'
            subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-I',str(app/'main'),
                            str(app/'tests/test_model.c'),str(app/'main/ear_model.c'),
                            str(app/'main/ear_synth.c'),str(app/'main/ear_controller.c'),'-lm','-o',str(exe)],check=True)
            subprocess.run([str(exe)],check=True)
