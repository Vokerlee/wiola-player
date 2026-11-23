#!/usr/bin/env bash
set -euo pipefail

# Build a static Qt SDK. An explicit workspace keeps temporary build products out of the SDK.
if [ "$#" -lt 1 ] || [ "$#" -gt 2 ]; then
    echo "usage: $0 <install-prefix> [<workspace>]" >&2
    exit 2
fi

prefix="$1"
work="${2:-$prefix/.work}"
script_directory="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
repository_directory="$(cd -- "$script_directory/../../.." && pwd)"
toolchain="${WIOLA_QT_TOOLCHAIN_FILE:-$repository_directory/cmake/mingw-w64.cmake}"

# Cross-builds borrow moc, rcc and uic from the host, and Qt refuses a version mismatch.
version="$(qmake6 -query QT_VERSION)"
host_prefix="$(qmake6 -query QT_INSTALL_PREFIX)"
host_cmake_dir="$(qmake6 -query QT_INSTALL_LIBS)/cmake"
if [ ! -d "$host_cmake_dir/Qt6" ]; then
    echo "host Qt CMake packages not found: $host_cmake_dir/Qt6" >&2
    exit 1
fi

mkdir -p "$work"
cmake -P "$script_directory/context.cmake" > "$work/context.identity"

source_dir="$(PYTHONPATH="$repository_directory${PYTHONPATH:+:$PYTHONPATH}" \
    python3 -m tools.sdk.qt_source "$version" "${WIOLA_QT_SOURCE_CACHE:-$work}")"

cmake -G Ninja -S "$source_dir" -B "$work/build" \
    -DCMAKE_TOOLCHAIN_FILE="$toolchain" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX="$prefix" \
    -DBUILD_SHARED_LIBS=OFF \
    -DQT_HOST_PATH="$host_prefix" \
    -DQT_HOST_PATH_CMAKE_DIR="$host_cmake_dir" \
    -DQT_BUILD_EXAMPLES=OFF \
    -DQT_BUILD_TESTS=OFF \
    -DFEATURE_sql=OFF \
    -DFEATURE_dbus=OFF \
    -DFEATURE_testlib=ON

cmake --build "$work/build" --parallel "${WIOLA_QT_BUILD_JOBS:-$(nproc)}"
cmake --install "$work/build"

test -f "$prefix/lib/cmake/Qt6Test/Qt6TestConfig.cmake"

echo "Qt $version installed to $prefix; export QT_MINGW_ROOT=$prefix"
