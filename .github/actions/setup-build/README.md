# Ubuntu build environment action

This GitHub adapter restores dependency/compiler caches and installs the requested Ubuntu host tools. It does not configure the player, select a GUI implementation, build an SDK or execute tests.

```mermaid
flowchart LR
  Inputs[Build identity, paths and package requirements] --> Cache[Restore existing caches]
  Cache --> Packages[Install required host packages]
  Packages --> Environment[C and C++ compiler launcher environment]
  Environment --> Build[Calling workflow's build]
```

`name` partitions caches, `build-dir` identifies actual dependency output, `packages` adds package requirements, and `qt` controls host Qt tools. Headless builds pass `qt: false`. The Windows workflow requests only the x86-64 POSIX MinGW compiler package, whose dependencies supply its matching C compiler/binutils; the all-architecture `mingw-w64` metapackage is unnecessary.

Cache restores precede network installation. APT installs only explicit dependencies, including Qt platform plugins for GUI checks, and omits optional recommended packages. Operations retry failed transfers and use bounded inactivity timeouts. Additional package names are passed as array arguments, not evaluated shell text. Ccache is made available to both C and C++ CMake compilations. A restored cache is reuse evidence, not proof that a build or test passed.

SDK construction and cache identity belong to [the Qt SDK owner](../../../tools/sdk/qt_mingw/README.md). The workflow owns scheduling and artifact transport. Changes to those responsibilities belong with their owners rather than inside this environment adapter.
