import importlib.util
import hashlib
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location(
    "coverage_report",
    Path(__file__).resolve().parents[2] / "scripts/coverage_report.py",
)
coverage = importlib.util.module_from_spec(spec)
spec.loader.exec_module(coverage)


class CoverageInventory(unittest.TestCase):
    def test_missing_or_empty_profile_fails_instead_of_reusing_an_old_report(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)
            with self.assertRaises(ValueError):
                coverage.require_profiles(path, 1)
            (path / "empty.profraw").touch()
            with self.assertRaises(ValueError):
                coverage.require_profiles(path, 1)
            (path / "empty.profraw").write_bytes(b"a nonempty process profile")
            self.assertEqual(len(coverage.require_profiles(path, 1)), 1)
            with self.assertRaises(ValueError):
                coverage.require_profiles(path, 2)

    def test_only_detailed_zero_hash_mapping_sentinels_are_accepted(self):
        sentinel = "warning: 1 functions have mismatched data\nhash-mismatch: No profile record found for 'unused_inline' with hash = 0x0\n"
        self.assertEqual(coverage.coverage_diagnostics(sentinel), ["unused_inline"])
        with self.assertRaises(ValueError):
            coverage.coverage_diagnostics(sentinel.replace("0x0", "0x1234"))
        with self.assertRaises(ValueError):
            coverage.coverage_diagnostics(sentinel.splitlines()[0])
        with self.assertRaises(ValueError):
            coverage.coverage_diagnostics("warning: ignored profile")

    def test_workflow_scope_is_owned_instead_of_inferred_from_its_folder(self):
        from tools.coverage.source_policy.policy import SourcePolicy, SourceRecord

        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            policy = SourcePolicy(
                [
                    SourceRecord(
                        "src/engine/moved.hpp",
                        "engine.contract",
                        "cpp",
                        "production",
                        True,
                    )
                ],
                [],
            )
            self.assertTrue(
                coverage.workflow_file(root / "src/engine/moved.hpp", root, policy)
            )
            self.assertFalse(
                coverage.workflow_file(root / "src/gui/not-qualified.cpp", root, policy)
            )
            self.assertFalse(
                coverage.workflow_file(root / "tests/gui/test.cpp", root, policy)
            )

    def test_profile_refresh_rejects_shared_inputs_and_unknown_targets(self):
        entries = {"tests": {"view_test": {"sources": ["tests/gui/view_test.cpp"]}}}
        previous = {"revision": "qualified-revision"}
        with patch.object(coverage, "capture", return_value="tests/gui/view_test.cpp"):
            self.assertEqual(
                coverage.refresh_inputs(
                    Path("/repo"), previous, entries, ["view_test"]
                ),
                ["tests/gui/view_test.cpp"],
            )
            with self.assertRaises(ValueError):
                coverage.refresh_inputs(Path("/repo"), previous, entries, [])
            with self.assertRaises(ValueError):
                coverage.refresh_inputs(Path("/repo"), previous, entries, ["missing"])
        for changed in [
            "src/gui/view.cpp",
            "tests/fixtures/shared.hpp",
            "cmake/build.cmake",
        ]:
            with patch.object(coverage, "capture", return_value=changed):
                with self.assertRaises(ValueError):
                    coverage.refresh_inputs(
                        Path("/repo"), previous, entries, ["view_test"]
                    )

    def test_profile_refresh_proof_rejects_modified_or_unhashed_retained_profiles(self):
        with tempfile.TemporaryDirectory() as directory:
            report = Path(directory)
            raw = report / "raw/other_test"
            raw.mkdir(parents=True)
            profile = raw / "one.profraw"
            profile.write_bytes(b"qualified profile")
            original = {
                "revision": "original",
                "tools": {},
                "scope": "workflow",
                "completed": {
                    "other_test": {"tests": ["Other.Test"], "profiles": 1},
                    "stream_test": {"tests": ["Stream.Test"], "profiles": 1},
                },
            }
            (report / "reused-manifest.json").write_text(json.dumps(original))
            (report / "inventory.json").write_text(
                json.dumps(
                    {"tests": {"stream_test": {"sources": ["tests/media/stream.cpp"]}}}
                )
            )
            manifest = dict(
                original,
                reused_evidence={
                    "refreshed_targets": ["stream_test"],
                    "changed_inputs": ["tests/media/stream.cpp"],
                    "retained_profiles": {
                        "other_test": {
                            "one.profraw": hashlib.sha256(
                                profile.read_bytes()
                            ).hexdigest()
                        }
                    },
                },
            )
            with patch.object(
                coverage, "capture", return_value="tests/media/stream.cpp"
            ):
                coverage.verify_reused_profiles(Path("/repo"), report, manifest)
                profile.write_bytes(b"wrong evidence")
                with self.assertRaises(ValueError):
                    coverage.verify_reused_profiles(Path("/repo"), report, manifest)
                manifest["reused_evidence"]["retained_profiles"]["other_test"] = {}
                with self.assertRaises(ValueError):
                    coverage.verify_reused_profiles(Path("/repo"), report, manifest)

    def test_zero_hits_and_distinct_branch_counts_remain_visible(self):
        summary = {
            metric: {"count": count, "covered": 0}
            for metric, count in [
                ("lines", 7),
                ("functions", 2),
                ("regions", 9),
                ("branches", 4),
            ]
        }
        totals = coverage.summarize([{"summary": summary}])
        self.assertEqual(totals["lines"]["uncovered"], 7)
        self.assertEqual(totals["branches"]["uncovered"], 4)


if __name__ == "__main__":
    unittest.main()
