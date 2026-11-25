# Quantities contract tests

The existing utils_units_test target retains its case identities after relocation. It compiles against the public quantities target and GoogleTest, without wiola_core or a source-root include. It verifies dimensional conversions, chrono interoperation, arithmetic, comparisons, unordered values and literals. A direct C++ consumer exercises the public target; raw header/template coverage remains visible.

`Quantities.Consumer` builds `consumer.cpp` and links `wiola::quantities`. CTest runs the normal target executable; there is no install/export or recursive build step.