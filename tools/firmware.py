#!/usr/bin/env python3
"""Build-only entrypoint. Runs inside the pinned ESP-IDF Compose service."""

import argparse
import fcntl
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
from datetime import datetime, timezone


ROOT = Path(__file__).resolve().parents[1]
BUILD_ROOT = Path("/build")
APP = "FoloToy-AI-Passport"
TARGETS = {"upstream": "vendor/ai-passport", "hardware-test": "firmware/hardware-test",
           "resistor-reader": "firmware/resistor-reader"}


def app_name(name):
    return "resistor-reader" if name == "resistor-reader" else APP


def archive_command(name, action, directory):
    if name == "resistor-reader":
        return [sys.executable, ROOT / "tools/package_firmware.py", action, directory, "--app", app_name(name)]
    return [sys.executable, ROOT / "vendor/ai-passport/tools/archive_firmware.py", action, directory]


def project_path(root, name):
    if name not in TARGETS:
        raise ValueError(f"Unknown project {name!r}; choose {', '.join(TARGETS)}")
    path = root / TARGETS[name]
    if path.resolve() != path or not (path / "CMakeLists.txt").is_file():
        raise ValueError(f"Project must be a real in-repository ESP-IDF directory: {path}")
    return path


def parse_args(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    for name in ("doctor", "gate", "static", "test", "cache-stats"):
        commands.add_parser(name)
    preview = commands.add_parser("preview")
    preview.add_argument("project", choices=["resistor-reader"])
    build = commands.add_parser("build")
    build.add_argument("project", nargs="?", default="hardware-test", choices=TARGETS)
    build.add_argument("--benchmark", action="store_true", help="Run renderer diagnostic comparison at startup (resistor-reader only)")
    args = parser.parse_args(argv)
    if getattr(args, "benchmark", False) and args.project != "resistor-reader":
        parser.error("--benchmark is only available for resistor-reader")
    return args


def config_identity(root, project):
    """Config changes get a new build tree; ordinary source edits stay incremental."""
    paths = set()
    directories = [project, root / "config", root / "cmake", root / "components",
                   root / "vendor/ai-passport/components", root / "vendor/ai-passport/main"]
    for directory in directories:
        if not directory.is_dir():
            continue
        for path in directory.rglob("*"):
            if {"managed_components", "build", "__pycache__"}.intersection(path.relative_to(directory).parts):
                continue
            if path.is_file() and (path.name in ("CMakeLists.txt", "idf_component.yml", "dependencies.lock")
                                   or path.name.startswith(("sdkconfig.defaults", "Kconfig"))
                                   or path.suffix in (".cmake", ".csv", ".defaults")):
                paths.add(path)
    for relative in ("Dockerfile", "vendor/ai-passport-source.json", "vendor/ai-passport/sdkconfig.defaults"):
        path = root / relative
        if path.is_file():
            paths.add(path)
    digest = hashlib.sha256()
    for path in sorted(paths):
        digest.update(str(path.relative_to(root)).encode() + b"\0" + path.read_bytes() + b"\0")
    return digest.hexdigest()[:16]


def run(command, cwd=ROOT, capture=False):
    print("+ " + " ".join(map(str, command)), flush=True)
    return subprocess.run(list(map(str, command)), cwd=cwd, check=True,
                          text=True, stdout=subprocess.PIPE if capture else None)


def check_environment():
    if not Path("/.dockerenv").exists():
        raise ValueError("Use ./tools/dev; toolchains must run inside Docker")
    version = run(["idf.py", "--version"], capture=True).stdout.strip()
    if not re.search(r"\bESP-IDF v5\.5\.3(?:\s|$)", version):
        raise ValueError(f"Expected ESP-IDF v5.5.3, got {version}")
    print(version, flush=True)
    return version


def export_archive(name, build_dir, archive_root, mode, idf_version):
    image = build_dir / f"{app_name(name)}-full.bin"
    image_hash = hashlib.sha256(image.read_bytes()).hexdigest()
    source = archive_root / image_hash
    run(archive_command(name, "verify", source))
    output_root = ROOT / "artifacts" / name
    output_root.mkdir(parents=True, exist_ok=True)
    output = output_root / image_hash
    if not output.exists():
        # A uniquely named temporary directory avoids overwriting any prior output.
        import tempfile
        with tempfile.TemporaryDirectory(prefix=".export-", dir=output_root) as temporary:
            staging = Path(temporary) / image_hash
            shutil.copytree(source, staging)
            run(archive_command(name, "verify", staging))
            staging.rename(output)
    run(archive_command(name, "verify", output))
    git_sha = run(["git", "rev-parse", "HEAD"], capture=True).stdout.strip()
    dirty = bool(run(["git", "status", "--porcelain", "--untracked-files=normal"], capture=True).stdout)
    manifest = json.loads((output / "manifest.json").read_text())
    provenance = {
        "project": name, "mode": mode, "upstream": json.loads((ROOT / "vendor/ai-passport-source.json").read_text()),
        "repository_commit": git_sha, "repository_dirty": dirty, "idf_version": idf_version,
        "base_image": "espressif/idf:v5.5.3@sha256:8ccd4d2ce413889c6c2bba57e986c670302094efb91c913c6091152e317a7805",
        "full_bin_sha256": image_hash, "app_elf_sha256": manifest["app_elf_sha256"],
        "full_bin_name": image.name,
        "verified_archive": str(output.relative_to(ROOT)), "completed_at": datetime.now(timezone.utc).isoformat(),
        "device_tests": "NOT RUN",
    }
    # Sidecars stay outside the upstream archive, whose file set is validated.
    provenance_path = output_root / f"{image_hash}.build.json"
    provenance_path.write_text(json.dumps(provenance, indent=2) + "\n")
    latest = output_root / "latest.json"
    temporary_latest = latest.with_suffix(".json.tmp")
    temporary_latest.write_text(json.dumps(provenance, indent=2) + "\n")
    temporary_latest.replace(latest)
    print(f"Verified firmware archive: {output}\nFull image SHA256: {image_hash}", flush=True)


def build_project(name, idf_version, benchmark=False):
    project = project_path(ROOT, name)
    build = BUILD_ROOT / name / (config_identity(ROOT, project) + ("-benchmark" if benchmark else ""))
    build.mkdir(parents=True, exist_ok=True)
    command = ["idf.py", "-B", build, "-D", f"SDKCONFIG={build / 'sdkconfig'}"]
    if name == "resistor-reader":
        command += ["-D", f"RR_BENCHMARK={'ON' if benchmark else 'OFF'}"]
    run(command + ["build"], cwd=project)
    run(["idf.py", "-B", build, "merge-bin", "-o", build / f"{app_name(name)}-full.bin"], cwd=project)
    upstream = project_path(ROOT, "upstream")
    if name != "resistor-reader":
        run([sys.executable, upstream / "tools/verify_firmware.py", build])
    archive_root = BUILD_ROOT / name / "archives"
    run(archive_command(name, "create", build) + ["--archive-root", archive_root])
    export_archive(name, build, archive_root, "benchmark" if benchmark else "incremental", idf_version)


def main(argv=None):
    args = parse_args(argv)
    if args.command == "test":
        run([sys.executable, "-m", "unittest", "discover", "-s", "tests"])
        return
    version = check_environment()
    if args.command == "doctor":
        run(["ccache", "--version"])
        run(["actionlint", "--version"])
        for key in ("CCACHE_DIR", "IDF_COMPONENT_CACHE_PATH", "XDG_CACHE_HOME"):
            path = Path(os.environ[key])
            path.mkdir(parents=True, exist_ok=True)
            print(f"{key}={path}")
        return
    if args.command == "cache-stats":
        run(["ccache", "--show-stats"])
        return
    if args.command == "preview":
        run([sys.executable, ROOT / "tools/render_resistor_preview.py"])
        return
    name = args.project if args.command == "build" else "upstream"
    lock_dir = BUILD_ROOT / "locks"
    lock_dir.mkdir(parents=True, exist_ok=True)
    with (lock_dir / f"{name}.lock").open("a") as lock:
        fcntl.flock(lock, fcntl.LOCK_EX)
        if args.command == "build":
            build_project(name, version, args.benchmark)
        else:
            project = project_path(ROOT, "upstream")
            run(["bash", "tools/validate.sh"] + (["--static"] if args.command == "static" else []), cwd=project)
            if args.command == "gate":
                export_archive("upstream", project / "build", project / "build/firmware", "full-gate", version)


if __name__ == "__main__":
    try:
        main()
    except (ValueError, OSError, subprocess.CalledProcessError) as error:
        print(f"ERROR: {error}", file=sys.stderr)
        sys.exit(1)
