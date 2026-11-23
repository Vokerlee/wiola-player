# CMake inventory

`CMakeInventory` reads a configured build's File API codemodel-v2 reply. It reports actual target names, kinds, source ownership, dependency names and artifact paths. It does not configure/build targets or infer a path from a preset name. Declared targets are not evidence that their binaries were built or tested.

```mermaid
flowchart LR
  Build[Explicit build directory] --> Index[File API index]
  Index --> Model[One configured codemodel]
  Model --> Targets[Target/source/dependency/artifact inventory]
```

Reply references must be local JSON filenames. Unsupported/multiple configurations and inconsistent target identities fail explicitly rather than select an arbitrary variant. Generated sources remain distinct; external dependency sources do not become first-party files. The [contract tests](tests/README.md) exercise a representative reply and rejected variants. Use the command's optional `--build-dir` input to reconcile the configured sources with the ownership registry.

Sources from another in-repository CMake output tree remain external even when reused by a different configuration. Build boundaries are observed through actual CMake cache markers, not directory-name guesses. Explicit declared first-party sources take precedence over an incidental output boundary; generated sources in the active build remain distinct.
