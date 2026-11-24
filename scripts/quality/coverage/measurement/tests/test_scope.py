import contextlib
import io
import json
import sys
from unittest.mock import patch

from scripts.build.cmake import BuildTarget, CMakeInventory
from scripts.quality.architecture.contracts.catalog import (
    AbstractionContract,
    ContractCatalog,
)
from scripts.quality.architecture.ownership.registry import OwnershipRegistry
from scripts.quality.coverage.measurement import __main__ as cli
from scripts.quality.coverage.measurement.report import CoverageMeasurement
from scripts.quality.coverage.source_policy.policy import SourcePolicy, SourceRecord
from ._fixtures import MeasurementFixture


class ComponentScopeTest(MeasurementFixture):
    def test_owner_selection_excludes_unrelated_configured_sources_and_keeps_part_gaps(
        self,
    ):
        policy = SourcePolicy(
            [
                *self.policy.records.values(),
                SourceRecord("elsewhere/part.cpp", "part", "cpp", "production"),
                SourceRecord(
                    "src/primitive/unrelated.cpp", "neighbor", "cpp", "production"
                ),
                SourceRecord(
                    "src/primitive/unrelated.hpp", "neighbor", "cpp", "production"
                ),
            ],
            [],
        )
        inventory = CMakeInventory(
            self.root,
            (
                BuildTarget(
                    "part", "STATIC_LIBRARY", ("elsewhere/part.cpp",), (), (), ()
                ),
                BuildTarget(
                    "neighbor",
                    "STATIC_LIBRARY",
                    ("src/primitive/unrelated.cpp",),
                    (),
                    (),
                    (),
                ),
            ),
        )
        raw = self.raw(total=1, covered=1)
        raw["data"][0]["files"] += self.raw("elsewhere/part.cpp")["data"][0]["files"]
        raw["data"][0]["files"] += self.raw(
            "src/primitive/unrelated.hpp", total=500, covered=500
        )["data"][0]["files"]
        # The unrelated translation unit has no mapping; it is not this component's obligation.
        measurement = CoverageMeasurement(
            policy, self.root, owners=("primitive", "part")
        )
        result = measurement.cpp(raw, inventory)
        self.assertEqual(set(result["owners"]), {"primitive", "part"})
        self.assertEqual(
            result["totals"]["instantiations"],
            {"count": 11, "covered": 8, "uncovered": 3},
        )
        self.assertEqual(
            result["owners"]["part"]["configured_sources"], ["elsewhere/part.cpp"]
        )
        self.assertEqual(
            result["scope"],
            {
                "owners": ["part", "primitive"],
                "unselected_mapped_files": ["src/primitive/unrelated.hpp"],
            },
        )
        result["scope"]["owners"].append("cannot mutate the selection")
        self.assertEqual(
            measurement.cpp(raw, inventory)["scope"]["owners"], ["part", "primitive"]
        )

    def test_python_scope_does_not_require_unselected_tools_or_unmeasured_languages(
        self,
    ):
        policy = SourcePolicy(
            [
                *self.policy.records.values(),
                SourceRecord("other/tool.py", "other", "python", "tooling"),
            ],
            [],
        )
        measurement = CoverageMeasurement(policy, self.root, owners=["reader"])
        summary = {
            "num_statements": 2,
            "covered_lines": 1,
            "num_branches": 2,
            "covered_branches": 1,
        }
        raw = {
            "files": {
                "tools/reader/read.py": {"summary": summary},
                "other/tool.py": {"summary": {}},
                "tools/reader/tests/test_read.py": {"summary": {}},
            }
        }
        result = measurement.python(raw)
        self.assertEqual(
            result["totals"]["branches"], {"count": 2, "covered": 1, "uncovered": 1}
        )
        self.assertEqual(set(result["owners"]), {"reader"})
        self.assertEqual(result["scope"]["unselected_mapped_files"], ["other/tool.py"])
        self.assertEqual(measurement.unmeasured(), [])
        del raw["files"]["other/tool.py"]
        self.assertEqual(
            measurement.python(raw)["scope"]["unselected_mapped_files"], []
        )
        self.assertEqual(
            CoverageMeasurement(policy, self.root, owners=["sdk"]).unmeasured()[0][
                "owner"
            ],
            "sdk",
        )

    def test_scope_never_hides_unknown_first_party_sources_or_unmapped_selected_headers(
        self,
    ):
        measurement = CoverageMeasurement(self.policy, self.root, owners=["primitive"])
        with self.assertRaisesRegex(ValueError, "no applicability"):
            measurement.cpp(self.raw("unknown.cpp"), self.inventory)
        reader = CoverageMeasurement(self.policy, self.root, owners=["reader"])
        with self.assertRaisesRegex(ValueError, "no applicability"):
            reader.python({"files": {"unknown.py": {"summary": {}}}})
        result = measurement.cpp({"data": [{"files": []}]}, self.inventory)
        self.assertFalse(result["owners"]["primitive"]["complete_mapping"])
        self.assertEqual(
            result["owners"]["primitive"]["unmapped_files"],
            ["src/primitive/public/value.hpp"],
        )
        self.assertEqual(result["totals"]["lines"]["count"], 0)

    def test_selection_rejects_empty_unknown_and_string_inputs(self):
        for owners in ([], "primitive", ["unknown"], [""], [None]):
            with self.subTest(owners=owners), self.assertRaises(ValueError):
                CoverageMeasurement(self.policy, self.root, owners=owners)
        supplied = ["primitive"]
        measurement = CoverageMeasurement(self.policy, self.root, owners=iter(supplied))
        supplied.append("reader")
        self.assertEqual(
            measurement.cpp(self.raw(), self.inventory)["scope"]["owners"],
            ["primitive"],
        )

    def test_cli_uses_the_exact_declared_component_and_requires_only_its_languages(
        self,
    ):
        part = SourceRecord(
            "src/primitive/private/member.cpp", "part", "cpp", "production"
        )
        metadata = "src/primitive/metadata.json"
        policy = SourcePolicy([*self.policy.records.values(), part], [])
        contract = AbstractionContract(
            "primitive",
            "src/primitive",
            "Owns value semantics",
            ("Values are portable",),
            parts=("part", "metadata"),
        )
        catalog = ContractCatalog((contract,), {})
        owners = {}
        for source in policy.records.values():
            owners.setdefault(source.owner, []).append(source.path)
        owners["metadata"] = [metadata]
        registry = OwnershipRegistry(
            [
                json.dumps(
                    {
                        "schema_version": 1,
                        "owners": [
                            {
                                "id": owner,
                                "purpose": "Controlled measurement member",
                                "files": files,
                            }
                            for owner, files in owners.items()
                        ],
                    }
                )
            ]
        )
        cpp = self.root / "cpp.json"
        raw = self.raw(total=1, covered=1)
        raw["data"][0]["files"] += self.raw(part.path, total=1, covered=0)["data"][0][
            "files"
        ]
        cpp.write_text(json.dumps(raw))
        output = self.root / "scoped.json"
        arguments = [
            "measure",
            "--cpp-report",
            str(cpp),
            "--component",
            str(self.root / contract.root),
            "--build-dir",
            str(self.root / "configured"),
            "--output",
            str(output),
        ]
        with patch.object(sys, "argv", arguments), patch.object(
            cli, "load_current_registry", return_value=registry
        ), patch.object(cli, "load_current_policy", return_value=policy), patch.object(
            cli, "load_current_catalog", return_value=catalog
        ), patch.object(
            cli.CMakeInventory, "read", return_value=self.inventory
        ):
            self.assertEqual(cli.main(), 0)
        result = json.loads(output.read_text())
        self.assertEqual(
            result["component"],
            {"root": contract.root, "owners": ["primitive", "part", "metadata"]},
        )
        self.assertIsNone(result["python"])
        self.assertEqual(result["cpp"]["totals"]["instantiations"]["uncovered"], 1)
        self.assertEqual(result["cpp"]["scope"]["owners"], ["part", "primitive"])

    def test_python_component_cli_needs_no_cpp_export(self):
        contract = AbstractionContract(
            "reader", "tools/reader", "Owns reader behavior", ("Portable tooling",)
        )
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
                                "purpose": "Independent source member",
                                "files": files,
                            }
                            for owner, files in owners.items()
                        ],
                    }
                )
            ]
        )
        source = self.root / "python.json"
        source.write_text(
            json.dumps(
                {
                    "files": {
                        "tools/reader/read.py": {
                            "summary": {
                                "num_statements": 2,
                                "covered_lines": 1,
                                "num_branches": 2,
                                "covered_branches": 1,
                            }
                        }
                    }
                }
            )
        )
        output = self.root / "python-scope.json"
        arguments = [
            "measure",
            "--python-report",
            str(source),
            "--component",
            contract.root,
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
            cli, "load_current_catalog", return_value=ContractCatalog((contract,), {})
        ), patch.object(
            cli.CMakeInventory, "read", return_value=self.inventory
        ):
            self.assertEqual(cli.main(), 0)
        result = json.loads(output.read_text())
        self.assertIsNone(result["cpp"])
        self.assertEqual(set(result["python"]["owners"]), {"reader"})
        self.assertEqual(result["python"]["totals"]["branches"]["uncovered"], 1)
        self.assertEqual(result["unmeasured"], [])

    def test_cli_rejects_grouping_unknown_outside_and_missing_language_requests(self):
        grouped = AbstractionContract(
            "primitive",
            "src/primitive",
            "Groups code",
            ("Children own code",),
            grouping=True,
        )
        # A single-owner policy keeps these rejection cases independent of unrelated
        # source reconciliation.
        policy = SourcePolicy(
            [
                SourceRecord(
                    "src/primitive/public/value.hpp", "primitive", "cpp", "production"
                )
            ],
            [],
        )
        registry = OwnershipRegistry(
            [
                json.dumps(
                    {
                        "schema_version": 1,
                        "owners": [
                            {
                                "id": "primitive",
                                "purpose": "Fixture",
                                "files": list(policy.records),
                            }
                        ],
                    }
                )
            ]
        )
        catalog = ContractCatalog((grouped,), {})
        base = [
            "measure",
            "--build-dir",
            str(self.root / "configured"),
            "--output",
            str(self.root / "output.json"),
        ]
        output = io.StringIO()
        with patch.object(
            cli, "load_current_registry", return_value=registry
        ), patch.object(cli, "load_current_policy", return_value=policy), patch.object(
            cli, "load_current_catalog", return_value=catalog
        ), patch.object(
            cli.CMakeInventory, "read", return_value=self.inventory
        ), contextlib.redirect_stderr(
            output
        ):
            for selected in ("src/primitive", "src/unknown", "../outside"):
                with self.subTest(selected=selected), patch.object(
                    sys, "argv", [*base, "--component", selected]
                ):
                    self.assertEqual(cli.main(), 2)
            complete = AbstractionContract(
                "primitive", "src/primitive", "Owns values", ("Portable",)
            )
            with patch.object(
                cli,
                "load_current_catalog",
                return_value=ContractCatalog((complete,), {}),
            ):
                for selected in ([], ["--component", "src/primitive"]):
                    with self.subTest(arguments=selected), patch.object(
                        sys, "argv", [*base, *selected]
                    ):
                        self.assertEqual(cli.main(), 2)
            shell = SourcePolicy(
                [
                    SourceRecord(
                        "src/primitive/build.sh", "primitive", "shell", "tooling"
                    )
                ],
                [],
            )
            shell_registry = OwnershipRegistry(
                [
                    json.dumps(
                        {
                            "schema_version": 1,
                            "owners": [
                                {
                                    "id": "primitive",
                                    "purpose": "Fixture",
                                    "files": list(shell.records),
                                }
                            ],
                        }
                    )
                ]
            )
            with patch.object(
                cli, "load_current_policy", return_value=shell
            ), patch.object(
                cli, "load_current_registry", return_value=shell_registry
            ), patch.object(
                cli,
                "load_current_catalog",
                return_value=ContractCatalog((complete,), {}),
            ), patch.object(
                sys, "argv", [*base, "--component", "src/primitive"]
            ):
                self.assertEqual(cli.main(), 2)
        self.assertIn("source languages", output.getvalue())
        self.assertIn("no instrumented", output.getvalue())
