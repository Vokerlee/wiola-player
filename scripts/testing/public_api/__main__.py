"""Qualify a component-owned consumer against a relocated installed package."""

import argparse
from pathlib import Path
import subprocess
import tempfile

from scripts.build.cmake import CMakeInventory
from . import PackageProbe, PublicConsumerCheck, QualificationError


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--build-dir", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--compiler", required=True)
    parser.add_argument("--system", required=True)
    parser.add_argument("--cmake", default="cmake")
    parser.add_argument("--toolchain", type=Path)
    parser.add_argument("--run", action="store_true")
    args = parser.parse_args(argv)
    args.output.mkdir(parents=True, exist_ok=True)
    artifacts = Path(tempfile.mkdtemp(prefix="qualification-", dir=args.output))
    try:
        PublicConsumerCheck(
            CMakeInventory.read(args.build_dir),
            compiler=args.compiler,
            system=args.system,
            cmake=(args.cmake,),
            toolchain=args.toolchain,
        ).check(PackageProbe.read(args.manifest), artifacts, run=args.run)
    except (
        OSError,
        ValueError,
        subprocess.SubprocessError,
        QualificationError,
    ) as error:
        print(f"Public consumer qualification failed: {error}\nEvidence: {artifacts}")
        return 1
    print(f"Public consumer qualified. Evidence: {artifacts}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
