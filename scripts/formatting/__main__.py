"""Run maintained source formatting or install its one owned Git hook."""

import argparse
from pathlib import Path
import subprocess
import sys

from . import format_sources, install_hook
from ._rules import RULES


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repository", type=Path, default=Path.cwd())
    commands = parser.add_subparsers(dest="command", required=True)
    formatter = commands.add_parser("format")
    formatter.add_argument("language", choices=(*RULES, "all"))
    formatter.add_argument("--check", action="store_true")
    formatter.add_argument("--files", nargs="+", type=Path, default=())
    commands.add_parser("hook")
    commands.add_parser("install-hooks")
    args = parser.parse_args(argv)

    try:
        if args.command == "install-hooks":
            install_hook(args.repository)
        else:
            hook = args.command == "hook"
            languages = (
                tuple(RULES) if hook or args.language == "all" else (args.language,)
            )
            format_sources(
                args.repository,
                languages,
                check=hook or args.check,
                paths=() if hook else args.files,
            )
    except (OSError, ValueError, subprocess.SubprocessError) as error:
        print("Formatting failed: " + str(error), file=sys.stderr)
        return 1

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
