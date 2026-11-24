"""Require assertion evidence for declared regressions and exact source restoration."""

import argparse
import json
import os
from pathlib import Path
import signal
import subprocess
import sys

from scripts.build.cmake import CMakeBuild, CMakeInventory
from tools.architecture.inventory.git_inventory import GitInventory
from tools.architecture.ownership.registry import load_current_registry
from scripts.testing.gtest import GoogleTestRunner

from ._catalog import MutationCatalog
from ._verifier import MutationVerifier


def _interrupted(signum, frame):
    raise KeyboardInterrupt("Mutation qualification interrupted")


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, required=True)
    parser.add_argument(
        "--artifacts", type=Path, default=os.environ.get("WIOLA_MUTATION_ARTIFACTS")
    )
    parser.add_argument("--seed", nargs="+")
    parser.add_argument("--catalog", type=Path)
    parser.add_argument("--jobs", type=int)
    parser.add_argument("--build-timeout", type=float, default=900)
    parser.add_argument("--test-timeout", type=float, default=60)
    args = parser.parse_args(argv)
    args.artifacts = args.artifacts or args.build_dir / "Testing" / "Mutations"
    previous_signal = signal.getsignal(signal.SIGTERM)

    try:
        if args.test_timeout <= 0:
            raise ValueError("Test timeout must be positive")
        registry = load_current_registry()
        inventory = CMakeInventory.read(
            args.build_dir, first_party_sources=registry.paths
        )
        root = inventory.source_root
        status = subprocess.run(["git", "diff", "--quiet", "HEAD", "--"], cwd=root)
        if status.returncode == 1:
            raise ValueError("Mutation qualification requires a clean tracked worktree")
        status.check_returncode()
        revision = GitInventory(root).revision("HEAD")
        cases = MutationCatalog.read(args.catalog).select(args.seed)
        build = CMakeBuild(inventory, jobs=args.jobs, timeout=args.build_timeout)
        verifier = MutationVerifier(
            root,
            registry,
            build,
            GoogleTestRunner(),
            args.artifacts,
            revision,
            os.environ,
            timeout=args.test_timeout,
        )
        signal.signal(signal.SIGTERM, _interrupted)
        reports = []
        for case in cases:
            reports.append(verifier.verify(case))
            (args.artifacts / "mutations.json").write_text(
                json.dumps(reports, indent=2) + "\n", encoding="utf-8"
            )
            print(
                case.identity + ": assertion detected; original restored and passed",
                flush=True,
            )
    except KeyboardInterrupt:
        print(
            "Mutation qualification interrupted; inspect retained restoration evidence",
            file=sys.stderr,
        )
        return 130
    except (
        OSError,
        ValueError,
        TypeError,
        RuntimeError,
        subprocess.SubprocessError,
    ) as error:
        print(
            f"Mutation qualification failed: {error}; evidence: {args.artifacts.resolve()}",
            file=sys.stderr,
        )
        return 1
    finally:
        signal.signal(signal.SIGTERM, previous_signal)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
