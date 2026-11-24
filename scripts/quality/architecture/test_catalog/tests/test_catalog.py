import json
from pathlib import Path
import tempfile
import subprocess
import sys
import unittest

from scripts.build.cmake import BuildTarget, CMakeInventory
from scripts.quality.architecture.ownership.registry import OwnershipRegistry
from scripts.quality.architecture.test_catalog.catalog import (
    TestCatalog,
    TestContract,
    RequiredCase,
    load_current_catalog,
)


class TestCatalogTest(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.registry = OwnershipRegistry(
            [
                json.dumps(
                    {
                        "schema_version": 1,
                        "owners": [
                            {
                                "id": "contract",
                                "purpose": "Behavior witness",
                                "files": ["tests/check.cpp"],
                            }
                        ],
                    }
                )
            ]
        )
        self.contract = TestContract(
            "check", "contract", "domain", ("tests/check.cpp",), "none", ""
        )
        self.catalog = TestCatalog(
            [self.contract], [RequiredCase("transfer", "check", "Check.Transfer")]
        )
        self.binary = self.root / "build/check"
        self.target = BuildTarget(
            "check", "EXECUTABLE", ("tests/check.cpp",), (), (), (self.binary,)
        )
        self.discovery = {
            "tests": [{"name": "Check.Transfer", "command": [str(self.binary)]}]
        }

    def audit(self, targets=None, discovery=None):
        return self.catalog.audit(
            CMakeInventory(self.root, tuple(targets or [self.target])),
            discovery or self.discovery,
            self.registry,
        )

    def test_declared_contract_matches_configured_owned_source_and_case(self):
        self.assertEqual(self.audit(), ())

    def test_missing_semantic_case_fails_even_when_the_binary_is_discovered(self):
        problems = self.audit(
            discovery={
                "tests": [{"name": "Check.Unrelated", "command": [str(self.binary)]}]
            }
        )
        self.assertTrue(
            any("Missing semantic requirement transfer" in p for p in problems)
        )

    def test_changed_toolkit_coupling_and_sources_require_review(self):
        target = BuildTarget(
            "check",
            "EXECUTABLE",
            ("tests/other.cpp",),
            (),
            (),
            (self.binary,),
            ("QT_CORE_LIB",),
        )
        problems = self.audit(targets=[target])
        self.assertTrue(any("Toolkit applicability" in p for p in problems))
        self.assertTrue(any("source membership" in p for p in problems))

    def test_unknown_case_target_and_missing_executable_fail(self):
        problems = self.audit(
            discovery={
                "tests": [{"name": "Other.Case", "command": ["unknown-command"]}]
            }
        )
        self.assertTrue(any("no unique configured target" in p for p in problems))
        self.assertTrue(any("no executable discovery" in p for p in problems))

    def test_contradictory_roles_duplicate_targets_and_missing_requirement_targets_fail(
        self,
    ):
        with self.assertRaises(ValueError):
            TestContract(
                "check", "contract", "widget", ("tests/check.cpp",), "none", ""
            )
        with self.assertRaises(ValueError):
            TestContract(
                "check", "contract", "engine", ("tests/check.cpp",), "widgets", ""
            )
        with self.assertRaises(ValueError):
            TestCatalog([self.contract, self.contract], [])
        with self.assertRaises(ValueError):
            TestCatalog(
                [self.contract], [RequiredCase("case", "missing", "Missing.Case")]
            )
        with self.assertRaises(ValueError):
            TestCatalog(
                [self.contract],
                [
                    RequiredCase("same", "check", "One"),
                    RequiredCase("same", "check", "Two"),
                ],
            )

    def test_headless_selection_does_not_demand_widget_targets(self):
        widget = TestContract(
            "widget", "contract", "widget", ("tests/check.cpp",), "widgets", ""
        )
        catalog = TestCatalog(
            [self.contract, widget], [RequiredCase("visual", "widget", "Widget.Paint")]
        )
        inventory = CMakeInventory(self.root, (self.target,))
        self.assertEqual(
            catalog.audit(inventory, self.discovery, self.registry, with_qt=False), ()
        )
        self.assertTrue(
            any(
                "not configured: widget" in p
                for p in catalog.audit(inventory, self.discovery, self.registry)
            )
        )

    def test_auxiliary_runner_cannot_be_assigned_an_unrelated_owner(self):
        catalog = TestCatalog(
            [self.contract],
            [],
            {"Export.Check": {"owner": "contract", "script": "tests/check.cpp"}},
        )
        discovery = {
            "tests": self.discovery["tests"]
            + [{"name": "Export.Check", "command": ["wrong-script"]}]
        }
        problems = catalog.audit(
            CMakeInventory(self.root, (self.target,)), discovery, self.registry
        )
        self.assertTrue(any("matching owned runner" in p for p in problems))
        self.assertTrue(
            any(
                "auxiliary case is missing" in p
                for p in catalog.audit(
                    CMakeInventory(self.root, (self.target,)),
                    self.discovery,
                    self.registry,
                )
            )
        )

    def test_custom_python_tool_tests_have_explicit_purpose_ownership(self):
        registry = OwnershipRegistry(
            [
                json.dumps(
                    {
                        "schema_version": 1,
                        "owners": [
                            {
                                "id": "tool",
                                "purpose": "Tool contracts",
                                "files": ["tools/tool/tests/test_behavior.py"],
                            }
                        ],
                    }
                )
            ]
        )
        self.assertTrue(TestCatalog([], []).audit_sources(registry))
        self.assertEqual(
            TestCatalog(
                [], [], python_sources={"tools/tool/tests/test_behavior.py": "tool"}
            ).audit_sources(registry),
            (),
        )
        self.assertTrue(
            TestCatalog(
                [], [], python_sources={"tools/deleted/test_behavior.py": "tool"}
            ).audit_sources(registry)
        )

    def test_invalid_purpose_and_known_but_undeclared_executables_fail(self):
        with self.assertRaisesRegex(ValueError, "explicit Qt applicability"):
            TestContract(
                "check", "contract", "unknown", ("tests/check.cpp",), "none", ""
            )
        unknown = BuildTarget("undeclared", "EXECUTABLE", (), (), (), (self.binary,))
        self.assertTrue(
            any(
                "undeclared or inapplicable" in issue
                for issue in self.audit(targets=[unknown])
            )
        )

    def test_changed_and_missing_source_ownership_are_not_accepted(self):
        inventory = CMakeInventory(self.root, (self.target,))
        other = TestContract(
            "check", "other", "domain", ("tests/check.cpp",), "none", ""
        )
        issues = TestCatalog([other], []).audit(
            inventory, self.discovery, self.registry
        )
        self.assertTrue(any("different owner" in issue for issue in issues))
        unrelated = OwnershipRegistry(
            [
                json.dumps(
                    {
                        "schema_version": 1,
                        "owners": [
                            {
                                "id": "other",
                                "purpose": "Unrelated source",
                                "files": ["tests/other.cpp"],
                            }
                        ],
                    }
                )
            ]
        )
        issues = self.catalog.audit(inventory, self.discovery, unrelated)
        self.assertTrue(any("source is unowned" in issue for issue in issues))

    def test_owned_package_resources_load_with_their_actual_source_registry(self):
        from scripts.quality.architecture.ownership.registry import (
            load_current_registry,
        )

        self.assertEqual(
            load_current_catalog().audit_sources(load_current_registry()), ()
        )


class UndiscoveredCppTest(unittest.TestCase):
    def test_owned_test_source_without_discovery_or_catalog_entry_still_fails(self):
        registry = OwnershipRegistry(
            [
                json.dumps(
                    {
                        "schema_version": 1,
                        "owners": [
                            {
                                "id": "contract",
                                "purpose": "Test source",
                                "files": ["owner/tests/new_test.cpp"],
                            }
                        ],
                    }
                )
            ]
        )
        self.assertTrue(TestCatalog([], []).audit_sources(registry))


class PythonRunnerTest(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix="owned runner ")
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.script = "scripts/consumer/__main__.py"
        self.registry = OwnershipRegistry(
            [
                json.dumps(
                    {
                        "schema_version": 1,
                        "owners": [
                            {
                                "id": "consumer",
                                "purpose": "Independent consumer check",
                                "files": [self.script],
                            }
                        ],
                    }
                )
            ]
        )
        self.runner = {"owner": "consumer", "script": self.script}
        self.command = [
            "python",
            str(self.root / self.script),
            "--prefix",
            "prefix with spaces",
        ]

    def resolve(self, *, runner=None, command=None, tests=None):
        catalog = TestCatalog([], [], {"Consumer.Public": runner or self.runner})
        discovery = {
            "tests": (
                tests
                if tests is not None
                else [
                    {
                        "name": "Consumer.Public",
                        "command": command if command is not None else self.command,
                    }
                ]
            )
        }
        return catalog.python_runners(discovery, self.root, self.registry)

    def test_script_and_module_commands_preserve_every_argument(self):
        self.assertEqual(self.resolve(), (tuple(self.command[1:]),))
        module = dict(self.runner, module="scripts.consumer")
        command = ["python", "-m", "scripts.consumer", "--prefix", "prefix with spaces"]
        self.assertEqual(
            self.resolve(runner=module, command=command), (tuple(command[1:]),)
        )
        # An unrelated executable is not a Python runner or a source of guessed script paths.
        self.assertEqual(
            self.resolve(
                tests=[
                    {"name": "Binary.Case", "command": ["unrelated"]},
                    {"name": "Consumer.Public", "command": self.command},
                ]
            ),
            (tuple(self.command[1:]),),
        )

    def test_wrong_owner_unknown_source_and_nonpython_source_are_rejected(self):
        for change in (
            {"owner": "other"},
            {"script": "scripts/unknown.py"},
            {"script": "tests/contract.cpp"},
        ):
            with self.subTest(change=change), self.assertRaisesRegex(
                ValueError, "matching owned runner"
            ):
                self.resolve(runner=dict(self.runner, **change))
        cpp_registry = OwnershipRegistry(
            [
                json.dumps(
                    {
                        "schema_version": 1,
                        "owners": [
                            {
                                "id": "consumer",
                                "purpose": "Contract",
                                "files": ["tests/contract.cpp"],
                            }
                        ],
                    }
                )
            ]
        )
        catalog = TestCatalog(
            [],
            [],
            {"Consumer.Public": {"owner": "consumer", "script": "tests/contract.cpp"}},
        )
        with self.assertRaisesRegex(ValueError, "matching owned runner"):
            catalog.python_runners(
                {
                    "tests": [
                        {
                            "name": "Consumer.Public",
                            "command": [
                                "python",
                                str(self.root / "tests/contract.cpp"),
                            ],
                        }
                    ]
                },
                self.root,
                cpp_registry,
            )

    def test_declared_module_requires_its_exact_owned_package_entrypoint(self):
        for module, command in (
            ("scripts.other", ["python", "-m", "scripts.other"]),
            ("scripts..consumer", ["python", "-m", "scripts..consumer"]),
            ("scripts.consumer", self.command),
            ("scripts.consumer", ["python", "-m", "scripts.wrong"]),
            ("scripts.consumer", ["python", "-m"]),
        ):
            with self.subTest(module=module, command=command), self.assertRaisesRegex(
                ValueError, "matching owned runner"
            ):
                self.resolve(runner=dict(self.runner, module=module), command=command)
        for command in ([], ["python"], ["python", "wrong.py"]):
            with self.subTest(command=command), self.assertRaisesRegex(
                ValueError, "matching owned runner"
            ):
                self.resolve(command=command)

    def test_missing_and_duplicate_auxiliary_cases_are_rejected(self):
        with self.assertRaisesRegex(ValueError, "auxiliary case is missing"):
            self.resolve(tests=[])
        case = {"name": "Consumer.Public", "command": self.command}
        with self.assertRaisesRegex(ValueError, "Duplicate auxiliary case"):
            self.resolve(tests=[case, case])

    def test_owned_symlink_cannot_resolve_outside_the_source_root(self):
        with tempfile.TemporaryDirectory() as outside:
            path = self.root / self.script
            path.parent.mkdir(parents=True)
            target = Path(outside) / "runner.py"
            target.write_text("raise RuntimeError('must not execute')\n")
            path.symlink_to(target)
            with self.assertRaisesRegex(ValueError, "matching owned runner"):
                self.resolve(command=["python", str(target)])

    def test_resolved_module_executes_from_its_own_nonrepository_root(self):
        entry = self.root / self.script
        entry.parent.mkdir(parents=True)
        entry.write_text("import json, sys\nprint(json.dumps(sys.argv[1:]))\n")
        runner = dict(self.runner, module="scripts.consumer")
        command = [
            sys.executable,
            "-m",
            "scripts.consumer",
            "--prefix",
            "prefix with spaces",
        ]
        (arguments,) = self.resolve(runner=runner, command=command)
        result = subprocess.run(
            [sys.executable, *arguments],
            cwd=self.root,
            capture_output=True,
            text=True,
            timeout=10,
            check=True,
        )
        self.assertEqual(json.loads(result.stdout), command[3:])
