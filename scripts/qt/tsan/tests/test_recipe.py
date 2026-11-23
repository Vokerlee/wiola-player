from contextlib import redirect_stderr, redirect_stdout
from dataclasses import replace
from io import StringIO
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import textwrap
import unittest
from unittest.mock import patch

from scripts.qt.tsan.build import QtCoreTsanBuilder
from scripts.qt.tsan.context import QtCoreContext
from scripts.qt.tsan.runtime import QtCoreRuntime
from scripts.qt.tsan.__main__ import main
from scripts.qt.tsan.verify import main as verify


class ControlledSources:
    def __init__(self):
        self.requests = []

    def materialize(self, pin, workspace):
        self.requests.append((pin, workspace))
        source = workspace / pin.directory
        source.mkdir(parents=True, exist_ok=True)
        (source / "CMakeLists.txt").write_text("# external source-port fixture\n")
        return source


class QtCoreSdkTest(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.bin = self.root / "bin"
        self.bin.mkdir()
        self.host = self.root / "host Qt"
        self.host.mkdir()
        self.host_core = self.host / "libQt6Core.so.6"
        self.host_core.write_bytes(b"host-library")
        self.environment = patch.dict(
            os.environ,
            {
                "PATH": str(self.bin) + os.pathsep + os.environ["PATH"],
                "CXX": str(self.bin / "clang++-fixture"),
                "CC": str(self.bin / "clang-fixture"),
                "QT_SDK_HOST_LIBS": str(self.host),
                "QT_SDK_HOST_VERSION": "6.4.2",
                "QT_SDK_COMPILER_VERSION": "fixture compiler v1",
                "QT_SDK_GLIB_VERSION": "2.80.0",
                "QT_SDK_OTHER_CORE": str(self.root / "other SDK/libQt6Core.so.6"),
            },
        )
        self.environment.start()
        self.addCleanup(self.environment.stop)
        for name in ("clang++-fixture", "clang-fixture", "clang++", "clang"):
            self.tool(
                name,
                """
                import os, sys
                print('x86_64-linux-gnu' if '-dumpmachine' in sys.argv else os.environ['QT_SDK_COMPILER_VERSION'])
            """,
            )
        self.tool(
            "pkg-config",
            """
            import os, sys
            option, package = sys.argv[1:]
            if package == 'glib-2.0': print(os.environ['QT_SDK_GLIB_VERSION'])
            elif option == '--modversion': print(os.environ['QT_SDK_HOST_VERSION'])
            else: print(os.environ['QT_SDK_HOST_LIBS'])
        """,
        )
        self.tool(
            "cmake",
            """
            import json, os, pathlib, sys, time
            args = sys.argv[1:]
            if '--build' in args:
                if os.environ.get('QT_SDK_FAIL_BUILD'): sys.exit(17)
                directory = pathlib.Path(args[args.index('--build') + 1])
                library = directory / 'unusual output/core-image.bin'
                library.parent.mkdir(parents=True, exist_ok=True)
                library.write_bytes(b'instrumented Core image')
            else:
                if os.environ.get('QT_SDK_FAIL_CONFIGURE'): sys.exit(13)
                if os.environ.get('QT_SDK_SLOW_CONFIGURE'): time.sleep(5)
                directory = pathlib.Path(args[args.index('-B') + 1])
                source = pathlib.Path(args[args.index('-S') + 1])
                reply = directory / '.cmake/api/v1/reply'
                reply.mkdir(parents=True, exist_ok=True)
                (directory / 'configure-args.json').write_text(json.dumps(args))
                (reply / 'index-fixture.json').write_text(json.dumps({'reply': {'codemodel-v2': {'jsonFile': 'model.json'}}}))
                (reply / 'model.json').write_text(json.dumps({'version': {'major': 2}, 'paths': {'source': str(source)}, 'configurations': [{'targets': [{'id': 'core-id', 'name': 'Core', 'jsonFile': 'core.json'}]}]}))
                artifacts = [{'path': 'unusual output/core-image.bin'}]
                if os.environ.get('QT_SDK_AMBIGUOUS'): artifacts.append({'path': 'another.bin'})
                (reply / 'core.json').write_text(json.dumps({'type': 'SHARED_LIBRARY', 'artifacts': artifacts}))
        """,
        )
        self.tool(
            "nm",
            """
            import os
            print('ordinary_symbol' if os.environ.get('QT_SDK_NO_TSAN') else 'U __tsan_read8')
        """,
        )
        self.tool(
            "ldd",
            """
            import os,sys
            if os.environ.get('QT_SDK_BAD_LOADER'): sys.exit(5)
            expected = os.environ.get('QT_SDK_EXPECT_LIBRARY_PATH')
            if expected is not None and os.environ.get('LD_LIBRARY_PATH') != expected: sys.exit(6)
            print('libQt6Core.so.6 => ' + os.environ.get('QT_SDK_LOADED_CORE', os.environ['QT_SDK_OTHER_CORE']) + ' (0x000001)')
        """,
        )
        self.sources = ControlledSources()
        self.workspace = self.root / "SDK workspace (test)"
        self.context = QtCoreContext.discover()

    def tool(self, name, body):
        path = self.bin / name
        path.write_text("#!" + sys.executable + "\n" + textwrap.dedent(body))
        path.chmod(0o755)

    def builder(self, **changes):
        return QtCoreTsanBuilder(
            self.workspace, self.context, jobs=2, source_store=self.sources, **changes
        )

    def test_context_tracks_host_compiler_glib_and_declared_build_inputs(self):
        self.assertEqual(self.context.cxx, str(self.bin / "clang++-fixture"))
        self.assertEqual(self.context.cc, str(self.bin / "clang-fixture"))
        for changes in (
            {"QT_SDK_COMPILER_VERSION": "fixture compiler v2"},
            {"QT_SDK_GLIB_VERSION": "new glib"},
            {"CXXFLAGS": "-DTEST_CONTEXT=1"},
        ):
            with patch.dict(os.environ, changes):
                self.assertNotEqual(
                    QtCoreContext.discover().identity, self.context.identity
                )
        self.host_core.write_bytes(b"different host ABI")
        self.assertNotEqual(QtCoreContext.discover().identity, self.context.identity)
        with patch.dict(os.environ, QT_SDK_HOST_VERSION="6.99.0"):
            with self.assertRaises(ValueError):
                QtCoreContext.discover()
        with self.assertRaises(ValueError):
            QtCoreContext.discover(cxx=str(self.root / "missing compiler"))
        with patch.dict(os.environ, {"CC": ""}):
            self.assertEqual(
                QtCoreContext.discover().cc, str(self.bin / "clang-fixture")
            )
        with patch.dict(os.environ, {"CXX": "", "CC": ""}):
            self.assertEqual(QtCoreContext.discover().cxx, str(self.bin / "clang++"))

    def test_configured_artifact_is_qualified_and_reused_without_source_or_build_work(
        self,
    ):
        builder = self.builder()
        with patch.dict(os.environ, WIOLA_QT_SOURCE_CACHE=""):
            runtime = builder.prepare()
        artifact = QtCoreRuntime(runtime)
        self.assertTrue(artifact.qualified(self.context.identity))
        self.assertEqual(artifact.library.read_bytes(), b"instrumented Core image")
        self.assertEqual(len(self.sources.requests), 1)
        self.assertEqual(self.sources.requests[0][1], self.workspace.resolve())
        self.assertEqual(builder.prepare(), runtime)
        self.assertEqual(len(self.sources.requests), 1)
        build = self.workspace / "build" / self.context.build_identity
        arguments = json.loads((build / "configure-args.json").read_text())
        self.assertIn("-DCMAKE_CXX_COMPILER=" + self.context.cxx, arguments)
        self.assertIn("-DFEATURE_glib=ON", arguments)
        self.assertIn("-DFEATURE_sanitize_thread=ON", arguments)
        self.assertTrue((build / ".cmake/api/v1/query/codemodel-v2").is_file())
        artifact.library.write_bytes(b"corrupt cached image")
        self.assertFalse(artifact.qualified(self.context.identity))
        self.assertEqual(builder.prepare(), runtime)
        self.assertEqual(len(self.sources.requests), 2)
        self.assertNotEqual(
            self.context.identity,
            replace(self.context, recipe_sha256="different").identity,
        )
        policy_change = replace(self.context, qualification_sha256="different-policy")
        self.assertEqual(policy_change.build_identity, self.context.build_identity)
        self.assertNotEqual(policy_change.identity, self.context.identity)

    def test_failed_configure_build_timeout_or_missing_instrumentation_cannot_publish(
        self,
    ):
        for field, error in (
            ("QT_SDK_FAIL_CONFIGURE", subprocess.CalledProcessError),
            ("QT_SDK_FAIL_BUILD", subprocess.CalledProcessError),
            ("QT_SDK_NO_TSAN", ValueError),
            ("QT_SDK_AMBIGUOUS", ValueError),
        ):
            with patch.dict(os.environ, {field: "1"}):
                with self.assertRaises(error):
                    self.builder().prepare()
            self.assertFalse(
                QtCoreRuntime(self.workspace / "runtime").qualified(
                    self.context.identity
                )
            )
        with patch.dict(os.environ, QT_SDK_SLOW_CONFIGURE="1"):
            with self.assertRaises(subprocess.TimeoutExpired):
                self.builder(timeout=0.1).prepare()
        with self.assertRaises(ValueError):
            self.builder(timeout=0)

    def test_runtime_rejects_corruption_and_verifies_actual_loader_resolution(self):
        runtime = QtCoreRuntime(self.builder().prepare())
        log = self.root / "loader.log"
        program = self.root / "consumer"
        with patch.dict(os.environ, QT_SDK_LOADED_CORE=str(runtime.library)):
            runtime.verify_loaded(program, log, self.context.identity)
        self.assertIn(str(runtime.library), log.read_text())
        with self.assertRaises(ValueError):
            runtime.verify_loaded(program, log, self.context.identity)
        with patch.dict(os.environ, QT_SDK_BAD_LOADER="1"):
            with self.assertRaises(ValueError):
                runtime.verify_loaded(program, log, self.context.identity)
        with self.assertRaises(ValueError):
            runtime.verify_loaded(program, log, "another-context")
        receipt = json.loads(runtime.receipt.read_text())
        receipt["schema_version"] = True
        runtime.receipt.write_text(json.dumps(receipt))
        self.assertFalse(runtime.qualified(self.context.identity))
        runtime.receipt.write_text("invalid JSON")
        self.assertFalse(runtime.qualified(self.context.identity))

    def test_cli_identity_reuse_bounds_and_verification_use_public_recipe(self):
        runtime = QtCoreRuntime(self.builder().prepare())
        output = StringIO()
        with redirect_stdout(output):
            self.assertEqual(main(["--identity"]), 0)
            self.assertEqual(main([str(self.workspace)]), 0)
        self.assertIn(self.context.identity, output.getvalue())
        with patch.dict(
            os.environ,
            WIOLA_QT_CORE_RUNTIME=str(runtime.directory),
            QT_SDK_LOADED_CORE=str(runtime.library),
            LD_LIBRARY_PATH=str(self.host),
            QT_SDK_EXPECT_LIBRARY_PATH=str(runtime.directory)
            + os.pathsep
            + str(self.host),
        ):
            self.assertEqual(
                verify(
                    [
                        "--binary",
                        str(self.root / "program"),
                        "--output",
                        str(self.root / "verified.log"),
                    ]
                ),
                0,
            )
            self.assertEqual(os.environ["LD_LIBRARY_PATH"], str(self.host))
        with patch.dict(
            os.environ,
            LD_LIBRARY_PATH="",
            QT_SDK_EXPECT_LIBRARY_PATH=str(runtime.directory),
            QT_SDK_LOADED_CORE=str(runtime.library),
        ):
            self.assertEqual(
                verify(
                    [
                        "--binary",
                        str(self.root / "program"),
                        "--output",
                        str(self.root / "verified-empty-path.log"),
                        "--runtime",
                        str(runtime.directory),
                    ]
                ),
                0,
            )
            self.assertEqual(os.environ["LD_LIBRARY_PATH"], "")
        error = StringIO()
        with redirect_stderr(error):
            self.assertEqual(main([str(self.workspace), "--jobs", "0"]), 1)
            self.assertEqual(
                verify(
                    [
                        "--binary",
                        str(self.root / "program"),
                        "--output",
                        str(self.root / "wrong.log"),
                        "--runtime",
                        str(self.workspace / "absent"),
                    ]
                ),
                1,
            )
            with self.assertRaises(SystemExit):
                main([])
            with patch.dict(os.environ, WIOLA_QT_CORE_RUNTIME=""):
                with self.assertRaises(SystemExit):
                    verify(
                        [
                            "--binary",
                            str(self.root / "program"),
                            "--output",
                            str(self.root / "no-runtime.log"),
                        ]
                    )
        self.assertIn("Qt Core SDK failed", error.getvalue())
