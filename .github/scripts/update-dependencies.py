# SPDX-License-Identifier: MIT
# SPDX-FileCopyrightText: 2026 wonkystuff
"""Update pinned FetchContent revisions to their upstream default branches."""

from __future__ import annotations

import pathlib
import re
import subprocess
import sys

CMAKE_FILE = pathlib.Path("CMakeLists.txt")
DECLARATION = re.compile(
    r"(FetchContent_Declare\(\s*(?P<name>\w+)\s+"
    r"GIT_REPOSITORY\s+(?P<repository>\S+)\s+"
    r"(?:\#[^\n]*\n\s*)*GIT_TAG\s+)"
    r"(?P<revision>[0-9a-f]{40})(?P<suffix>\s*\))"
)


def upstream_head(repository: str) -> str:
    """Return the commit currently advertised as the repository's HEAD."""
    result = subprocess.run(
        ["git", "ls-remote", repository, "HEAD"],
        check=True,
        capture_output=True,
        text=True,
    )
    fields = result.stdout.split()
    if len(fields) != 2 or fields[1] != "HEAD" or not re.fullmatch(r"[0-9a-f]{40}", fields[0]):
        raise RuntimeError(f"Could not resolve a full commit for {repository} HEAD")
    return fields[0]


def main() -> int:
    source = CMAKE_FILE.read_text(encoding="utf-8")
    matches = list(DECLARATION.finditer(source))
    if not matches:
        raise RuntimeError(f"No SHA-pinned FetchContent declarations found in {CMAKE_FILE}")

    replacements: list[tuple[int, int, str]] = []
    for match in matches:
        name = match.group("name")
        current = match.group("revision")
        latest = upstream_head(match.group("repository"))
        if current == latest:
            print(f"{name}: up to date ({current})")
            continue
        print(f"{name}: {current} -> {latest}")
        replacements.append((match.start("revision"), match.end("revision"), latest))

    for start, end, revision in reversed(replacements):
        source = source[:start] + revision + source[end:]
    if replacements:
        CMAKE_FILE.write_text(source, encoding="utf-8")
    else:
        print("No dependency updates found.")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, RuntimeError, subprocess.CalledProcessError) as error:
        print(f"Dependency update check failed: {error}", file=sys.stderr)
        raise SystemExit(1)
