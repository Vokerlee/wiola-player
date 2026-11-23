#!/usr/bin/env bash
set -euo pipefail

# Match the host ABI and consume the shared reviewed Qt source provider. Only Core is replaced for TSan
# processes: its instrumented event-queue/atomic synchronization must be visible to the tool.
# Qt's upstream sanitizer switch: https://github.com/qt/qtbase/blob/v6.4.2/config_help.txt
if [[ $# != 1 ]]; then echo 'Usage: build-qt-core-tsan.sh CACHE_DIRECTORY' >&2; exit 2; fi
qt_cache=$(realpath -m "$1")
script_directory="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
export PYTHONPATH="$script_directory/..${PYTHONPATH:+:$PYTHONPATH}"
qt_version=$(pkg-config --modversion Qt6Core)
qt_source_identity=$(python3 -m tools.sdk.qt_source "$qt_version" --identity)
qt_cxx=${CXX:-clang++-18}
qt_cc=${CC:-clang-18}
if ! pkg-config --exists glib-2.0; then
    echo 'Qt Core needs the GLib development headers to match the system event dispatcher' >&2; exit 1
fi
mkdir -p "$qt_cache/runtime"
qt_recipe=$(sha256sum "$0" | cut -d' ' -f1)
if python3 - "$qt_cache/runtime" "$qt_version" "$qt_source_identity" "$qt_recipe" "$qt_cxx" <<'PY'
import hashlib,json,subprocess,sys
from pathlib import Path
root=Path(sys.argv[1]); receipt=root/'receipt.json'; library=root/'libQt6Core.so.6'
if not receipt.exists() or not library.exists(): raise SystemExit(1)
value=json.loads(receipt.read_text())
expected={'version':sys.argv[2],'source_identity':sys.argv[3],'recipe_sha256':sys.argv[4],
          'compiler':subprocess.check_output([sys.argv[5],'--version'],text=True),
          'library_sha256':hashlib.sha256(library.read_bytes()).hexdigest()}
raise SystemExit(0 if value==expected else 1)
PY
then
    echo "Qualified instrumented Qt Core cache: $qt_cache/runtime"; exit 0
fi
qt_source=$(python3 -m tools.sdk.qt_source "$qt_version" "${WIOLA_QT_SOURCE_CACHE:-$qt_cache}")
cmake -S "$qt_source" -B "$qt_cache/build" \
    -DCMAKE_CXX_COMPILER="$qt_cxx" -DCMAKE_C_COMPILER="$qt_cc" \
    -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_CXX_FLAGS_RELWITHDEBINFO='-O1 -g' \
    -DCMAKE_C_FLAGS_RELWITHDEBINFO='-O1 -g' -DFEATURE_sanitize_thread=ON -DFEATURE_glib=ON \
    -DQT_BUILD_TESTS=OFF -DQT_BUILD_EXAMPLES=OFF > "$qt_cache/configure.log" 2>&1
cmake --build "$qt_cache/build" --target Core --parallel "${WIOLA_QT_BUILD_JOBS:-4}" > "$qt_cache/build.log" 2>&1
cp -L "$qt_cache/build/lib/libQt6Core.so.6" "$qt_cache/runtime/libQt6Core.so.6"
qt_symbols=$(nm -D "$qt_cache/runtime/libQt6Core.so.6")
if ! grep -q '__tsan_' <<< "$qt_symbols"; then echo 'Qt Core has no TSan instrumentation' >&2; exit 1; fi
python3 - "$qt_cache/runtime" "$qt_version" "$qt_source_identity" "$qt_recipe" "$qt_cxx" <<'PY'
import hashlib,json,subprocess,sys
from pathlib import Path
root=Path(sys.argv[1]); library=root/'libQt6Core.so.6'
value={'version':sys.argv[2],'source_identity':sys.argv[3],'recipe_sha256':sys.argv[4],
       'compiler':subprocess.check_output([sys.argv[5],'--version'],text=True),
       'library_sha256':hashlib.sha256(library.read_bytes()).hexdigest()}
(root/'receipt.json').write_text(json.dumps(value,indent=2)+'\n')
PY
printf 'Instrumented Qt Core: %s\n' "$qt_cache/runtime"
