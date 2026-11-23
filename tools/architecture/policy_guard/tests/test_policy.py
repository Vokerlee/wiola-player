from dataclasses import replace
import unittest

from tools.architecture.policy_guard.policy import PolicyGuard, PolicySnapshot
from tools.coverage.source_policy.policy import SourcePolicy, SourceRecord, SourceMove


def snapshot():
    return PolicySnapshot(
        {"legacy": "a" * 64},
        frozenset({"existing-edge"}),
        {"application": frozenset({"engine"}), "engine": frozenset()},
        {"owner": "application"},
        frozenset(),
        frozenset({"operation.terminal"}),
        SourcePolicy(
            [SourceRecord("src/app/receipt.hpp", "owner", "cpp", "production", True)],
            {"operation_test"},
        ),
    )


class PolicyGuardTest(unittest.TestCase):
    def test_unchanged_or_reduced_debt_passes(self):
        base = snapshot()
        self.assertEqual(PolicyGuard(base).audit(base), ())
        self.assertEqual(
            PolicyGuard(base).audit(
                replace(base, legacy_owners={}, dependency_exceptions=frozenset())
            ),
            (),
        )

    def test_new_or_reset_legacy_debt_fails(self):
        base = snapshot()
        for changed in [
            replace(base, legacy_owners={"legacy": "b" * 64}),
            replace(
                base, dependency_exceptions=frozenset({"existing-edge", "new-edge"})
            ),
        ]:
            self.assertTrue(PolicyGuard(base).audit(changed))

    def test_capability_and_dependency_widening_are_rejected(self):
        base = snapshot()
        for changed in [
            replace(base, qt_modules=frozenset({"engine"})),
            replace(
                base,
                allowed={
                    "application": frozenset({"engine"}),
                    "engine": frozenset({"application"}),
                },
            ),
            replace(base, allowed={}),
        ]:
            self.assertTrue(PolicyGuard(base).audit(changed))

    def test_inward_owner_move_preserves_qualified_scope_and_identity(self):
        base = snapshot()
        current = replace(
            base,
            modules={"owner": "engine"},
            sources=SourcePolicy(
                [
                    SourceRecord(
                        "src/engine/receipt.hpp", "owner", "cpp", "production", True
                    )
                ],
                {"operation_test"},
                [SourceMove("src/app/receipt.hpp", "src/engine/receipt.hpp", "owner")],
            ),
        )
        self.assertEqual(PolicyGuard(base).audit(current), ())
        self.assertTrue(PolicyGuard(current).audit(base))

    def test_cosmetic_move_cannot_hide_scope_or_behaviors(self):
        base = snapshot()
        current = replace(
            base,
            sources=SourcePolicy(
                [SourceRecord("src/engine/receipt.hpp", "owner", "cpp", "test", False)],
                set(),
                [SourceMove("src/app/receipt.hpp", "src/engine/receipt.hpp", "owner")],
            ),
            required_cases=frozenset(),
        )
        problems = PolicyGuard(base).audit(current)
        self.assertTrue(any("purpose" in p for p in problems))
        self.assertTrue(any("workflow" in p for p in problems))
        self.assertTrue(any("semantic" in p for p in problems))

    def test_retirement_requires_an_explicit_reviewed_deleted_path(self):
        base = snapshot()
        current = replace(base, sources=SourcePolicy([], {"operation_test"}))
        self.assertTrue(PolicyGuard(base).audit(current))
        self.assertEqual(
            PolicyGuard(base).audit(current, frozenset({"src/app/receipt.hpp"})), ()
        )

    def test_trusted_snapshot_cannot_change_when_the_callers_mapping_changes(self):
        base = snapshot()
        original = {"application": frozenset({"engine"}), "engine": frozenset()}
        trusted = replace(base, allowed=original)
        original["engine"] = frozenset({"application"})
        self.assertTrue(PolicyGuard(trusted).audit(replace(base, allowed=original)))
