# Legacy settings fixtures

These authored Qt INI files preserve relative Unicode paths and duplicate queue entries. Legacy data has no occurrence IDs; current entries include full-width IDs and explicit selection. Duplicate IDs invalidate the occurrence mapping instead of choosing an arbitrary duplicate. Invalid numeric settings use typed defaults. Migration tests copy the file before writing.

See the [qualification owner](../../README.md) for the runner and preservation invariants.
