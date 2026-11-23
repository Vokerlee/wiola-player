"""Validate an explicit, current-state inventory of file ownership."""

from __future__ import annotations

from dataclasses import dataclass
import json
from pathlib import PurePosixPath
import re
from typing import Iterable


class RegistryError(ValueError):
    """An ownership declaration is ambiguous, unsafe, or unsupported."""


def _relative_path(value: object) -> str:
    if not isinstance(value, str) or not value:
        raise RegistryError("An owned path must be a nonempty string")
    path = PurePosixPath(value)
    if (
        not path.parts
        or path.is_absolute()
        or ".." in path.parts
        or str(path) != value
        or "\\" in value
        or ":" in value
        or any(character in value for character in "\x00\r\n")
        or path.parts[0] == ".git"
    ):
        raise RegistryError(f"Owned paths must be canonical repository paths: {value!r}")
    return value


def _unique_object(pairs: list[tuple[str, object]]) -> dict[str, object]:
    result: dict[str, object] = {}
    for key, value in pairs:
        if key in result:
            raise RegistryError(f"Duplicate JSON key: {key}")
        result[key] = value
    return result


@dataclass(frozen=True)
class Owner:
    """One named responsibility and its explicitly declared current files."""

    identity: str
    purpose: str
    files: frozenset[str]


@dataclass(frozen=True)
class InventoryDifference:
    unowned: tuple[str, ...]
    stale: tuple[str, ...]

    @property
    def matches(self) -> bool:
        return not self.unowned and not self.stale


class OwnershipRegistry:
    """An immutable one-owner-per-file inventory, independent of Git and CMake."""

    def __init__(self, documents: Iterable[str]) -> None:
        owners: dict[str, Owner] = {}
        file_owners: dict[str, str] = {}
        for text in documents:
            try:
                document = json.loads(text, object_pairs_hook=_unique_object)
            except json.JSONDecodeError as error:
                raise RegistryError(f"Invalid ownership JSON: {error.msg}") from error
            if not isinstance(document, dict) or set(document) != {
                "schema_version", "owners"
            }:
                raise RegistryError("Expected schema_version and owners")
            if type(document["schema_version"]) is not int or document["schema_version"] != 1:
                raise RegistryError("Unsupported ownership schema version")
            if not isinstance(document["owners"], list):
                raise RegistryError("Owners must be a list")
            for raw in document["owners"]:
                owner = self._owner(raw)
                if owner.identity in owners:
                    raise RegistryError(f"Duplicate owner identity: {owner.identity}")
                owners[owner.identity] = owner
                for path in owner.files:
                    if path in file_owners:
                        raise RegistryError(f"Multiple owners for {path}")
                    file_owners[path] = owner.identity
        if not owners:
            raise RegistryError("An ownership registry cannot be empty")
        self._owners = owners
        self._file_owners = file_owners

    @staticmethod
    def _owner(raw: object) -> Owner:
        if not isinstance(raw, dict) or set(raw) != {"id", "purpose", "files"}:
            raise RegistryError("An owner requires id, purpose, and files")
        identity, purpose, paths = raw["id"], raw["purpose"], raw["files"]
        if not isinstance(identity, str) or not re.fullmatch(
            r"[a-z][a-z0-9_]*(?:\.[a-z0-9_]+)*", identity
        ):
            raise RegistryError("Owner identities must be stable dotted names")
        if not isinstance(purpose, str) or not purpose.strip() or purpose != purpose.strip():
            raise RegistryError(f"Owner {identity} requires a nonempty purpose")
        if not isinstance(paths, list) or not paths:
            raise RegistryError(f"Owner {identity} requires owned files")
        normalized = [_relative_path(path) for path in paths]
        if len(set(normalized)) != len(normalized):
            raise RegistryError(f"Owner {identity} lists a file more than once")
        return Owner(identity, purpose, frozenset(normalized))

    @property
    def owners(self) -> tuple[Owner, ...]:
        return tuple(self._owners[key] for key in sorted(self._owners))

    def owner_of(self, path: str) -> Owner:
        return self._owners[self._file_owners[_relative_path(path)]]

    def compare(self, tracked_paths: Iterable[str]) -> InventoryDifference:
        tracked = {_relative_path(path) for path in tracked_paths}
        declared = set(self._file_owners)
        return InventoryDifference(
            tuple(sorted(tracked - declared)), tuple(sorted(declared - tracked))
        )
