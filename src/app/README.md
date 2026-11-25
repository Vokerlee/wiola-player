# Application workflows

This owner composes library and playback operations using standard C++ types. It contains catalog-derived indices, metadata/tag workflows, [playback commands](playback/README.md) and workspace discovery/reconciliation. Qt event-loop publication and window lifetime are supplied by the Qt application/frontend adapters.

```mermaid
flowchart LR
    Frontend[Frontend commands] --> Workflows[Application workflows]
    Workflows --> Library[Catalog / repository]
    Workflows --> Playback[Playback Session / processing]
    Workflows --> Media[Supplied media provider]
    Native[Explicit native path workflows] --> Local[Local resource / file adapters]
```

PlaybackService owns its Session and built-in processing together. Metadata and workspace operations own their job/result values and cooperative contexts. Current local-file workflows compose resource references through the local adapter; the portable kernel receives those references through its existing ports.

The existing aggregate target is `wiola_app`; focused workflow cases live beside their implementations in [tests](tests/README.md). Their controlled collaborators test acceptance, failures and cancellation independently of widgets. QObject publication, QSettings, JSON persistence and concrete window composition live under [qt](../qt/README.md).

`library/local_identity.hpp/cpp`, linked through `wiola::local_identity`, owns the historical local-import path-to-ID policy: absolute lexical spelling, native case choice and locale-independent ASCII folding. This compatibility mapping neither opens the resource nor resolves symlinks; it does not constrain catalog or provider identities.

Focused ordinary targets include `wiola::library_persistence` and `wiola::playlist_commands`; both can configure with catalog/value dependencies only. Their [library workflow guide](library/README.md) describes ownership, immutable revisions and per-operation results.
