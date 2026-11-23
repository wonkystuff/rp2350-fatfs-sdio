# SPDX-License-Identifier: MIT
# SPDX-FileCopyrightText: 2026 wonkystuff
"""Propose pinned FetchContent updates; never execute upstream code or merge PRs."""
import argparse
import json
import os
from pathlib import Path
import re
import subprocess
import tempfile
from urllib.parse import quote

DEPENDENCIES = (
    ("fatfs", "rp2350_fatfs_sdio_fatfs", "abbrev/fatfs", "master", "FatFs"),
    ("sdio", "rp2350_fatfs_sdio_driver", "rabbitholecomputing/SDIO_RP2350", "main", "Low-level SDIO driver and PIO"),
)
SHA = re.compile(r"[0-9a-f]{40}")


def run(*args, cwd=None):
    return subprocess.run(args, cwd=cwd, check=True, text=True, capture_output=True).stdout.strip()


def github(endpoint):
    return json.loads(run("gh", "api", endpoint))


def pin_pattern(target, upstream):
    # Restrict edits to the exact dependency declaration and expected source URL.
    return re.compile(
        r"(FetchContent_Declare\(" + re.escape(target) +
        r"\s+GIT_REPOSITORY\s+https://github\.com/" + re.escape(upstream) +
        r"\.git\s+(?:#[^\n]*\n\s*)*GIT_TAG\s+)([0-9a-f]{40})(\s*\))"
    )


def read_pin(source, target, upstream):
    matches = list(pin_pattern(target, upstream).finditer(source))
    if len(matches) != 1:
        raise ValueError(f"Expected one exact pinned declaration for {target}")
    return matches[0].group(2)


def updated_files(source, notices, dependency, old, new):
    slug, target, upstream, _, component = dependency
    if not SHA.fullmatch(new) or read_pin(source, target, upstream) != old:
        raise ValueError("Invalid or stale dependency revision")
    source = pin_pattern(target, upstream).sub(lambda m: m[1] + new + m[3], source)
    if slug == "fatfs":
        # A new upstream revision may invalidate this description of the old pin.
        source = source.replace(
            "    # R0.16, July 2026 metadata fixes and compressed Unicode tables included.\n", ""
        )
    row = re.compile(r"^\| " + re.escape(component) + r" \| .*?$", re.MULTILINE)
    rows = row.findall(notices)
    if len(rows) != 1 or f"https://github.com/{upstream} at `{old}`" not in rows[0]:
        raise ValueError(f"NOTICE.md must document the current pin for {component}")
    new_row = rows[0].replace(f"`{old}`", f"`{new}`", 1)
    if slug == "fatfs":
        new_row = new_row.replace(
            ", R0.16 with July 2026 metadata fixes and compressed Unicode tables", ""
        )
    notices = notices.replace(rows[0], new_row, 1)
    if slug == "sdio":
        # This is provenance of code already adapted into our source, not a build pin.
        notices = notices.replace(
            "| Adapted card command sequencing | Same revision,",
            f"| Adapted card command sequencing | Historical reference at `{old}`,",
            1,
        )
    return source, notices


def pr_body(upstream, branch, old, new, comparison):
    commits = comparison.get("commits", [])
    entries = [f"- [`{c['sha'][:12]}`](https://github.com/{upstream}/commit/{c['sha']}): "
               + c["commit"]["message"].splitlines()[0].replace("\r", " ")
               for c in commits[:30]]
    extra = "\nFurther commits are listed in the upstream comparison.\n" if len(commits) > 30 else ""
    return (
        f"Proposes updating `{upstream}` from `{old}` to `{new}`, tracking `{branch}`.\n\n"
        f"[Review the upstream changes](https://github.com/{upstream}/compare/{old}...{new}). "
        "The build remains pinned to an exact commit. NOTICE.md records the candidate build revision; "
        "existing source attribution and licence notices are preserved pending review.\n\n"
        "### Upstream commits\n\n" + "\n".join(entries) + extra + "\n\n"
        "### Acceptance before merge/release\n\n"
        "- [ ] Review changed source, API/configuration compatibility and licence/provenance.\n"
        "- [ ] Update retained notices and version descriptions if upstream changes require it.\n"
        "- [ ] Check CI, CodeQL and the source licence inventory on this candidate.\n"
        "- [ ] Record appropriate hardware/card acceptance or outstanding checks.\n"
        "- [ ] Prepare release notes/version only after accepting the update.\n\n"
        "This draft is a provisional update branch, not a release or automatic merge. "
        "If GitHub shows an approval banner, select **Approve workflows to run**.\n"
    )


