"""Measure maintained Python tools and their actual owned CTest script runners."""

import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile

import coverage

from scripts.build.cmake import CMakeInventory
from scripts.quality.architecture.ownership.registry import load_current_registry
from scripts.quality.architecture.test_catalog.catalog import load_current_catalog
from scripts.quality.coverage.source_policy.policy import load_current_policy


def _execute(
    root: Path, build_directory: Path, data_file: Path, source_roots: list[str]
) -> list[list[str]]:
    commands = []
    # Test discovery entrypoints are separate from instrumented production source_roots.
    test_packages = (("scripts", root), ("tests/scripts", None))
    tests = [
        [
            "-m",
            "unittest",
            "discover",
            "-s",
            directory,
            "-p",
            "test_*.py",
            *(["-t", str(top_level)] if top_level is not None else []),
        ]
        for directory, top_level in test_packages
    ]
    checks = [
        [
            "-m",
            "scripts.quality.architecture.commands.check_ownership",
            "--build-dir",
            str(build_directory.resolve()),
        ],
        [
            "-m",
            "scripts.quality.architecture.commands.check_dependencies",
            "--build-dir",
            str(build_directory.resolve()),
        ],
        [
            "-m",
            "scripts.quality.architecture.commands.check_tests",
            "--build-dir",
            str(build_directory.resolve()),
        ],
    ]
    discovery = json.loads(
        subprocess.check_output(
            ["ctest", "--test-dir", str(build_directory), "--show-only=json-v1"],
            cwd=root,
            text=True,
        )
    )
    runners = load_current_catalog().python_runners(
        discovery, root, load_current_registry()
    )
    for arguments in [*tests, *checks, *runners]:
        command = [
            sys.executable,
            "-m",
            "coverage",
            "run",
            "--branch",
            "--parallel-mode",
            "--data-file",
            str(data_file),
            "--source",
            ",".join(source_roots),
            *arguments,
        ]
        subprocess.run(command, cwd=root, check=True)
        commands.append(arguments)
    return commands


def _process_profiles(directory: Path) -> dict[str, str]:
    return {
        path.name: hashlib.sha256(path.read_bytes()).hexdigest()
        for path in sorted(directory.glob(".coverage.*"))
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    inventory = CMakeInventory.read(
        args.build_dir, first_party_sources=load_current_registry().paths
    )
    root = inventory.source_root
    policy = load_current_policy()
    sources = sorted(
        record.path for record in policy.records.values() if record.language == "python"
    )
    roots = sorted({str(root / Path(path).parts[0]) for path in sources})
    before = {
        path: hashlib.sha256((root / path).read_bytes()).hexdigest() for path in sources
    }
    args.output = args.output.resolve()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    raw = Path(tempfile.mkdtemp(prefix="python-profiles-", dir=args.output.parent))
    data_file = raw / ".coverage"
    collector = coverage.Coverage(
        data_file=str(data_file), data_suffix=True, branch=True, source=roots
    )
    collector.start()
    try:
        commands = _execute(root, args.build_dir, data_file, roots)
    finally:
        collector.stop()
        collector.save()
    after = {
        path: hashlib.sha256((root / path).read_bytes()).hexdigest() for path in sources
    }
    if before != after:
        raise ValueError("Instrumented Python source changed during collection")
    profiles = _process_profiles(raw)
    if len(profiles) != len(commands) + 1:
        raise ValueError(
            "Expected one Python profile per completed command and collector"
        )
    subprocess.run(
        [
            sys.executable,
            "-m",
            "coverage",
            "combine",
            "--keep",
            "--data-file",
            str(data_file),
        ],
        cwd=root,
        check=True,
    )
    subprocess.run(
        [
            sys.executable,
            "-m",
            "coverage",
            "json",
            "--keep-combined",
            "--data-file",
            str(data_file),
            "-o",
            str(args.output),
            *sources,
        ],
        cwd=root,
        check=True,
    )
    if _process_profiles(raw) != profiles:
        raise ValueError("Python process profiles changed during reporting")
    manifest = dict(
        coverage_version=coverage.__version__,
        instrumented_source_sha256=before,
        completed_commands=commands,
        process_profiles=profiles,
    )
    args.output.with_name(args.output.stem + "-manifest.json").write_text(
        json.dumps(manifest, indent=2) + "\n"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
