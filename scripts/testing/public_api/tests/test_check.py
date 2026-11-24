import contextlib
from dataclasses import replace
import io
import json
import platform
from pathlib import Path
import runpy
import shutil
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

from scripts.build.cmake import CMakeInventory, CMakeBuild
from scripts.testing.public_api import (
    PackageProbe,
    PublicConsumerCheck,
    QualificationError,
)


class PackageProbeTest(unittest.TestCase):
    def setUp(self):
        directory = tempfile.TemporaryDirectory(prefix="package probe ")
        self.addCleanup(directory.cleanup)
        self.root = Path(directory.name)
        self.consumer = self.root / "consumer.cpp"
        self.consumer.write_text("int main() {}\n")
        self.probe = PackageProbe(
            "example",
            "example::api",
            ("example",),
            ("example.hpp",),
            ("private.hpp",),
            self.consumer,
        )
        self.manifest = self.root / "probe.json"
        self.data = {
            "package": "example",
            "target": "example::api",
            "components": ["example"],
            "headers": ["example.hpp"],
            "private_headers": ["private.hpp"],
            "consumer": "consumer.cpp",
        }

    def read(self, data=None):
        self.manifest.write_text(json.dumps(self.data if data is None else data))
        return PackageProbe.read(self.manifest)

    def test_manifest_preserves_explicit_facts_and_immutable_values(self):
        self.assertEqual(self.read(), self.probe)
        self.assertEqual(
            replace(
                self.probe, package="example-api.2", target="lib+test::portable-api.2"
            ).package,
            "example-api.2",
        )
        self.assertEqual(
            replace(self.probe, headers=["example.hpp"]).headers, ("example.hpp",)
        )
        self.assertEqual(
            replace(self.probe, components=["example"]).components, ("example",)
        )

    def test_header_component_and_cmake_identifier_validation(self):
        for field, invalid in (
            ("headers", ()),
            ("headers", "abc"),
            ("headers", ("a.hpp", "a.hpp")),
            ("headers", ("../secret.hpp",)),
            ("headers", ("/absolute.hpp",)),
            ("headers", ("a//b.hpp",)),
            ("headers", (".",)),
            ("headers", ("a;injection.hpp",)),
            ("headers", (None,)),
            ("private_headers", ()),
            ("components", ()),
            ("components", "abc"),
            ("components", ("same", "same")),
            ("components", ("bad;component",)),
            ("components", (None,)),
            ("package", "bad;package"),
            ("target", "bad target"),
        ):
            with self.subTest(field=field, invalid=invalid), self.assertRaises(
                ValueError
            ):
                replace(self.probe, **{field: invalid})
        with self.assertRaisesRegex(ValueError, "source file"):
            replace(self.probe, consumer=self.root)
        with self.assertRaises(FileNotFoundError):
            replace(self.probe, consumer=self.root / "missing.cpp")

    def test_manifest_schema_and_consumer_directory_are_explicit(self):
        for invalid in (
            [],
            {},
            dict(self.data, extra=True),
            dict(self.data, package=1),
            dict(self.data, headers="example.hpp"),
        ):
            with self.subTest(invalid=invalid), self.assertRaises(ValueError):
                self.read(invalid)
        with tempfile.TemporaryDirectory() as directory:
            outside = Path(directory) / "outside.cpp"
            outside.write_text("int main() {}\n")
            with self.assertRaisesRegex(ValueError, "manifest directory"):
                self.read(dict(self.data, consumer=str(outside)))
            self.consumer.unlink()
            self.consumer.symlink_to(outside)
            with self.assertRaisesRegex(ValueError, "manifest directory"):
                self.read()


