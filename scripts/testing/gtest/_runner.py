"""Execute one explicit GoogleTest selection and retain reproducible evidence."""

from dataclasses import dataclass
import hashlib
import json
from pathlib import Path
import platform
import subprocess
from typing import Mapping
import xml.etree.ElementTree as ET

from ._report import GoogleTestReport, discovered_cases


class TestExecutionError(RuntimeError):
    """Execution cannot provide trustworthy test evidence."""


@dataclass(frozen=True)
class GoogleTestResult:
    returncode: int
    selected: frozenset[str]
    report: GoogleTestReport
    artifacts: Path

    @property
    def passed(self) -> bool:
        return (
            self.returncode == 0
            and self.report.covers(self.selected)
            and not self.report.failed
        )

    @property
    def assertion_failure(self) -> bool:
        # GoogleTest exits 1 for assertion failures. Signals and Windows exception statuses
        # must never count as successfully detecting a seeded regression.
        return (
            self.returncode == 1
            and self.report.covers(self.selected)
            and bool(self.report.failed)
        )


class GoogleTestRunner:
    """No Qt choice, repository path, target layout or project case name lives here."""

    def run(
        self,
        binary: Path,
        selection: str,
        artifacts: Path,
        *,
        environment: Mapping[str, str],
        revision: str,
        timeout: float,
        minimum_tests: int = 1,
        required_cases: frozenset[str] = frozenset(),
    ) -> GoogleTestResult:
        if timeout <= 0 or minimum_tests < 1 or not selection or not revision:
            raise ValueError(
                "Execution needs a selection, revision and positive bounds"
            )
        binary, artifacts = binary.resolve(), artifacts.resolve()
        if not binary.is_file():
            raise TestExecutionError("Missing test executable: " + str(binary))
        artifacts.mkdir(parents=True, exist_ok=True)
        xml = artifacts / "results.xml"
        xml.unlink(missing_ok=True)
        original = self._digest(binary)
        manifest = {
            "revision": revision,
            "binary": str(binary),
            "sha256": original,
            "platform": platform.platform(),
            "filter": selection,
            "timeout": timeout,
        }
        manifest_path = artifacts / "manifest.json"
        manifest_path.write_text(
            json.dumps(manifest, indent=2) + "\n", encoding="utf-8"
        )
        selected = self._discover(binary, selection, artifacts, environment, timeout)
        if len(selected) < minimum_tests or not required_cases.issubset(selected):
            raise TestExecutionError("GoogleTest selection omitted required contracts")
        manifest["selected"] = sorted(selected)
        manifest_path.write_text(
            json.dumps(manifest, indent=2) + "\n", encoding="utf-8"
        )
        with (artifacts / "execution.log").open("w", encoding="utf-8") as log:
            process = self._execute(
                [
                    str(binary),
                    "--gtest_filter=" + selection,
                    "--gtest_output=xml:" + str(xml),
                ],
                environment,
                log,
                timeout,
            )
        if self._digest(binary) != original:
            raise TestExecutionError("Test executable changed during execution")
        try:
            report = GoogleTestReport.read(xml)
        except (OSError, ValueError, ET.ParseError) as error:
            raise TestExecutionError(
                "Missing or invalid GoogleTest report: " + str(xml)
            ) from error
        result = GoogleTestResult(process.returncode, selected, report, artifacts)
        manifest.update(
            returncode=result.returncode,
            passed=result.passed,
            assertion_failure=result.assertion_failure,
        )
        manifest_path.write_text(
            json.dumps(manifest, indent=2) + "\n", encoding="utf-8"
        )
        return result

    def _discover(self, binary, selection, artifacts, environment, timeout):
        path = artifacts / "discovery.log"
        with path.open("w", encoding="utf-8") as log:
            process = self._execute(
                [str(binary), "--gtest_filter=" + selection, "--gtest_list_tests"],
                environment,
                log,
                timeout,
            )
        if process.returncode:
            raise TestExecutionError("GoogleTest discovery process failed")
        try:
            return discovered_cases(path.read_text(encoding="utf-8", errors="replace"))
        except ValueError as error:
            raise TestExecutionError(str(error)) from error

    @staticmethod
    def _execute(command, environment, log, timeout):
        try:
            return subprocess.run(
                command,
                env=dict(environment),
                stdout=log,
                stderr=subprocess.STDOUT,
                timeout=timeout,
                check=False,
            )
        except (subprocess.TimeoutExpired, OSError) as error:
            raise TestExecutionError("Test process failed: " + str(error)) from error

    @staticmethod
    def _digest(path: Path) -> str:
        with path.open("rb") as source:
            return hashlib.file_digest(source, "sha256").hexdigest()
