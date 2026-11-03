# SPDX-License-Identifier: MIT
# SPDX-FileCopyrightText: 2026 wonkystuff
"""Fetch the exact FetchContent revisions declared by the library."""
import pathlib
import re
import subprocess

source = pathlib.Path("CMakeLists.txt").read_text()
root = pathlib.Path("_deps")
root.mkdir(exist_ok=True)
for name, repository, revision in re.findall(
    r"FetchContent_Declare\((\w+)\s+GIT_REPOSITORY\s+(\S+)\s+(?:#[^\n]*\n\s*)*GIT_TAG\s+([0-9a-f]{40})\s*\)", source
):
    destination = root / name
    subprocess.run(["git", "init", str(destination)], check=True)
    subprocess.run(["git", "-C", str(destination), "fetch", "--depth=1", repository, revision], check=True)
    subprocess.run(["git", "-C", str(destination), "checkout", "--detach", "FETCH_HEAD"], check=True)
if len(list(root.glob("*/.git"))) != 2:
    raise SystemExit("Expected exactly two pinned FetchContent dependencies")
