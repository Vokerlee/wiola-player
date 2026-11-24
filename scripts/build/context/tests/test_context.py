import json
from pathlib import Path
import subprocess
import tempfile
import unittest

from scripts.build.cmake import CMakeInventory, CMakeBuild
from scripts.quality.architecture.inventory.git_inventory import GitInventory


class ComponentContextTest(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix="portable context ")
        self.addCleanup(temporary.cleanup)
        self.work = Path(temporary.name)
        self.root = GitInventory(Path(__file__).parent).root

    def configure(self, selected, *, tests=False):
        directory = Path(tempfile.mkdtemp(prefix="configured ", dir=self.work))
        command = [
            "cmake",
            "-S",
            str(self.root),
            "-B",
            str(directory),
            "-DWIOLA_COMPONENTS=" + selected,
            "-DBUILD_TESTING=" + ("ON" if tests else "OFF"),
            "-DCMAKE_DISABLE_FIND_PACKAGE_Qt6=ON",
            "-DCMAKE_DISABLE_FIND_PACKAGE_PkgConfig=ON",
            "-DFETCHCONTENT_FULLY_DISCONNECTED=ON",
            "-DCMAKE_BUILD_TYPE=Debug",
        ]
        result = subprocess.run(
            command, cwd=self.work, capture_output=True, text=True, timeout=120
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertFalse((directory / "_deps").exists())
        return directory

    def test_portable_selections_prepare_only_their_declared_targets(self):
        for selected in (
            "borrowed_reference",
            "quantities",
            "media",
            "quantities;media",
        ):
            with self.subTest(selected=selected):
                directory = self.configure(selected)
                inventory = CMakeInventory.read(directory)
                expected = {"wiola_media_core"} if "media" in selected else set()
                actual = {target.name for target in inventory.targets}
                self.assertEqual(actual, expected)
                build = CMakeBuild(inventory, jobs=2, timeout=120)
                for target in expected:
                    build.build(target, directory / "build.log")
                self.assertFalse((directory / "_deps").exists())

    def test_standard_only_component_tests_need_no_fetched_test_framework(self):
        directory = self.configure("borrowed_reference", tests=True)
        CMakeBuild(CMakeInventory.read(directory), jobs=2, timeout=120).build(
            "borrowed_reference_test", directory / "build.log"
        )
        tested = subprocess.run(
            ["ctest", "--test-dir", str(directory), "--output-on-failure"],
            capture_output=True,
            text=True,
            timeout=120,
        )
        self.assertEqual(tested.returncode, 0, tested.stdout + tested.stderr)
        discovery = json.loads(
            subprocess.check_output(
                ["ctest", "--test-dir", str(directory), "--show-only=json-v1"],
                text=True,
            )
        )
        self.assertEqual(
            {test["name"] for test in discovery["tests"]},
            {
                "BorrowedReference.OwnershipTransfer",
                "BorrowedReference.InstalledConsumer",
            },
        )
        self.assertFalse((directory / "_deps").exists())

    def test_unknown_component_fails_before_dependency_preparation(self):
        for selected in ("unknown_component", "OFF", "0"):
            with self.subTest(selected=selected):
                directory = Path(tempfile.mkdtemp(prefix="invalid ", dir=self.work))
                result = subprocess.run(
                    [
                        "cmake",
                        "-S",
                        str(self.root),
                        "-B",
                        str(directory),
                        "-DWIOLA_COMPONENTS=" + selected,
                        "-DBUILD_TESTING=OFF",
                    ],
                    cwd=self.work,
                    capture_output=True,
                    text=True,
                    timeout=120,
                )
                self.assertNotEqual(result.returncode, 0)
                self.assertIn(
                    "Unknown portable component: " + selected,
                    result.stdout + result.stderr,
                )
                self.assertFalse((directory / "_deps").exists())
