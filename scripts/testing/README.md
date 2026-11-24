# Test execution tools

This family owns reusable test protocols and project-specific execution composition. Each component keeps its implementation, public API, focused tests and invariant guide together.

```mermaid
flowchart LR
  Caller[Developer or CI] --> Desktop[Desktop composition]
  Desktop --> Protocol[Generic GoogleTest protocol]
  Mutation[Mutation qualification] --> Protocol
  Protocol --> Process[Explicit executable and retained case evidence]
```

[GoogleTest](gtest/README.md) owns discovery, execution and exact report evidence without Qt, project identities or build layouts. Import its public API from `scripts.testing.gtest`. [Desktop composition](desktop/README.md) supplies only the project's fixture environment and caller-selected GUI adapter: `python3 -m scripts.testing.desktop --help`.

Environment changes are confined to child processes. Executable paths come from callers or configured build metadata, never guessed output directories. Test-policy catalog and mutation tools retain their existing ownership until their complete family migration; they are not additional implementations of these protocols.
