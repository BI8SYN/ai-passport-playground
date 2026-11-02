#!/usr/bin/env python3
"""用现有 Apple Swift 编译器构建诊断桥；不启动、不安装工具链。"""
from __future__ import annotations

import argparse
import json
import pathlib
import platform
import plistlib
import shutil
import subprocess
import sys


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=pathlib.Path)
    args = parser.parse_args()
    source = pathlib.Path(__file__).resolve().parent
    root = source.parent.parent
    bundle = (args.output or root / "artifacts/handmic-simulator/RFLinkBLESimulator.app").resolve()
    if sys.platform != "darwin":
        parser.error("CoreBluetooth 诊断桥只能在 macOS 使用现有 xcrun/swiftc 构建")
    binary = bundle / "Contents/MacOS/RFLinkBLESimulator"
    binary.parent.mkdir(parents=True, exist_ok=True)
    cache = bundle.parent / "swift-module-cache"
    cache.mkdir(parents=True, exist_ok=True)
    plist = plistlib.loads((source / "Info.plist").read_bytes())
    assert plist["NSBluetoothAlwaysUsageDescription"]
    assert plist["CFBundleExecutable"] == binary.name
    target = f"{platform.machine()}-apple-macosx{plist['LSMinimumSystemVersion']}"
    subprocess.run([
        "xcrun", "swiftc", "-swift-version", "5", "-O", "-warnings-as-errors",
        "-target", target,
        "-module-cache-path", str(cache), "-framework", "AppKit", "-framework", "CoreBluetooth",
        "-o", str(binary), str(source / "BLEBridge.swift"),
    ], check=True, cwd=root)
    shutil.copyfile(source / "Info.plist", bundle / "Contents/Info.plist")
    # Ad-hoc identity is local only; this neither installs nor launches the app.
    subprocess.run(["codesign", "--force", "--sign", "-", str(bundle)], check=True)
    subprocess.run(["codesign", "--verify", "--strict", str(bundle)], check=True)
    print(json.dumps({"bundle": str(bundle), "target": target, "compiled": True, "launched": False}, ensure_ascii=False))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
