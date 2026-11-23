# Architecture tools

These tools describe and check the current repository. They do not make the existing architecture clean simply by assigning names. Ownership declarations are explicit; a move must update the same stable owner rather than silently infer a new owner from its new path.

```mermaid
flowchart LR
  Command[Ownership command] --> Git[Git inventory adapter]
  Command --> Registry[Ownership registry]
  Git --> Index[Tracked index]
  Registry --> Declarations[Declared current ownership]
```

[Inventory](inventory/README.md) handles Git. [Ownership](ownership/README.md) validates declarations independently of Git. [CMake inventory](cmake_inventory/README.md) reconciles configured targets without executing a build. [Commands](commands/README.md) assemble the check and return a useful exit status. [Abstraction contracts](contracts/README.md) add explicit cohesion and migration-debt checks. No component imports player or Qt implementation.
