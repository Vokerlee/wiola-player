"""Run architecture acceptance and compare policy with a trusted Git revision."""

import argparse
import json
import os
from pathlib import Path
import subprocess
import sys

from tools.architecture.inventory.git_inventory import GitInventory
from tools.architecture.policy_guard.policy import PolicyGuard, PolicySnapshot
from tools.architecture.review_contract.review import ChangeReview, validate_subject
from scripts.quality.coverage.source_policy.policy import (
    SourcePolicy,
    SourceRecord,
    SourceMove,
)

POLICY_FILES = {
    "legacy": "tools/architecture/contracts/data/legacy.json",
    "exceptions": "tools/architecture/dependency_policy/data/legacy.json",
    "dependencies": "tools/architecture/dependency_policy/data/modules.json",
    "requirements": "tools/testing/catalog/data/requirements.json",
    "sources": "scripts/quality/coverage/source_policy/data/sources.json",
    "moves": "scripts/quality/coverage/source_policy/data/moves.json",
}


def _snapshot(git: GitInventory, reference: str | None) -> PolicySnapshot:
    data = {
        name: json.loads(
            git.read_previous(path, reference) if reference else git.read(path)
        )
        for name, path in POLICY_FILES.items()
    }
    rules = data["dependencies"]
    sources = SourcePolicy(
        [SourceRecord(**record) for record in data["sources"]["files"]],
        data["sources"]["workflow_targets"],
        [SourceMove(**move) for move in data["moves"]],
    )
    return PolicySnapshot(
        data["legacy"],
        frozenset(data["exceptions"]),
        {
            module: frozenset(dependencies)
            for module, dependencies in rules["allowed"].items()
        },
        rules["owners"],
        frozenset(rules["qt_modules"]),
        frozenset(case["identity"] for case in data["requirements"]["required"]),
        sources,
    )


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repository", type=Path, default=Path.cwd())
    parser.add_argument("--base-ref")
    parser.add_argument("--review", type=Path, help="Local concrete PR review body")
    parser.add_argument("--subject", help="Local proposed PR subject")
    parser.add_argument("--build-dir", type=Path)
    parser.add_argument(
        "--test-discovery",
        action="store_true",
        help="Inspect an already-built runnable native test suite",
    )
    parser.add_argument(
        "--event-file", type=Path, default=os.environ.get("GITHUB_EVENT_PATH")
    )
    args = parser.parse_args()
    if args.test_discovery and not args.build_dir:
        parser.error("--test-discovery requires --build-dir")
    root = args.repository.resolve()
    git = GitInventory(root)
    try:
        event = json.loads(Path(args.event_file).read_text()) if args.event_file else {}
        if args.review:
            subject, body = args.subject or "", args.review.read_text()
            published = False
        elif "pull_request" in event:
            pr = event["pull_request"]
            subject, body = pr["title"], pr.get("body") or ""
            published = False
        else:
            message = git.commit_message()
            subject, _, body = message.partition("\n")
            published = True
        base = (
            args.base_ref
            or (event.get("pull_request", {}).get("base", {}).get("sha"))
            or event.get("before")
        )
        if not base:
            raise ValueError("A trusted base revision is required")
        validate_subject(subject, published=published)
        review = ChangeReview.read(body)
        previous, current = _snapshot(git, base), _snapshot(git, None)
        deleted = review.reviewed_deletions(git.deleted_paths(base))
        problems = PolicyGuard(previous).audit(current, deleted)
        for problem in problems:
            print(problem)
        if problems:
            return 1
        commands = ["check_ownership", "check_dependencies"]
        for command in commands:
            arguments = [
                sys.executable,
                "-m",
                "tools.architecture.commands." + command,
                "--repository",
                str(root),
            ]
            if args.build_dir:
                arguments.extend(["--build-dir", str(args.build_dir.resolve())])
            subprocess.run(arguments, cwd=root, check=True)
        if args.test_discovery:
            subprocess.run(
                [
                    sys.executable,
                    "-m",
                    "tools.architecture.commands.check_tests",
                    "--build-dir",
                    str(args.build_dir.resolve()),
                ],
                cwd=root,
                check=True,
            )
        from tools.architecture.ownership.registry import load_current_registry

        if current.sources.reconcile(load_current_registry()):
            raise ValueError(
                "Current source applicability does not match tracked ownership"
            )
    except (
        OSError,
        ValueError,
        KeyError,
        TypeError,
        subprocess.SubprocessError,
    ) as error:
        print(f"Design acceptance failed: {error}", file=sys.stderr)
        return 2
    print("Design acceptance passed; semantic review remains required")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
