"""Reject silent weakening of previously accepted architecture constraints."""

from dataclasses import dataclass
from typing import Mapping
from types import MappingProxyType

from tools.coverage.source_policy.policy import SourcePolicy


@dataclass(frozen=True)
class PolicySnapshot:
    legacy_owners: Mapping[str, str]
    dependency_exceptions: frozenset[str]
    allowed: Mapping[str, frozenset[str]]
    modules: Mapping[str, str]
    qt_modules: frozenset[str]
    required_cases: frozenset[str]
    sources: SourcePolicy

    def __post_init__(self):
        object.__setattr__(
            self, "legacy_owners", MappingProxyType(dict(self.legacy_owners))
        )
        object.__setattr__(
            self,
            "allowed",
            MappingProxyType(
                {key: frozenset(value) for key, value in self.allowed.items()}
            ),
        )
        object.__setattr__(self, "modules", MappingProxyType(dict(self.modules)))


class PolicyGuard:
    def __init__(self, previous: PolicySnapshot) -> None:
        self._previous = previous

    def audit(
        self, current: PolicySnapshot, reviewed_deletions: frozenset[str] = frozenset()
    ) -> tuple[str, ...]:
        previous = self._previous
        problems = []
        for owner, identity in current.legacy_owners.items():
            if previous.legacy_owners.get(owner) != identity:
                problems.append("Legacy ownership debt added or reset: " + owner)
        for edge in current.dependency_exceptions - previous.dependency_exceptions:
            problems.append("New dependency migration exception: " + edge)
        for module, dependencies in previous.allowed.items():
            if module not in current.allowed:
                problems.append("Existing module rule removed: " + module)
            elif current.allowed[module] - dependencies:
                problems.append(
                    "Existing module dependency permissions widened: " + module
                )
        for module in (
            current.qt_modules - previous.qt_modules
        ) & previous.allowed.keys():
            problems.append("Existing portable module granted Qt: " + module)
        for owner, old_module in previous.modules.items():
            new_module = current.modules.get(owner)
            if new_module is not None and new_module != old_module:
                if new_module not in previous.allowed[old_module]:
                    problems.append(
                        "Owner moved outward or into an unrelated capability: " + owner
                    )
        for requirement in previous.required_cases - current.required_cases:
            problems.append("Required semantic test identity removed: " + requirement)
        for path, old in previous.sources.records.items():
            destination = current.sources.current_path(path)
            new = current.sources.records.get(destination)
            if new is None:
                if path not in reviewed_deletions:
                    problems.append(
                        "Source removed without reviewed deletion or lineage: " + path
                    )
                continue
            if old.owner != new.owner or old.language != new.language:
                problems.append("Source measurement identity changed: " + path)
            if old.role != new.role and not (
                old.role == "test" and new.role in {"production", "tooling"}
            ):
                problems.append("Source purpose changed without qualification: " + path)
            if old.workflow and not new.workflow:
                problems.append("Source removed from qualified workflow: " + path)
        if not previous.sources.workflow_targets.issubset(
            current.sources.workflow_targets
        ):
            problems.append("Qualified workflow test targets removed")
        return tuple(sorted(problems))
