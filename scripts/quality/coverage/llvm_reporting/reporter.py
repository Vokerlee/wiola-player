"""Keep tool failures visible without relaxing coverage qualification."""

from dataclasses import dataclass
from pathlib import Path
import re
import subprocess


@dataclass(frozen=True)
class MappingDiagnostics:
    zero_hash_mappings: tuple[str, ...]

    @classmethod
    def read(cls, stderr: str) -> "MappingDiagnostics":
        records = []
        expected = 0
        for line in _lines(stderr):
            count = re.fullmatch(r"warning: (\d+) functions have mismatched data", line)
            if count:
                expected += int(count.group(1))
                continue
            entry = re.fullmatch(
                r"hash-mismatch: No profile record found for '(.+)' with hash = (0x[0-9a-fA-F]+)",
                line,
            )
            if not entry or int(entry.group(2), 16) != 0:
                raise ValueError("Invalid coverage profile diagnostic: " + line)
            records.append(entry.group(1))
        if expected != len(records):
            raise ValueError(
                "Coverage diagnostic count did not match its detailed records"
            )
        return cls(tuple(records))

    def verify_summary(self, stderr: str) -> None:
        expected = 0
        for line in _lines(stderr):
            warning = re.fullmatch(
                r"warning: (\d+) functions have mismatched data", line
            )
            if not warning:
                raise ValueError("Unexpected coverage diagnostic: " + line)
            expected += int(warning.group(1))
        if expected != len(self.zero_hash_mappings):
            raise ValueError("Coverage commands disagree about mapping diagnostics")


def _lines(stderr):
    clean = re.sub(r"\x1b\[[0-9;]*m", "", stderr)
    return (line for line in clean.splitlines() if line.strip())


class LLVMReportError(RuntimeError):
    """An LLVM command failed; its raw diagnostic is retained."""


class LLVMReporter:
    def __init__(self, command: tuple[str, ...], artifacts: Path):
        self.command = command
        self.artifacts = artifacts
        self._diagnostics: dict[str, MappingDiagnostics] = {}

    @property
    def diagnostics(self) -> dict:
        return {
            phase: {"zero_hash_mappings": list(value.zero_hash_mappings)}
            for phase, value in self._diagnostics.items()
        }

    def execute(self, arguments, destination=None, *, phase="whole"):
        if not arguments:
            raise ValueError("Expected a nonempty LLVM report command")
        operation = arguments[0]
        if operation not in {"report", "export", "show"} or not re.fullmatch(
            r"[a-z][a-z0-9-]*", phase
        ):
            raise ValueError("Expected an LLVM report operation and named phase")
        if operation != "report" and phase not in self._diagnostics:
            raise ValueError(
                "Collect detailed report diagnostics before rendering: " + phase
            )
        if operation == "report":
            self._diagnostics.pop(phase, None)
        self.artifacts.mkdir(parents=True, exist_ok=True)
        diagnostic_path = self.artifacts / (
            phase + "-" + operation + "-mapping-diagnostics.txt"
        )
        result = subprocess.run(
            [
                *self.command,
                *map(str, arguments),
                # LLVM debug reports write to a shared stream from worker threads.
                # Serialize that operation; exports/HTML retain normal parallelism.
                *(["-num-threads=1", "-dump"] if operation == "report" else []),
            ],
            stdout=destination,
            stderr=subprocess.PIPE,
            text=True,
            check=False,
        )
        # Persist before checking status or parsing. The previous caller lost stderr exactly
        # when LLVM aborted, leaving a long command traceback instead of the tool's diagnosis.
        diagnostic_path.write_text(result.stderr, encoding="utf-8")
        if result.returncode:
            raise LLVMReportError(
                f"LLVM coverage {operation} exited {result.returncode}; diagnostics: {diagnostic_path}\n{result.stderr}"
            )
        if operation == "report":
            self._diagnostics[phase] = MappingDiagnostics.read(result.stderr)
        else:
            self._diagnostics[phase].verify_summary(result.stderr)
        return result


def coverage_diagnostics(stderr: str) -> list[str]:
    """Compatibility for the collection entrypoint's existing public parser function."""
    return list(MappingDiagnostics.read(stderr).zero_hash_mappings)
