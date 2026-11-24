# Playback resource lifetime

This directory owns the per-track decoder/queue/transport bundle and reusable output streams. Session selects what to load and activates a prepared bundle; output construction stays supplied by the host. The processing chain is borrowed and must outlive the cache and all leases.

```mermaid
flowchart LR
    Session --> Cache[OutputSession: reusable committed stream]
    Cache --> Lease[Prepared output lease]
    Pipeline[PlaybackPipeline] --> Lease
    Pipeline --> Player[Player and transport worker]
    Player --> Queue[PCM queue and buffer source]
    Lease --> Stream[Owned output / shaped source / relay]
    Stream --> Queue
```

`OutputSession::prepare` constructs a stopped output or pins the matching committed stream. Failure or discarding a candidate changes neither cache nor source attachment. `activate` retains its stream and attaches the borrowed source only after old callbacks have quiesced. A lease has stable identity even when its owning unique pointer moves; its destructor detaches only if it still owns the stream's attachment. A superseded or unconnected candidate cannot detach a successor, including one using the same source address.

The cache and leases share stream ownership. A pipeline's lease pins the device and render bridge even if the cache changes format or is destroyed. Stream members destroy the output before its borrowed shaped source and relay. This shared ownership is local to output lifetime, not a general service registry.

PlaybackPipeline declares queue, decoded source, output lease and Player in that order. Reverse destruction joins/quiesces transport first, releases the decoder and device claim, detaches the source and releases the output pin, then frees source/queue storage. Player delegates shutdown to its owned TransportWorker rather than repeating stop/join. Setup-thread claim acquisition permits a candidate to be constructed before the prior track releases the same device.

A lease returned by this cache is activated through the same cache. Source shape must match the prepared stream. The private installation caller owns these preconditions and callback quiescence. This resource boundary does not yet make Session's entire installation path transactional; Session acceptance/rollback is separate work.
