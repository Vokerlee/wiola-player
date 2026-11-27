"""Exercise coverage through real CMake, CTest and LLVM, including unlinked code."""

import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
TOOLS = ("cmake", "clang++-18", "llvm-profdata-18", "llvm-cov-18")


@unittest.skipUnless(all(shutil.which(tool) for tool in TOOLS), "LLVM 18 is required")
class CoverageTest(unittest.TestCase):
    def test_native_target_retains_unlinked_code_and_focused_report_selects_tests(self):
        with tempfile.TemporaryDirectory(prefix="wiola coverage ") as temporary:
            source = Path(temporary) / "source with spaces"
            build = Path(temporary) / "build with spaces"
            (source / "src").mkdir(parents=True)
            (source / "src/used.cpp").write_text("int used() { return 42; }\n")
            (source / "src/unlinked.cpp").write_text("int unlinked() { return 7; }\n")
            (source / "src/main.cpp").write_text(
                "int used();\nint main() { return used() == 42 ? 0 : 1; }\n"
            )
            (source / "CMakeLists.txt").write_text(
                f"""cmake_minimum_required(VERSION 3.28)
project(coverage_fixture LANGUAGES CXX)
include(CTest)
cmake_file_api(QUERY API_VERSION 1 CODEMODEL 2)
add_library(wiola_coverage INTERFACE)
target_compile_options(wiola_coverage INTERFACE -fprofile-instr-generate -fcoverage-mapping)
target_link_options(wiola_coverage INTERFACE -fprofile-instr-generate)
add_library(ordinary_name STATIC src/used.cpp)
target_link_libraries(ordinary_name PRIVATE wiola_coverage)
add_library(unlinked_library STATIC src/unlinked.cpp)
target_link_libraries(unlinked_library PRIVATE wiola_coverage)
add_executable(client src/main.cpp)
target_link_libraries(client PRIVATE ordinary_name wiola_coverage)
add_test(NAME client COMMAND client)
include("{ROOT / 'cmake/coverage.cmake'}")
"""
            )
            environment = dict(os.environ, PYTHONPATH=str(ROOT))

            def run(*command):
                subprocess.run(
                    command,
                    env=environment,
                    check=True,
                    stdout=subprocess.PIPE,
                    stderr=subprocess.STDOUT,
                    text=True,
                    timeout=60,
                )

            run(
                "cmake", "-S", str(source), "-B", str(build),
                "-DCMAKE_BUILD_TYPE=Debug", "-DCMAKE_CXX_COMPILER=clang++-18",
            )
            run("cmake", "--build", str(build), "--target", "coverage", "--parallel", "2")
            full = json.loads((build / "coverage/summary.json").read_text())["data"][0]
            files = {Path(item["filename"]).name: item["summary"] for item in full["files"]}
            self.assertEqual(files["used.cpp"]["lines"]["percent"], 100)
            self.assertEqual(files["unlinked.cpp"]["lines"]["percent"], 0)
            self.assertTrue((build / "coverage/html/index.html").is_file())
            self.assertTrue((build / "coverage/coverage.lcov").is_file())

            focused = build / "focused"
            run(
                "python3", "-m", "scripts.coverage", "--build-dir", str(build),
                "--source", str(source / "src"), "--target", "client",
                "--output", str(focused),
            )
            data = json.loads((focused / "summary.json").read_text())["data"][0]
            names = {Path(item["filename"]).name for item in data["files"]}
            self.assertEqual(names, {"main.cpp", "used.cpp"})
