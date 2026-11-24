# Contributing

## Building

| | Minimum | Where it comes from |
|---|---|---|
| CMake | 3.28 | `cmake` |
| Compiler | GCC 13 or Clang 17 | `g++`, C++23 |
| Qt | 6, Widgets | `qt6-base-dev` |

The ordinary GUI build needs an installed Qt. Portable component builds below do not.
FFmpeg configuration lives in cmake/ffmpeg.cmake; miniaudio and test-only GoogleTest are fetched at the versions pinned in cmake/dependencies.cmake.

### Linux

```bash
sudo apt install cmake g++ qt6-base-dev

cmake --preset linux                   # configure into build/
cmake --build --preset linux           # build/bin/wiola-player
ctest --preset linux                   # run the tests
```

Builds are `RelWithDebInfo` unless a type is named. That is deliberate rather than a default: the
decoder feeds a device callback that has to answer within a period, and unoptimized it runs several
times slower than the deadline allows.

`-DENABLE_UNIT_TESTS=OFF` skips the tests and GoogleTest with them. In-source builds are refused,
so configure from the repository root as above.

### Portable components

Configure only an existing foundation/media component when working without player SDKs:

```bash
cmake -S . -B build-components -DWIOLA_COMPONENTS=media -DBUILD_TESTING=OFF
cmake --build build-components
```

Supported selections are borrowed_reference, quantities and media; semicolon-separated combinations are accepted. The component build skips Qt, FFmpeg and miniaudio preparation. With tests enabled, only quantities/decoder need the existing pinned GoogleTest dependency. Empty WIOLA_COMPONENTS retains the ordinary presets. [CMake component selection](cmake/components.cmake) defines scope; the decoder public install boundary is still being extracted.

### Windows

Cross-compiled from Linux. The result is one executable that needs no DLLs beside it.

```bash
sudo apt install g++-mingw-w64-x86-64-posix
```

The build also needs a Qt built for MinGW-w64, which no distribution packages. Build one once with
`scripts/qt/windows/build.sh`, described in [scripts/README.md](scripts/README.md); it compiles Qt
from source, so expect it to take a while and to want several gigabytes. `QT_MINGW_ROOT` then
points the toolchain file at it and has to be set in the shell that configures:

```bash
scripts/qt/windows/build.sh ~/opt/qt6-mingw-static
export QT_MINGW_ROOT=~/opt/qt6-mingw-static

cmake --preset windows                 # configure into build-win/
cmake --build --preset windows         # build-win/bin/wiola-player.exe
```

The tests are off in this preset: they are built for Windows and the host cannot run them. Run
them under the linux preset instead.

To confirm the executable stands alone, list what it imports. Every name should be a Windows
system library:

```bash
x86_64-w64-mingw32-objdump -p build-win/bin/wiola-player.exe | grep "DLL Name"
```

### Releases

What a published binary is built from, one preset per platform:

```bash
cmake --preset linux-release            # configure into build-release/
cmake --build --preset linux-release    # build-release/bin/wiola-player

cmake --preset windows-release          # configure into build-win-release/
cmake --build --preset windows-release  # build-win-release/bin/wiola-player.exe
```

They differ from the presets above in build type only, `Release` rather than the `RelWithDebInfo`
a plain preset leaves in place, and each has a directory of its own so that neither disturbs the
build you develop in. Unit tests are off in both: nothing is shipped from them, and skipping them
skips fetching GoogleTest.

### When configure fails

`Could not find a package configuration file provided by "Qt6"` means Qt is missing: install
`qt6-base-dev` for the linux preset, or set `QT_MINGW_ROOT` for the windows one.

Changing the compiler or `QT_MINGW_ROOT` after a successful configure does nothing, because both
are already recorded in the cache. Delete the build directory and configure again.

## Source file headers

Every first-party C/C++ source and header file, including tests, must carry the complete GPL notice
in its file header. Copyright-only or otherwise abbreviated headers are not accepted. Use ASCII for
the copyright line: `@copyright (c) 2026, <vokerlee@gmail.com>`. Copy this block verbatim after the
`@copyright` line:

```cpp
 *
 * Wiola is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Wiola is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Wiola. If not, see <http://www.gnu.org/licenses/>.
```

## Formatting

Use the maintained command so developer, hook and CI checks select inputs and invoke the same pinned tools.

```bash
pip install -r requirements.txt

python3 -m scripts.formatting format cpp
python3 -m scripts.formatting format cmake
python3 -m scripts.formatting format all --check
python3 -m scripts.formatting format cpp --check --files src/main.cpp
python3 -m scripts.formatting install-hooks
```

Default selection includes existing tracked files. `--files` selects only explicit tracked paths relative to the worktree root. Missing tools and formatter failures fail the hook as well as CI; installation preserves regular custom hook files. The hook checks working-tree files, not separately staged partial-file contents.

`.clang-format` and `.gersemirc` decide style; requirements.txt pins the tool versions. Keep these facts in their owning files. [The formatting component](scripts/formatting/README.md) explains selection and hook invariants.

## Coverage

Use the instrumented CMake preset and LLVM tools matching its Clang version:

```bash
cmake --preset linux-coverage
cmake --build --preset linux-coverage --parallel 4
cmake --build --preset linux-coverage --target wiola_coverage_inventory --parallel 4
python3 -m scripts.coverage --build-dir build-coverage --source src --exclude '/tests/'
```

The helper runs CTest with a fresh profile directory and invokes `llvm-profdata` and `llvm-cov`. Reports are generated build artifacts: `summary.txt`, LLVM's `summary.json`, `coverage.lcov`, and `html/index.html`. The link-only inventory includes unexecuted production libraries in a full report. No owner registry, file-movement table, stored coverage floor or Python test-coverage framework is required.

For a focused report, build the affected instrumented test targets and pass `--target TARGET...`. Only their tests and executable mappings are used; this report describes that selected scope. `--source` chooses the source directory and `--exclude` passes a filename expression to LLVM. The example excludes colocated tests from the production report.

Coverage thresholds belong to CI. The current workflow uses the ordinary `diff-cover` command to require full coverage of changed measured production lines in pull requests. Aggregate coverage remains visible in LLVM's reports; this is not a claim of full repository coverage.

```bash
diff-cover build-coverage/coverage/coverage.lcov --compare-branch origin/main --fail-under=100
```

## Commit names

```
area: what the commit does (#PR)
```

`area` is where the change lives, lower case, followed by a colon. The ones in use:

```
cmake  codec  engine  cli  audio  utils  lockfree  core
tests  scripts  docs  formatting  gitignore  ci  readme
```

Lower case after the colon, no full stop, and say what the commit does rather than what was
wrong. The pull request number closes the line.

```
codec: support seeking for format files while decoding (#43)
audio: add states enum for device (#60)
lockfree: implement clear method for SPSC ring-buffer (#47)
cmake: decrease CXX standard 26->23 (#51)
```

One logical change per commit, and every commit builds and passes the tests on its own. A bisect
that lands on a broken commit costs more than a large one ever saves.
