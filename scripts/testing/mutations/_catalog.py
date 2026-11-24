"""Regression-specific edits are data bound to stable ownership identities."""

from dataclasses import dataclass
from importlib.resources import files
import json
from pathlib import Path
import re
from types import MappingProxyType

from tools.architecture.ownership.registry import OwnershipRegistry


@dataclass(frozen=True)
class MutationCase:
    identity: str
    source_owner: str
    anchor: str
    replacement: str
    target: str
    selection: str

    def __post_init__(self):
        if any(
            not isinstance(value, str)
            for value in (
                self.identity,
                self.source_owner,
                self.anchor,
                self.replacement,
                self.target,
                self.selection,
            )
        ):
            raise ValueError("Mutation case fields must be strings")
        if (
            not re.fullmatch(r"[a-z][a-z0-9-]*", self.identity)
            or not self.source_owner
            or not self.target
            or not self.selection
            or not self.anchor
            or self.anchor == self.replacement
        ):
            raise ValueError(
                "Mutation case needs an identity, source owner, changing edit and target selection"
            )

    def source(self, registry: OwnershipRegistry, root: Path) -> Path:
        owners = [
            owner for owner in registry.owners if owner.identity == self.source_owner
        ]
        if len(owners) != 1:
            raise ValueError("Unknown mutation source owner: " + self.source_owner)
        implementations = [
            root / name for name in owners[0].files if Path(name).suffix == ".cpp"
        ]
        if len(implementations) != 1:
            raise ValueError(
                "Mutation owner needs one unambiguous implementation: "
                + self.source_owner
            )
        path = implementations[0].resolve()
        if not path.is_relative_to(root.resolve()):
            raise ValueError("Mutation source escapes its repository")
        return path


class MutationCatalog:
    def __init__(self, cases):
        entries = tuple(cases)
        self.cases = MappingProxyType({case.identity: case for case in entries})
        if not entries or len(entries) != len(self.cases):
            raise ValueError(
                "Mutation catalog must have unique, nonempty case identities"
            )

    def select(self, names=None) -> tuple[MutationCase, ...]:
        names = tuple(self.cases) if names is None else tuple(names)
        if not names or len(names) != len(set(names)) or set(names) - self.cases.keys():
            raise ValueError(
                "Mutation selection contains missing or duplicate case identities"
            )
        return tuple(self.cases[name] for name in names)

    @classmethod
    def read(cls, path: Path | None = None) -> "MutationCatalog":
        text = (
            path.read_text(encoding="utf-8")
            if path
            else files(__package__).joinpath("cases.json").read_text(encoding="utf-8")
        )
        data = json.loads(text)
        if (
            not isinstance(data, dict)
            or set(data) != {"schema_version", "cases"}
            or type(data["schema_version"]) is not int
            or data["schema_version"] != 1
            or not isinstance(data["cases"], list)
        ):
            raise ValueError("Unsupported mutation catalog schema")
        return cls(MutationCase(**case) for case in data["cases"])
