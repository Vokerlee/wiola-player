"""Reconcile raw C++ and Python coverage with explicit source ownership."""

import argparse
import json
from pathlib import Path
import sys

from scripts.build.cmake import CMakeInventory
from tools.architecture.ownership.registry import load_current_registry
from tools.coverage.measurement.report import CoverageMeasurement
from tools.coverage.source_policy.policy import load_current_policy


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cpp-report", type=Path, required=True)
    parser.add_argument("--python-report", type=Path, required=True)
    parser.add_argument("--build-dir", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    try:
        registry = load_current_registry()
        inventory = CMakeInventory.read(
            args.build_dir, first_party_sources=registry.paths
        )
        policy = load_current_policy()
        problems = policy.reconcile(registry)
        if problems:
            raise ValueError("\n".join(problems))
        measurement = CoverageMeasurement(policy, inventory.source_root)
        result = dict(
            schema_version=1,
            cpp=measurement.cpp(json.loads(args.cpp_report.read_text()), inventory),
            python=measurement.python(json.loads(args.python_report.read_text())),
            unmeasured=measurement.unmeasured(),
        )
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(result, indent=2) + "\n")
    except (OSError, ValueError, KeyError, TypeError) as error:
        print(f"Coverage ownership failed: {error}", file=sys.stderr)
        return 2
    print(
        f"Coverage reconciled: {len(result['cpp']['owners'])} C++ owners, "
        f"{len(result['python']['owners'])} Python owners; {len(result['unmeasured'])} unmeasured inputs remain"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
