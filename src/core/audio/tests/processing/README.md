# Processing tests

The existing biquad, equalizer, volume, sanitizer, relay, processing-chain and shaped-source targets live here. They link the actual processing target and controlled audio fixtures, without a device, decoder SDK, Qt or the checkout-wide include root. Case/target identities are retained.

Policy cases include non-finite/below-unity bounds, retained authored configuration and out-of-range gain commands preserving accepted state. Relay destruction through `Source` must preserve the source it borrows. Concurrency cases check coherent parameter publication and processing behavior. Bounded handoff follows from its fixed storage and single-exchange paths; coverage and stress tests alone do not prove a callback time bound.

`AudioProcessing.InstalledConsumer` installs both output and processing components, compiles actual installed headers independently and runs the public C++ chain consumer. The output-only consumer remains separate and installs no DSP archive. CMake's ordinary required-component behavior reports a missing processing component at configuration time.

`audio_equalizer_publication_test` exercises the private handoff independently of filter math: initial state, latest-pending coalescing, a held bank surviving repeated publication, untorn/monotonic reads under concurrent writes and final-update adoption after writer completion. Assertions inspect payloads rather than slot indices or timing thresholds. Existing rapid-update/audio tests also run against the real equalizer.

The equalizer rollback case fails each preparation allocation in turn until a full preparation succeeds. Every failure must preserve observable band count, accepted gains, preamp, stable configuration references and the next PCM block including existing filter history. It then retries successfully. The shared C++ witness in `tests/support` is linked only to these fault-injection executables; TSan retains its own allocator and runs normal/concurrent behavior cases.

Chain transaction cases cover discarded preparations, a later stage failing after earlier preparation, candidate destruction, all-child activation, inert repeated commit and stateless/empty chains. Equalizer settings changed after preparation survive commit. Preparation alongside the processing role must leave the old stream's PCM and history unchanged; the controlled role stops/joins before commit. Failing every preparation allocation checks whole-chain rollback through the same C++ witness. The installed consumer implements its own stateful Stage using only the public port.

The shaped-source unit test and relocated consumer supply independent Stage implementations directly. They check the rendered prefix, preserved unused tail and stream identity through the public port, without requiring ProcessingChain or built-in equalizer controls in the adapter.
