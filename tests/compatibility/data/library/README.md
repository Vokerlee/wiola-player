# Library record fixtures

The current v1 input retains Unicode metadata, explicit empty/cleared overrides, two values for one tag key, duplicate playlist occurrences with distinct IDs, missing resources and a decimal revision above exact JSON floating-point range. The minimal v1 input freezes omitted-field defaults. An orphan reference is invalid; future versions must survive store load/save unchanged.

See the [qualification owner](../../README.md) for the runner and preservation invariants.
