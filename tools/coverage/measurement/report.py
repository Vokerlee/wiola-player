"""Normalize raw coverage by source owner without hiding unmeasured code."""

from collections import defaultdict
from pathlib import Path

from tools.architecture.cmake_inventory.inventory import CMakeInventory
from tools.coverage.source_policy.policy import SourcePolicy

CPP_METRICS = ("lines", "functions", "regions", "branches")


def _counts(total: int, covered: int) -> dict:
    if type(total) is not int or type(covered) is not int or not 0 <= covered <= total:
        raise ValueError(
            "Coverage counts must be nonnegative integers with covered <= total"
        )
    return dict(count=total, covered=covered, uncovered=total - covered)


def _sum(records: list[dict], metrics: tuple[str, ...]) -> dict:
    return {
        metric: _counts(
            sum(record[metric]["count"] for record in records),
            sum(record[metric]["covered"] for record in records),
        )
        for metric in metrics
    }


class CoverageMeasurement:
    def __init__(self, policy: SourcePolicy, root: Path) -> None:
        self._policy = policy
        self._root = root.resolve()

    def _path(self, path: str) -> str | None:
        value = (self._root / path).resolve()
        if not value.is_relative_to(self._root):
            return None
        return value.relative_to(self._root).as_posix()

    def _owners(
        self,
        language: str,
        files: dict[str, dict],
        metrics: tuple[str, ...],
        configured: set[str] | None = None,
    ) -> dict:
        owners = defaultdict(list)
        for record in self._policy.records.values():
            if record.language == language and record.role in {"production", "tooling"}:
                owners[record.owner].append(record.path)
        result = {}
        for owner, paths in sorted(owners.items()):
            mapped = sorted(set(paths) & files.keys())
            unmapped = sorted(set(paths) - files.keys())
            result[owner] = dict(
                totals=_sum([files[path] for path in mapped], metrics),
                mapped_files=mapped,
                unmapped_files=unmapped,
                configured_sources=sorted(set(paths) & (configured or set())),
                complete_mapping=not unmapped,
            )
        return result

    def cpp(self, raw: dict, inventory: CMakeInventory) -> dict:
        if inventory.source_root.resolve() != self._root:
            raise ValueError("Coverage and configured sources have different roots")
        if len(raw["data"]) != 1:
            raise ValueError("Select exactly one LLVM export dataset")
        measured, ignored = {}, []
        for entry in raw["data"][0]["files"]:
            path = self._path(entry["filename"])
            if path is None:
                continue
            record = self._policy.records.get(path)
            if record is None:
                raise ValueError(
                    "Mapped first-party file has no applicability: " + path
                )
            if record.language != "cpp" or record.role != "production":
                ignored.append(path)
                continue
            if path in measured:
                raise ValueError("Duplicate LLVM source mapping: " + path)
            measured[path] = {
                metric: _counts(
                    entry["summary"][metric]["count"],
                    entry["summary"][metric]["covered"],
                )
                for metric in CPP_METRICS
            }
        configured = {
            path
            for target in inventory.targets
            for path in target.sources
            if path in self._policy.records
            and self._policy.records[path].role == "production"
            and self._policy.records[path].language == "cpp"
        }
        absent = {
            path for path in configured if path.endswith(".cpp")
        } - measured.keys()
        if absent:
            raise ValueError(
                "Configured production sources have no coverage mapping: "
                + ", ".join(sorted(absent))
            )
        return dict(
            totals=_sum(list(measured.values()), CPP_METRICS),
            files=measured,
            owners=self._owners("cpp", measured, CPP_METRICS, configured),
            ignored_nonproduction_mappings=sorted(ignored),
        )

    def python(self, raw: dict) -> dict:
        measured = {}
        for filename, entry in raw["files"].items():
            path = self._path(filename)
            if path is None:
                continue
            record = self._policy.records.get(path)
            if record is None:
                raise ValueError("Measured Python source has no applicability: " + path)
            if record.language != "python" or record.role != "tooling":
                continue
            if path in measured:
                raise ValueError("Duplicate Python source mapping: " + path)
            summary = entry["summary"]
            measured[path] = {
                "lines": _counts(summary["num_statements"], summary["covered_lines"]),
                "branches": _counts(
                    summary["num_branches"], summary["covered_branches"]
                ),
            }
        expected = {
            record.path
            for record in self._policy.records.values()
            if record.language == "python" and record.role == "tooling"
        }
        absent = expected - measured.keys()
        if absent:
            raise ValueError(
                "Python tools have no statement mapping: " + ", ".join(sorted(absent))
            )
        return dict(
            totals=_sum(list(measured.values()), ("lines", "branches")),
            files=measured,
            owners=self._owners("python", measured, ("lines", "branches")),
        )

    def unmeasured(self) -> list[dict]:
        return [
            dict(
                path=record.path,
                owner=record.owner,
                language=record.language,
                reason="No statement/branch instrumentation is qualified for this language",
            )
            for record in self._policy.records.values()
            if record.language not in {"cpp", "python"} and record.role != "test"
        ]
