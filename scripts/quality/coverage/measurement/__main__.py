"""Reconcile raw C++ and Python coverage with explicit source ownership."""

import argparse
import json
from pathlib import Path
import sys

from scripts.build.cmake import CMakeInventory
from scripts.quality.architecture.contracts.catalog import (
    AbstractionContract,
    load_current_catalog,
)
from scripts.quality.architecture.ownership.registry import load_current_registry
from scripts.quality.coverage.measurement.report import CoverageMeasurement
from scripts.quality.coverage.source_policy.policy import load_current_policy


def _component(path: Path, source_root: Path) -> AbstractionContract:
    relative = (source_root / path).resolve().relative_to(source_root).as_posix()
    contract = next(
        (
            contract
            for contract in load_current_catalog().contracts
            if contract.root == relative
        ),
        None,
    )
    if contract is None or contract.grouping:
        raise ValueError("Select the exact root of one reviewed complete component")
    return contract


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cpp-report", type=Path)
    parser.add_argument("--python-report", type=Path)
    parser.add_argument(
        "--component",
        type=Path,
        help="Exact source root of one reviewed complete component",
    )
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
        component = (
            _component(args.component, inventory.source_root)
            if args.component
            else None
        )
        owners = None
        languages = {"cpp", "python"}
        if component is not None:
            owners = set(component.owner_ids) & {
                record.owner for record in policy.records.values()
            }
            languages = {
                record.language
                for record in policy.records.values()
                if record.owner in owners and record.role in {"production", "tooling"}
            } & {"cpp", "python"}
            if not languages:
                raise ValueError(
                    "Selected component has no instrumented C++ or Python sources"
                )
        reports = {"cpp": args.cpp_report, "python": args.python_report}
        if any(reports[language] is None for language in languages):
            raise ValueError(
                "Supply reports for the selected source languages: "
                + ", ".join(sorted(languages))
            )
        measurement = CoverageMeasurement(policy, inventory.source_root, owners=owners)
        result = dict(
            schema_version=2,
            cpp=(
                measurement.cpp(json.loads(args.cpp_report.read_text()), inventory)
                if args.cpp_report
                else None
            ),
            python=(
                measurement.python(json.loads(args.python_report.read_text()))
                if args.python_report
                else None
            ),
            unmeasured=measurement.unmeasured(),
        )
        if component is not None:
            result["component"] = dict(root=component.root, owners=component.owner_ids)
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(result, indent=2) + "\n")
    except (OSError, ValueError, KeyError, TypeError) as error:
        print(f"Coverage ownership failed: {error}", file=sys.stderr)
        return 2
    summary = ", ".join(
        f"{len(result[language]['owners'])} {label} owners"
        for language, label in (("cpp", "C++"), ("python", "Python"))
        if result[language] is not None
    )
    print(
        f"Coverage reconciled: {summary}; {len(result['unmeasured'])} unmeasured inputs remain"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
