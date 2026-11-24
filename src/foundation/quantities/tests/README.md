# Quantities contract tests

The existing utils_units_test target retains its case identities after relocation. It compiles against the public quantities target and GoogleTest, without wiola_core or a source-root include. It verifies dimensional conversions, chrono interoperation, arithmetic, comparisons, unordered values and literals. Independent build/install consumer checks qualify the actual exported header closure; raw header/template coverage remains visible.

The installed-consumer case is registered in this directory's `CMakeLists.txt`. CMake installs and relocates the package, compiles every actual installed header independently, and links the C++ consumer. Native consumers also run; cross consumers only link. Each run retains compiler/test logs in its own `Testing/PublicPackage-*` directory under the configured build. No Python checker or JSON header manifest is involved.
