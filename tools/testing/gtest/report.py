"""Interpret executed GoogleTest cases rather than trusting a process exit code alone."""

from dataclasses import dataclass
from pathlib import Path
import re
import xml.etree.ElementTree as ET


def discovered_cases(output: str) -> frozenset[str]:
    suite = None
    cases = []
    for line in output.splitlines():
        value = line.split("#", 1)[0].strip()
        if not line.startswith(" ") and re.fullmatch(r"[A-Za-z0-9_/]+\.", value):
            suite = value
        elif line.startswith("  ") and re.fullmatch(r"[A-Za-z0-9_/]+", value):
            if suite is None:
                raise ValueError("GoogleTest case has no suite")
            cases.append(suite + value)
    if not cases or len(cases) != len(set(cases)):
        raise ValueError("GoogleTest discovery is empty or contains duplicate cases")
    return frozenset(cases)


@dataclass(frozen=True)
class GoogleTestReport:
    cases: frozenset[str]
    completed: frozenset[str]
    failed: frozenset[str]
    errors: frozenset[str]

    @classmethod
    def read(cls, path: Path) -> "GoogleTestReport":
        root = ET.parse(path).getroot()
        if root.tag != "testsuites":
            raise ValueError("Expected a GoogleTest testsuites report")
        cases, completed, failed, errors = set(), set(), set(), set()
        for case in root.findall("./testsuite/testcase"):
            suite, name = case.get("classname"), case.get("name")
            if not suite or not name:
                raise ValueError("Test case has no complete identity")
            identity = suite + "." + name
            if identity in cases:
                raise ValueError("Duplicate case in GoogleTest report: " + identity)
            cases.add(identity)
            if (
                case.get("status") == "run"
                and case.get("result") == "completed"
                and not case.findall("skipped")
            ):
                completed.add(identity)
            if case.findall("failure"):
                failed.add(identity)
            if case.findall("error"):
                errors.add(identity)
        if (
            not cases
            or int(root.get("tests", "0")) != len(cases)
            or int(root.get("failures", "0")) != len(failed)
            or int(root.get("errors", "0")) != len(errors)
        ):
            raise ValueError("GoogleTest report totals disagree with its case evidence")
        return cls(
            frozenset(cases), frozenset(completed), frozenset(failed), frozenset(errors)
        )

    def covers(self, selected: frozenset[str]) -> bool:
        return self.cases == self.completed == selected and not self.errors
