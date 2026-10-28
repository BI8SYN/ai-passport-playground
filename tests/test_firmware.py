import importlib.util
import contextlib
import io
from pathlib import Path
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]


class FirmwareToolTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        spec = importlib.util.spec_from_file_location("firmware", ROOT / "tools/firmware.py")
        cls.tool = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(cls.tool)

    def test_upstream_and_monorepo_have_separate_build_identities(self):
        self.assertEqual(self.tool.project_path(ROOT, "upstream"), ROOT / "vendor/ai-passport")
        self.assertEqual(self.tool.project_path(ROOT, "hardware-test"), ROOT / "firmware/hardware-test")

    def test_unknown_and_traversal_targets_are_rejected(self):
        for name in ("../vendor/ai-passport", "/tmp", "missing", "--help", "a/b"):
            with self.subTest(name=name), self.assertRaises(ValueError):
                self.tool.project_path(ROOT, name)

    def test_symlink_cannot_escape_project_directory(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / "firmware").mkdir()
            outside = root / "outside"
            outside.mkdir()
            (outside / "CMakeLists.txt").write_text("project(test)")
            (root / "firmware/hardware-test").symlink_to(outside, target_is_directory=True)
            with self.assertRaises(ValueError):
                self.tool.project_path(root, "hardware-test")

    def test_flash_and_arbitrary_idf_commands_are_not_exposed(self):
        for args in (["flash"], ["build", "upstream", "flash"], ["monitor"], ["erase-flash"]):
            with self.subTest(args=args), contextlib.redirect_stderr(io.StringIO()), self.assertRaises(SystemExit):
                self.tool.parse_args(args)

    def test_benchmark_is_explicit_and_product_only(self):
        self.assertTrue(self.tool.parse_args(["build", "resistor-reader", "--benchmark"]).benchmark)
        self.assertFalse(self.tool.parse_args(["build", "resistor-reader"]).benchmark)
        with contextlib.redirect_stderr(io.StringIO()), self.assertRaises(SystemExit):
            self.tool.parse_args(["build", "upstream", "--benchmark"])

    def test_configuration_identity_tracks_config_not_application_source(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            project = root / "firmware/test"
            project.mkdir(parents=True)
            (project / "CMakeLists.txt").write_text("project(test)")
            defaults = project / "sdkconfig.defaults"
            defaults.write_text("CONFIG_TEST=y\n")
            first = self.tool.config_identity(root, project)
            (project / "main.c").write_text("void app_main(void) {}")
            self.assertEqual(first, self.tool.config_identity(root, project))
            defaults.write_text("CONFIG_TEST=n\n")
            self.assertNotEqual(first, self.tool.config_identity(root, project))

    def test_shared_configuration_changes_identity(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            project = root / "firmware/test"
            project.mkdir(parents=True)
            (root / "config").mkdir()
            common = root / "config/passport.defaults"
            common.write_text("CONFIG_A=y\n")
            first = self.tool.config_identity(root, project)
            common.write_text("CONFIG_A=n\n")
            self.assertNotEqual(first, self.tool.config_identity(root, project))


if __name__ == "__main__":
    unittest.main()
