# Test execution tools

This family owns reusable test protocols and project-specific execution composition. Each component keeps its implementation, public API, focused tests and invariant guide together.

```mermaid
flowchart LR
  Caller[Developer or CI] --> Desktop[Desktop composition]
  Desktop --> Protocol[Generic GoogleTest protocol]
  Protocol --> Process[Explicit executable and retained case evidence]
```

[GoogleTest](gtest/README.md) owns discovery, execution and exact report evidence without Qt, project identities or build layouts. Import its public API from `scripts.testing.gtest`. [Desktop composition](desktop/README.md) supplies only the project's fixture environment and caller-selected GUI adapter: `python3 -m scripts.testing.desktop --help`.

Environment changes are confined to child processes. Executable paths come from callers or configured build metadata, never guessed output directories. 

[Public package checks](public_api/README.md) own the shared install/relocate/compile/link protocol. Component-owned manifests and consumers supply API-specific assertions; a private include qualifies only through its precise missing-header diagnostic.
