"""Apply declared layer and private-file rules to observed dependencies."""

from dataclasses import dataclass
from typing import Iterable, Mapping

from tools.architecture.contracts.catalog import AbstractionContract
from tools.architecture.dependency_graph.graph import DependencyEdge


@dataclass(frozen=True, order=True)
class DependencyViolation:
    identity: str
    detail: str


class DependencyPolicy:
    """Explicit owner-to-module assignments; no runtime classification by folder name."""

    def __init__(
        self,
        owners: Mapping[str, str],
        allowed: Mapping[str, Iterable[str]],
        qt_modules: Iterable[str],
        contracts: Iterable[AbstractionContract],
    ) -> None:
        self._owners = dict(owners)
        self._allowed = {module: frozenset(dependencies) for module, dependencies in allowed.items()}
        self._qt_modules = frozenset(qt_modules)
        self._contracts = {contract.owner: contract for contract in contracts}
        if set(self._owners.values()) - self._allowed.keys():
            raise ValueError("Every owner module requires an explicit dependency rule")
        if any(set(dependencies) - self._allowed.keys() for dependencies in self._allowed.values()):
            raise ValueError("Dependency rules reference unknown modules")
        if self._qt_modules - self._allowed.keys():
            raise ValueError("Qt capability references an unknown module")

    def inspect(self, edges: Iterable[DependencyEdge]) -> tuple[DependencyViolation, ...]:
        violations = set()
        for edge in edges:
            origin = self._owners.get(edge.origin)
            if origin is None:
                violations.add(DependencyViolation(edge.identity, "Source owner has no module assignment"))
                continue
            if edge.kind in {"framework", "configured-framework"}:
                if origin not in self._qt_modules:
                    identity = (f"configured-framework|{edge.source}|{origin}"
                                if edge.kind.startswith("configured-") else edge.identity)
                    violations.add(DependencyViolation(identity, f"Qt dependency in portable module {origin}"))
                continue
            destination = self._owners.get(edge.destination)
            if destination is None:
                violations.add(DependencyViolation(edge.identity, "Dependency owner has no module assignment"))
                continue
            if origin != destination and destination not in self._allowed[origin]:
                identity = (f"configured-target|{edge.source}|{edge.target}|{origin}|{destination}"
                            if edge.kind.startswith("configured-") else edge.identity)
                violations.add(DependencyViolation(identity, f"Forbidden module direction: {origin} -> {destination}"))
            contract = self._contracts.get(edge.destination)
            if contract and edge.kind in {"include", "import"} and edge.origin != edge.destination and not contract.grouping:
                if edge.target not in contract.public_files:
                    violations.add(DependencyViolation(edge.identity, "Dependency uses another abstraction's private file"))
        return tuple(sorted(violations))


def compare_exceptions(
    violations: Iterable[DependencyViolation], legacy: Mapping[str, str]
) -> tuple[DependencyViolation, ...]:
    if any(not isinstance(reason, str) or not reason.strip() for reason in legacy.values()):
        raise ValueError("Every migration exception needs a removal responsibility")
    actual = {violation.identity + "|" + violation.detail: violation for violation in violations}
    failures = [violation for identity, violation in actual.items() if identity not in legacy]
    failures.extend(DependencyViolation(identity, "Stale migration exception must be removed")
                    for identity in legacy.keys() - actual.keys())
    return tuple(sorted(failures))
