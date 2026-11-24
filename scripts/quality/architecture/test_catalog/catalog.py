"""Reconcile declared test purposes with configured targets and actual discovery."""

from dataclasses import dataclass
from importlib.resources import files
import json
from pathlib import Path
from typing import Iterable
from types import MappingProxyType

from scripts.build.cmake import CMakeInventory
from scripts.quality.architecture.ownership.registry import OwnershipRegistry

ROLES = frozenset(
    {
        "domain",
        "engine",
        "application",
        "adapter",
        "presentation",
        "widget",
        "native",
        "process",
        "test_support",
        "export",
    }
)


@dataclass(frozen=True)
class TestContract:
    target: str
    owner: str
    role: str
    sources: tuple[str, ...]
    qt: str
    coupling: str

    def __post_init__(self):
        if (
            not self.target
            or not self.owner
            or not self.sources
            or self.role not in ROLES
            or self.qt not in {"none", "core", "widgets"}
        ):
            raise ValueError(
                "Test contract needs an owner, purpose, sources and explicit Qt applicability"
            )
        if self.role == "widget" and self.qt != "widgets":
            raise ValueError("Widget contracts require the widget toolkit")
        if (
            self.role in {"domain", "engine", "application"}
            and self.qt != "none"
            and not self.coupling
        ):
            raise ValueError("Incidental toolkit coupling must be explained")


@dataclass(frozen=True)
class RequiredCase:
    identity: str
    target: str
    name: str


