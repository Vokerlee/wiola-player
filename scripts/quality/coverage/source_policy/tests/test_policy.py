import copy
import json
from pathlib import Path
import tempfile
import unittest

from scripts.quality.architecture.ownership.registry import OwnershipRegistry
from scripts.quality.coverage.source_policy.policy import (
    SourcePolicy,
    SourceRecord,
    SourceMove,
)


class SourcePolicyTest(unittest.TestCase):
    def record(self, path="src/engine/api.hpp", **changes):
        values = dict(
            path=path,
            owner="engine.contract",
            language="cpp",
            role="production",
            workflow=True,
        )
        values.update(changes)
        return SourceRecord(**values)

    def test_owned_scope_survives_folder_changes_and_external_files_are_excluded(self):
        policy = SourcePolicy([self.record()], [])
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            self.assertTrue(policy.workflow_file(root / "src/engine/api.hpp", root))
            self.assertFalse(policy.workflow_file(root / "src/app/unowned.hpp", root))
            self.assertFalse(policy.workflow_file(root.parent / "external.hpp", root))

    def test_missing_stale_and_changed_owners_fail_reconciliation(self):
        registry = OwnershipRegistry(
            [
                json.dumps(
                    {
                        "schema_version": 1,
                        "owners": [
                            {
                                "id": "new.owner",
                                "purpose": "Fixture",
                                "files": ["src/engine/api.hpp"],
                            }
                        ],
                    }
                )
            ]
        )
        self.assertTrue(SourcePolicy([self.record()], []).reconcile(registry))
        self.assertEqual(
            SourcePolicy([self.record(owner="new.owner")], []).reconcile(registry), ()
        )
        self.assertTrue(
            SourcePolicy([self.record("src/old.hpp")], []).reconcile(registry)
        )

    def test_lineage_preserves_every_metric_and_does_not_modify_its_input(self):
        policy = SourcePolicy(
            [self.record()],
            [],
            [SourceMove("src/app/api.hpp", "src/engine/api.hpp", "engine.contract")],
        )
        original = {
            "files": [
                {
                    "file": "src/app/api.hpp",
                    "counts": {"lines": {"count": 11, "covered": 9}},
                }
            ],
            "totals": {"lines": {"count": 11, "covered": 9}},
        }
        before = copy.deepcopy(original)
        migrated = policy.migrate_report(original)
        self.assertEqual(original, before)
        self.assertEqual(migrated["files"][0]["file"], "src/engine/api.hpp")
        self.assertEqual(migrated["files"][0]["counts"], before["files"][0]["counts"])
        self.assertEqual(migrated["totals"], before["totals"])
        migrated["files"][0]["counts"]["lines"]["covered"] = 0
        self.assertEqual(original, before)

    def test_lineage_rejects_cycles_merges_owner_changes_and_unretired_files(self):
        bad_moves = [
            [
                SourceMove("a.hpp", "b.hpp", "engine.contract"),
                SourceMove("b.hpp", "a.hpp", "engine.contract"),
            ],
            [
                SourceMove("a.hpp", "src/engine/api.hpp", "engine.contract"),
                SourceMove("b.hpp", "src/engine/api.hpp", "engine.contract"),
            ],
            [SourceMove("a.hpp", "src/engine/api.hpp", "other.owner")],
            [SourceMove("src/engine/api.hpp", "elsewhere.hpp", "engine.contract")],
            [SourceMove("../a.hpp", "src/engine/api.hpp", "engine.contract")],
        ]
        for moves in bad_moves:
            with self.subTest(moves=moves), self.assertRaises(ValueError):
                SourcePolicy([self.record()], [], moves)

    def test_move_chain_and_duplicate_baseline_destinations_are_checked(self):
        policy = SourcePolicy(
            [self.record()],
            [],
            [
                SourceMove("a.hpp", "b.hpp", "engine.contract"),
                SourceMove("b.hpp", "src/engine/api.hpp", "engine.contract"),
            ],
        )
        self.assertEqual(policy.current_path("a.hpp"), "src/engine/api.hpp")
        with self.assertRaises(ValueError):
            policy.migrate_report({"files": [{"file": "a.hpp"}, {"file": "b.hpp"}]})

    def test_invalid_roles_paths_languages_and_duplicates_fail(self):
        for changes in [
            {"path": "../api.hpp"},
            {"path": "C:\\api.hpp"},
            {"path": "src/api.py"},
            {"role": "hidden"},
            {"owner": ""},
            {"workflow": 1},
            {"role": "test"},
        ]:
            with self.subTest(changes=changes), self.assertRaises(ValueError):
                self.record(**changes)
        with self.assertRaises(ValueError):
            SourcePolicy([self.record(), self.record()], [])


class LanguagePurposeTest(unittest.TestCase):
    def test_production_roles_cannot_hide_python_from_tool_measurement(self):
        with self.assertRaises(ValueError):
            SourceRecord("tool.py", "tool", "python", "production")
