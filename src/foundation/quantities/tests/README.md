# Quantities contract tests

The existing utils_units_test target retains its case identities after relocation. It compiles against the public quantities target and GoogleTest, without wiola_core or a source-root include. It verifies dimensional conversions, chrono interoperation, arithmetic, comparisons, unordered values and literals. Independent build/install consumer checks qualify the actual exported header closure; raw header/template coverage remains visible.
