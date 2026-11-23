"""Check current source dependencies against explicit module and migration rules."""

import argparse
from importlib.resources import files
import json
from pathlib import Path
import sys

from tools.architecture.contracts.catalog import load_current_catalog
from scripts.build.cmake import CMakeInventory
from tools.architecture.dependency_graph.graph import (
    SourceDependencyGraph,
    ConfiguredDependencyGraph,
)
from tools.architecture.dependency_policy.policy import (
    DependencyPolicy,
    compare_exceptions,
)
from tools.architecture.ownership.registry import load_current_registry


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repository", type=Path, default=Path.cwd())
    parser.add_argument(
        "--build-dir", type=Path, help="Check an existing configured CMake graph too"
    )
    args = parser.parse_args()
    try:
        registry = load_current_registry()
        policy_data = files("tools.architecture.dependency_policy").joinpath("data")
        rules = json.loads(
            policy_data.joinpath("modules.json").read_text(encoding="utf-8")
        )
        legacy = json.loads(
            policy_data.joinpath("legacy.json").read_text(encoding="utf-8")
        )
        policy = DependencyPolicy(
            rules["owners"],
            rules["allowed"],
            rules["qt_modules"],
            load_current_catalog().contracts,
        )
        edges = SourceDependencyGraph(args.repository.resolve(), registry).edges()
        missing = {owner.identity for owner in registry.owners} - rules["owners"].keys()
        if missing:
            raise ValueError(f"Owners without module rules: {sorted(missing)}")
        configured_targets = set()
        if args.build_dir:
            inventory = CMakeInventory.read(
                args.build_dir, first_party_sources=registry.paths
            )
            if inventory.source_root != args.repository.resolve():
                raise ValueError(
                    "The configured graph belongs to a different source root"
                )
            configured_targets = {target.name for target in inventory.targets}
            edges += ConfiguredDependencyGraph(inventory, registry).edges()
        violations = policy.inspect(edges)
        selected = {
            key: value
            for key, value in legacy.items()
            if not key.startswith("configured-")
            or key.split("|", 2)[1] in configured_targets
        }
        failures = compare_exceptions(violations, selected)
    except (OSError, ValueError, SyntaxError) as error:
        print(f"Dependency check failed: {error}", file=sys.stderr)
        return 2
    for failure in failures:
        print(f"{failure.identity}: {failure.detail}")
    print(
        f"Observed {len(edges)} dependency edges; {len(selected)} applicable legacy violations"
    )
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
