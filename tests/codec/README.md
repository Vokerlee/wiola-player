# Codec and playback integration tests

These targets own encoded-media fixtures and real provider/decoder behavior. Driver cases exercise FFmpeg input, packets, conversion, seek recovery, supported containers and metadata reads/writes. Adding a driver case does not require changing kernel lifecycle fixtures.

```mermaid
flowchart LR
    Encoded[Encoded fixture / temporary WAV] --> Provider[LocalMediaProvider / FFmpeg]
    Provider --> Port[Public media contracts]
    Port --> Session[Playback Session]
    Session --> Output[Controlled output and supplied Stage]
```

`codec_player_integration_test` checks real decoding through the transport. `codec_session_integration_test` checks the complete provider-to-Session boundary: decoder duration, metadata for opened/appended FLAC/WAV queues, actual stream shape controlling output reuse, and an unreadable resource preserving the installed decoder. Its processing implementation uses only the public Stage port; no built-in DSP composition is required.

Session unit tests supply explicit DecoderStreamInfo values and metadata through a controlled MediaProvider. They create no encoded files and import no codec fixture directory. Actual format/path parsing stays here. Temporary WAV inputs use the existing per-run unique helper; checked-in encoded inputs come from this owner's data directory through the ordinary CMake test declaration.

Tests that construct or edit encoded files own a `TemporaryDirectory` for the complete operation. Small format-building helpers receive its path; decoders, assets and streams are destroyed before the directory. Metadata write tests copy checked-in inputs into that directory because the provider stages output beside its input. Checked-in fixtures remain read-only, and separate test processes and sanitizer configurations can run concurrently.

Registry header-read failure coverage includes a native procfs case, skipped when that OS facility is absent. Portable registry policy tests use injected backends; native I/O failure behavior stays with the adapter integration.
