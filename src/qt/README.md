# Qt adapters

This owner contains adapters whose contract depends on Qt. Portable playback, library and presentation policy live in their respective source owners.

```mermaid
flowchart LR
    Consumer[Qt frontend / adapter consumer] --> Settings[Typed Qt settings adapter]
    Consumer --> Paths[Native path / QString conversion]
    Settings --> Paths
    Settings --> Policy[Application volume / audio configuration]
    Paths --> QtCore[QtCore]
```

[paths.hpp](paths.hpp) owns native path/QString conversion. `wiola::qt_paths` is a header-only QtCore target with no player/kernel dependency. Windows conversion uses native UTF-16; other platforms preserve their existing string representation. This adapter translates native file paths, rather than interpreting arbitrary resource-provider keys.

[settings.hpp](settings.hpp) and [settings.cpp](settings.cpp) own conversion between QSettings and typed player/queue values. Defaults, legacy stored keys, gains and occurrence selection retain their existing semantics. Link `wiola_qt_settings` when using that adapter. Qt settings and GUI tests are selected only when the Qt frontend is enabled.

The [focused tests](tests) stay with these adapters. Portable code can be configured and tested with the Qt frontend disabled; the Qt-specific types do not belong to the public playback/media SDKs.
