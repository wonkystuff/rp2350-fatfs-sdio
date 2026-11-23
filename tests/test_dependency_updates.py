# SPDX-License-Identifier: MIT
# SPDX-FileCopyrightText: 2026 wonkystuff
"""Offline regression tests for updater edits, review preservation and ancestry policy."""
import contextlib
import importlib.util
import io
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("updater", ROOT / ".github/scripts/check-dependency-updates.py")
updater = importlib.util.module_from_spec(spec)
spec.loader.exec_module(updater)
OLD = "a" * 40
NEW = "b" * 40


def fixture(dependency):
    _, target, upstream, _, component = dependency
    source = f"FetchContent_Declare({target}\n GIT_REPOSITORY https://github.com/{upstream}.git\n # pinned revision\n GIT_TAG {OLD}\n)\n"
    notices = f"| {component} | https://github.com/{upstream} at `{OLD}` | Original licence |\n"
    notices += "| Adapted card command sequencing | Same revision, `src/sdfat_sdcard_rp2350.cpp` | MIT |\n"
    return source, notices


class DependencyUpdateTests(unittest.TestCase):
    def test_updates_only_build_pin_and_preserves_historical_attribution(self):
        dep = updater.DEPENDENCIES[1]
        source, notices = fixture(dep)
        changed, provenance = updater.updated_files(source, notices, dep, OLD, NEW)
        self.assertEqual(updater.read_pin(changed, dep[1], dep[2]), NEW)
        self.assertIn(f"https://github.com/{dep[2]} at `{NEW}`", provenance)
        self.assertIn(f"Historical reference at `{OLD}`", provenance)
        self.assertIn("Original licence", provenance)
        # Subsequent updates cannot move the historical attribution again.
        _, later = updater.updated_files(changed, provenance, dep, NEW, "c" * 40)
        self.assertIn(f"Historical reference at `{OLD}`", later)

    def test_refuses_stale_or_ambiguous_metadata(self):
        dep = updater.DEPENDENCIES[0]
        source, notices = fixture(dep)
        for bad_source, bad_notices in ((source * 2, notices), (source, notices.replace(OLD, NEW))):
            with self.assertRaises(ValueError):
                updater.updated_files(bad_source, bad_notices, dep, OLD, NEW)

    def test_closed_or_open_existing_pr_is_not_recreated_or_overwritten(self):
        dep = updater.DEPENDENCIES[0]
        source, notices = fixture(dep)
        with patch.object(updater, "run", return_value='[{"url": "https://example.test/pr/1"}]') as run:
            result = updater.propose("owner/repo", "main", dep, OLD, NEW, source, notices, {})
        self.assertIn("already reviewed/proposed", result)
        self.assertEqual(run.call_count, 1)

    def test_newer_tip_does_not_create_parallel_proposals_for_one_dependency(self):
        dep = updater.DEPENDENCIES[0]
        source, notices = fixture(dep)
        pending = '[{"url": "https://example.test/pr/2", "headRefName": "updates/fatfs-earlier"}]'
        with patch.object(updater, "run", side_effect=["[]", pending]) as run:
            result = updater.propose("owner/repo", "main", dep, OLD, NEW, source, notices, {})
        self.assertIn("finish existing review first", result)
        self.assertEqual(run.call_count, 2)

    def test_no_changes_and_diverged_history_create_no_branches(self):
        dep = updater.DEPENDENCIES[0]
        source, notices = fixture(dep)
        for tip, status, fails in ((OLD, None, False), (NEW, "diverged", True), (NEW, "ahead", False)):
            def api(endpoint):
                if "/git/ref/" in endpoint:
                    return {"object": {"sha": tip}}
                return {"status": status}
            with tempfile.TemporaryDirectory() as temp:
                root = Path(temp)
                (root / "CMakeLists.txt").write_text(source)
                (root / "NOTICE.md").write_text(notices)
                with chdir(root):
                    with patch.object(updater, "DEPENDENCIES", (dep,)), patch.object(updater, "github", side_effect=api), \
                         patch.object(updater, "propose") as propose, patch.dict(os.environ, {"GITHUB_REPOSITORY": "owner/repo"}), \
                         patch.object(sys, "argv", ["updater", "--check-only"]), contextlib.redirect_stdout(io.StringIO()):
                        if fails:
                            with self.assertRaises(SystemExit):
                                updater.main()
                        else:
                            updater.main()
                        propose.assert_not_called()

    def test_candidate_branch_has_only_expected_edits_and_does_not_move_main(self):
        dep = updater.DEPENDENCIES[1]
        source, notices = fixture(dep)
        real_run = updater.run
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            remote, repo = root / "remote.git", root / "repo"
            real_run("git", "init", "--bare", str(remote))
            real_run("git", "init", "-b", "main", str(repo))
            (repo / "CMakeLists.txt").write_text(source)
            (repo / "NOTICE.md").write_text(notices)
            (repo / "unrelated.txt").write_text("preserve me\n")
            real_run("git", "add", ".", cwd=repo)
            real_run("git", "-c", "user.name=Test", "-c", "user.email=test@example.test", "commit", "-m", "baseline", cwd=repo)
            real_run("git", "remote", "add", "origin", str(remote), cwd=repo)
            original = real_run("git", "rev-parse", "HEAD", cwd=repo)
            def run(*args, **kwargs):
                if args[:3] == ("gh", "pr", "list"):
                    return "[]"
                if args[:3] == ("gh", "pr", "create"):
                    body = Path(args[args.index("--body-file") + 1]).read_text()
                    self.assertIn(f"/compare/{OLD}...{NEW}", body)
                    self.assertIn("--draft", args)
                    return "https://example.test/pr/1"
                return real_run(*args, **kwargs)
            with chdir(repo), patch.object(updater, "run", side_effect=run):
                updater.propose("owner/repo", "main", dep, OLD, NEW, source, notices, {"commits": []})
                self.assertEqual(real_run("git", "rev-parse", "HEAD"), original)
                self.assertEqual(real_run("git", "status", "--porcelain"), "")
                candidate = f"updates/sdio-{NEW[:12]}"
                self.assertEqual(real_run("git", "diff", "--name-only", f"main..{candidate}"), "CMakeLists.txt\nNOTICE.md")
                # An existing remote branch must prevent a second push/PR even if
                # the preceding run stopped between publishing its branch and PR.
                with self.assertRaises(ValueError):
                    updater.propose("owner/repo", "main", dep, OLD, NEW, source, notices, {})


@contextlib.contextmanager
def chdir(path):
    previous = Path.cwd()
    os.chdir(path)
    try:
        yield
    finally:
        os.chdir(previous)


if __name__ == "__main__":
    unittest.main()
