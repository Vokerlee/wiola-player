# Scripts

`format-cpp.sh`, `format-cmake.sh`, `install-hooks.sh` and `coverage.sh` are described in
[CONTRIBUTING.md](../CONTRIBUTING.md). This file covers `build-qt-mingw.sh`.

## build-qt-mingw.sh

The windows preset needs a Qt built for MinGW-w64, which no distribution packages; without one,
configure fails at `find_package(Qt6)`. This script builds it. Only qtbase is built, and it is
static, so the executable still needs no DLLs beside it.

```bash
sudo apt install g++-mingw-w64-x86-64-posix qt6-base-dev cmake ninja-build curl

scripts/build-qt-mingw.sh ~/opt/qt6-mingw-static
export QT_MINGW_ROOT=~/opt/qt6-mingw-static
```

The host `qt6-base-dev` is required: a cross-build takes `moc`, `rcc` and `uic` from the host and
refuses a version mismatch. The script reads the version from `qmake6` and fetches the matching
source.

The install prefix is a required argument, and everything the script writes goes under it. Sources
and the build tree land in `.work` there, which can be deleted once the install has finished;
re-running reuses what is already unpacked. Qt is built from source, so the first run takes a
while. Run the script from inside a clone, as it finds the toolchain file through git.

`QT_MINGW_ROOT` is read by `cmake/mingw-w64.cmake`, so it has to be set in the shell that
configures the build.


The SDK implementation now lives with its identity logic and process contracts in
[tools/sdk/qt_mingw](../tools/sdk/qt_mingw/README.md). The existing script remains its public
launcher. CI supplies a second argument for a scratch workspace outside the install prefix,
so caches contain installed SDK files rather than downloaded sources and compiler objects.
Only x86-64 POSIX MinGW is supported; the 32-bit and Win32-threading metapackages are not needed.

## Packaged desktop checks

`run-desktop-journeys.py` is a compatibility launcher for
[the desktop contract composition](../tools/testing/desktop_checks/README.md).
The shared [GoogleTest protocol](../tools/testing/gtest/README.md) owns process
discovery, complete case evidence and assertion/crash classification. The runner
does not guess platform or build paths; pass a Qt platform explicitly when needed
(for example `--qt-platform offscreen`) or preserve the caller environment.

## Regression qualification

`check-gui-mutations.py` launches [owned mutation checks](../tools/testing/mutation_checks/README.md).
Regression recipes use stable source-owner identities; the engine does not embed GUI paths.
Run `cmake --build --preset linux-debug --target wiola_check_mutations` for the current native GUI configuration.
CMake supplies the actual build directory and available parallelism replaces fixed worker counts.
