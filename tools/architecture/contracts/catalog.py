"""Check complete abstraction ownership while tracking exact legacy debt."""

from dataclasses import dataclass
import hashlib
from importlib.resources import files
import json
from pathlib import PurePosixPath
from typing import Iterable, Mapping

from tools.architecture.ownership.registry import Owner, OwnershipRegistry


class ContractError(ValueError):
    """A declared abstraction contract is internally inconsistent."""


@dataclass(frozen=True)
class AbstractionContract:
    owner: str
    root: str
    responsibility: str
    invariants: tuple[str, ...]
    public_files: tuple[str, ...] = ()
    grouping: bool = False
    parts: tuple[str, ...] = ()

    def __post_init__(self):
        if isinstance(self.parts, str):
            raise ContractError(
                "Component parts must be a collection of inventory identities"
            )
        object.__setattr__(self, "parts", tuple(self.parts))

    @property
    def owner_ids(self) -> tuple[str, ...]:
        """Stable inventory parts forming this one reviewed component."""
        return (self.owner, *self.parts)


@dataclass(frozen=True)
class ContractIssue:
    owner: str
    detail: str


@dataclass(frozen=True)
class ContractAudit:
    issues: tuple[ContractIssue, ...]
    legacy_owners: tuple[str, ...]

    @property
    def accepted(self) -> bool:
        return not self.issues


def file_set_identity(owner: Owner) -> str:
    """Freeze inventory membership, not a count or a wildcard directory exception."""
    return hashlib.sha256("\0".join(sorted(owner.files)).encode("utf-8")).hexdigest()


def needs_contract(owner: Owner) -> bool:
    return any(
        PurePosixPath(path).suffix
        in {".cpp", ".hpp", ".py", ".sh", ".cmake", ".yml", ".yaml"}
        or PurePosixPath(path).name == "CMakeLists.txt"
        for path in owner.files
    )


class ContractCatalog:
    """A declared complete owner or an unchanged, explicitly unreviewed legacy owner."""

    def __init__(
        self,
        contracts: Iterable[AbstractionContract],
        legacy: Mapping[str, str],
    ) -> None:
        self._contracts: dict[str, AbstractionContract] = {}
        self._by_owner: dict[str, AbstractionContract] = {}
        for contract in contracts:
            root = PurePosixPath(contract.root)
            if (
                not root.parts
                or root.is_absolute()
                or ".." in root.parts
                or str(root) != contract.root
                or "\\" in contract.root
                or ":" in contract.root
            ):
                raise ContractError(f"Invalid abstraction root: {contract.root!r}")
            identities = contract.owner_ids
            if any(
                not isinstance(identity, str) or not identity.strip()
                for identity in identities
            ):
                raise ContractError(
                    "Component parts need explicit inventory identities"
                )
            if len(set(identities)) != len(identities) or any(
                identity in self._by_owner for identity in identities
            ):
                raise ContractError(f"Duplicate abstraction contract: {contract.owner}")
            if contract.grouping and contract.parts:
                raise ContractError(
                    "A grouping describes children, not implementation parts"
                )
            if not contract.responsibility.strip() or not contract.invariants:
                raise ContractError(
                    f"Contract needs responsibility and invariants: {contract.owner}"
                )
            if any(not invariant.strip() for invariant in contract.invariants):
                raise ContractError(
                    f"Contract contains an empty invariant: {contract.owner}"
                )
            self._contracts[contract.owner] = contract
            self._by_owner.update((identity, contract) for identity in identities)
        self._legacy = dict(legacy)
        if set(self._legacy) & set(self._by_owner):
            raise ContractError(
                "A reviewed contract must be removed from the legacy allowance"
            )
        if any(
            not isinstance(value, str)
            or len(value) != 64
            or any(c not in "0123456789abcdef" for c in value)
            for value in self._legacy.values()
        ):
            raise ContractError("Legacy allowances must identify exact file sets")

    @property
    def contracts(self) -> tuple[AbstractionContract, ...]:
        return tuple(self._contracts.values())

    def audit(self, registry: OwnershipRegistry) -> ContractAudit:
        owners = {owner.identity: owner for owner in registry.owners}
        issues: list[ContractIssue] = []
        legacy = []
        for identity in sorted(
            (self._by_owner.keys() | self._legacy.keys()) - owners.keys()
        ):
            issues.append(
                ContractIssue(identity, "Declaration no longer owns tracked files")
            )
        for identity, owner in sorted(owners.items()):
            if identity in self._by_owner or not needs_contract(owner):
                continue
            if self._legacy.get(identity) != file_set_identity(owner):
                issues.append(
                    ContractIssue(identity, "Requires a reviewed abstraction contract")
                )
            else:
                legacy.append(identity)
        for contract in self._contracts.values():
            identity = contract.owner
            root = PurePosixPath(contract.root)
            component_files = set()
            for part in contract.owner_ids:
                if part not in owners:
                    continue
                for path in owners[part].files:
                    component_files.add(path)
                    if not PurePosixPath(path).is_relative_to(root):
                        issues.append(
                            ContractIssue(part, f"Implementation outside owner: {path}")
                        )
            readme = str(root / "README.md")
            if identity not in owners or readme not in owners[identity].files:
                issues.append(
                    ContractIssue(
                        identity, "Owner README is not declared with the abstraction"
                    )
                )
            for path in contract.public_files:
                if path not in component_files:
                    issues.append(
                        ContractIssue(identity, f"Public file is not owned: {path}")
                    )
            if contract.grouping:
                for path in component_files:
                    file = PurePosixPath(path)
                    if (
                        file.suffix in {".cpp", ".hpp", ".py"}
                        and file.name != "__init__.py"
                    ):
                        issues.append(
                            ContractIssue(
                                identity,
                                "A grouping owner cannot hide an implementation",
                            )
                        )
            else:
                for other in owners.values():
                    if other.identity in contract.owner_ids:
                        continue
                    if any(
                        PurePosixPath(path).is_relative_to(root) for path in other.files
                    ):
                        issues.append(
                            ContractIssue(
                                identity,
                                f"Abstraction subtree contains unrelated owner {other.identity}",
                            )
                        )
        return ContractAudit(tuple(issues), tuple(legacy))


def load_current_catalog() -> ContractCatalog:
    """Load this checkout's contracts without exposing their storage layout."""
    from .definitions import CONTRACTS

    legacy = files(__package__).joinpath("data", "legacy.json")
    return ContractCatalog(CONTRACTS, json.loads(legacy.read_text(encoding="utf-8")))
