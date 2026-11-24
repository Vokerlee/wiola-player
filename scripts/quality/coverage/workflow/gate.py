#!/usr/bin/env python3
"""Enforce exact-count workflow coverage floors and changed mapped lines/branches."""

import argparse
import json
from pathlib import Path
import re
import subprocess

from scripts.quality.architecture.inventory.git_inventory import GitInventory
from . import collect as coverage_report
from scripts.quality.coverage.source_policy.policy import load_current_policy

METRICS = coverage_report.METRICS


def counts(value):
    result = {}
    for metric in METRICS:
        raw = value[metric]
        total, hit = raw["count"], raw["covered"]
        if type(total) is not int or type(hit) is not int or not 0 <= hit <= total:
            raise ValueError("Invalid coverage counts for " + metric)
        result[metric] = {"count": total, "covered": hit, "uncovered": total - hit}
    return result


def compare(current, baseline):
    failures = []
    now = {entry["file"]: counts(entry["counts"]) for entry in current["files"]}
    old = {entry["file"]: counts(entry["counts"]) for entry in baseline["files"]}
    if len(now) != len(current["files"]) or len(old) != len(baseline["files"]):
        raise ValueError("Duplicate coverage source inventory")
    for path, previous in old.items():
        if path not in now:
            failures.append(
                path + ": removed from measured inventory; explicit migration required"
            )
            continue
        for metric in METRICS:
            expected, actual = previous[metric], now[path][metric]
            if actual["count"] < expected["count"]:
                failures.append(
                    f'{path}: {metric} denominator reduced ({actual["count"]} < {expected["count"]})'
                )
            if (
                expected["count"]
                and actual["covered"] * expected["count"]
                < expected["covered"] * actual["count"]
            ):
                failures.append(
                    f'{path}: {metric} coverage regressed; uncovered={actual["uncovered"]}'
                )
    for path in now.keys() - old.keys():
        if now[path]["lines"]["count"] and not now[path]["lines"]["covered"]:
            failures.append(
                path + ": new production file has no covered executable lines"
            )
    for metric in METRICS:
        expected, actual = (
            counts(baseline["totals"])[metric],
            counts(current["totals"])[metric],
        )
        if actual["count"] < expected["count"]:
            failures.append(metric + ": workflow denominator reduced")
        if (
            expected["count"]
            and actual["covered"] * expected["count"]
            < expected["covered"] * actual["count"]
        ):
            failures.append(metric + ": workflow coverage floor regressed")
    return failures


def changed_lines(diff):
    files = {}
    path = None
    number = 0
    for line in diff.splitlines():
        if line.startswith("+++ b/"):
            path = line[6:]
            files.setdefault(path, set())
        elif line.startswith("@@"):
            match = re.search(r"\+(\d+)(?:,\d+)?", line)
            if match:
                number = int(match.group(1))
        elif line.startswith("+") and path:
            files[path].add(number)
            number += 1
        elif line.startswith(" "):
            number += 1
    return files


def lcov_records(text, root):
    records = {}
    file = None
    for line in text.splitlines():
        if line.startswith("SF:"):
            path = Path(line[3:]).resolve()
            file = (
                path.relative_to(root).as_posix() if path.is_relative_to(root) else None
            )
            if file:
                records.setdefault(file, {"lines": {}, "branches": {}})
        elif file and line.startswith("DA:"):
            number, hits, *_ = line[3:].split(",")
            records[file]["lines"][int(number)] = int(hits)
        elif file and line.startswith("BRDA:"):
            number, block, branch, hits = line[5:].split(",")
            records[file]["branches"].setdefault(int(number), []).append(
                (block, branch, 0 if hits == "-" else int(hits))
            )
    return records


def check_changed(changed, records):
    failures, unmapped = [], []
    for path, numbers in changed.items():
        values = records.get(path, {"lines": {}, "branches": {}})
        for number in sorted(numbers):
            if number not in values["lines"]:
                unmapped.append(f"{path}:{number}")
                continue
            if not values["lines"][number]:
                failures.append(f"{path}:{number}: added executable line has zero hits")
            for block, branch, hits in values["branches"].get(number, []):
                if not hits:
                    failures.append(
                        f"{path}:{number}: added decision {block}/{branch} has zero hits"
                    )
    return failures, unmapped


