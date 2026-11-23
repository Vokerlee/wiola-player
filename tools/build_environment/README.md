# Host build environment

This grouping owns the current [Ubuntu package adapter](ubuntu_packages/README.md): dependency resolution, installation and verified archive reuse. Caller profiles select packages; CI transports cache data.

```mermaid
flowchart LR
  Caller[Local build or CI] --> Packages[Ubuntu package adapter]
  Packages --> APT[Actual resolver and package archives]
```

Configured target observation/building lives in the complete [CMake adapter](../../scripts/build/cmake/README.md). It is consumed through its public API and has no dependency on these host package mechanics.
