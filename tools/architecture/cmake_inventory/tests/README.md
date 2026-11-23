# CMake inventory contracts

Tests construct small independent File API replies in an isolated directory. They assert exact source, generated-source, dependency and artifact resolution, then reject missing replies, escaping references, unknown dependencies and ambiguous configurations.

```mermaid
flowchart LR
  Fixture[Independent codemodel fixture] --> Reader[CMakeInventory]
  Reader --> Valid[Exact immutable target values]
  Broken[Invalid reply variants] --> Reader
  Reader --> Failure[BuildInventoryError]
```

Run `python3 -m unittest tools.architecture.cmake_inventory.tests.test_inventory`. No player build or SDK installation is required.