def check_manifest(manifest, baseline, report):
    failures = []
    if manifest.get("exclusions") != baseline["exclusions"]:
        failures.append("Coverage exclusions changed; explicit qualification required")
    completed = {
        name for group in manifest["completed"].values() for name in group["tests"]
    }
    missing = set(baseline["test_names"]) - completed
    if missing:
        failures.append(
            "Missing previously qualified tests: " + ", ".join(sorted(missing))
        )
    for name, group in manifest["completed"].items():
        files = coverage_report.require_profiles(
            report / "raw" / name, len(group["tests"])
        )
        if len(files) != group["profiles"]:
            failures.append(name + ": profile inventory changed after measurement")
    if (
        sum(group["profiles"] for group in manifest["completed"].values())
        != manifest["raw_profiles"]
    ):
        failures.append("Raw profile total disagrees with completed process inventory")
    return failures


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--report", type=Path, default=Path("build-coverage/coverage"))
    parser.add_argument(
        "--baseline",
        type=Path,
        default=Path(__file__).with_name("workflow-baseline.json"),
    )
    parser.add_argument("--base-ref")
    args = parser.parse_args()
    root = Path(
        coverage_report.capture(["git", "rev-parse", "--show-toplevel"])
    ).resolve()
    state = coverage_report.source_state(root)
    manifest = json.loads((args.report / "manifest.json").read_text())
    if any(manifest.get(key) != state[key] for key in ["revision", "source_sha256"]):
        raise ValueError(
            "Coverage report does not identify the current clean tested revision"
        )
    baseline = json.loads(args.baseline.read_text())
    current = json.loads((args.report / "workflow.json").read_text())
    policy = load_current_policy()
    current = policy.migrate_report(current)
    baseline = policy.migrate_report(baseline)
    failures = compare(current, baseline)
    if args.base_ref:
        baseline_path = args.baseline.resolve().relative_to(root).as_posix()
        try:
            trusted = GitInventory(root).read_previous(baseline_path, args.base_ref)
        except subprocess.CalledProcessError:
            trusted = None
        if trusted is not None:
            previous = policy.migrate_report(json.loads(trusted))
            failures.extend(
                "Trusted base: " + message for message in compare(current, previous)
            )
            failures.extend(
                "Policy weakened: " + message for message in compare(baseline, previous)
            )
            if previous.get("exclusions") != baseline.get("exclusions"):
                failures.append("Policy changed trusted coverage exclusions")
            if previous.get("compiler_target") != baseline.get("compiler_target"):
                failures.append("Policy changed the qualified compiler target")
            if not set(previous["test_names"]).issubset(baseline["test_names"]):
                failures.append("Policy removed previously qualified test identities")
        else:
            base_tree = coverage_report.capture(
                ["git", "rev-parse", args.base_ref + "^{tree}"]
            )
            if baseline.get("measured_tree") != base_tree:
                failures.append(
                    "Initial policy must identify the exact trusted base tree"
                )
    target = re.search(r"^Target: (.+)$", manifest["tools"]["compiler"], re.M).group(1)
    if target != baseline["compiler_target"]:
        failures.append(
            "Incompatible compiler target; measure this platform separately"
        )
    failures.extend(check_manifest(manifest, baseline, args.report))
    coverage_report.verify_reused_profiles(root, args.report, manifest)
    unmapped = []
    if args.base_ref:
        diff = coverage_report.capture(
            ["git", "diff", "--unified=0", args.base_ref, "HEAD", "--", "src"]
        )
        extra, unmapped = check_changed(
            changed_lines(diff),
            lcov_records((args.report / "workflow.lcov").read_text(), root),
        )
        failures.extend(extra)
    gaps = [
        {"file": entry["file"], "counts": counts(entry["counts"])}
        for entry in current["files"]
        if any(
            entry["counts"][metric]["count"] != entry["counts"][metric]["covered"]
            for metric in METRICS
        )
    ]
    result = {
        "revision": manifest["revision"],
        "failures": failures,
        "uncovered_files": gaps,
        "unmapped_changed_lines_for_review": unmapped,
        "applicability": baseline["applicability"],
    }
    (args.report / "gate.json").write_text(json.dumps(result, indent=2) + "\n")
    for failure in failures:
        print(failure)
    print(
        f"Workflow gate: {len(failures)} failures; {len(gaps)} files retain measured gaps."
    )
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
