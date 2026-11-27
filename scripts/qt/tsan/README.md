# Instrumented native Qt Core

This owner builds and qualifies the native Qt Core replacement used by TSan tests. `QtCoreContext` discovers its compiler, host Qt/GLib and declared build inputs. `QtCoreTsanBuilder` configures/builds only Core. `QtCoreRuntime` owns atomic output publication, receipt validation and loader verification. Complete implementation, CMake integration, tests and this guide live here.

```mermaid
flowchart LR
  Host[Actual compiler and host Qt/GLib] --> Context[QtCoreContext]
  Context --> Source[Shared qt_source provider]
  Source --> Build[Configure and build the actual Core target]
  Build --> Metadata[CMake File API artifact]
  Metadata --> Symbols[Require TSan symbols]
  Symbols --> Runtime[Atomic runtime and receipt]
  Runtime --> Loader[Verify the configured consumer loads this Core]
```

`python3 -m scripts.qt.tsan WORKSPACE` prepares `WORKSPACE/runtime`. This is the canonical command. Select compilers with `--cxx`/`--cc` or `CXX`/`CC`; otherwise discover Clang and its C counterpart on PATH. There is no fixed compiler version or `/usr`/architecture directory guess. Build parallelism comes from explicit input, `WIOLA_QT_BUILD_JOBS`, `CMAKE_BUILD_PARALLEL_LEVEL` or available CPUs. `--timeout` bounds configure/build operations.

`--identity` prints the context fingerprint without downloading/building. It includes the reviewed source/provider identity, compiler identity/target, actual host Core library digest, host Qt prefix/version, GLib version, declared build environment and this recipe. Build trees are separated by build-input identity, preventing old CMake cache settings from being reused as a different configuration. Runtime qualification also includes its verification policy: a policy change rechecks existing build output instead of recompiling unchanged Qt sources. Source acquisition belongs only to [qt_source](../source/README.md); `WIOLA_QT_SOURCE_CACHE` can share its explicit workspace. Native runtime qualification does not silently broaden support beyond the reviewed current Qt/host profile.

Reuse requires both the context receipt and exact runtime library digest. Failed builds and missing instrumentation cannot publish a qualified runtime. The Core shared-library path comes from its configured CMake target, rather than a `build/lib` assumption. The `runtime/libQt6Core.so.6` filename is this native Qt 6 ELF adapter's output contract.

`wiola_add_qt_tsan_runtime_check(CONSUMER)` registers a verification target using CMake's actual `TARGET_FILE`, not a guessed test path. The verifier takes `--runtime` or `WIOLA_QT_CORE_RUNTIME`, retains loader output and requires resolution to the qualified generated library. It applies the runtime search path only to its loader-inspection child process. Build tools continue to load their host Qt. Actual test processes receive the selected runtime from their caller. This owner has no GitHub dependencies; the workflow supplies a temporary workspace, cache transport and test environment. Other GUI backends do not consume this Qt adapter.
