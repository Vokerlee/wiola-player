# Playback contracts

`playback_contract_test` links only `wiola::playback_api`, GoogleTest and selected instrumentation. It verifies media-to-open-status translation and occurrence identity behavior. Existing Session, Playlist and presentation suites continue to verify outcome correlation, retention, revisions and actual snapshot production through their service APIs.

```mermaid
flowchart LR
    Cases[Portable contract cases] --> API[Playback public API]
    Consumer[Installed C++ consumer] --> API
    Buffering[Buffering tests] --> Private[Scoped private buffering target]
    Transport[Transport tests] --> Protocol[Scoped private transport target]
```

`Playback.InstalledConsumer` uses ordinary CMake to install/relocate the package, compile every actual installed header independently and link `consumer.cpp`. Only media/quantities packages are installed with it; audio, devices, transport, worker and GUI libraries are unnecessary. Native consumers run, while cross consumers compile/link only. There is no header manifest or Python checker.

[Buffering tests](buffering/README.md) cover the separate private queue/render mechanism. No standalone operations or snapshot module remains.

[Transport tests](transport/README.md) cover state, playhead, worker failures and player commands without a codec, filesystem or output SDK. A real FFmpeg-to-player integration journey remains in `tests/codec/player_integration_test.cpp`.

[Loading tests](loading/README.md) independently qualify request identity, cooperative cancellation, decoder lifetime, external callbacks outside locks and late completion rejection.

The public QueueEntry contract and installed consumer check owned path/metadata observations and immutable shared artwork without importing private queue storage. Queue rollback tests enumerate allocations until success instead of encoding implementation allocation counts.

The installed consumer supplies its own QueueView implementation from public headers. Queue owner tests exercise live mutation through that read port; frontend projection cases replace backing storage and switch owners with equal revision numbers, checking retained facts and occurrence selection without depending on private ordering.

Public policy contracts reject nonpositive/nonfinite buffering durations, empty decode blocks and nonpositive polling periods. The relocated consumer compiles these configuration headers and OutputFactory independently. Session/application cases inject the same MediaProvider port as production; the C++ fake owns its callbacks instead of requiring public test-only kernel constructors.

Session lifecycle cases continue through the facade after implementation hiding, including cancellation, allocation rollback, active callback teardown and media handoff. The compiler can consume its header with only public playback/media/quantity include roots (plus the current facade location), without private worker, storage, transport or DSP include roots. This does not yet constitute an installed Session export.
