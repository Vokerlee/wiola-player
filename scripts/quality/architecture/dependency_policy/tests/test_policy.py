import unittest

from scripts.quality.architecture.contracts.catalog import AbstractionContract
from scripts.quality.architecture.dependency_graph.graph import DependencyEdge
from scripts.quality.architecture.dependency_policy.policy import (
    DependencyPolicy,
    compare_exceptions,
)


class DependencyPolicyTest(unittest.TestCase):
    def component_policy(self):
        return DependencyPolicy(
            {"engine": "engine", "values": "engine", "app": "application"},
            {"engine": [], "application": ["engine"]},
            [],
            [
                AbstractionContract(
                    "engine",
                    "engine",
                    "Playback",
                    ("Portable",),
                    ("engine/api.hpp", "engine/values.hpp"),
                    parts=("values",),
                )
            ],
        )

    def test_parts_share_private_implementation_but_external_clients_use_public_api(
        self,
    ):
        policy = self.component_policy()
        internal_and_public = [
            DependencyEdge("engine/a.cpp", "engine/detail.hpp", "values", "engine"),
            DependencyEdge(
                "engine/b.cpp", "engine/values-private.hpp", "engine", "values"
            ),
            DependencyEdge("app/a.cpp", "engine/values.hpp", "app", "values"),
        ]
        self.assertEqual(policy.inspect(internal_and_public), ())
        private = policy.inspect(
            [DependencyEdge("app/a.cpp", "engine/values-private.hpp", "app", "values")]
        )
        self.assertEqual(len(private), 1)
        self.assertIn("private file", private[0].detail)

    def test_component_parts_cannot_merge_capabilities_or_unassigned_owners(self):
        for owners in (
            {"engine": "engine", "values": "application"},
            {"engine": "engine"},
        ):
            with self.subTest(owners=owners), self.assertRaises(ValueError):
                DependencyPolicy(
                    owners,
                    {"engine": [], "application": ["engine"]},
                    [],
                    [
                        AbstractionContract(
                            "engine",
                            "engine",
                            "Playback",
                            ("Portable",),
                            parts=("values",),
                        )
                    ],
                )
        with self.assertRaises(ValueError):
            DependencyPolicy(
                {"engine": "engine"},
                {"engine": []},
                [],
                [
                    AbstractionContract("engine", "engine", "Playback", ("Portable",)),
                    AbstractionContract(
                        "other", "other", "Other", ("Separate",), parts=("engine",)
                    ),
                ],
            )

    def policy(self):
        return DependencyPolicy(
            {"engine": "engine", "app": "application", "widget": "frontend"},
            {
                "engine": [],
                "application": ["engine"],
                "frontend": ["engine", "application"],
            },
            ["frontend"],
            [
                AbstractionContract(
                    "engine", "engine", "Playback", ("Portable",), ("engine/api.hpp",)
                )
            ],
        )

    def test_reverse_direction_and_private_access_are_distinct_failures(self):
        edges = [
            DependencyEdge("engine/a.hpp", "app/api.hpp", "engine", "app"),
            DependencyEdge("app/a.cpp", "engine/private.hpp", "app", "engine"),
        ]
        violations = self.policy().inspect(edges)
        self.assertEqual(len(violations), 2)
        self.assertTrue(
            any("Forbidden module direction" in v.detail for v in violations)
        )
        self.assertTrue(any("private file" in v.detail for v in violations))

    def test_public_dependency_and_declared_toolkit_are_allowed(self):
        self.assertEqual(
            self.policy().inspect(
                [
                    DependencyEdge("app/a.cpp", "engine/api.hpp", "app", "engine"),
                    DependencyEdge(
                        "widget/a.cpp", "QObject", "widget", "external.qt", "framework"
                    ),
                ]
            ),
            (),
        )

    def test_portable_qt_dependency_fails_from_source_or_configured_usage(self):
        for kind in ("framework", "configured-framework"):
            with self.subTest(kind=kind):
                violations = self.policy().inspect(
                    [
                        DependencyEdge(
                            "engine/a.hpp", "QObject", "engine", "external.qt", kind
                        )
                    ]
                )
                self.assertIn("Qt dependency in portable", violations[0].detail)

    def test_unknown_membership_is_rejected(self):
        violations = self.policy().inspect(
            [DependencyEdge("a", "b", "unknown", "engine")]
        )
        self.assertIn("no module assignment", violations[0].detail)
        destination = self.policy().inspect(
            [DependencyEdge("app/a.cpp", "unknown/api.hpp", "app", "unknown")]
        )
        self.assertIn(
            "Dependency owner has no module assignment", destination[0].detail
        )
        with self.assertRaises(ValueError):
            DependencyPolicy({"a": "unknown"}, {"engine": []}, [], [])
        with self.assertRaises(ValueError):
            DependencyPolicy({}, {"engine": ["unknown"]}, [], [])
        with self.assertRaises(ValueError):
            DependencyPolicy({}, {"engine": []}, ["unknown"], [])

    def test_exception_records_cannot_hide_a_second_violation_on_the_same_edge(self):
        policy = DependencyPolicy(
            {"a": "engine", "b": "application"},
            {"engine": [], "application": []},
            [],
            [AbstractionContract("b", "b", "App", ("Private",), ("b/api.hpp",))],
        )
        violations = policy.inspect(
            [DependencyEdge("a/a.cpp", "b/private.hpp", "a", "b")]
        )
        self.assertEqual(len(violations), 2)
        baseline = {
            violations[0].identity + "|" + violations[0].detail: "Split owning boundary"
        }
        self.assertEqual(len(compare_exceptions(violations, baseline)), 1)
        self.assertEqual(len(compare_exceptions([], baseline)), 1)
        complete = {
            v.identity + "|" + v.detail: "Extract the owning boundary"
            for v in violations
        }
        self.assertEqual(compare_exceptions(violations, complete), ())
        with self.assertRaises(ValueError):
            compare_exceptions([], {"bad": ""})
