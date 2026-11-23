#!/usr/bin/env bash
set -euo pipefail

# Pin the same ABI as Ubuntu 24.04's Qt packages. Only the Core DSO is replaced for TSan
# processes: its instrumented event-queue/atomic synchronization must be visible to the tool.
# Qt's upstream sanitizer switch: https://github.com/qt/qtbase/blob/v6.4.2/config_help.txt
if [[ $# != 1 ]]; then echo 'Usage: build-qt-core-tsan.sh CACHE_DIRECTORY' >&2; exit 2; fi
qt_cache=$(realpath -m "$1")
qt_version=6.4.2
qt_digest=a88bc6cedbb34878a49a622baa79cace78cfbad4f95fdbd3656ddb21c705525d
qt_cxx=${CXX:-clang++-18}
qt_cc=${CC:-clang-18}
if [[ $(pkg-config --modversion Qt6Core) != "$qt_version" ]]; then
    echo "Instrumented Core must match system Qt $qt_version exactly" >&2; exit 1
fi
if ! pkg-config --exists glib-2.0; then
    echo 'Qt Core needs the GLib development headers to match the system event dispatcher' >&2; exit 1
fi
mkdir -p "$qt_cache/runtime" "$qt_cache/download"
qt_recipe=$(sha256sum "$0" | cut -d' ' -f1)
if python3 - "$qt_cache/runtime" "$qt_version" "$qt_digest" "$qt_recipe" "$qt_cxx" <<'PY'
import hashlib,json,subprocess,sys
from pathlib import Path
root=Path(sys.argv[1]); receipt=root/'receipt.json'; library=root/'libQt6Core.so.6'
if not receipt.exists() or not library.exists(): raise SystemExit(1)
value=json.loads(receipt.read_text())
expected={'version':sys.argv[2],'source_sha256':sys.argv[3],'recipe_sha256':sys.argv[4],
          'compiler':subprocess.check_output([sys.argv[5],'--version'],text=True),
          'library_sha256':hashlib.sha256(library.read_bytes()).hexdigest()}
raise SystemExit(0 if value==expected else 1)
PY
then
    echo "Qualified instrumented Qt Core cache: $qt_cache/runtime"; exit 0
fi
qt_archive="$qt_cache/download/qtbase-everywhere-src-$qt_version.tar.xz"
if [[ ! -f "$qt_archive" ]]; then
    curl -fsSL --retry 3 "https://download.qt.io/archive/qt/6.4/$qt_version/submodules/qtbase-everywhere-src-$qt_version.tar.xz" -o "$qt_archive"
fi
printf '%s  %s\n' "$qt_digest" "$qt_archive" | sha256sum --check
if [[ ! -f "$qt_cache/qtbase-everywhere-src-$qt_version/CMakeLists.txt" ]]; then
    tar -xf "$qt_archive" -C "$qt_cache"
fi
cmake -S "$qt_cache/qtbase-everywhere-src-$qt_version" -B "$qt_cache/build" \
    -DCMAKE_CXX_COMPILER="$qt_cxx" -DCMAKE_C_COMPILER="$qt_cc" \
    -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_CXX_FLAGS_RELWITHDEBINFO='-O1 -g' \
    -DCMAKE_C_FLAGS_RELWITHDEBINFO='-O1 -g' -DFEATURE_sanitize_thread=ON -DFEATURE_glib=ON \
    -DQT_BUILD_TESTS=OFF -DQT_BUILD_EXAMPLES=OFF > "$qt_cache/configure.log" 2>&1
cmake --build "$qt_cache/build" --target Core --parallel "${WIOLA_QT_BUILD_JOBS:-4}" > "$qt_cache/build.log" 2>&1
cp -L "$qt_cache/build/lib/libQt6Core.so.6" "$qt_cache/runtime/libQt6Core.so.6"
qt_symbols=$(nm -D "$qt_cache/runtime/libQt6Core.so.6")
if ! grep -q '__tsan_' <<< "$qt_symbols"; then echo 'Qt Core has no TSan instrumentation' >&2; exit 1; fi
python3 - "$qt_cache/runtime" "$qt_version" "$qt_digest" "$qt_recipe" "$qt_cxx" <<'PY'
import hashlib,json,subprocess,sys
from pathlib import Path
root=Path(sys.argv[1]); library=root/'libQt6Core.so.6'
value={'version':sys.argv[2],'source_sha256':sys.argv[3],'recipe_sha256':sys.argv[4],
       'compiler':subprocess.check_output([sys.argv[5],'--version'],text=True),
       'library_sha256':hashlib.sha256(library.read_bytes()).hexdigest()}
(root/'receipt.json').write_text(json.dumps(value,indent=2)+'\n')
PY
printf 'Instrumented Qt Core: %s\n' "$qt_cache/runtime"
