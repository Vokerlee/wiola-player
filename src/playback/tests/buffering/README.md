# Buffering tests

These targets link the scoped buffering target and GoogleTest directly:

- `spsc_ring_buffer_test`: capacity, region/commit, wraparound, discard, endpoint transfer and producer/consumer concurrency. Oversized constructor requests must throw before invalid power-of-two arithmetic.
- `pcm_queue_test`: checked frame capacity, channel order, aligned short operations and wraparound.
- `audio_buffer_source_test`: the render bridge, borrowed consumer lifetime, short/empty output and untouched trailing samples.
- `cache_line_test`: the private index-separation prerequisite.
- `playback_relay_test`: attachment, short output, source replacement and preserving borrowed source lifetime.
- `playback_shaped_source_test`: supplied Stage dispatch, prefix-only processing, unused tail and stream identity. It explicitly links the built-in chain only as one test-supplied implementation.

Existing buffering cases are retained; relay and shaped-source cases use playback-owned targets. Mode-specific allocation witnesses remain in their supported configurations. Concurrency cases use controlled endpoints and native TSan; the overflow regression also has a direct UBSan reproduction. No device, FFmpeg or Qt implementation is needed.

PCM zero-minimum cases transfer a complete frame through the actual producer/consumer for mono, stereo and larger channel shapes. An unrepresentable one-frame request must fail before storage allocation. Assertions express usable frame capacity and payload preservation rather than freezing the generic ring's incidental two-sample minimum into the PCM contract.
