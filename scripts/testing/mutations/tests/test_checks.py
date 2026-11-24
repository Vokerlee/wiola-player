import json
from contextlib import redirect_stderr
from io import StringIO
import signal
from unittest.mock import patch
from pathlib import Path
import subprocess
import tempfile
from types import SimpleNamespace
import unittest

from tools.architecture.ownership.registry import OwnershipRegistry
from scripts.testing.mutations import MutationCase, MutationCatalog
from scripts.testing.mutations import MutationVerifier
from scripts.testing.mutations.__main__ import main, _interrupted
from scripts.testing.mutations._edit import SourceConflict


def registry(paths):
    return OwnershipRegistry(
        [
            json.dumps(
                {
                    "schema_version": 1,
                    "owners": [
                        {
                            "id": "source.component",
                            "purpose": "Owns component behavior",
                            "files": paths,
                        }
                    ],
                }
            )
        ]
    )


def case(**changes):
    values = dict(
        identity="regression",
        source_owner="source.component",
        anchor="valid",
        replacement="false",
        target="configured_target",
        selection="Contract.*",
    )
    values.update(changes)
    return MutationCase(**values)


class MutationCatalogTest(unittest.TestCase):
    def test_source_resolution_follows_stable_owner_across_relocation(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            self.assertEqual(
                case().source(registry(["old/value.cpp"]), root), root / "old/value.cpp"
            )
            self.assertEqual(
                case().source(registry(["new/component/implementation.cpp"]), root),
                root / "new/component/implementation.cpp",
            )
            for catalog in (
                registry(["header.hpp"]),
                registry(["first.cpp", "second.cpp"]),
            ):
                with self.assertRaises(ValueError):
                    case().source(catalog, root)
            with self.assertRaises(ValueError):
                case(source_owner="source.unknown").source(
                    registry(["value.cpp"]), root
                )

    def test_catalog_rejects_ambiguous_and_invalid_selections_or_edits(self):
        for values in (
            {"identity": "../escape"},
            {"anchor": ""},
            {"replacement": "valid"},
            {"target": ""},
            {"anchor": 7},
        ):
            with self.assertRaises(ValueError):
                case(**values)
        with self.assertRaises(ValueError):
            MutationCatalog([])
        with self.assertRaises(ValueError):
            MutationCatalog([case(), case()])
        catalog = MutationCatalog([case()])
        self.assertEqual(catalog.select(), (case(),))
        self.assertEqual(catalog.select(["regression"]), (case(),))
        for selection in ([], ["missing"], ["regression", "regression"]):
            with self.assertRaises(ValueError):
                catalog.select(selection)

    def test_default_recipes_are_data_and_custom_schema_is_checked(self):
        self.assertGreater(len(MutationCatalog.read().select()), 0)
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "catalog.json"
            for data in (
                {"schema_version": True, "cases": []},
                {"schema_version": 2, "cases": []},
                [],
            ):
                path.write_text(json.dumps(data))
                with self.assertRaises(ValueError):
                    MutationCatalog.read(path)
            path.write_text(json.dumps({"schema_version": 1, "cases": [vars(case())]}))
            self.assertEqual(MutationCatalog.read(path).select(), (case(),))

    def test_source_symlink_cannot_escape_the_selected_repository(self):
        with tempfile.TemporaryDirectory() as temporary, tempfile.TemporaryDirectory() as external:
            root = Path(temporary)
            outside = Path(external) / "outside.cpp"
            outside.write_text("external source")
            (root / "linked.cpp").symlink_to(outside)
            with self.assertRaisesRegex(ValueError, "escapes"):
                case().source(registry(["linked.cpp"]), root)


class ControlledBuild:
    def __init__(self, source, failure=None):
        self.source, self.failure = source, failure
        self.phases = []

    def build(self, name, log):
        phase = log.name.split("-", 1)[0]
        self.phases.append((phase, self.source.read_bytes()))
        if phase == self.failure:
            raise subprocess.CalledProcessError(17, ["controlled-build"])

    def executable(self, name):
        return self.source.parent / "arbitrarily placed test executable"


class ControlledTests:
    def __init__(self, source, mode):
        self.source, self.mode = source, mode
        self.phases = []

    def run(self, binary, selection, artifacts, **arguments):
        phase = artifacts.name
        self.phases.append((phase, self.source.read_bytes()))
        if phase == "mutant":
            if self.mode == "timeout":
                raise subprocess.TimeoutExpired([str(binary)], 1)
            if self.mode == "interrupt":
                raise KeyboardInterrupt("controlled interrupt")
            if self.mode == "external-edit":
                self.source.write_bytes(b"external writer")
        return SimpleNamespace(
            passed=not (phase == "original" and self.mode == "baseline-fail")
            and not (phase == "restored" and self.mode == "restored-fail"),
            assertion_failure=phase == "mutant"
            and self.mode not in {"survived", "crashed"},
        )


class MutationVerifierTest(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.source = self.root / "component.cpp"
        self.source.write_bytes(b"valid behavior\r\n")
        self.original = self.source.read_bytes()
        self.registry = registry(["component.cpp"])

    def verifier(self, mode="killed", build_failure=None):
        self.build = ControlledBuild(self.source, build_failure)
        self.tests = ControlledTests(self.source, mode)
        return MutationVerifier(
            self.root,
            self.registry,
            self.build,
            self.tests,
            self.root / "artifacts",
            "test-revision",
            {"CALLER": "preserved"},
        )

    def evidence(self):
        return json.loads(
            (self.root / "artifacts/regression/mutation.json").read_text()
        )

    def test_success_requires_original_assertion_and_restored_evidence(self):
        result = self.verifier().verify(case())
        self.assertTrue(result["killed_by_assertion"])
        self.assertTrue(result["restored"])
        self.assertEqual(result["original_sha256"], result["restored_sha256"])
        self.assertEqual(self.source.read_bytes(), self.original)
        for phases in (self.build.phases, self.tests.phases):
            self.assertEqual(
                [phase for phase, content in phases], ["original", "mutant", "restored"]
            )
            self.assertEqual(
                [content for phase, content in phases],
                [self.original, b"false behavior\r\n", self.original],
            )

    def test_build_failures_timeouts_interrupts_survivors_and_crashes_restore_and_recheck(
        self,
    ):
        for mode, failure, error in (
            ("killed", "mutant", subprocess.CalledProcessError),
            ("timeout", None, subprocess.TimeoutExpired),
            ("interrupt", None, KeyboardInterrupt),
            ("survived", None, RuntimeError),
            ("crashed", None, RuntimeError),
        ):
            verifier = self.verifier(mode, failure)
            with self.subTest(mode=mode, failure=failure), self.assertRaises(error):
                verifier.verify(case())
            self.assertEqual(self.source.read_bytes(), self.original)
            self.assertEqual(self.build.phases[-1], ("restored", self.original))
            self.assertEqual(self.tests.phases[-1], ("restored", self.original))
            self.assertTrue(self.evidence()["restored"])
            self.assertIn("error", self.evidence())

    def test_original_failure_and_anchor_drift_cannot_modify_source(self):
        with self.assertRaises(RuntimeError):
            self.verifier("baseline-fail").verify(case())
        self.assertEqual(self.source.read_bytes(), self.original)
        self.assertEqual([phase for phase, content in self.build.phases], ["original"])
        with self.assertRaises(ValueError):
            self.verifier().verify(case(anchor="missing"))
        self.assertEqual(self.source.read_bytes(), self.original)
        self.assertEqual(self.build.phases, [])
        self.assertIn("error", self.evidence())

    def test_external_edits_are_not_overwritten_or_qualified_as_restored(self):
        with self.assertRaises(SourceConflict):
            self.verifier("external-edit").verify(case())
        self.assertEqual(self.source.read_bytes(), b"external writer")
        self.assertEqual(
            [phase for phase, content in self.build.phases], ["original", "mutant"]
        )
        self.assertFalse(self.evidence()["restored"])

    def test_restored_test_failure_is_not_a_successful_qualification(self):
        with self.assertRaisesRegex(RuntimeError, "Restored contract"):
            self.verifier("restored-fail").verify(case())
        self.assertEqual(self.source.read_bytes(), self.original)
        self.assertFalse(self.evidence()["restored"])


class MutationCommandTest(unittest.TestCase):
    def test_invalid_deadline_fails_before_loading_or_editing_sources(self):
        with tempfile.TemporaryDirectory() as temporary:
            output = StringIO()
            previous = signal.getsignal(signal.SIGTERM)
            with redirect_stderr(output):
                code = main(["--build-dir", temporary, "--test-timeout", "0"])
            self.assertEqual(code, 1)
            self.assertIn("Test timeout must be positive", output.getvalue())
            self.assertIn(
                str(Path(temporary) / "Testing" / "Mutations"), output.getvalue()
            )
            self.assertEqual(list(Path(temporary).iterdir()), [])
            self.assertEqual(signal.getsignal(signal.SIGTERM), previous)

    def test_interruption_returns_interrupt_status_and_restores_signal_handler(self):
        with tempfile.TemporaryDirectory() as temporary:
            output = StringIO()
            previous = signal.getsignal(signal.SIGTERM)
            with patch(
                "scripts.testing.mutations.__main__.load_current_registry",
                side_effect=KeyboardInterrupt,
            ), redirect_stderr(output):
                code = main(["--build-dir", temporary])
            self.assertEqual(code, 130)
            self.assertIn("interrupted", output.getvalue())
            self.assertEqual(signal.getsignal(signal.SIGTERM), previous)
            self.assertEqual(list(Path(temporary).iterdir()), [])

    def test_sigterm_is_translated_to_scoped_interruption(self):
        previous = signal.getsignal(signal.SIGTERM)
        try:
            signal.signal(signal.SIGTERM, _interrupted)
            with self.assertRaisesRegex(
                KeyboardInterrupt, "Mutation qualification interrupted"
            ):
                signal.raise_signal(signal.SIGTERM)
        finally:
            signal.signal(signal.SIGTERM, previous)
