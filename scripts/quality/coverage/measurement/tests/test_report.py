from pathlib import Path
import tempfile
import json
import sys
from unittest.mock import patch
import unittest

from scripts.build.cmake import BuildTarget, CMakeInventory
from scripts.quality.coverage.measurement.report import CoverageMeasurement
from scripts.quality.coverage.source_policy.policy import SourcePolicy, SourceRecord


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
                                    "instantiations",
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

    def test_uncalled_template_instantiation_remains_visible_with_full_function_coverage(
        self,
    ):
        raw = self.raw(total=1, covered=1)
        raw["data"][0]["files"][0]["summary"]["instantiations"] = {
            "count": 2,
            "covered": 1,
        }
        result = self.measurement.cpp(raw, self.inventory)
        self.assertEqual(
            result["totals"]["functions"], {"count": 1, "covered": 1, "uncovered": 0}
        )
        expected = {"count": 2, "covered": 1, "uncovered": 1}
        self.assertEqual(result["totals"]["instantiations"], expected)
        self.assertEqual(
            result["owners"]["primitive"]["totals"]["instantiations"], expected
        )
        self.assertEqual(
            result["files"]["src/primitive/public/value.hpp"]["instantiations"],
            expected,
        )

    def test_missing_or_invalid_instantiation_counts_cannot_be_silently_dropped(self):
        raw = self.raw()
        del raw["data"][0]["files"][0]["summary"]["instantiations"]
        with self.assertRaisesRegex(ValueError, "lacks a required metric"):
            self.measurement.cpp(raw, self.inventory)
        for total, covered in ((2, 3), (-1, 0), (1, True)):
            with self.subTest(total=total, covered=covered), self.assertRaises(
                ValueError
            ):
                raw = self.raw()
                raw["data"][0]["files"][0]["summary"]["instantiations"] = {
                    "count": total,
                    "covered": covered,
                }
                self.measurement.cpp(raw, self.inventory)

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

    def test_external_mappings_do_not_enter_first_party_totals(self):
        with tempfile.TemporaryDirectory() as directory:
            outside = Path(directory)
            cpp = self.measurement.cpp(
                self.raw(str(outside / "value.hpp")), self.inventory
            )
            self.assertEqual(
                cpp["totals"]["instantiations"],
                {"count": 0, "covered": 0, "uncovered": 0},
            )
            self.assertFalse(cpp["owners"]["primitive"]["complete_mapping"])
            summary = {
                "num_statements": 2,
                "covered_lines": 1,
                "num_branches": 2,
                "covered_branches": 1,
            }
            python = self.measurement.python(
                {
                    "files": {
                        "tools/reader/read.py": {"summary": summary},
                        str(outside / "external.py"): {"summary": {}},
                    }
                }
            )
            self.assertEqual(python["totals"]["lines"]["count"], 2)
            self.assertEqual(set(python["files"]), {"tools/reader/read.py"})

    def test_python_aliases_cannot_double_count_the_same_source(self):
        actual = self.root / "tools/reader/read.py"
        actual.parent.mkdir(parents=True)
        actual.write_text("print('owned source')\n")
        alias = self.root / "alias.py"
        alias.symlink_to(actual)
        summary = {
            "num_statements": 2,
            "covered_lines": 1,
            "num_branches": 2,
            "covered_branches": 1,
        }
        with self.assertRaisesRegex(ValueError, "Duplicate Python"):
            self.measurement.python(
                {
                    "files": {
                        "tools/reader/read.py": {"summary": summary},
                        str(alias): {"summary": summary},
                    }
                }
            )

    def test_cli_marks_the_new_metric_schema_and_rejects_incomplete_summaries(self):
        from scripts.quality.coverage.measurement import __main__ as cli
        from scripts.quality.architecture.ownership.registry import OwnershipRegistry

        owners = {}
        for source in self.policy.records.values():
            owners.setdefault(source.owner, []).append(source.path)
        registry = OwnershipRegistry(
            [
                json.dumps(
                    {
                        "schema_version": 1,
                        "owners": [
                            {
                                "id": owner,
                                "purpose": "Independent measurement fixture",
                                "files": files,
                            }
                            for owner, files in owners.items()
                        ],
                    }
                )
            ]
        )
        cpp = self.root / "cpp.json"
        cpp.write_text(json.dumps(self.raw(total=1, covered=1)))
        python = self.root / "python.json"
        python.write_text(
            json.dumps(
                {
                    "files": {
                        "tools/reader/read.py": {
                            "summary": {
                                "num_statements": 1,
                                "covered_lines": 1,
                                "num_branches": 0,
                                "covered_branches": 0,
                            }
                        }
                    }
                }
            )
        )
        output = self.root / "report.json"
        arguments = [
            "measure",
            "--cpp-report",
            str(cpp),
            "--python-report",
            str(python),
            "--build-dir",
            str(self.root / "configured"),
            "--output",
            str(output),
        ]
        with patch.object(sys, "argv", arguments), patch.object(
            cli, "load_current_registry", return_value=registry
        ), patch.object(
            cli, "load_current_policy", return_value=self.policy
        ), patch.object(
            cli.CMakeInventory, "read", return_value=self.inventory
        ):
            self.assertEqual(cli.main(), 0)
            result = json.loads(output.read_text())
            self.assertEqual(result["schema_version"], 2)
            self.assertEqual(
                result["cpp"]["totals"]["instantiations"],
                {"count": 1, "covered": 1, "uncovered": 0},
            )
            original = output.read_bytes()
            incomplete = self.raw()
            del incomplete["data"][0]["files"][0]["summary"]["instantiations"]
            cpp.write_text(json.dumps(incomplete))
            self.assertEqual(cli.main(), 2)
            self.assertEqual(output.read_bytes(), original)
