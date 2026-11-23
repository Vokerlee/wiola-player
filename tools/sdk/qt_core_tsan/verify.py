"""Verify a configured executable loads the qualified generated Core runtime."""

import argparse
import os
from pathlib import Path
import subprocess
import sys

from .context import QtCoreContext
from .runtime import QtCoreRuntime


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument(
        "--runtime", type=Path, default=os.environ.get("WIOLA_QT_CORE_RUNTIME") or None
    )
    args = parser.parse_args(argv)
    if args.runtime is None:
        parser.error("pass --runtime or set WIOLA_QT_CORE_RUNTIME")
    try:
        context = QtCoreContext.discover()
        runtime = QtCoreRuntime(args.runtime)
        environment = dict(os.environ)
        inherited = environment.get("LD_LIBRARY_PATH")
        environment["LD_LIBRARY_PATH"] = str(runtime.directory) + (
            os.pathsep + inherited if inherited else ""
        )
        runtime.verify_loaded(
            args.binary, args.output, context.identity, environment=environment
        )
    except (OSError, ValueError, subprocess.SubprocessError) as error:
        print("Instrumented Qt verification failed: " + str(error), file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
