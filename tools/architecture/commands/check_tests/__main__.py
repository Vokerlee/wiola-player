"""Check test ownership, applicability and independently required semantic cases."""

import argparse
import json
from pathlib import Path
import subprocess
import sys

from scripts.build.cmake import CMakeInventory
from tools.architecture.ownership.registry import load_current_registry
from tools.testing.catalog.catalog import load_current_catalog


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, required=True)
    parser.add_argument(
        "--headless",
        action="store_true",
        help="Require only toolkit-free test contracts",
    )
    args = parser.parse_args()
    try:
        registry = load_current_registry()
        inventory = CMakeInventory.read(
            args.build_dir, first_party_sources=registry.paths
        )
        discovery = json.loads(
            subprocess.check_output(
                ["ctest", "--test-dir", str(args.build_dir), "--show-only=json-v1"],
                text=True,
                timeout=60,
            )
        )
        catalog = load_current_catalog()
        problems = catalog.audit_sources(registry) + catalog.audit(
            inventory, discovery, registry, with_qt=not args.headless
        )
    except (
        OSError,
        ValueError,
        KeyError,
        TypeError,
        subprocess.SubprocessError,
    ) as error:
        print(f"Test contract inventory failed: {error}", file=sys.stderr)
        return 2
    for problem in problems:
        print(problem)
    print(
        f"Reconciled {len(discovery['tests'])} cases against {len(catalog.contracts)} declared test targets"
    )
    return 1 if problems else 0


if __name__ == "__main__":
    raise SystemExit(main())
