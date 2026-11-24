"""GoogleTest protocol independent of GUI toolkits and build layouts."""

from ._report import GoogleTestReport, discovered_cases
from ._runner import GoogleTestResult, GoogleTestRunner, TestExecutionError

__all__ = [
    "GoogleTestReport",
    "GoogleTestResult",
    "GoogleTestRunner",
    "TestExecutionError",
    "discovered_cases",
]