def propose(repository, base, dependency, old, new, source, notices, comparison):
    slug, _, upstream, tracked_branch, _ = dependency
    branch = f"updates/{slug}-{new[:12]}"
    existing = json.loads(run("gh", "pr", "list", "--repo", repository,
                              "--head", branch, "--state", "all", "--json", "url"))
    if existing:
        return f"{slug}: candidate already reviewed/proposed: {existing[0]['url']}"
    pending = json.loads(run("gh", "pr", "list", "--repo", repository,
                             "--state", "open", "--limit", "100", "--json", "url,headRefName"))
    for pr in pending:
        if pr["headRefName"].startswith(f"updates/{slug}-"):
            return f"{slug}: a newer revision is available; finish existing review first: {pr['url']}"
    # Never overwrite an existing branch or force-push a reviewer's changes.
    if run("git", "ls-remote", "--heads", "origin", f"refs/heads/{branch}"):
        raise ValueError(f"Branch {branch} exists without a PR; manual review needed")
    changed_source, changed_notices = updated_files(source, notices, dependency, old, new)
    base_sha = run("git", "rev-parse", "HEAD")
    with tempfile.TemporaryDirectory(prefix="sdio-dependency-") as temp:
        checkout = Path(temp) / "candidate"
        run("git", "worktree", "add", "--detach", str(checkout), base_sha)
        try:
            run("git", "switch", "-c", branch, cwd=checkout)
            (checkout / "CMakeLists.txt").write_text(changed_source)
            (checkout / "NOTICE.md").write_text(changed_notices)
            run("git", "add", "CMakeLists.txt", "NOTICE.md", cwd=checkout)
            run("git", "-c", "user.name=github-actions[bot]", "-c",
                "user.email=41898282+github-actions[bot]@users.noreply.github.com",
                "commit", "-m", f"Propose {slug} update to {new[:12]}", cwd=checkout)
            # Branch creation is conditional on it still being absent on the remote.
            run("git", "push", "--force-with-lease=refs/heads/" + branch + ":",
                "origin", f"HEAD:refs/heads/{branch}", cwd=checkout)
            body = Path(temp) / "body.md"
            body.write_text(pr_body(upstream, tracked_branch, old, new, comparison))
            url = run("gh", "pr", "create", "--repo", repository, "--base", base,
                      "--head", branch, "--draft", "--title",
                      f"Update {slug} to {new[:12]}", "--body-file", str(body))
            return f"{slug}: opened {url}"
        finally:
            run("git", "worktree", "remove", "--force", str(checkout))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check-only", action="store_true", help="Read upstream state without creating branches or PRs")
    args = parser.parse_args()
    repository = os.environ.get("GITHUB_REPOSITORY")
    base = os.environ.get("GH_BASE_BRANCH", "main")
    if not repository:
        parser.error("Set GITHUB_REPOSITORY=owner/repo")
    source = Path("CMakeLists.txt").read_text()
    notices = Path("NOTICE.md").read_text()
    messages, errors = [], []
    for dependency in DEPENDENCIES:
        slug, target, upstream, branch, _ = dependency
        try:
            old = read_pin(source, target, upstream)
            new = github(f"repos/{upstream}/git/ref/heads/{quote(branch, safe='')}")["object"]["sha"]
            if not SHA.fullmatch(new):
                raise ValueError("Upstream returned an invalid commit SHA")
            if new == old:
                messages.append(f"{slug}: up to date ({old[:12]})")
                continue
            comparison = github(f"repos/{upstream}/compare/{old}...{new}")
            if comparison["status"] != "ahead":
                raise ValueError("Upstream is not a fast-forward from the pin; manual review required")
            updated_files(source, notices, dependency, old, new)
            if args.check_only:
                messages.append(f"{slug}: update available {old[:12]} -> {new[:12]} (check only)")
            else:
                messages.append(propose(repository, base, dependency, old, new, source, notices, comparison))
        except (KeyError, ValueError, subprocess.CalledProcessError) as error:
            detail = error.stderr if isinstance(error, subprocess.CalledProcessError) else str(error)
            errors.append(f"{slug}: {detail.strip()}")
    report = "\n".join(messages + errors)
    print(report)
    if os.environ.get("GITHUB_STEP_SUMMARY"):
        with open(os.environ["GITHUB_STEP_SUMMARY"], "a") as summary:
            summary.write("## Upstream dependency check\n\n" + report + "\n")
    if errors:
        raise SystemExit(1)


if __name__ == "__main__":
    main()
