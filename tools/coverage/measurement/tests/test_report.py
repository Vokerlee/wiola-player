from pathlib import Path
import tempfile
import unittest

from scripts.build.cmake import BuildTarget, CMakeInventory
from tools.coverage.measurement.report import CoverageMeasurement
from tools.coverage.source_policy.policy import SourcePolicy, SourceRecord


class CoverageMeasurementTest(unittest.TestCase):
    def setUp(self):
        directory = tempfile.TemporaryDirectory()
        self.addCleanup(directory.cleanup)
        self.root = Path(directory.name)
        self.policy = SourcePolicy(
            [
                SourceRecord(
                    "src/primitive/public/value.hpp", "primitive", "cpp", "production"
                ),
                SourceRecord(
                    "src/primitive/tests/value_test.cpp", "primitive", "cpp", "test"
                ),
                SourceRecord("tools/reader/read.py", "reader", "python", "tooling"),
                SourceRecord(
                    "tools/reader/tests/test_read.py", "reader", "python", "test"
                ),
                SourceRecord("scripts/sdk.sh", "sdk", "shell", "tooling"),
            ],
            [],
        )
        self.measurement = CoverageMeasurement(self.policy, self.root)
        self.inventory = CMakeInventory(self.root, ())

    def raw(self, path="src/primitive/public/value.hpp", total=10, covered=7):
        return {
            "data": [
                {
                    "files": [
                        {
                            "filename": str(self.root / path),
                            "summary": {
                                metric: {"count": total, "covered": covered}
                                for metric in (
                                    "lines",
                                    "functions",
                                    "regions",
                                    "branches",
                                )
                            },
                        }
                    ]
                }
            ]
        }

    def test_full_template_counts_and_gaps_are_retained_by_owner(self):
        result = self.measurement.cpp(self.raw(), self.inventory)
        self.assertEqual(
            result["totals"]["lines"], {"count": 10, "covered": 7, "uncovered": 3}
        )
        self.assertEqual(
            result["owners"]["primitive"]["mapped_files"],
            ["src/primitive/public/value.hpp"],
        )
        self.assertEqual(
            result["owners"]["primitive"]["totals"]["branches"]["uncovered"], 3
        )

    def test_tests_inside_an_owner_never_inflate_production_coverage(self):
        result = self.measurement.cpp(
            self.raw("src/primitive/tests/value_test.cpp"), self.inventory
        )
        self.assertEqual(result["totals"]["lines"]["count"], 0)
        self.assertEqual(
            result["ignored_nonproduction_mappings"],
            ["src/primitive/tests/value_test.cpp"],
        )
        self.assertFalse(result["owners"]["primitive"]["complete_mapping"])

    def test_unmapped_configured_translation_unit_and_unknown_mapping_fail(self):
        policy = SourcePolicy(
            [SourceRecord("src/engine.cpp", "engine", "cpp", "production")], []
        )
        measurement = CoverageMeasurement(policy, self.root)
        inventory = CMakeInventory(
            self.root,
            (BuildTarget("engine", "STATIC_LIBRARY", ("src/engine.cpp",), (), (), ()),),
        )
        with self.assertRaisesRegex(ValueError, "no coverage mapping"):
            measurement.cpp({"data": [{"files": []}]}, inventory)
        with self.assertRaisesRegex(ValueError, "no applicability"):
            self.measurement.cpp(self.raw("src/unknown.cpp"), self.inventory)

    def test_duplicate_mappings_invalid_counts_and_different_build_root_fail(self):
        ambiguous = self.raw()
        ambiguous["data"] *= 2
        with self.assertRaisesRegex(ValueError, "one LLVM"):
            self.measurement.cpp(ambiguous, self.inventory)
        raw = self.raw()
        raw["data"][0]["files"] *= 2
        with self.assertRaisesRegex(ValueError, "Duplicate"):
            self.measurement.cpp(raw, self.inventory)
        for total, covered in [(1, 2), (-1, 0), (1, True)]:
            with self.subTest(total=total, covered=covered), self.assertRaises(
                ValueError
            ):
                self.measurement.cpp(
                    self.raw(total=total, covered=covered), self.inventory
                )
        with self.assertRaisesRegex(ValueError, "different roots"):
            self.measurement.cpp(self.raw(), CMakeInventory(self.root / "other", ()))

    def test_python_lines_and_branches_remain_separate_from_cpp_functions(self):
        summary = dict(
            num_statements=8, covered_lines=6, num_branches=4, covered_branches=1
        )
        result = self.measurement.python(
            {
                "files": {
                    "tools/reader/read.py": {"summary": summary},
                    "tools/reader/tests/test_read.py": {"summary": summary},
                }
            }
        )
        self.assertEqual(result["totals"]["lines"]["uncovered"], 2)
        self.assertEqual(result["totals"]["branches"]["uncovered"], 3)
        self.assertNotIn("functions", result["totals"])
        self.assertEqual(len(result["files"]), 1)
        self.assertEqual(self.measurement.unmeasured()[0]["owner"], "sdk")

    def test_zero_hits_remain_visible_and_absent_python_tools_invalidate_measurement(
        self,
    ):
        with self.assertRaisesRegex(ValueError, "no statement mapping"):
            self.measurement.python({"files": {}})
        result = self.measurement.python(
            {
                "files": {
                    "tools/reader/read.py": {
                        "summary": {
                            "num_statements": 8,
                            "covered_lines": 0,
                            "num_branches": 4,
                            "covered_branches": 0,
                        }
                    }
                }
            }
        )
        self.assertEqual(result["totals"]["lines"]["uncovered"], 8)
        self.assertEqual(result["totals"]["branches"]["uncovered"], 4)
        with self.assertRaisesRegex(ValueError, "no applicability"):
            self.measurement.python({"files": {"unknown.py": {"summary": {}}}})
