import importlib.util
import os
from pathlib import Path
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]

class BuildCacheTests(unittest.TestCase):
    def setUp(self):
        spec = importlib.util.spec_from_file_location('build_cache', ROOT / 'tools/build_cache.py')
        self.cache = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(self.cache)
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self.project = self.root / 'resistor-reader'
        self.project.mkdir()

    def build(self, index, benchmark=False):
        p = self.project / (f'{index:016x}' + ('-benchmark' if benchmark else ''))
        p.mkdir()
        (p / 'object.o').write_bytes(b'cached' * 100)
        marker = p / '.last-used'
        marker.write_text('')
        os.utime(marker, (index, index))
        return p

    def test_preview_retains_two_normal_one_benchmark_without_deleting(self):
        paths = [self.build(i) for i in range(1, 5)]
        bench = [self.build(i, True) for i in range(5, 8)]
        report = self.cache.prune(self.root, 'resistor-reader')
        self.assertEqual(set(report['candidates']), {str(p) for p in paths[:2] + bench[:2]})
        self.assertTrue(all(p.exists() for p in paths + bench))
        self.assertEqual(report['deleted'], [])
        self.assertGreater(report['reclaimable_bytes'], 0)

    def test_apply_protects_current_and_preserves_archives_unknown_and_outside_symlinks(self):
        current = self.build(1)
        older = self.build(2)
        newest = self.build(3)
        archives = self.project / 'archives'; archives.mkdir()
        (archives / 'debug.elf').write_text('retain')
        unknown = self.project / 'manual-build'; unknown.mkdir()
        outside = self.root / 'outside'; outside.mkdir()
        (outside / 'important').write_text('retain')
        (self.project / 'ffffffffffffffff').symlink_to(outside, target_is_directory=True)
        (newest / 'external-link').symlink_to(outside, target_is_directory=True)
        (older / 'external-link').symlink_to(outside, target_is_directory=True)
        report = self.cache.prune(self.root, 'resistor-reader', protected={current.name}, apply=True)
        self.assertEqual(report['deleted'], [str(older)])
        self.assertTrue(current.exists() and newest.exists() and archives.exists() and unknown.exists())
        self.assertEqual((outside / 'important').read_text(), 'retain')
        self.assertTrue((self.project / 'ffffffffffffffff').is_symlink())

    def test_rejects_escape_and_symlinked_project(self):
        for name in ('../outside', '/tmp', 'a/b'):
            with self.assertRaises(ValueError): self.cache.prune(self.root, name, apply=True)
        self.project.rmdir()
        outside = self.root / 'outside'; outside.mkdir()
        self.project.symlink_to(outside, target_is_directory=True)
        with self.assertRaises(ValueError): self.cache.prune(self.root, 'resistor-reader', apply=True)

    def test_legacy_ninja_log_used_when_no_marker_and_separate_retention(self):
        paths = [self.build(i) for i in range(1, 4)]
        for i,p in enumerate(paths):
            (p / '.last-used').unlink()
            (p / '.ninja_log').write_text('legacy')
            os.utime(p / '.ninja_log', (100 + i, 100 + i))
            os.utime(p, (1, 1))
        bench = self.build(4, True)
        report = self.cache.prune(self.root, 'resistor-reader', keep_normal=1, keep_benchmark=0, apply=True)
        self.assertEqual(set(report['deleted']), {str(p) for p in paths[:2] + [bench]})
        self.assertTrue(paths[2].exists())

    def test_missing_project_is_noop_and_invalid_limits_rejected(self):
        report = self.cache.prune(self.root, 'hardware-test', apply=True)
        self.assertEqual(report['deleted'], [])
        for kwargs in ({'keep_normal':0}, {'keep_normal':-1}, {'keep_benchmark':-1}):
            with self.assertRaises(ValueError): self.cache.prune(self.root, 'resistor-reader', **kwargs)

    def test_explicit_cleanup_skips_locked_project(self):
        import contextlib
        import fcntl
        import io
        from unittest.mock import patch
        from types import SimpleNamespace
        spec = importlib.util.spec_from_file_location('firmware_cleanup_test', ROOT / 'tools/firmware.py')
        firmware = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(firmware)
        paths = [self.build(i) for i in range(1, 5)]
        locks = self.root / 'locks'; locks.mkdir()
        with (locks / 'resistor-reader.lock').open('a') as lock:
            fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
            output = io.StringIO()
            with patch.object(firmware, 'BUILD_ROOT', self.root), contextlib.redirect_stdout(output):
                firmware.clean_builds(SimpleNamespace(project='resistor-reader', keep=2,
                                                     keep_benchmark=1, apply=True))
        self.assertIn('build is running', output.getvalue())
        self.assertTrue(all(p.exists() for p in paths))

    def test_protects_current_configuration_across_build_modes(self):
        normal = [self.build(i) for i in range(1, 4)]
        bench = [self.build(i, True) for i in range(1, 4)]
        report = self.cache.prune(self.root, 'resistor-reader',
                                 protected={normal[0].name, bench[0].name}, apply=True)
        self.assertTrue(normal[0].exists() and normal[2].exists() and bench[0].exists())
        self.assertEqual(set(report['deleted']), {str(normal[1]), str(bench[1]), str(bench[2])})

    def test_auto_cleanup_only_after_archive_export_and_protects_both_modes(self):
        from unittest.mock import patch
        spec = importlib.util.spec_from_file_location('firmware_export_test', ROOT / 'tools/firmware.py')
        firmware = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(firmware)
        identity = '0123456789abcdef'
        events = []
        with patch.object(firmware, 'BUILD_ROOT', self.root), \
             patch.object(firmware, 'config_identity', return_value=identity), \
             patch.object(firmware, 'run'), \
             patch.object(firmware, 'export_archive') as export, \
             patch.object(firmware, 'prune') as prune:
            export.side_effect = ValueError('archive verification failed')
            with self.assertRaises(ValueError):
                firmware.build_project('resistor-reader', 'test', benchmark=True)
            prune.assert_not_called()
            export.side_effect = lambda *a: events.append('exported')
            prune.side_effect = lambda *a, **kw: events.append('pruned') or {}
            firmware.build_project('resistor-reader', 'test', benchmark=True)
            self.assertEqual(events, ['exported', 'pruned'])
            self.assertEqual(prune.call_args.kwargs['protected'], {identity, identity + '-benchmark'})
