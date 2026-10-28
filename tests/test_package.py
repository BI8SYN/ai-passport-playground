import importlib.util
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class ProductPackageTests(unittest.TestCase):
    def setUp(self):
        self.assertTrue((ROOT / "tools/package_firmware.py").exists(), "Generic product packager missing")
        self.package = load("product_package", ROOT / "tools/package_firmware.py")
        fixture_module = load("original_archive_fixtures", ROOT / "vendor/ai-passport/tests/test_archive_firmware.py")
        self.fixture = fixture_module.FirmwareArchiveTest()
        self.fixture.setUp()
        self.addCleanup(self.fixture.doCleanups)
        fixture = self.fixture
        for suffix in ("bin", "elf", "map"):
            (fixture.build / f"FoloToy-AI-Passport.{suffix}").rename(fixture.build / f"resistor-reader.{suffix}")
        (fixture.build / fixture.full_name).unlink()
        fixture.app_name = "resistor-reader.bin"
        fixture.full_name = "resistor-reader-full.bin"
        fixture.offsets[fixture.app_name] = fixture.offsets.pop("FoloToy-AI-Passport.bin")
        app = bytearray((fixture.build / fixture.app_name).read_bytes())
        app[80:112] = b"resistor-reader".ljust(32, b"\x00")
        fixture.write(fixture.app_name, app)
        args = (fixture.build / "flash_args").read_text().replace("FoloToy-AI-Passport.bin", fixture.app_name)
        (fixture.build / "flash_args").write_text(args)
        fixture.merge()

    def test_product_identity_and_round_trip(self):
        directory = self.package.create(self.fixture.build, self.fixture.output, "resistor-reader")
        result = self.package.verify(directory, "resistor-reader")
        self.assertEqual(result["app_descriptor"]["project_name"], "resistor-reader")
        self.assertEqual(result["full_bin_sha256"], directory.name)
        self.assertTrue((directory / "resistor-reader.elf").exists())

    def test_wrong_elf_and_tampered_archive_are_rejected(self):
        directory = self.package.create(self.fixture.build, self.fixture.output, "resistor-reader")
        elf = directory / "resistor-reader.elf"
        elf.write_bytes(elf.read_bytes() + b"changed")
        with self.assertRaises(ValueError):
            self.package.verify(directory, "resistor-reader")

    def test_invalid_project_name_is_rejected(self):
        with self.assertRaises(ValueError):
            self.package.create(self.fixture.build, self.fixture.output, "../resistor-reader")
