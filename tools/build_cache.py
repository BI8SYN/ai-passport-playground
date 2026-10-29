"""Prune only disposable configuration build trees; callers own the project lock."""
from pathlib import Path
import re
import shutil

BUILD_NAME = re.compile(r'[0-9a-f]{16}(?:-benchmark)?\Z')

def _last_used(path):
    marker = path / '.last-used'
    if not marker.is_symlink() and marker.is_file():
        return marker.stat().st_mtime
    times = [path.stat().st_mtime]
    for name in ('.ninja_log',):
        marker = path / name
        if not marker.is_symlink() and marker.is_file():
            times.append(marker.stat().st_mtime)
    return max(times)

def _bytes(path):
    # rglob does not traverse directory symlinks; count only local inode blocks.
    paths = [path, *path.rglob('*')]
    return sum(p.lstat().st_blocks * 512 for p in paths)

def prune(root, project, *, keep_normal=2, keep_benchmark=1, protected=(), apply=False):
    root = Path(root)
    if not re.fullmatch(r'[a-z][a-z0-9-]*', project):
        raise ValueError('Invalid build project')
    if keep_normal < 1 or keep_benchmark < 0:
        raise ValueError('Keep at least one normal build; benchmark count cannot be negative')
    directory = root / project
    if root.is_symlink() or directory.is_symlink():
        raise ValueError('Build root/project cannot be a symlink')
    report = {'project': project, 'apply': apply, 'kept': [], 'candidates': [], 'deleted': [],
              'reclaimable_bytes': 0, 'reclaimed_bytes': 0, 'errors': []}
    if not directory.exists():
        return report
    if not directory.is_dir():
        raise ValueError('Build project must be a directory')
    candidates = [p for p in directory.iterdir()
                  if BUILD_NAME.fullmatch(p.name) and not p.is_symlink() and p.is_dir()]
    selected = []
    protected = set(protected)
    for benchmark, limit in ((False, keep_normal), (True, keep_benchmark)):
        group = sorted((p for p in candidates if p.name.endswith('-benchmark') == benchmark),
                       key=lambda p: (_last_used(p), p.name), reverse=True)
        retained = {p.name for p in group if p.name in protected}
        for p in group:
            if p.name in retained:
                continue
            if len(retained) < limit:
                retained.add(p.name)
            else:
                selected.append(p)
        report['kept'].extend(str(p) for p in group if p.name in retained)
    for p in selected:
        report['candidates'].append(str(p))
        try:
            size = _bytes(p)
            report['reclaimable_bytes'] += size
            if apply:
                # Fail closed if the tree was replaced since enumeration.
                if p.is_symlink() or not p.is_dir() or directory.is_symlink() or root.is_symlink():
                    raise ValueError('Build tree changed during cleanup')
                shutil.rmtree(p)
                report['deleted'].append(str(p))
                report['reclaimed_bytes'] += size
        except (OSError, ValueError) as error:
            report['errors'].append({'path': str(p), 'error': str(error)})
    return report
