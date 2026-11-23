import json
from pathlib import Path
import tempfile
import unittest

from tools.architecture.cmake_inventory.inventory import BuildTarget, CMakeInventory
from tools.architecture.ownership.registry import OwnershipRegistry
from tools.testing.catalog.catalog import TestCatalog, TestContract, RequiredCase


class TestCatalogTest(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.registry = OwnershipRegistry([json.dumps({"schema_version": 1, "owners": [
            {"id": "contract", "purpose": "Behavior witness", "files": ["tests/check.cpp"]}]})])
        self.contract = TestContract("check", "contract", "domain", ("tests/check.cpp",), "none", "")
        self.catalog = TestCatalog([self.contract], [RequiredCase("transfer", "check", "Check.Transfer")])
        self.binary = self.root / "build/check"
        self.target = BuildTarget("check", "EXECUTABLE", ("tests/check.cpp",), (), (), (self.binary,))
        self.discovery = {"tests": [{"name": "Check.Transfer", "command": [str(self.binary)]}]}

    def audit(self, targets=None, discovery=None):
        return self.catalog.audit(CMakeInventory(self.root, tuple(targets or [self.target])),
                                  discovery or self.discovery, self.registry)

    def test_declared_contract_matches_configured_owned_source_and_case(self):
        self.assertEqual(self.audit(), ())

    def test_missing_semantic_case_fails_even_when_the_binary_is_discovered(self):
        problems = self.audit(discovery={"tests": [{"name": "Check.Unrelated", "command": [str(self.binary)]}]})
        self.assertTrue(any("Missing semantic requirement transfer" in p for p in problems))

    def test_changed_toolkit_coupling_and_sources_require_review(self):
        target = BuildTarget("check", "EXECUTABLE", ("tests/other.cpp",), (), (), (self.binary,), ("QT_CORE_LIB",))
        problems = self.audit(targets=[target])
        self.assertTrue(any("Toolkit applicability" in p for p in problems))
        self.assertTrue(any("source membership" in p for p in problems))

    def test_unknown_case_target_and_missing_executable_fail(self):
        problems = self.audit(discovery={"tests": [{"name": "Other.Case", "command": ["unknown-command"]}]})
        self.assertTrue(any("no unique configured target" in p for p in problems))
        self.assertTrue(any("no executable discovery" in p for p in problems))

    def test_contradictory_roles_duplicate_targets_and_missing_requirement_targets_fail(self):
        with self.assertRaises(ValueError):
            TestContract("check", "contract", "widget", ("tests/check.cpp",), "none", "")
        with self.assertRaises(ValueError):
            TestContract("check", "contract", "engine", ("tests/check.cpp",), "widgets", "")
        with self.assertRaises(ValueError):
            TestCatalog([self.contract, self.contract], [])
        with self.assertRaises(ValueError):
            TestCatalog([self.contract], [RequiredCase("case", "missing", "Missing.Case")])
        with self.assertRaises(ValueError):
            TestCatalog([self.contract], [RequiredCase("same", "check", "One"), RequiredCase("same", "check", "Two")])

    def test_headless_selection_does_not_demand_widget_targets(self):
        widget = TestContract("widget", "contract", "widget", ("tests/check.cpp",), "widgets", "")
        catalog = TestCatalog([self.contract, widget], [RequiredCase("visual", "widget", "Widget.Paint")])
        inventory = CMakeInventory(self.root, (self.target,))
        self.assertEqual(catalog.audit(inventory, self.discovery, self.registry, with_qt=False), ())
        self.assertTrue(any("not configured: widget" in p for p in catalog.audit(inventory, self.discovery, self.registry)))

    def test_auxiliary_runner_cannot_be_assigned_an_unrelated_owner(self):
        catalog = TestCatalog([self.contract], [], {"Export.Check": {"owner": "contract", "script": "tests/check.cpp"}})
        discovery = {"tests": self.discovery["tests"] + [{"name": "Export.Check", "command": ["wrong-script"]}]}
        problems = catalog.audit(CMakeInventory(self.root, (self.target,)), discovery, self.registry)
        self.assertTrue(any("matching owned runner" in p for p in problems))
        self.assertTrue(any("auxiliary case is missing" in p for p in catalog.audit(
            CMakeInventory(self.root, (self.target,)), self.discovery, self.registry)))

    def test_custom_python_tool_tests_have_explicit_purpose_ownership(self):
        registry = OwnershipRegistry([json.dumps({"schema_version": 1, "owners": [
            {"id": "tool", "purpose": "Tool contracts", "files": ["tools/tool/tests/test_behavior.py"]}]})])
        self.assertTrue(TestCatalog([], []).audit_sources(registry))
        self.assertEqual(TestCatalog([], [], python_sources={"tools/tool/tests/test_behavior.py": "tool"}).audit_sources(registry), ())
        self.assertTrue(TestCatalog([], [], python_sources={"tools/deleted/test_behavior.py": "tool"}).audit_sources(registry))


class UndiscoveredCppTest(unittest.TestCase):
    def test_owned_test_source_without_discovery_or_catalog_entry_still_fails(self):
        registry = OwnershipRegistry([json.dumps({"schema_version": 1, "owners": [
            {"id": "contract", "purpose": "Test source", "files": ["owner/tests/new_test.cpp"]}]})])
        self.assertTrue(TestCatalog([], []).audit_sources(registry))
