"""Compare current declarations with Git's tracked-file inventory."""

import argparse
from importlib.resources import files
from pathlib import Path
import subprocess
import sys

from tools.architecture.inventory.git_inventory import GitInventory
from tools.architecture.cmake_inventory.inventory import CMakeInventory, BuildInventoryError
from tools.architecture.ownership.registry import OwnershipRegistry, RegistryError


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repository", type=Path, default=Path.cwd())
    parser.add_argument("--build-dir", type=Path, help="Read an existing CMake codemodel")
    args = parser.parse_args()
    try:
        directory = files("tools.architecture.ownership").joinpath("data")
        documents = [
            path.read_text(encoding="utf-8")
            for path in sorted(directory.iterdir(), key=lambda path: path.name)
            if path.name.endswith(".json")
        ]
        registry = OwnershipRegistry(documents)
        paths = GitInventory(args.repository).paths()
        difference = registry.compare(paths)
        build_inventory = CMakeInventory.read(args.build_dir) if args.build_dir else None
        if build_inventory:
            if build_inventory.source_root != args.repository.resolve():
                raise BuildInventoryError("The build belongs to a different source root")
            for target in build_inventory.targets:
                for source in target.sources:
                    registry.owner_of(source)
    except (RegistryError, BuildInventoryError, KeyError, OSError, subprocess.SubprocessError, UnicodeError) as error:
        print(f"Ownership inventory failed: {error}", file=sys.stderr)
        return 2
    for path in difference.unowned:
        print(f"Unowned tracked file: {path}")
    for path in difference.stale:
        print(f"Declared file is not tracked: {path}")
    if not difference.matches:
        return 1
    if build_inventory:
        print(f"CMake ownership verified: {len(build_inventory.targets)} declared targets")
    print(f"Ownership verified: {len(paths)} files, {len(registry.owners)} owners")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
