# LLVM reporting tests

Independent stderr fixtures cover detailed sentinels, unknown/nonzero-hash warnings and mismatched summary counts. Controlled real commands verify retained failure stderr, strict rejection after successful exits, phase ordering, snapshot isolation and command construction. Existing raw LLVM profiles can separately replay reporting without rerunning C++ tests.

Controlled backend contracts require serial detailed reporting while preserving ordinary export/HTML behavior. A real retained multi-object dataset is used for repeated crash reproduction and unchanged-count verification when validating this backend policy.
