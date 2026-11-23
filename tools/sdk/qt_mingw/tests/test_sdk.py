import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import textwrap
import unittest

REPOSITORY = Path(__file__).resolve().parents[4]
BUILD = REPOSITORY / "scripts/build-qt-mingw.sh"
CONTEXT = REPOSITORY / "tools/sdk/qt_mingw/context.cmake"


class QtSdkTest(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.binary_directory = self.root / "bin"
        self.binary_directory.mkdir()
        self.real_cmake = shutil.which("cmake")
        self.prefix = self.root / "installed sdk"
        self.workspace = self.root / "build workspace"
        host = self.root / "host/lib"
        (host / "cmake/Qt6").mkdir(parents=True)
        self.environment = dict(
            os.environ,
            PATH=str(self.binary_directory) + os.pathsep + os.environ["PATH"],
            TEST_HOST_LIBS=str(host),
            TEST_HOST_PREFIX=str(host.parent),
            TEST_QT_VERSION="6.4.2",
            TEST_COMPILER_VERSION="13.2.0",
            TEST_COMPILER_TARGET="x86_64-w64-mingw32",
            WIOLA_QT_BUILD_JOBS="2",
        )
        self.executable(
            "qmake6",
            """
            import os, sys
            print({"QT_VERSION": os.environ["TEST_QT_VERSION"], "QT_INSTALL_PREFIX": os.environ["TEST_HOST_PREFIX"], "QT_INSTALL_LIBS": os.environ["TEST_HOST_LIBS"]}[sys.argv[-1]])
        """,
        )
        self.executable(
            "cmake",
            """
            import json, os, pathlib, sys
            args = sys.argv[1:]
            if args[0] == "-P":
                print("0" * 64)
            elif args[0] == "--build":
                if os.environ.get("TEST_FAIL_BUILD"): sys.exit(1)
                pathlib.Path(args[1], "objects.bin").write_bytes(b"temporary compiler output")
            elif args[0] == "--install":
                prefix = pathlib.Path(json.loads(pathlib.Path(args[1], "state.json").read_text())["prefix"])
                prefix.mkdir(parents=True, exist_ok=True)
                if not os.environ.get("TEST_MISSING_TESTLIB"):
                    target = prefix / "lib/cmake/Qt6Test/Qt6TestConfig.cmake"
                    target.parent.mkdir(parents=True)
                    target.write_text("# installed target contract")
            else:
                directory = pathlib.Path(args[args.index("-B") + 1])
                directory.mkdir(parents=True, exist_ok=True)
                prefix = next(a.split("=", 1)[1] for a in args if a.startswith("-DCMAKE_INSTALL_PREFIX="))
                (directory / "state.json").write_text(json.dumps({"prefix": prefix}))
        """,
        )
        self.executable(
            "curl",
            """
            import sys
            raise RuntimeError("an existing source workspace must not go to the network")
        """,
        )
        compiler = self.executable(
            "target-compiler",
            """
            import os, sys
            print(os.environ["TEST_COMPILER_TARGET"] if "-dumpmachine" in sys.argv else os.environ["TEST_COMPILER_VERSION"])
        """,
        )
        toolchain = self.root / "toolchain.cmake"
        toolchain.write_text(f'set(CMAKE_CXX_COMPILER "{compiler.as_posix()}")\n')
        self.environment["WIOLA_QT_TOOLCHAIN_FILE"] = str(toolchain)

    def executable(self, name, source):
        path = self.binary_directory / name
        path.write_text("#!/usr/bin/env python3\n" + textwrap.dedent(source))
        path.chmod(0o755)
        return path

    def sources(self, workspace):
        (workspace / "qtbase-everywhere-src-6.4.2").mkdir(parents=True)

    def run_builder(self, *arguments, **changes):
        return subprocess.run(
            [str(BUILD), *map(str, arguments)],
            cwd=self.root,
            env=dict(self.environment, **changes),
            text=True,
            capture_output=True,
        )

    def test_explicit_workspace_does_not_pollute_the_installed_sdk(self):
        self.sources(self.workspace)
        result = self.run_builder(self.prefix, self.workspace)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertTrue(
            (self.prefix / "lib/cmake/Qt6Test/Qt6TestConfig.cmake").is_file()
        )
        self.assertTrue((self.workspace / "build/objects.bin").is_file())
        self.assertFalse((self.prefix / ".work").exists())
        self.assertFalse((self.prefix / "build").exists())

    def test_legacy_one_argument_call_keeps_its_existing_workspace(self):
        self.sources(self.prefix / ".work")
        result = self.run_builder(self.prefix)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertTrue((self.prefix / ".work/build/objects.bin").exists())

    def test_build_failure_cannot_publish_an_installed_sdk(self):
        self.sources(self.workspace)
        result = self.run_builder(self.prefix, self.workspace, TEST_FAIL_BUILD="1")
        self.assertNotEqual(result.returncode, 0)
        self.assertFalse(self.prefix.exists())

    def test_missing_required_test_library_rejects_an_incomplete_install(self):
        self.sources(self.workspace)
        result = self.run_builder(self.prefix, self.workspace, TEST_MISSING_TESTLIB="1")
        self.assertNotEqual(result.returncode, 0)
        self.assertNotIn("installed to", result.stdout)

    def test_invalid_arguments_fail_before_building_or_downloading(self):
        for arguments in [(), ("a", "b", "c")]:
            result = self.run_builder(*arguments)
            self.assertEqual(result.returncode, 2)
            self.assertFalse(self.prefix.exists())

    def test_cache_identity_changes_with_host_qt_compiler_or_target(self):
        identities = []
        for changes in [
            {},
            {"TEST_QT_VERSION": "6.5.0"},
            {"TEST_COMPILER_VERSION": "14.1.0"},
        ]:
            result = subprocess.run(
                [self.real_cmake, "-P", str(CONTEXT)],
                env=dict(self.environment, **changes),
                capture_output=True,
                text=True,
            )
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertRegex(result.stdout.strip(), "^[0-9a-f]{64}$")
            identities.append(result.stdout.strip())
        self.assertEqual(len(set(identities)), len(identities))

    def test_non_x64_toolchains_cannot_produce_a_cache_identity(self):
        for target in ("i686-w64-mingw32", "aarch64-w64-mingw32"):
            result = subprocess.run(
                [self.real_cmake, "-P", str(CONTEXT)],
                env=dict(self.environment, TEST_COMPILER_TARGET=target),
                capture_output=True,
                text=True,
            )
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("x86-64 MinGW only", result.stderr)

    def test_failed_download_never_publishes_an_sdk(self):
        result = self.run_builder(self.prefix, self.workspace)
        self.assertNotEqual(result.returncode, 0)
        self.assertFalse(self.prefix.exists())
