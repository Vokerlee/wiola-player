"""Explicit measurement applicability and stable source identity across moves."""

from dataclasses import dataclass
from copy import deepcopy
from importlib.resources import files
import json
from pathlib import Path, PurePosixPath
from types import MappingProxyType
from typing import Iterable

from tools.architecture.ownership.registry import OwnershipRegistry

LANGUAGES = {
    ".cpp": "cpp",
    ".hpp": "cpp",
    ".py": "python",
    ".sh": "shell",
    ".cmake": "cmake",
    ".yml": "yaml",
    ".yaml": "yaml",
}
ROLES = {
    "cpp": frozenset({"production", "test", "consumer"}),
    "python": frozenset({"tooling", "test"}),
    "shell": frozenset({"tooling", "test"}),
    "cmake": frozenset({"build"}),
    "yaml": frozenset({"ci"}),
}


def source_language(path: str) -> str | None:
    return (
        "cmake"
        if PurePosixPath(path).name == "CMakeLists.txt"
        else LANGUAGES.get(PurePosixPath(path).suffix)
    )


def _canonical(path: str) -> bool:
    value = PurePosixPath(path)
    return (
        bool(value.parts)
        and not value.is_absolute()
        and ".." not in value.parts
        and str(value) == path
        and not any(c in path for c in "\\:\x00\n\r")
    )


@dataclass(frozen=True)
class SourceRecord:
    path: str
    owner: str
    language: str
    role: str
    workflow: bool = False

    def __post_init__(self):
        if (
            not _canonical(self.path)
            or not self.owner
            or self.language != source_language(self.path)
        ):
            raise ValueError(
                "Source record needs a canonical path, owner and matching language"
            )
        if self.role not in ROLES[self.language] or type(self.workflow) is not bool:
            raise ValueError(
                "Source purpose and workflow applicability must be explicit"
            )
        if self.workflow and (self.role != "production" or self.language != "cpp"):
            raise ValueError("The qualified workflow is a production C++ scope")


@dataclass(frozen=True)
class SourceMove:
    previous: str
    current: str
    owner: str


class SourcePolicy:
    def __init__(
        self,
        records: Iterable[SourceRecord],
        workflow_targets: Iterable[str],
        moves: Iterable[SourceMove] = (),
    ) -> None:
        records, moves = tuple(records), tuple(moves)
        self.records = MappingProxyType({record.path: record for record in records})
        if len(self.records) != len(records):
            raise ValueError("Duplicate source applicability")
        self.workflow_targets = frozenset(workflow_targets)
        self._moves = {move.previous: move for move in moves}
        if len(self._moves) != len(moves) or len(
            {move.current for move in moves}
        ) != len(moves):
            raise ValueError("Source lineage must be one to one")
        for move in moves:
            if (
                not _canonical(move.previous)
                or not _canonical(move.current)
                or move.previous in self.records
            ):
                raise ValueError("Moved source must retire its previous owned path")
            target = self.records.get(self.current_path(move.previous))
            if target is None or target.owner != move.owner:
                raise ValueError("Source moves preserve an existing stable owner")

    def current_path(self, path: str) -> str:
        visited = set()
        while path in self._moves:
            if path in visited:
                raise ValueError("Cyclic source lineage")
            visited.add(path)
            move = self._moves[path]
            path = move.current
        return path

    def reconcile(self, registry: OwnershipRegistry) -> tuple[str, ...]:
        observed = {
            path: owner.identity
            for owner in registry.owners
            for path in owner.files
            if source_language(path)
        }
        problems = [
            f"Source applicability is missing or has a different owner: {path}"
            for path, owner in observed.items()
            if path not in self.records or self.records[path].owner != owner
        ]
        problems.extend(
            f"Stale source applicability: {path}"
            for path in self.records.keys() - observed.keys()
        )
        return tuple(sorted(problems))

    def workflow_file(self, filename: str | Path, root: Path) -> bool:
        path = Path(filename).resolve()
        if not path.is_relative_to(root.resolve()):
            return False
        record = self.records.get(path.relative_to(root.resolve()).as_posix())
        return bool(record and record.workflow)

    def migrate_report(self, report: dict) -> dict:
        """Map historical filenames without changing a single metric or denominator."""
        result = deepcopy(report)
        result["files"] = [
            dict(entry, file=self.current_path(entry["file"]))
            for entry in result["files"]
        ]
        paths = [entry["file"] for entry in result["files"]]
        if len(paths) != len(set(paths)):
            raise ValueError("Source lineage merges distinct measured files")
        return result


def load_current_policy() -> SourcePolicy:
    directory = files(__package__).joinpath("data")
    declarations = json.loads(
        directory.joinpath("sources.json").read_text(encoding="utf-8")
    )
    moves = json.loads(directory.joinpath("moves.json").read_text(encoding="utf-8"))
    return SourcePolicy(
        [SourceRecord(**record) for record in declarations["files"]],
        declarations["workflow_targets"],
        [SourceMove(**move) for move in moves],
    )
