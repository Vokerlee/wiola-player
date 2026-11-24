# Buffering tests

These four targets link the scoped buffering target and GoogleTest directly, without the checkout-wide `wiola_core` include root:

- `spsc_ring_buffer_test`: capacity, region/commit, wraparound, discard, endpoint transfer and producer/consumer concurrency. Oversized constructor requests must throw before invalid power-of-two arithmetic.
- `pcm_queue_test`: checked frame capacity, channel order, aligned short operations and wraparound.
- `audio_buffer_source_test`: the render bridge, borrowed consumer lifetime, short/empty output and untouched trailing samples.
- `cache_line_test`: the private index-separation prerequisite.

Existing case names and target identities are retained. Mode-specific allocation witnesses remain in their supported configurations. Concurrency cases use controlled endpoints and native TSan; the overflow regression also has a direct UBSan reproduction. No device, FFmpeg or Qt implementation is needed.
