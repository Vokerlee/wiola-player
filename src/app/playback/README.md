# Application playback

PlaybackService owns the portable kernel Session and the built-in equalizer/volume chain used by application commands. Playback policy and presentation stay here; widgets and native device construction belong to their adapters.

```mermaid
flowchart LR
    Frontend[Frontend commands] --> Service[PlaybackService]
    Service --> Kernel[Playback Session]
    Service --> Processing[Owned processing chain]
    Native[Native playlist workflow] --> Queue[PlaybackQueue]
    Queue --> Local[Explicit local resource conversion]
    Queue --> Service
```

Service commands and observations use `media::ResourceReference`. The same reference reaches Session, snapshots and resource handoff without a second path cache or filename interpretation. The caller supplies the media provider and output factory. A new provider can interpret its own keys without changing this service or the kernel.

PlaybackQueue adapts the current path-based library and saved-playlist workflows into those commands. Its explicitly named path operations perform local conversion at this boundary. ActivePlaylist and playlist-transfer operations own destination/occurrence policy independently of the queue being played. Frontend event delivery and persistence completion do not belong to Session.
