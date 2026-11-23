import json
import unittest

from tools.architecture.contracts.catalog import (
    AbstractionContract, ContractCatalog, file_set_identity,
)
from tools.architecture.ownership.registry import OwnershipRegistry


def registry(files=None, extra=()):
    owners = [{"id": "component", "purpose": "Owns the component contract", "files": files or [
        "src/component/README.md", "src/component/component.hpp", "src/component/component.cpp"]}]
    owners.extend(extra)
    return OwnershipRegistry([json.dumps({"schema_version": 1, "owners": owners})])


def contract(**changes):
    values = dict(owner="component", root="src/component", responsibility="Owns one invariant",
                  invariants=("Inputs are explicit",), public_files=("src/component/component.hpp",))
    values.update(changes)
    return AbstractionContract(**values)


class ContractCatalogTest(unittest.TestCase):
    def test_complete_abstraction_and_owned_public_api_are_accepted(self):
        audit = ContractCatalog([contract()], {}).audit(registry())
        self.assertTrue(audit.accepted)
        self.assertEqual(audit.legacy_owners, ())

    def test_split_implementation_missing_readme_and_public_leak_fail(self):
        owners = registry(["src/component/component.hpp", "elsewhere/component.cpp"])
        audit = ContractCatalog([contract(public_files=("private/unowned.hpp",))], {}).audit(owners)
        self.assertFalse(audit.accepted)
        for expected in ["outside owner", "README", "Public file"]:
            self.assertTrue(any(expected in issue.detail for issue in audit.issues))

    def test_unrelated_implementation_cannot_hide_inside_an_abstraction(self):
        owners = registry(extra=[{"id": "other", "purpose": "Another invariant",
                                  "files": ["src/component/other.cpp"]}])
        audit = ContractCatalog([contract()], {}).audit(owners)
        self.assertTrue(any("unrelated owner" in issue.detail for issue in audit.issues))

    def test_grouping_folder_can_contain_separately_owned_children(self):
        owners = registry(extra=[{"id": "group", "purpose": "Groups components",
                                  "files": ["src/README.md"]}])
        group = AbstractionContract("group", "src", "Groups components", ("Children own behavior",), grouping=True)
        self.assertTrue(ContractCatalog([contract(), group], {}).audit(owners).accepted)

    def test_grouping_is_not_a_bypass_for_unrelated_implementation(self):
        grouping = contract(grouping=True)
        audit = ContractCatalog([grouping], {}).audit(registry())
        self.assertTrue(any("cannot hide" in issue.detail for issue in audit.issues))

    def test_legacy_allowance_is_exact_and_visible(self):
        owners = registry()
        baseline = {"component": file_set_identity(owners.owners[0])}
        catalog = ContractCatalog([], baseline)
        self.assertEqual(catalog.audit(owners).legacy_owners, ("component",))
        changed = registry(["src/component/README.md", "src/component/new.hpp"])
        self.assertFalse(catalog.audit(changed).accepted)
        self.assertFalse(ContractCatalog([], {}).audit(owners).accepted)

    def test_stale_or_ambiguous_contracts_are_rejected(self):
        with self.assertRaises(ValueError):
            ContractCatalog([contract(), contract()], {})
        with self.assertRaises(ValueError):
            ContractCatalog([contract()], {"component": "0" * 64})
        with self.assertRaises(ValueError):
            ContractCatalog([], {"component": "not-a-digest"})
        audit = ContractCatalog([contract(owner="gone")], {}).audit(registry())
        self.assertTrue(any("no longer" in issue.detail for issue in audit.issues))

    def test_root_and_invariant_contracts_are_explicit(self):
        for changes in [{"root": "../outside"}, {"root": "."}, {"root": "/absolute"},
                        {"invariants": ()}, {"invariants": ("",)}, {"responsibility": ""}]:
            with self.subTest(changes=changes), self.assertRaises(ValueError):
                ContractCatalog([contract(**changes)], {})


if __name__ == "__main__":
    unittest.main()
