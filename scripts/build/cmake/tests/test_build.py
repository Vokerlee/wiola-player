import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

from scripts.build.cmake import BuildTarget, CMakeBuild, CMakeInventory


class CMakeBuildTest(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.binary = self.root / "arbitrary output location" / "native contract"
        self.binary.parent.mkdir()
        self.binary.write_text("built artifact")
        self.target = BuildTarget("contract", "EXECUTABLE", (), (), (), (self.binary,))
        self.inventory = CMakeInventory(
            self.root, (self.target,), self.root / "configured build"
        )

    def test_execution_uses_the_snapshot_directory_and_rejects_observation_only_data(
        self,
    ):
        adapter = CMakeBuild(self.inventory, jobs=1)
        self.assertEqual(adapter.directory, self.inventory.build_directory.resolve())
        with self.assertRaises(ValueError):
            CMakeBuild(CMakeInventory(self.root, (self.target,)), jobs=1)

    def test_resolves_the_configured_artifact_without_path_or_suffix_guesses(self):
        adapter = CMakeBuild(self.inventory, jobs=2)
        self.assertEqual(adapter.executable("contract"), self.binary)
        self.binary.unlink()
        with self.assertRaises(ValueError):
            adapter.executable("contract")
        with self.assertRaises(ValueError):
            adapter.executable("unknown")

    def test_non_executable_ambiguous_and_duplicate_targets_are_rejected(self):
        for kind, artifacts in (
            ("STATIC_LIBRARY", (self.binary,)),
            ("EXECUTABLE", ()),
            ("EXECUTABLE", (self.binary, self.root / "another")),
        ):
            target = BuildTarget("contract", kind, (), (), (), artifacts)
            with self.assertRaises(ValueError):
                CMakeBuild(
                    CMakeInventory(
                        self.root, (target,), self.inventory.build_directory
                    ),
                    jobs=1,
                ).executable("contract")
        with self.assertRaises(ValueError):
            CMakeBuild(
                CMakeInventory(
                    self.root,
                    (self.target, self.target),
                    self.inventory.build_directory,
                ),
                jobs=1,
            )

    def test_parallelism_uses_explicit_input_then_environment_then_available_cpus(self):
        with patch.dict(os.environ, {"CMAKE_BUILD_PARALLEL_LEVEL": "3"}):
            self.assertEqual(CMakeBuild(self.inventory).jobs, 3)
            self.assertEqual(CMakeBuild(self.inventory, jobs=2).jobs, 2)
        with patch.dict(os.environ, {}, clear=True):
            self.assertGreaterEqual(CMakeBuild(self.inventory).jobs, 1)
        for changes in ({"jobs": 0}, {"jobs": 1, "timeout": 0}):
            with self.assertRaises(ValueError):
                CMakeBuild(self.inventory, **changes)

    def test_real_build_process_logs_and_propagates_failures_and_timeouts(self):
        script = self.root / "build tool.py"
        script.write_text("import json,sys\nprint(json.dumps(sys.argv[1:]))\n")
        adapter = CMakeBuild(
            self.inventory,
            jobs=2,
            command=(sys.executable, str(script)),
        )
        log = self.root / "logs" / "build.log"
        adapter.build("contract", log)
        arguments = json.loads(log.read_text())
        self.assertIn(str(adapter.directory), arguments)
        self.assertEqual(arguments[-1], "contract")
        with self.assertRaises(ValueError):
            adapter.build("unknown", self.root / "must-not-exist.log")
        self.assertFalse((self.root / "must-not-exist.log").exists())
        script.write_text("raise SystemExit(17)\n")
        with self.assertRaises(subprocess.CalledProcessError):
            adapter.build("contract", log)
        script.write_text("import time\ntime.sleep(5)\n")
        bounded = CMakeBuild(
            self.inventory,
            jobs=1,
            timeout=0.1,
            command=(sys.executable, str(script)),
        )
        with self.assertRaises(subprocess.TimeoutExpired):
            bounded.build("contract", log)
