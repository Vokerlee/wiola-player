"""Run configured tests and produce ordinary LLVM coverage reports."""

import argparse
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

from scripts.build.cmake import CMakeBuild, CMakeInventory


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", required=True, type=Path)
    parser.add_argument("--source", default=Path("src"), type=Path)
    parser.add_argument("--objects", type=Path, help="CMake-generated LLVM response file")
    parser.add_argument("--output", type=Path)
    parser.add_argument(
        "--target",
        nargs="+",
        help="Limit tests and objects to these executable targets",
    )
    parser.add_argument(
        "--exclude", default="/tests/", help="LLVM filename exclusion expression"
    )
    args = parser.parse_args()
    source = (Path(__file__).resolve().parents[1] / args.source).resolve()
    cache = (args.build_dir / "CMakeCache.txt").read_text()

    if not source.is_dir():
        parser.error("--source must be an existing source directory")

    output = (args.output or args.build_dir / "coverage").resolve()
    output.mkdir(parents=True, exist_ok=True)
    raw = Path(tempfile.mkdtemp(prefix="raw-", dir=output))

    command = [
        "ctest",
        "--test-dir",
        str(args.build_dir.resolve()),
        "--no-tests=error",
        "--output-on-failure",
        "--timeout",
        "120",
    ]

    if args.target:
        build = CMakeBuild(CMakeInventory.read(args.build_dir))
        objects = sorted({build.executable(name) for name in args.target})
        mappings = [str(objects[0]), *["-object=" + str(path) for path in objects[1:]]]
        discovered = json.loads(
            subprocess.check_output(
                [
                    "ctest",
                    "--test-dir",
                    str(args.build_dir.resolve()),
                    "--show-only=json-v1",
                ],
                text=True,
            )
        )
        cases, matched = [], set()

        for test in discovered["tests"]:
            found = {Path(argument) for argument in test.get("command", [])} & set(
                objects
            )
            if found:
                cases.append(test["name"])
                matched.update(found)

        if matched != set(objects):
            parser.error("Every selected target must have a configured CTest case")

        command += ["-R", "^(" + "|".join(re.escape(name) for name in cases) + ")$"]
    else:
        response = args.objects

        if response is None:
            configuration = re.search(r"^CMAKE_BUILD_TYPE:[^=]+=(.*)$", cache, re.M)
            if configuration is None:
                parser.error("Select a CMake build configuration or pass --objects")

            response = args.build_dir / (
                "coverage-objects-" + configuration.group(1) + ".rsp"
            )

        if not response.is_file():
            parser.error("Configure with ENABLE_COVERAGE=ON before collecting coverage")

        mappings = ["@" + str(response.resolve())]

    subprocess.run(
        command,
        check=True,
        env=dict(os.environ, LLVM_PROFILE_FILE=str(raw / "%m-%p.profraw")),
    )
    profiles = sorted(raw.glob("*.profraw"))

    if not profiles or any(path.stat().st_size == 0 for path in profiles):
        raise RuntimeError(
            "Tests produced no complete LLVM profiles; enable coverage instrumentation"
        )

    compiler = re.search(r"^CMAKE_CXX_COMPILER:[^=]+=(.+)$", cache, re.M)

    if compiler is None:
        raise RuntimeError("Configured C++ compiler is missing from the CMake cache")

    version = subprocess.check_output([compiler.group(1), "--version"], text=True)
    major = re.search(r"clang version (\d+)", version)

    if major is None:
        raise RuntimeError("LLVM source coverage requires a Clang build")

    tools = [
        shutil.which(name + "-" + major.group(1))
        for name in ("llvm-profdata", "llvm-cov")
    ]

    if not all(tools):
        raise RuntimeError(
            "Install llvm-profdata and llvm-cov matching the configured Clang version"
        )

    profdata, cov = tools
    merged = output / "coverage.profdata"
    subprocess.run(
        [profdata, "merge", "-sparse", *map(str, profiles), "-o", str(merged)],
        check=True,
    )
    arguments = [
        *mappings,
        "-instr-profile=" + str(merged),
        str(source),
    ]

    if args.exclude:
        arguments.append("-ignore-filename-regex=" + args.exclude)

    for operation, filename, options in (
        ("report", "summary.txt", []),
        ("export", "summary.json", ["-summary-only"]),
        ("export", "coverage.lcov", ["-format=lcov"]),
    ):
        with (output / filename).open("w") as destination:
            subprocess.run(
                [cov, operation, *arguments, *options], stdout=destination, check=True
            )

    subprocess.run(
        [
            cov,
            "show",
            *arguments,
            "-format=html",
            "-output-dir=" + str(output / "html"),
        ],
        check=True,
    )
    summary = json.loads((output / "summary.json").read_text())
    percent = summary["data"][0]["totals"]["lines"]["percent"]
    (output / "html/badge.json").write_text(
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
    print((output / "summary.txt").read_text())
    print("Reports:", output)


if __name__ == "__main__":
    main()