class TestCatalog:
    def __init__(
        self,
        contracts: Iterable[TestContract],
        required: Iterable[RequiredCase],
        auxiliary: dict[str, dict[str, str]] | None = None,
        python_sources: dict[str, str] | None = None,
    ) -> None:
        contracts, required = tuple(contracts), tuple(required)
        self.contracts = MappingProxyType(
            {contract.target: contract for contract in contracts}
        )
        if len(self.contracts) != len(contracts):
            raise ValueError("Duplicate test target")
        if len({case.identity for case in required}) != len(required):
            raise ValueError("Duplicate semantic requirement identity")
        if any(
            not case.identity or not case.name or case.target not in self.contracts
            for case in required
        ):
            raise ValueError(
                "Required cases need a stable identity and a declared target"
            )
        self._required = required
        self._auxiliary = dict(auxiliary or {})
        self._python_sources = dict(python_sources or {})

    def audit(
        self,
        inventory: CMakeInventory,
        discovery: dict,
        registry: OwnershipRegistry,
        with_qt: bool = True,
    ) -> tuple[str, ...]:
        problems = []
        targets = {target.name: target for target in inventory.targets}
        expected = {
            name: contract
            for name, contract in self.contracts.items()
            if with_qt or contract.qt == "none"
        }
        binaries = {
            str(path.resolve()): target.name
            for target in inventory.targets
            for path in target.artifacts
        }
        cases: dict[str, set[str]] = {}
        for test in discovery["tests"]:
            name = test["name"]
            if name in self._auxiliary:
                continue
            commands = {
                binaries.get(str(Path(str(arg)).resolve()))
                for arg in test.get("command", [])
            }
            commands.discard(None)
            if len(commands) != 1:
                problems.append(
                    f"Discovered case has no unique configured target: {name}"
                )
                continue
            (target,) = commands
            if target not in expected:
                problems.append(
                    f"Discovered undeclared or inapplicable test target: {target}"
                )
            cases.setdefault(target, set()).add(name)
        for name, contract in sorted(expected.items()):
            target = targets.get(name)
            if target is None:
                problems.append(f"Required test target is not configured: {name}")
                continue
            if tuple(sorted(contract.sources)) != target.sources:
                problems.append(f"Test source membership changed: {name}")
            if target.kind != "EXECUTABLE" or name not in cases:
                problems.append(f"Test target has no executable discovery: {name}")
            actual_qt = (
                "widgets"
                if "QT_WIDGETS_LIB" in target.compile_definitions
                else "core" if "QT_CORE_LIB" in target.compile_definitions else "none"
            )
            if actual_qt != contract.qt:
                problems.append(
                    f"Toolkit applicability changed: {name}: {contract.qt} -> {actual_qt}"
                )
            for source in contract.sources:
                try:
                    if registry.owner_of(source).identity != contract.owner:
                        problems.append(f"Test source has a different owner: {source}")
                except KeyError:
                    problems.append(f"Test source is unowned: {source}")
        for case in self._required:
            if case.target in expected and case.name not in cases.get(
                case.target, set()
            ):
                problems.append(
                    f"Missing semantic requirement {case.identity}: {case.name}"
                )
        try:
            self.python_runners(discovery, inventory.source_root, registry)
        except ValueError as error:
            problems.append(str(error))
        return tuple(problems)

    def python_runners(
        self, discovery: dict, source_root: Path, registry: OwnershipRegistry
    ) -> tuple[tuple[str, ...], ...]:
        """Resolve only declared owned auxiliary Python commands, preserving their arguments."""
        seen = set()
        commands = []
        for test in discovery["tests"]:
            name = test["name"]
            if name not in self._auxiliary:
                continue
            if name in seen:
                raise ValueError("Duplicate auxiliary case: " + name)
            seen.add(name)
            commands.append(self._python_arguments(test, source_root, registry))
        missing = self._auxiliary.keys() - seen
        if missing:
            raise ValueError(
                "Required auxiliary case is missing: " + ", ".join(sorted(missing))
            )
        return tuple(commands)

    def _python_arguments(self, test, source_root, registry):
        runner = self._auxiliary[test["name"]]
        script = runner["script"]
        expected = (source_root / script).resolve()
        arguments = test.get("command", [])[1:]
        module = runner.get("module")
        if module is None:
            matches = arguments[:1] == [str(expected)]
        else:
            parts = module.split(".")
            matches = (
                all(part.isidentifier() for part in parts)
                and script == "/".join(parts) + "/__main__.py"
                and arguments[:2] == ["-m", module]
            )
        try:
            owned = registry.owner_of(script).identity == runner["owner"]
        except KeyError:
            owned = False
        if (
            not owned
            or not script.endswith(".py")
            or not expected.is_relative_to(source_root.resolve())
            or not matches
        ):
            raise ValueError(
                f"Auxiliary test {test['name']} has no matching owned runner"
            )
        return tuple(arguments)

    def audit_sources(self, registry: OwnershipRegistry) -> tuple[str, ...]:
        observed = {
            path: owner.identity
            for owner in registry.owners
            for path in owner.files
            if Path(path).suffix == ".py" and Path(path).name.startswith("test_")
        }
        problems = [
            f"Python test purpose/owner is undeclared or changed: {path}"
            for path, owner in observed.items()
            if self._python_sources.get(path) != owner
        ]
        problems.extend(
            f"Stale Python test contract: {path}"
            for path in self._python_sources.keys() - observed.keys()
        )
        cpp_sources = {
            source: contract.owner
            for contract in self.contracts.values()
            for source in contract.sources
        }
        for owner in registry.owners:
            for path in owner.files:
                if (
                    path.endswith("_test.cpp")
                    and cpp_sources.get(path) != owner.identity
                ):
                    problems.append(
                        f"C++ test purpose/owner is undeclared or changed: {path}"
                    )
        return tuple(sorted(problems))


def load_current_catalog() -> TestCatalog:
    directory = files(__package__).joinpath("data")
    targets = json.loads(directory.joinpath("targets.json").read_text(encoding="utf-8"))
    cases = json.loads(
        directory.joinpath("requirements.json").read_text(encoding="utf-8")
    )
    return TestCatalog(
        [
            TestContract(**dict(value, sources=tuple(value["sources"])))
            for value in targets
        ],
        [RequiredCase(**case) for case in cases["required"]],
        cases["auxiliary"],
        json.loads(directory.joinpath("python.json").read_text(encoding="utf-8")),
    )
