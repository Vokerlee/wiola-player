"""Apply declared layer and private-file rules to observed dependencies."""

from dataclasses import dataclass
from typing import Iterable, Mapping

from scripts.quality.architecture.contracts.catalog import (
    AbstractionContract,
    ContractCatalog,
)
from scripts.quality.architecture.dependency_graph.graph import DependencyEdge


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
        self._allowed = {
            module: frozenset(dependencies) for module, dependencies in allowed.items()
        }
        self._qt_modules = frozenset(qt_modules)
        self._contracts = {}
        for contract in ContractCatalog(contracts, {}).contracts:
            runtime_parts = (contract.owner, *contract.parts)
            if (contract.parts or contract.test_parts) and (
                any(identity not in self._owners for identity in contract.owner_ids)
                or len({self._owners[identity] for identity in runtime_parts}) != 1
            ):
                raise ValueError(
                    "Component parts must share one declared module capability"
                )
            if any(
                self._owners[identity] != "test" for identity in contract.test_parts
            ):
                raise ValueError("Component test members must retain the test module")
            self._contracts.update(
                (identity, contract) for identity in contract.owner_ids
            )
        if set(self._owners.values()) - self._allowed.keys():
            raise ValueError("Every owner module requires an explicit dependency rule")
        if any(
            set(dependencies) - self._allowed.keys()
            for dependencies in self._allowed.values()
        ):
            raise ValueError("Dependency rules reference unknown modules")
        if self._qt_modules - self._allowed.keys():
            raise ValueError("Qt capability references an unknown module")

    def inspect(
        self, edges: Iterable[DependencyEdge]
    ) -> tuple[DependencyViolation, ...]:
        violations = set()
        for edge in edges:
            origin = self._owners.get(edge.origin)
            if origin is None:
                violations.add(
                    DependencyViolation(
                        edge.identity, "Source owner has no module assignment"
                    )
                )
                continue
            if edge.kind in {"framework", "configured-framework"}:
                if origin not in self._qt_modules:
                    identity = (
                        f"configured-framework|{edge.source}|{origin}"
                        if edge.kind.startswith("configured-")
                        else edge.identity
                    )
                    violations.add(
                        DependencyViolation(
                            identity, f"Qt dependency in portable module {origin}"
                        )
                    )
                continue
            destination = self._owners.get(edge.destination)
            if destination is None:
                violations.add(
                    DependencyViolation(
                        edge.identity, "Dependency owner has no module assignment"
                    )
                )
                continue
            if origin != destination and destination not in self._allowed[origin]:
                identity = (
                    f"configured-target|{edge.source}|{edge.target}|{origin}|{destination}"
                    if edge.kind.startswith("configured-")
                    else edge.identity
                )
                violations.add(
                    DependencyViolation(
                        identity,
                        f"Forbidden module direction: {origin} -> {destination}",
                    )
                )
            contract = self._contracts.get(edge.destination)
            if (
                contract
                and edge.kind in {"include", "import"}
                and self._contracts.get(edge.origin) is not contract
                and not contract.grouping
            ):
                if edge.target not in contract.public_files:
                    violations.add(
                        DependencyViolation(
                            edge.identity,
                            "Dependency uses another abstraction's private file",
                        )
                    )
        return tuple(sorted(violations))


def compare_exceptions(
    violations: Iterable[DependencyViolation], legacy: Mapping[str, str]
) -> tuple[DependencyViolation, ...]:
    if any(
        not isinstance(reason, str) or not reason.strip() for reason in legacy.values()
    ):
        raise ValueError("Every migration exception needs a removal responsibility")
    actual = {
        violation.identity + "|" + violation.detail: violation
        for violation in violations
    }
    failures = [
        violation for identity, violation in actual.items() if identity not in legacy
    ]
    failures.extend(
        DependencyViolation(identity, "Stale migration exception must be removed")
        for identity in legacy.keys() - actual.keys()
    )
    return tuple(sorted(failures))
