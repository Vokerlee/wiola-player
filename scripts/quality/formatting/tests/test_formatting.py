from contextlib import redirect_stderr
from io import StringIO
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

from scripts.quality.formatting import format_sources, install_hook
from scripts.quality.formatting.__main__ import main


class FormattingTest(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix="format repository ")
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        subprocess.run(["git", "init", "--quiet", str(self.root)], check=True)
        self.files = [
            "a.cpp",
            "header.hpp",
            "nested/音楽 file.c",
            "CMakeLists.txt",
            "nested/setup.cmake.in",
            "notes.txt",
            "deleted.cpp",
        ]
        for name in self.files:
            path = self.root / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text("fixture contents\n")
        subprocess.run(["git", "add", "--all"], cwd=self.root, check=True)
        (self.root / "untracked.cpp").write_text("not selected")
        (self.root / "deleted.cpp").unlink()
        self.bin = self.root / "controlled tools"
        self.bin.mkdir()
        self.log = self.root / "calls.jsonl"
        for name in ("gersemi", "clang-format"):
            executable = self.bin / name
            executable.write_text(
                f"#!{sys.executable}\n"
                "import json, os, pathlib, sys\n"
                "with pathlib.Path(os.environ['FORMAT_CONTRACT_LOG']).open('a') as log:\n"
                "    log.write(json.dumps({'tool': pathlib.Path(sys.argv[0]).name, 'args': sys.argv[1:], 'cwd': os.getcwd()}) + '\\n')\n"
                "sys.exit(int(os.environ.get('FORMAT_CONTRACT_EXIT', '0')))\n"
            )
            executable.chmod(0o755)
        environment = patch.dict(
            os.environ,
            PATH=str(self.bin) + os.pathsep + os.environ["PATH"],
            FORMAT_CONTRACT_LOG=str(self.log),
        )
        environment.start()
        self.addCleanup(environment.stop)

    def calls(self):
        return [json.loads(line) for line in self.log.read_text().splitlines()]

    def test_default_inventory_preserves_protocol_paths_and_environment(self):
        inherited = dict(os.environ)
        current = Path.cwd()
        format_sources(self.root / "nested", check=True)
        cmake, cpp = self.calls()
        self.assertEqual(
            cmake["args"],
            [
                "--check",
                str(self.root / "CMakeLists.txt"),
                str(self.root / "nested/setup.cmake.in"),
            ],
        )
        self.assertEqual(
            cpp["args"],
            [
                "--dry-run",
                "--Werror",
                str(self.root / "a.cpp"),
                str(self.root / "header.hpp"),
                str(self.root / "nested/音楽 file.c"),
            ],
        )
        self.assertEqual(cmake["cwd"], str(self.root))
        self.assertEqual(cpp["cwd"], str(self.root))
        self.assertEqual(dict(os.environ), inherited)
        self.assertEqual(Path.cwd(), current)

    def test_explicit_selection_only_rewrites_selected_tracked_input(self):
        format_sources(self.root, ("cpp",), paths=(Path("nested/音楽 file.c"),))
        self.assertEqual(
            self.calls()[0]["args"], ["-i", str(self.root / "nested/音楽 file.c")]
        )
        self.assertEqual((self.root / "a.cpp").read_text(), "fixture contents\n")

    def test_invalid_selections_and_external_sources_fail_before_execution(self):
        for languages in ((), ("unknown",)):
            with self.assertRaises(ValueError):
                format_sources(self.root, languages)
        for name in ("untracked.cpp", "notes.txt", "deleted.cpp", "../external.cpp"):
            with self.subTest(name=name), self.assertRaises(ValueError):
                format_sources(self.root, ("cpp",), paths=(Path(name),))
        with tempfile.TemporaryDirectory() as external:
            outside = Path(external) / "value.cpp"
            outside.write_text("external data")
            (self.root / "linked.cpp").symlink_to(outside)
            subprocess.run(["git", "add", "linked.cpp"], cwd=self.root, check=True)
            with self.assertRaisesRegex(ValueError, "escapes"):
                format_sources(self.root, ("cpp",))
            self.assertEqual(outside.read_text(), "external data")
        self.assertFalse(self.log.exists())

    def test_nonzero_and_missing_tools_propagate_without_a_successful_check(self):
        with patch.dict(os.environ, FORMAT_CONTRACT_EXIT="23"):
            with self.assertRaises(subprocess.CalledProcessError) as error:
                format_sources(self.root, ("cpp",), check=True)
            self.assertEqual(error.exception.returncode, 23)
        (self.bin / "clang-format").unlink()
        # Avoid discovering a real system formatter through the inherited search path.
        with patch.dict(os.environ, PATH=str(self.bin)):
            with self.assertRaises(FileNotFoundError):
                format_sources(self.root, ("cpp",), check=True)

    def test_empty_matching_inventory_does_not_require_a_formatter(self):
        subprocess.run(
            ["git", "rm", "--cached", "-r", "--quiet", "."], cwd=self.root, check=True
        )
        format_sources(self.root, ("cpp",), check=True)
        self.assertFalse(self.log.exists())

    def test_installation_preserves_regular_hooks_and_replaces_only_the_link(self):
        destination = install_hook(self.root / "nested")
        self.assertEqual(destination, self.root / ".git/hooks/pre-commit")
        self.assertTrue(destination.is_symlink())
        self.assertFalse(os.path.isabs(os.readlink(destination)))
        self.assertIn("scripts.quality.formatting hook", destination.read_text())
        destination.unlink()
        custom = self.root / "custom hook"
        custom.write_text("preserved user hook")
        destination.symlink_to(custom)
        install_hook(self.root)
        self.assertEqual(custom.read_text(), "preserved user hook")
        destination.unlink()
        destination.symlink_to(self.root / "removed old launcher")
        install_hook(self.root)
        self.assertTrue(destination.is_file())
        destination.unlink()
        destination.write_text("regular custom hook")
        with self.assertRaisesRegex(ValueError, "overwrite"):
            install_hook(self.root)
        self.assertEqual(destination.read_text(), "regular custom hook")
        self.assertFalse(list(destination.parent.glob(".wiola-hook-*")))

    def test_canonical_commands_and_hook_failure_are_observable(self):
        prefix = ["--repository", str(self.root)]
        self.assertEqual(
            main([*prefix, "format", "cpp", "--check", "--files", "a.cpp"]), 0
        )
        self.assertEqual(main([*prefix, "format", "all"]), 0)
        self.assertEqual(main([*prefix, "install-hooks"]), 0)
        self.assertEqual(main([*prefix, "hook"]), 0)
        with patch.dict(os.environ, FORMAT_CONTRACT_EXIT="23"), redirect_stderr(
            StringIO()
        ) as error:
            self.assertEqual(main([*prefix, "hook"]), 1)
        self.assertIn("Formatting failed", error.getvalue())
        process = subprocess.run(
            [
                sys.executable,
                "-m",
                "scripts.quality.formatting",
                *prefix,
                "format",
                "cpp",
                "--check",
            ],
            capture_output=True,
            text=True,
        )
        self.assertEqual(process.returncode, 0, process.stderr)

    def test_hook_location_respects_core_hookspath_and_actual_worktrees(self):
        subprocess.run(
            ["git", "config", "core.hooksPath", "custom hooks"],
            cwd=self.root,
            check=True,
        )
        self.assertEqual(install_hook(self.root), self.root / "custom hooks/pre-commit")
        subprocess.run(
            ["git", "config", "--unset", "core.hooksPath"], cwd=self.root, check=True
        )
        subprocess.run(
            [
                "git",
                "-c",
                "user.name=Fixture",
                "-c",
                "user.email=fixture@example.invalid",
                "commit",
                "--quiet",
                "-m",
                "tests: create worktree fixture",
            ],
            cwd=self.root,
            check=True,
        )
        worktree = self.root / "linked worktree"
        subprocess.run(
            ["git", "worktree", "add", "--quiet", "--detach", str(worktree), "HEAD"],
            cwd=self.root,
            check=True,
        )
        self.assertEqual(install_hook(worktree), self.root / ".git/hooks/pre-commit")

    def test_failed_atomic_hook_publication_keeps_old_link_and_cleans_temporary_state(
        self,
    ):
        destination = install_hook(self.root)
        original = os.readlink(destination)
        with patch(
            "scripts.quality.formatting._hooks.os.replace",
            side_effect=OSError("controlled publication failure"),
        ):
            with self.assertRaisesRegex(OSError, "publication failure"):
                install_hook(self.root)
        self.assertEqual(os.readlink(destination), original)
        self.assertFalse(list(destination.parent.glob(".wiola-hook-*")))
