#!/usr/bin/env python3
"""Validate named product images using upstream parsing and ELF descriptor rules."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
import sys
import tempfile

sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "vendor/ai-passport/tools"))
import verify_firmware as layout
from archive_firmware import application_descriptor, read_artifact, safe_path


def inspect(directory, app):
    if not re.fullmatch(r"[A-Za-z0-9_-]{1,31}", app):
        raise ValueError("Invalid firmware project name")
    directory = safe_path(directory)
    names = [f"{app}.bin", f"{app}.elf", f"{app}.map", f"{app}-full.bin",
             "bootloader/bootloader.bin", "partition_table/partition-table.bin", "flash_args"]
    data = {name: read_artifact(directory, name) for name in names}
    args = data["flash_args"].decode("utf-8")
    if "--flash_size 8MB" not in args:
        raise ValueError("Expected 8 MB flash configuration")
    offsets = layout.parse_flash_args(args)
    required = {f"{app}.bin", "bootloader/bootloader.bin", "partition_table/partition-table.bin"}
    # These products use the minimal baseline layout, not additional resource images.
    if set(offsets) != required or offsets["bootloader/bootloader.bin"] != 0:
        raise ValueError("Unexpected image names or bootloader offset")
    full = data[f"{app}-full.bin"]
    if len(full) > layout.FLASH_SIZE:
        raise ValueError("Merged image exceeds 8 MB")
    layout.verify_flash_images(full, directory, offsets)
    table_offset = offsets["partition_table/partition-table.bin"]
    if table_offset < len(data["bootloader/bootloader.bin"]) or table_offset % 0x1000:
        raise ValueError("Invalid partition table location")
    partitions, md5 = layout.parse_partition_table(data["partition_table/partition-table.bin"], table_offset + 0x1000)
    ordered = sorted(partitions, key=lambda p: p.offset)
    if not md5 or len({p.label for p in partitions}) != len(partitions):
        raise ValueError("Invalid partition MD5 or duplicate labels")
    if any(a.end > b.offset for a, b in zip(ordered, ordered[1:])):
        raise ValueError("Partitions overlap")
    apps = [p for p in partitions if p.kind == 0 and p.offset == offsets[f"{app}.bin"]]
    if len(apps) != 1 or len(data[f"{app}.bin"]) > apps[0].size:
        raise ValueError("Application must fit its configured app partition")
    descriptor = application_descriptor(data[f"{app}.bin"])
    elf = data[f"{app}.elf"]
    if len(elf) < 52 or elf[:6] != b"\x7fELF\x01\x01" or struct.unpack_from("<H", elf, 18)[0] != 243:
        raise ValueError("Expected 32-bit little-endian RISC-V ELF")
    if descriptor["project_name"] != app or descriptor["embedded_elf_sha256"] != hashlib.sha256(elf).hexdigest():
        raise ValueError("Project identity or embedded ELF SHA256 mismatch")
    manifest = {
        "schema_version": 1, "target": "esp32c3", "project": app,
        "flash_size_bytes": layout.FLASH_SIZE,
        "full_bin_sha256": hashlib.sha256(full).hexdigest(),
        "app_elf_sha256": hashlib.sha256(elf).hexdigest(),
        "app_descriptor": descriptor, "image_offsets": offsets,
        "files": {name: {"size": len(value), "sha256": hashlib.sha256(value).hexdigest()} for name, value in data.items()},
    }
    return manifest, data


def verify(directory, app):
    directory = safe_path(directory)
    manifest, _ = inspect(directory, app)
    stored = json.loads(read_artifact(directory, "manifest.json"))
    if stored != manifest or directory.name != manifest["full_bin_sha256"]:
        raise ValueError("Archive manifest or directory identity mismatch")
    actual = set()
    for path in directory.rglob("*"):
        if path.is_symlink():
            raise ValueError("Archive cannot contain symlinks")
        if path.is_file():
            actual.add(path.relative_to(directory).as_posix())
    if actual != set(manifest["files"]) | {"manifest.json"}:
        raise ValueError("Unexpected archive files")
    print(f"Product firmware archive: PASS ({directory})")
    return manifest


def create(build, archive_root, app):
    manifest, data = inspect(build, app)
    archive_root = safe_path(archive_root)
    archive_root.mkdir(parents=True, exist_ok=True)
    destination = archive_root / manifest["full_bin_sha256"]
    if destination.exists():
        previous = verify(destination, app)
        # Like the upstream archiver, retain the first MAP for identical images/ELF.
        if any(previous["files"][name] != metadata for name, metadata in manifest["files"].items() if name != f"{app}.map"):
            raise ValueError("Conflicting existing archive")
        return destination
    with tempfile.TemporaryDirectory(prefix=".package-", dir=archive_root) as temporary:
        staging = Path(temporary) / destination.name
        staging.mkdir()
        for name, content in data.items():
            target = staging / name
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(content)
        (staging / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
        verify(staging, app)
        staging.rename(destination)
    return destination


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("command", choices=["create", "verify"])
    parser.add_argument("directory", type=Path)
    parser.add_argument("--app", required=True)
    parser.add_argument("--archive-root", type=Path)
    args = parser.parse_args()
    try:
        if args.command == "verify":
            verify(args.directory, args.app)
        else:
            if args.archive_root is None:
                parser.error("create requires --archive-root")
            print(create(args.directory, args.archive_root, args.app))
    except (ValueError, OSError) as error:
        print(f"ERROR: {error}", file=sys.stderr)
        sys.exit(1)