class PublicConsumerCheckTest(unittest.TestCase):
    def setUp(self):
        directory = tempfile.TemporaryDirectory(prefix="independent public package ")
        self.addCleanup(directory.cleanup)
        self.root = Path(directory.name)
        self.source = self.root / "package source"
        self.source.mkdir()
        (self.source / "public").mkdir()
        (self.source / "public/example.hpp").write_text("#pragma once\nint answer();\n")
        (self.source / "implementation.cpp").write_text("int answer() { return 42; }\n")
        (self.source / "CMakeLists.txt").write_text(
            """cmake_minimum_required(VERSION 3.28)
project(independent_package LANGUAGES CXX)
cmake_file_api(QUERY API_VERSION 1 CODEMODEL 2)
add_library(example STATIC implementation.cpp)
set_target_properties(example PROPERTIES EXPORT_NAME api)
target_include_directories(example PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/public> $<INSTALL_INTERFACE:include>)
install(TARGETS example EXPORT example_targets ARCHIVE DESTINATION lib COMPONENT example)
install(DIRECTORY public/ DESTINATION include COMPONENT example)
install(EXPORT example_targets FILE example-config.cmake NAMESPACE example:: DESTINATION lib/cmake/example COMPONENT example)
"""
        )
        self.build = self.root / "configured build"
        self._command(["cmake", "-S", self.source, "-B", self.build])
        self._command(["cmake", "--build", self.build])
        self.consumer = self.root / "consumer.cpp"
        self.consumer.write_text(
            "#include <cstddef>\n#include <example.hpp>\nint main() { return answer() == 42 ? 0 : 1; }\n"
        )
        self.probe = PackageProbe(
            "example",
            "example::api",
            ("example",),
            ("example.hpp",),
            ("private/hidden.hpp",),
            self.consumer,
        )
        self.inventory = CMakeInventory.read(self.build)
        self.compiler = shutil.which("c++")
        self.assertIsNotNone(self.compiler)
        self.checker = PublicConsumerCheck(
            self.inventory,
            compiler=self.compiler,
            system=platform.system(),
            jobs=2,
            timeout=30,
        )
        self.artifacts = self.root / "evidence"

    def _command(self, command):
        result = subprocess.run(
            list(map(str, command)), capture_output=True, text=True, timeout=30
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_real_relocated_archive_links_runs_and_checks_headers(self):
        toolchain = self.root / "explicit toolchain.cmake"
        toolchain.write_text("# Caller-owned compiler context\n")
        checker = PublicConsumerCheck(
            self.inventory,
            compiler=self.compiler,
            system=platform.system(),
            toolchain=toolchain,
            jobs=2,
        )
        checker.check(self.probe, self.artifacts, run=True)
        self.assertTrue((self.artifacts / "qualified.txt").is_file())
        self.assertEqual(
            (self.artifacts / "consumer.cpp").read_bytes(), self.consumer.read_bytes()
        )
        self.assertTrue((self.artifacts / "run.log").is_file())
        checker.check(self.probe, self.root / "link-only", run=False)
        self.assertFalse((self.root / "link-only/run.log").exists())

    def test_evidence_retains_the_compiled_source_snapshot(self):
        original = self.consumer.read_bytes()
        build = CMakeBuild.build

        def changed(instance, name, log):
            if name == "private_0":
                self.consumer.write_text("int main() { return 99; }\n")
            return build(instance, name, log)

        with patch.object(CMakeBuild, "build", changed):
            self.checker.check(self.probe, self.artifacts)
        self.assertNotEqual(self.consumer.read_bytes(), original)
        self.assertEqual((self.artifacts / "consumer.cpp").read_bytes(), original)

    def test_leaked_header_does_not_qualify(self):
        hidden = self.source / "public/private/hidden.hpp"
        hidden.parent.mkdir()
        hidden.write_text("#pragma once\n")
        with self.assertRaisesRegex(QualificationError, "leaked"):
            self.checker.check(self.probe, self.artifacts)
        self.assertFalse((self.artifacts / "qualified.txt").exists())

    def test_a_header_mention_in_an_unrelated_error_does_not_qualify(self):
        hidden = self.source / "public/private/hidden.hpp"
        hidden.parent.mkdir()
        hidden.write_text(
            "#error fatal error: private/hidden.hpp: No such file or directory\n"
        )
        with self.assertRaisesRegex(QualificationError, "unrelated reason"):
            self.checker.check(self.probe, self.artifacts)
        self.assertIn("#error", (self.artifacts / "private-0.log").read_text())

    def test_missing_link_implementation_cannot_be_hidden_by_negative_probes(self):
        (self.source / "implementation.cpp").write_text(
            "int different() { return 42; }\n"
        )
        self._command(["cmake", "--build", self.build])
        with self.assertRaises(subprocess.CalledProcessError):
            self.checker.check(self.probe, self.artifacts)
        self.assertIn("answer", (self.artifacts / "public.log").read_text())
        self.assertFalse((self.artifacts / "private-0.log").exists())

    def test_each_header_must_compile_without_the_consumers_other_includes(self):
        (self.source / "public/example.hpp").write_text(
            "#pragma once\nstd::size_t answer();\n"
        )
        with self.assertRaises(subprocess.CalledProcessError):
            self.checker.check(self.probe, self.artifacts)
        self.assertTrue((self.artifacts / "public.log").is_file())
        self.assertIn("size_t", (self.artifacts / "headers.log").read_text())
        self.assertFalse((self.artifacts / "private-0.log").exists())

    def test_evidence_is_never_overwritten_and_checkout_workspace_is_rejected(self):
        self.artifacts.mkdir()
        marker = self.artifacts / "keep.txt"
        marker.write_text("owned by caller")
        with self.assertRaisesRegex(ValueError, "empty directory"):
            self.checker.check(self.probe, self.artifacts)
        self.assertEqual(marker.read_text(), "owned by caller")
        with patch(
            "scripts.testing.public_api._check.tempfile.TemporaryDirectory",
            return_value=contextlib.nullcontext(str(self.source)),
        ):
            with self.assertRaisesRegex(ValueError, "outside the source checkout"):
                self.checker.check(self.probe, self.root / "other evidence")

    def test_timeout_in_a_private_probe_is_a_failed_check(self):
        build = CMakeBuild.build

        def bounded(instance, name, log):
            if name == "private_0":
                raise subprocess.TimeoutExpired("controlled private compilation", 1)
            return build(instance, name, log)

        with patch.object(CMakeBuild, "build", bounded):
            with self.assertRaises(subprocess.TimeoutExpired):
                self.checker.check(self.probe, self.artifacts)
        self.assertFalse((self.artifacts / "qualified.txt").exists())

    def test_cli_success_failure_and_module_entrypoint(self):
        manifest = self.root / "probe.json"
        manifest.write_text(
            json.dumps(
                {
                    "package": "example",
                    "target": "example::api",
                    "components": ["example"],
                    "headers": ["example.hpp"],
                    "private_headers": ["private/hidden.hpp"],
                    "consumer": "consumer.cpp",
                }
            )
        )
        arguments = [
            "--manifest",
            str(manifest),
            "--build-dir",
            str(self.build),
            "--output",
            str(self.artifacts),
            "--compiler",
            self.compiler,
            "--system",
            platform.system(),
            "--run",
        ]
        output = io.StringIO()
        with contextlib.redirect_stdout(output), patch.object(
            sys, "argv", ["public-api", *arguments]
        ):
            with self.assertRaises(SystemExit) as status:
                runpy.run_module("scripts.testing.public_api", run_name="__main__")
        self.assertEqual(status.exception.code, 0)
        from scripts.testing.public_api.__main__ import main

        manifest.write_text("{}")
        with contextlib.redirect_stdout(output):
            self.assertEqual(main(arguments), 1)
        self.assertIn("Evidence:", output.getvalue())
        self.assertEqual(len(list(self.artifacts.glob("qualification-*"))), 2)
