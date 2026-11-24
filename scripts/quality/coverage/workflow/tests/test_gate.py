import copy
from pathlib import Path
import tempfile
import unittest

from scripts.quality.coverage.workflow import gate
from scripts.quality.coverage.source_policy.policy import SourcePolicy, SourceRecord


def report(total=100, hit=90):
    counts = {m: {"count": total, "covered": hit} for m in gate.METRICS}
    return {
        "totals": copy.deepcopy(counts),
        "files": [{"file": "src/gui/view.cpp", "counts": counts}],
    }


class CoverageGate(unittest.TestCase):
    def test_exact_full_coverage_is_not_rounded_up(self):
        self.assertTrue(gate.compare(report(10000, 9999), report(10000, 10000)))
        self.assertEqual(gate.compare(report(10000, 10000), report(10000, 10000)), [])

    def test_removed_mapping_and_smaller_denominator_are_rejected(self):
        missing = report()
        missing["files"] = []
        self.assertTrue(gate.compare(missing, report()))
        self.assertTrue(gate.compare(report(99, 99), report()))

    def test_new_uncovered_file_cannot_hide_behind_an_improved_total(self):
        current = report(200, 200)
        extra = report(5, 0)["files"][0]
        extra["file"] = "src/gui/new.cpp"
        current["files"].append(extra)
        self.assertTrue(any("no covered" in f for f in gate.compare(current, report())))

    def test_counts_must_be_valid_and_source_inventory_unique(self):
        for total, hit in [(-1, 0), (1, 2), (1, -1), (1, True)]:
            with self.assertRaises(ValueError):
                gate.compare(report(total, hit), report())
        duplicate = report()
        duplicate["files"] *= 2
        with self.assertRaises(ValueError):
            gate.compare(duplicate, report())

    def test_mapped_changed_line_and_each_branch_must_have_hits(self):
        diff = "+++ b/src/gui/view.cpp\n@@ -8,0 +9,3 @@\n+if (valid) {\n+    apply();\n+}\n"
        changes = gate.changed_lines(diff)
        self.assertEqual(changes, {"src/gui/view.cpp": {9, 10, 11}})
        policy = SourcePolicy(
            [SourceRecord("src/gui/view.cpp", "view", "cpp", "production")], []
        )
        lcov = "SF:/tmp/repo/src/gui/view.cpp\nDA:9,1\nDA:10,0\nBRDA:9,0,0,1\nBRDA:9,0,1,-\nend_of_record\n"
        failures, unmapped = gate.check_changed(
            changes, gate.lcov_records(lcov, Path("/tmp/repo")), policy
        )
        self.assertEqual(len(failures), 2)
        self.assertEqual(unmapped, ["src/gui/view.cpp:11"])
        fixed = lcov.replace("DA:10,0", "DA:10,1").replace(
            "BRDA:9,0,1,-", "BRDA:9,0,1,1"
        )
        self.assertEqual(
            gate.check_changed(
                changes, gate.lcov_records(fixed, Path("/tmp/repo")), policy
            )[0],
            [],
        )

    def test_missing_tests_profiles_or_changed_exclusions_cannot_pass(self):
        baseline = {
            "test_names": ["Critical.Test"],
            "exclusions": ["external dependencies"],
        }
        manifest = {
            "completed": {"target": {"tests": ["Critical.Test"], "profiles": 1}},
            "raw_profiles": 1,
            "exclusions": baseline["exclusions"],
        }
        with tempfile.TemporaryDirectory() as directory:
            report = Path(directory)
            raw = report / "raw/target"
            raw.mkdir(parents=True)
            with self.assertRaises(ValueError):
                gate.check_manifest(manifest, baseline, report)
            (raw / "process.profraw").write_bytes(b"actual process profile")
            self.assertEqual(gate.check_manifest(manifest, baseline, report), [])
            manifest["completed"]["target"]["tests"] = []
            self.assertTrue(gate.check_manifest(manifest, baseline, report))
            manifest["exclusions"] = ["all difficult code"]
            self.assertTrue(
                any(
                    "exclusions" in f
                    for f in gate.check_manifest(manifest, baseline, report)
                )
            )
            manifest["raw_profiles"] = 2
            self.assertTrue(
                any(
                    "total disagrees" in f
                    for f in gate.check_manifest(manifest, baseline, report)
                )
            )

    def test_deletions_do_not_shift_added_line_numbers(self):
        diff = "+++ b/src/a.cpp\n@@ -1,2 +1,2 @@\n-old\n+new\n same\n@@ -8,0 +9 @@\n+next\n"
        self.assertEqual(gate.changed_lines(diff), {"src/a.cpp": {1, 9}})

    def test_changed_scope_uses_declared_roles_instead_of_folder_names(self):
        records = (
            SourceRecord("src/feature/tests/fake.cpp", "fixture", "cpp", "test"),
            SourceRecord(
                "src/feature/tests/consumer.cpp", "fixture", "cpp", "consumer"
            ),
            SourceRecord("scripts/backend/driver.cpp", "driver", "cpp", "production"),
            SourceRecord("src/feature/CMakeLists.txt", "build", "cmake", "build"),
            SourceRecord("scripts/tool.py", "tool", "python", "tooling"),
        )
        policy = SourcePolicy(records, [])
        changed = {source.path: {1} for source in records}
        changed["README.md"] = {1}
        mapped = {
            path: {"lines": {1: 0}, "branches": {1: [("0", "0", 0)]}}
            for path in changed
        }
        failures, unmapped = gate.check_changed(changed, mapped, policy)
        self.assertEqual(
            failures,
            [
                "scripts/backend/driver.cpp:1: added executable line has zero hits",
                "scripts/backend/driver.cpp:1: added decision 0/0 has zero hits",
            ],
        )
        self.assertEqual(unmapped, [])
        mapped["scripts/backend/driver.cpp"] = {"lines": {}, "branches": {}}
        failures, unmapped = gate.check_changed(changed, mapped, policy)
        self.assertEqual(failures, [])
        self.assertEqual(unmapped, ["scripts/backend/driver.cpp:1"])

    def test_unknown_source_cannot_escape_explicit_applicability(self):
        for path in ("src/missing.cpp", "elsewhere/private.hpp", "scripts/missing.py"):
            with self.subTest(path=path), self.assertRaisesRegex(
                ValueError, "no explicit applicability"
            ):
                gate.check_changed({path: {1}}, {}, SourcePolicy([], []))


if __name__ == "__main__":
    unittest.main()
