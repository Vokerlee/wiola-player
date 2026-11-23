"""Run packaged desktop contracts through the shared GoogleTest protocol."""

import argparse
import os
from pathlib import Path
import sys

from .environment import DesktopEnvironment
from tools.testing.gtest.runner import GoogleTestRunner, TestExecutionError


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--artifacts", type=Path, required=True)
    parser.add_argument("--revision", required=True)
    parser.add_argument("--filter", default="DesktopJourney.*")
    parser.add_argument("--minimum-tests", type=int, default=1)
    parser.add_argument("--media-dir", type=Path)
    parser.add_argument("--timeout", type=float, default=120)
    parser.add_argument(
        "--qt-platform", help="Explicit Qt adapter choice; otherwise inherit the caller"
    )
    parser.add_argument("--require-case", action="append", default=[])
    args = parser.parse_args(argv)
    try:
        environment = DesktopEnvironment(
            args.artifacts, args.media_dir, args.qt_platform
        ).apply(os.environ)
        result = GoogleTestRunner().run(
            args.binary,
            args.filter,
            args.artifacts,
            environment=environment,
            revision=args.revision,
            timeout=args.timeout,
            minimum_tests=args.minimum_tests,
            required_cases=frozenset(args.require_case),
        )
        print(
            (result.artifacts / "execution.log").read_text(
                encoding="utf-8", errors="replace"
            )
        )
        if not result.passed:
            raise TestExecutionError(
                "Desktop contracts did not all complete successfully"
            )
    except (OSError, ValueError, TestExecutionError) as error:
        print(
            f"Desktop check failed: {error}; evidence: {args.artifacts.resolve()}",
            file=sys.stderr,
        )
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
