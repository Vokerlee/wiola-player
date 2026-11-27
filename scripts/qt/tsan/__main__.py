"""Prepare the instrumented native Qt Core SDK or print its reuse identity."""

import argparse
import os
from pathlib import Path
import subprocess
import sys

from .build import QtCoreTsanBuilder
from .context import QtCoreContext


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("workspace", type=Path, nargs="?")
    parser.add_argument("--identity", action="store_true")
    parser.add_argument("--cxx")
    parser.add_argument("--cc")
    parser.add_argument(
        "--jobs", type=int, default=os.environ.get("WIOLA_QT_BUILD_JOBS") or None
    )
    parser.add_argument("--timeout", type=float, default=900)
    args = parser.parse_args(argv)

    if not args.identity and args.workspace is None:
        parser.error("workspace is required when building the runtime")

    try:
        if args.jobs is not None and args.jobs < 1:
            raise ValueError("Build parallelism must be positive")

        context = QtCoreContext.discover(args.cxx, args.cc)

        if args.identity:
            print(context.identity)
        else:
            runtime = QtCoreTsanBuilder(
                args.workspace, context, jobs=args.jobs, timeout=args.timeout
            ).prepare()
            print("Qualified instrumented Qt Core: " + str(runtime))
    except (OSError, ValueError, subprocess.SubprocessError) as error:
        print(
            f"Qt Core SDK failed: {error}; workspace: {args.workspace}", file=sys.stderr
        )
        return 1

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
