#!/usr/bin/env python3
"""Reproducible LLVM coverage for explicitly inventoried first-party targets."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess

# File-based invocations need the checkout's package root before project imports.
if __package__ in {None, ""}:
    import sys

    sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from tools.architecture.cmake_inventory.inventory import CMakeInventory
from tools.architecture.ownership.registry import load_current_registry
from tools.coverage.source_policy.policy import load_current_policy
from tools.testing.catalog.catalog import load_current_catalog
from tools.build_environment.cmake.build import CMakeBuild
from tools.coverage.llvm_reporting.reporter import LLVMReporter, coverage_diagnostics

METRICS = ("lines", "functions", "regions", "branches")


def run(command, **kwargs):
    return subprocess.run([str(item) for item in command], check=True, **kwargs)


def capture(command):
    return subprocess.check_output([str(item) for item in command], text=True).strip()


def source_state(root):
    changed = capture(
        [
            "git",
            "diff",
            "HEAD",
            "--name-only",
            "--",
            "src",
            "tests",
            "cmake",
            "scripts",
            "tools",
            ".github",
            "requirements.txt",
            "CMakeLists.txt",
            "CMakePresets.json",
        ]
    )
    if changed:
        raise ValueError("Coverage requires a committed tested revision:\n" + changed)
    files = capture(
        [
            "git",
            "ls-files",
            "src",
            "tests",
            "cmake",
            "scripts",
            "tools",
            ".github",
            "requirements.txt",
            "CMakeLists.txt",
            "CMakePresets.json",
        ]
    ).splitlines()
    digest = hashlib.sha256()
    for name in sorted(files):
        digest.update(name.encode() + b"\0" + (root / name).read_bytes())
    return {
        "revision": capture(["git", "rev-parse", "HEAD"]),
        "source_sha256": digest.hexdigest(),
    }


def inventory(root, build):
    registry = load_current_registry()
    configured = CMakeInventory.read(build, first_party_sources=registry.paths)
    if configured.source_root != root.resolve():
        raise ValueError("The configured build belongs to another source root")
    policy = load_current_policy()
    issues = policy.reconcile(registry)
    if issues:
        raise ValueError("\n".join(issues))
    catalog = load_current_catalog()
    production, tests, sources = {}, {}, set()
    for target in configured.targets:
        if target.name in catalog.contracts:
            if target.kind != "EXECUTABLE" or not target.artifacts:
                raise ValueError(
                    "Declared test is not a configured executable: " + target.name
                )
            tests[target.name] = {
                "binary": str(target.artifacts[0]),
                "sources": list(target.sources),
                "workflow": target.name in policy.workflow_targets,
            }
            continue
        owned = [
            path
            for path in target.sources
            if path in policy.records
            and policy.records[path].language == "cpp"
            and policy.records[path].role == "production"
        ]
        if owned and target.kind in {"STATIC_LIBRARY", "EXECUTABLE"}:
            production[target.name] = {
                "artifacts": [str(path) for path in target.artifacts],
                "sources": owned,
            }
            sources.update(owned)
    if not production or not tests or not sources:
        raise ValueError("Incomplete first-party production/test inventory")
    return {"production": production, "tests": tests, "sources": sorted(sources)}


def require_profiles(raw, expected):
    profiles = sorted(raw.glob("*.profraw"))
    if len(profiles) < expected or any(p.stat().st_size == 0 for p in profiles):
        raise ValueError(
            f"{raw.name}: expected at least {expected} complete process profiles, got {len(profiles)}"
        )
    return profiles


def workflow_file(filename, root, policy=None):
    return (policy or load_current_policy()).workflow_file(filename, root)


def summarize(files):
    result = {}
    for metric in METRICS:
        count = sum(f["summary"][metric]["count"] for f in files)
        covered = sum(f["summary"][metric]["covered"] for f in files)
        result[metric] = {
            "count": count,
            "covered": covered,
            "uncovered": count - covered,
        }
    return result


def refresh_inputs(root, previous, entries, targets):
    if not targets or not set(targets).issubset(entries["tests"]):
        raise ValueError("Refresh requires known test target identities")
    changed = capture(
        ["git", "diff", "--name-only", previous["revision"], "HEAD"]
    ).splitlines()
    allowed = {
        "scripts/coverage_report.py",
        "scripts/coverage_gate.py",
        "tests/scripts/test_coverage_inventory.py",
    }
    allowed.update(
        source for target in targets for source in entries["tests"][target]["sources"]
    )
    if set(changed) - allowed:
        raise ValueError(
            "Changed shared/production inputs require fresh collection: "
            + ", ".join(sorted(set(changed) - allowed))
        )
    return changed


def verify_reused_profiles(root, report, manifest):
    evidence = manifest.get("reused_evidence")
    if not evidence:
        return
    original = json.loads((report / "reused-manifest.json").read_text())
    entries = json.loads((report / "inventory.json").read_text())
    changed = refresh_inputs(root, original, entries, evidence["refreshed_targets"])
    if changed != evidence["changed_inputs"] or original["tools"] != manifest["tools"]:
        raise ValueError("Reused profile input/toolchain proof changed")
    expected = set(original["completed"]) - set(evidence["refreshed_targets"])
    if (
        set(evidence["retained_profiles"]) != expected
        or original["scope"] != manifest["scope"]
    ):
        raise ValueError("Retained target scope changed")
    for target, hashes in evidence["retained_profiles"].items():
        if set(hashes) != {p.name for p in (report / "raw" / target).glob("*.profraw")}:
            raise ValueError("Retained profile hash inventory changed: " + target)
        if manifest["completed"][target] != original["completed"][target]:
            raise ValueError(
                "Retained target discovery/profile inventory changed: " + target
            )
        for name, digest in hashes.items():
            if (
                hashlib.sha256(
                    (report / "raw" / target / name).read_bytes()
                ).hexdigest()
                != digest
            ):
                raise ValueError(
                    "Retained process profile changed: " + target + "/" + name
                )


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--scope", choices=["all", "workflow"], default="all")
    parser.add_argument("--jobs", type=int, default=4)
    parser.add_argument(
        "--refresh-target",
        nargs="+",
        help="Recollect only changed test TUs; reject shared/production changes",
    )
    parser.add_argument("--build-dir", type=Path, default=Path("build-coverage"))
    args = parser.parse_args()
    root = Path(capture(["git", "rev-parse", "--show-toplevel"])).resolve()
    os.chdir(root)
    before = source_state(root)
    build = args.build_dir.resolve()
    query = build / ".cmake/api/v1/query"
    query.mkdir(parents=True, exist_ok=True)
    (query / "codemodel-v2").touch()
    run(["cmake", "--preset", "linux-coverage", "-B", build])
    entries = inventory(root, build)
    selected = {
        name: value
        for name, value in entries["tests"].items()
        if args.scope == "all" or value["workflow"]
    }
    report = build / "coverage"
    previous = None
    reused = None
    if args.refresh_target:
        previous = json.loads((report / "manifest.json").read_text())
        if previous.get("reused_evidence"):
            raise ValueError(
                "Chained refreshes require fresh collection to keep provenance bounded"
            )
        old_inventory = json.loads((report / "inventory.json").read_text())
        if entries != old_inventory or previous["scope"] != args.scope:
            raise ValueError("Changed build inventory/scope requires fresh collection")
        changed = refresh_inputs(root, previous, entries, args.refresh_target)
        if not set(args.refresh_target).issubset(selected):
            raise ValueError("Refresh target is outside the selected scope")
        reused = {
            "refreshed_targets": args.refresh_target,
            "changed_inputs": changed,
            "retained_profiles": {},
        }
    targets = args.refresh_target or [
        "wiola_coverage_inventory",
        *entries["production"],
        *selected,
    ]
    run(["cmake", "--build", build, "-j", args.jobs, "--target", *targets])
    # Verify the actual configured compiler, not merely a shell default named clang.
    cache = (build / "CMakeCache.txt").read_text()
    compiler = re.search(r"^CMAKE_CXX_COMPILER:[^=]+=(.+)$", cache, re.M).group(1)
    compiler_version = capture([compiler, "--version"])
    match = re.search(r"clang version (\d+)", compiler_version)
    if not match:
        raise ValueError("Configured compiler is not Clang")
    major = match.group(1)
    profdata = shutil.which("llvm-profdata-" + major)
    cov = shutil.which("llvm-cov-" + major)
    if not profdata or not cov:
        raise ValueError(
            "Matching LLVM profile tools are missing for compiler " + major
        )
    tool_versions = {
        tool: capture([path, "--version"])
        for tool, path in [("compiler", compiler), ("profdata", profdata), ("cov", cov)]
    }
    for tool in ["profdata", "cov"]:
        if not re.search(r"version\s+" + major + r"\.", tool_versions[tool], re.I):
            raise ValueError("Mismatched " + tool + " version")
    prior = build / "coverage-previous"
    if previous:
        if previous["tools"] != tool_versions:
            raise ValueError("Changed coverage toolchain requires fresh collection")
        if prior.exists():
            shutil.rmtree(prior)
        report.rename(prior)
    elif report.exists():
        shutil.rmtree(report)
    report.mkdir()
    if previous:
        shutil.copyfile(prior / "manifest.json", report / "reused-manifest.json")
    (report / "inventory.json").write_text(json.dumps(entries, indent=2) + "\n")
    discovery = json.loads(
        capture(["ctest", "--test-dir", build, "--show-only=json-v1"])
    )
    groups = {name: [] for name in selected}
    for test in discovery["tests"]:
        binaries = {
            Path(str(a)).name.removesuffix(".exe") for a in test.get("command", [])
        }
        for name in binaries & selected.keys():
            groups[name].append(test["name"])
    if any(not cases for cases in groups.values()):
        raise ValueError("Selected test executable missing from CTest discovery")
    profiles, completed = [], {}
    for target, cases in sorted(groups.items()):
        raw = report / "raw" / target
        raw.mkdir(parents=True)
        environment = dict(os.environ, LLVM_PROFILE_FILE=str(raw / "%m-%p.profraw"))
        expression = "^(" + "|".join(re.escape(name) for name in cases) + ")$"
        if previous and target not in args.refresh_target:
            if cases != previous["completed"][target]["tests"]:
                raise ValueError("Unaffected test discovery changed: " + target)
            shutil.copytree(prior / "raw" / target, raw, dirs_exist_ok=True)
            shutil.copyfile(prior / (target + ".log"), report / (target + ".log"))
            print(target + ": verified retained profiles; tests not rerun", flush=True)
            reused["retained_profiles"][target] = {
                p.name: hashlib.sha256(p.read_bytes()).hexdigest()
                for p in raw.glob("*.profraw")
            }
        else:
            with (report / (target + ".log")).open("w") as output:
                run(
                    [
                        "ctest",
                        "--test-dir",
                        build,
                        "-R",
                        expression,
                        "--no-tests=error",
                        "--timeout",
                        "120",
                        "--output-on-failure",
                    ],
                    env=environment,
                    stdout=output,
                    stderr=subprocess.STDOUT,
                )
        produced = require_profiles(raw, len(cases))
        profiles.extend(produced)
        completed[target] = {"tests": cases, "profiles": len(produced)}
        print(f"{target}: {len(cases)} tests, {len(produced)} profiles", flush=True)
    merged = report / "wiola.profdata"
    run([profdata, "merge", "-sparse", *profiles, "-o", merged])
    configured_build = CMakeBuild(
        build,
        CMakeInventory.read(build, first_party_sources=load_current_registry().paths),
    )
    player_binary = configured_build.executable("wiola-player")
    mapping_binary = configured_build.executable("wiola_coverage_inventory")
    objects = [player_binary, mapping_binary]
    objects.extend(Path(value["binary"]) for value in selected.values())
    if any(not p.is_file() for p in objects):
        raise ValueError("An inventoried production/test object is absent")
    arguments = [
        objects[0],
        *[part for p in objects[1:] for part in ["-object", p]],
        "-instr-profile=" + str(merged),
        str(root / "src"),
    ]
    reporter = LLVMReporter((cov,), report)
    measured = reporter.execute

    for command, output in [("report", "summary.txt"), ("export", "summary.json")]:
        options = ["-format=text", "-summary-only"] if command == "export" else []
        with (report / output).open("w") as destination:
            measured([command, *arguments, *options], destination)
    with (report / "coverage.lcov").open("w") as destination:
        measured(["export", *arguments, "-format=lcov"], destination)
    measured(
        ["show", *arguments, "-format=html", "-output-dir=" + str(report / "html")]
    )
    data = json.loads((report / "summary.json").read_text())["data"][0]
    mapped = {Path(f["filename"]).resolve() for f in data["files"]}
    absent = [s for s in entries["sources"] if (root / s).resolve() not in mapped]
    if absent:
        raise ValueError(
            "Production sources absent from coverage mapping: " + ", ".join(absent)
        )
    # Keep workflow mappings stable whether the profile run selects workflow or all tests.
    # Non-workflow test TUs can instantiate extra inline functions; their mappings must not
    # silently alter the qualified denominator. Hits still use the same collected profile.
    workflow_objects = [
        mapping_binary,
        *[Path(value["binary"]) for value in selected.values() if value["workflow"]],
    ]
    workflow_arguments = [
        player_binary,
        *[part for p in workflow_objects for part in ["-object", p]],
        "-instr-profile=" + str(merged),
        str(root / "src"),
    ]
    with (report / "workflow-summary.txt").open("w") as destination:
        measured(["report", *workflow_arguments], destination, phase="workflow")
    with (report / "workflow-summary.json").open("w") as destination:
        measured(
            ["export", *workflow_arguments, "-format=text", "-summary-only"],
            destination,
            phase="workflow",
        )
    with (report / "workflow.lcov").open("w") as destination:
        measured(
            ["export", *workflow_arguments, "-format=lcov"],
            destination,
            phase="workflow",
        )
    workflow_data = json.loads((report / "workflow-summary.json").read_text())["data"][
        0
    ]
    files = [f for f in workflow_data["files"] if workflow_file(f["filename"], root)]
    workflow = {
        "totals": summarize(files),
        "files": [
            {"file": str(Path(f["filename"]).relative_to(root)), "counts": f["summary"]}
            for f in files
        ],
    }
    (report / "workflow.json").write_text(json.dumps(workflow, indent=2) + "\n")
    (report / "workflow.txt").write_text(
        "\n".join(
            f'{name}: {v["covered"]}/{v["count"]}; uncovered={v["uncovered"]}'
            for name, v in workflow["totals"].items()
        )
        + "\n"
    )
    after = source_state(root)
    if before != after:
        raise ValueError("Tested source changed during measurement")
    manifest = dict(
        before,
        scope=args.scope,
        platform=os.name,
        tools=tool_versions,
        completed=completed,
        raw_profiles=len(profiles),
        mapping_diagnostics=reporter.diagnostics,
        exclusions=[
            "external dependencies and generated files: only src/ is selected",
            "compile/link boundary executables are not production targets",
        ],
    )
    if reused:
        manifest["reused_evidence"] = reused
        verify_reused_profiles(root, report, manifest)
    (report / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    percent = data["totals"]["lines"]["percent"]
    (report / "html/badge.json").write_text(
        json.dumps(
            {
                "schemaVersion": 1,
                "label": "coverage",
                "message": f"{percent:.1f}%",
                "color": (
                    "brightgreen"
                    if percent >= 90
                    else "yellow" if percent >= 75 else "red"
                ),
            }
        )
        + "\n"
    )
    print((report / "workflow.txt").read_text())
    print("Reports: " + str(report))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
