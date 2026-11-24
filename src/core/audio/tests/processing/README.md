# Processing tests

The existing biquad, equalizer, volume, sanitizer, relay, processing-chain and shaped-source targets live here. They link the actual processing target and controlled audio fixtures, without a device, decoder SDK, Qt or the checkout-wide include root. Case/target identities are retained.

Policy cases include non-finite/below-unity bounds, retained authored configuration and out-of-range gain commands preserving accepted state. Relay destruction through `Source` must preserve the source it borrows. Concurrency/allocation cases continue to check bounded parameter publication and processing behavior.

`AudioProcessing.InstalledConsumer` installs both output and processing components, compiles actual installed headers independently and runs the public C++ chain consumer. The output-only consumer remains separate and installs no DSP archive. CMake's ordinary required-component behavior reports a missing processing component at configuration time.
