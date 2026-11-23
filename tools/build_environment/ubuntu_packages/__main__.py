"""Prepare a resolved archive-cache identity or install its requested packages."""

import argparse
from pathlib import Path
import subprocess
import sys

from .packages import PackagePlan, UbuntuPackages


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    plan = commands.add_parser("plan")
    plan.add_argument("--output", required=True, type=Path)
    plan.add_argument("packages", nargs="+")
    install = commands.add_parser("install")
    install.add_argument("--plan", required=True, type=Path)
    install.add_argument("--archives", required=True, type=Path)
    args = parser.parse_args()
    manager = UbuntuPackages()
    try:
        if args.command == "plan":
            result = manager.resolve(tuple(args.packages))
            args.output.parent.mkdir(parents=True, exist_ok=True)
            args.output.write_text(result.serialize() + "\n")
            print(result.identity)
        else:
            manager.install(PackagePlan.read(args.plan), args.archives)
    except (
        OSError,
        ValueError,
        KeyError,
        TypeError,
        subprocess.CalledProcessError,
    ) as error:
        print(f"Ubuntu package setup failed: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
