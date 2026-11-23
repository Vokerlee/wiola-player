"""Check complete abstraction ownership while tracking exact legacy debt."""

from dataclasses import dataclass
import hashlib
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
        PurePosixPath(path).suffix in {".cpp", ".hpp", ".py", ".sh", ".cmake", ".yml", ".yaml"}
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
        for contract in contracts:
            root = PurePosixPath(contract.root)
            if (
                not root.parts or root.is_absolute() or ".." in root.parts
                or str(root) != contract.root or "\\" in contract.root
                or ":" in contract.root
            ):
                raise ContractError(f"Invalid abstraction root: {contract.root!r}")
            if contract.owner in self._contracts:
                raise ContractError(f"Duplicate abstraction contract: {contract.owner}")
            if not contract.responsibility.strip() or not contract.invariants:
                raise ContractError(f"Contract needs responsibility and invariants: {contract.owner}")
            if any(not invariant.strip() for invariant in contract.invariants):
                raise ContractError(f"Contract contains an empty invariant: {contract.owner}")
            self._contracts[contract.owner] = contract
        self._legacy = dict(legacy)
        if set(self._legacy) & set(self._contracts):
            raise ContractError("A reviewed contract must be removed from the legacy allowance")
        if any(not isinstance(value, str) or len(value) != 64
               or any(c not in "0123456789abcdef" for c in value)
               for value in self._legacy.values()):
            raise ContractError("Legacy allowances must identify exact file sets")

    def audit(self, registry: OwnershipRegistry) -> ContractAudit:
        owners = {owner.identity: owner for owner in registry.owners}
        issues: list[ContractIssue] = []
        legacy = []
        for identity in sorted((self._contracts.keys() | self._legacy.keys()) - owners.keys()):
            issues.append(ContractIssue(identity, "Declaration no longer owns tracked files"))
        for identity, owner in sorted(owners.items()):
            contract = self._contracts.get(identity)
            if contract is None:
                if not needs_contract(owner):
                    continue
                if self._legacy.get(identity) != file_set_identity(owner):
                    issues.append(ContractIssue(identity, "Requires a reviewed abstraction contract"))
                else:
                    legacy.append(identity)
                continue
            root = PurePosixPath(contract.root)
            for path in owner.files:
                if not PurePosixPath(path).is_relative_to(root):
                    issues.append(ContractIssue(identity, f"Implementation outside owner: {path}"))
            readme = str(root / "README.md")
            if readme not in owner.files:
                issues.append(ContractIssue(identity, "Owner README is not declared with the abstraction"))
            for path in contract.public_files:
                if path not in owner.files:
                    issues.append(ContractIssue(identity, f"Public file is not owned: {path}"))
            if contract.grouping:
                for path in owner.files:
                    file = PurePosixPath(path)
                    if file.suffix in {".cpp", ".hpp", ".py"} and file.name != "__init__.py":
                        issues.append(ContractIssue(
                            identity, "A grouping owner cannot hide an implementation"
                        ))
            else:
                for other in owners.values():
                    if other.identity == identity:
                        continue
                    if any(PurePosixPath(path).is_relative_to(root) for path in other.files):
                        issues.append(ContractIssue(
                            identity, f"Abstraction subtree contains unrelated owner {other.identity}"
                        ))
        return ContractAudit(tuple(issues), tuple(legacy))
