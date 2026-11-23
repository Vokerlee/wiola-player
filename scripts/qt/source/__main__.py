"""Materialize reviewed Qt sources, or print their SDK reuse identity."""

import argparse
from pathlib import Path
import subprocess
import sys
import tarfile

from .provider import QtSourcePin, QtSourceStore


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("version")
    parser.add_argument("workspace", type=Path, nargs="?")
    parser.add_argument("--identity", action="store_true")
    args = parser.parse_args(argv)
    if not args.identity and args.workspace is None:
        parser.error("workspace is required to materialize sources")
    try:
        pin = QtSourcePin.resolve(args.version)
        print(
            pin.identity
            if args.identity
            else QtSourceStore().materialize(pin, args.workspace)
        )
    except (OSError, ValueError, tarfile.TarError, subprocess.SubprocessError) as error:
        print("Qt source preparation failed: " + str(error), file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
