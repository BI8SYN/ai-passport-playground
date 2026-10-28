#!/usr/bin/env bash
# Portable document freshness check for macOS and Linux.
set -euo pipefail
python3 - <<'CHECK'
from datetime import date
from pathlib import Path
import json
import re
import subprocess

messages = []
for path in sorted(Path("docs").rglob("*.md")):
    match = re.search(r"^last-reviewed: (\d{4}-\d{2}-\d{2})$", path.read_text(), re.M)
    if match:
        age = (date.today() - date.fromisoformat(match.group(1))).days
        if age > 30:
            messages.append(f"{path}: {age} days since review")
try:
    branch = subprocess.run(["git", "branch", "--show-current"], check=True,
                            capture_output=True, text=True).stdout.strip()
    if branch and branch != "main":
        base = subprocess.run(["git", "merge-base", "HEAD", "main"], check=True,
                              capture_output=True, text=True).stdout.strip()
        changes = subprocess.run(["git", "diff", "--name-only", base, "HEAD"],
                                 check=True, capture_output=True, text=True).stdout.splitlines()
        if len(changes) >= 10:
            messages.append(f"{len(changes)} committed files changed relative to main")
except (OSError, subprocess.CalledProcessError):
    pass
if messages:
    print(json.dumps({"systemMessage": "文档检查提示：" + "; ".join(messages)}, ensure_ascii=False))
CHECK
