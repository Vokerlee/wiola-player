import contextlib
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch
import warnings
import zipfile

from scripts.quality.architecture.ownership.registry import OwnershipRegistry
from scripts.quality.coverage.source_policy.policy import SourcePolicy, SourceRecord
from scripts.quality.coverage.python_runner import __main__ as runner


class PythonProfileTest(unittest.TestCase):
    def setUp(self):
        directory = tempfile.TemporaryDirectory(prefix="python profile evidence ")
        self.addCleanup(directory.cleanup)
        self.root = Path(directory.name)
        self.source = self.root / "source"
        self.source.mkdir()
        (self.source / "scripts").mkdir()
        (self.source / "scripts/tiny.py").write_text(
            "print('controlled covered process')\n"
        )
        (self.source / "CMakeLists.txt").write_text(
            """cmake_minimum_required(VERSION 3.28)
project(profile_fixture LANGUAGES NONE)
cmake_file_api(QUERY API_VERSION 1 CODEMODEL 2)
"""
        )
        self.build = self.root / "configured"
        subprocess.run(
            ["cmake", "-S", str(self.source), "-B", str(self.build)],
            check=True,
            capture_output=True,
            timeout=30,
        )
        self.policy = SourcePolicy(
            [SourceRecord("scripts/tiny.py", "tool", "python", "tooling")], []
        )
        self.registry = OwnershipRegistry(
            [
                json.dumps(
                    {
                        "schema_version": 1,
                        "owners": [
                            {
                                "id": "tool",
                                "purpose": "Independent covered process",
                                "files": ["scripts/tiny.py", "CMakeLists.txt"],
                            }
                        ],
                    }
                )
            ]
        )
        self.output = self.root / "evidence/python.json"
        self.arguments = [
            "python-coverage",
            "--build-dir",
            str(self.build),
            "--output",
            str(self.output),
        ]

    def execute(self, root, build, data_file, source_roots):
        command = [
            sys.executable,
            "-m",
            "coverage",
            "run",
            "--branch",
            "--parallel-mode",
            "--data-file",
            str(data_file),
            "--source",
            ",".join(source_roots),
            str(root / "scripts/tiny.py"),
        ]
        subprocess.run(command, cwd=root, check=True, capture_output=True, timeout=30)
        return [["scripts/tiny.py"]]

    @contextlib.contextmanager
    def inputs(self, execute=None):
        with patch.object(sys, "argv", self.arguments), patch.object(
            runner, "load_current_policy", return_value=self.policy
        ), patch.object(
            runner, "load_current_registry", return_value=self.registry
        ), patch.object(
            runner, "_execute", side_effect=execute or self.execute
        ), warnings.catch_warnings(
            record=True
        ) as observed:
            # This independent fixture traces its tiny script in the child; the composition
            # running outside the fixture has no matching source lines of its own.
            yield
            self.assertTrue(
                all("No data was collected" in str(item.message) for item in observed)
            )

    def test_json_reporting_retains_every_original_profile_and_hash(self):
        with self.inputs():
            self.assertEqual(runner.main(), 0)
        manifest = json.loads(self.output.with_name("python-manifest.json").read_text())
        (directory,) = self.output.parent.glob("python-profiles-*")
        actual = {
            path.name: hashlib.sha256(path.read_bytes()).hexdigest()
            for path in directory.iterdir()
            if path.name in manifest["process_profiles"]
        }
        self.assertEqual(len(actual), 2)
        self.assertEqual(manifest["process_profiles"], actual)
        self.assertEqual(manifest["completed_commands"], [["scripts/tiny.py"]])
        self.assertEqual(
            json.loads(self.output.read_text())["totals"]["covered_lines"], 1
        )

        # Artifact upload excludes dot-prefixed path components by default. A transport
        # round trip must preserve every original profile identified by the manifest.
        archive = self.root / "profiles.zip"
        with zipfile.ZipFile(archive, "w") as bundle:
            for path in self.output.parent.rglob("*"):
                relative = path.relative_to(self.output.parent)
                if path.is_file() and not any(
                    part.startswith(".") for part in relative.parts
                ):
                    bundle.write(path, str(relative))
        with zipfile.ZipFile(archive) as bundle:
            restored = json.loads(bundle.read("python-manifest.json"))
            for name, digest in restored["process_profiles"].items():
                archived = bundle.read(directory.name + "/" + name)
                self.assertEqual(hashlib.sha256(archived).hexdigest(), digest)

    def test_missing_command_profile_cannot_qualify(self):
        with self.inputs(execute=lambda *args: [["missing-profile"]]):
            with self.assertRaisesRegex(
                ValueError, "one Python profile per completed command"
            ):
                runner.main()
        self.assertFalse(self.output.exists())

    def test_failed_owned_command_still_stops_and_saves_the_collector(self):
        def failed(*arguments):
            raise subprocess.CalledProcessError(1, ["controlled failed command"])

        with self.inputs(execute=failed), patch.object(
            runner.coverage, "Coverage"
        ) as coverage:
            with self.assertRaises(subprocess.CalledProcessError):
                runner.main()
        coverage.return_value.start.assert_called_once_with()
        coverage.return_value.stop.assert_called_once_with()
        coverage.return_value.save.assert_called_once_with()
        self.assertFalse(self.output.exists())

    def test_source_mutation_during_collection_cannot_qualify(self):
        def changed(*arguments):
            commands = self.execute(*arguments)
            (self.source / "scripts/tiny.py").write_text(
                "print('changed after measurement')\n"
            )
            return commands

        with self.inputs(execute=changed):
            with self.assertRaisesRegex(ValueError, "source changed during collection"):
                runner.main()
        self.assertFalse(self.output.exists())

    def test_report_step_must_not_mutate_retained_profiles(self):
        original = subprocess.run

        def reporting(command, **kwargs):
            result = original(command, **kwargs)
            if command[1:4] == ["-m", "coverage", "json"]:
                data_file = Path(command[command.index("--data-file") + 1])
                profile = next(data_file.parent.glob(data_file.name + ".*"))
                profile.write_bytes(b"changed after reporting")
            return result

        with self.inputs(), patch.object(runner.subprocess, "run", reporting):
            with self.assertRaisesRegex(
                ValueError, "profiles changed during reporting"
            ):
                runner.main()
        self.assertFalse(self.output.with_name("python-manifest.json").exists())
