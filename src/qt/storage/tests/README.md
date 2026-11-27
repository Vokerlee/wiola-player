# Qt storage contracts

The JSON, library-storage and repository-restoration suites live beside their adapter. They qualify round trips, stored-schema compatibility, invalid/future/oversized input, atomic output failures, verified backup recovery and stable restored identities. Tests use scoped Qt temporary directories and supplied C++ output implementations.

These targets link the QtCore library-store adapter. Theme, layout and session tests separately exercise consumers of the generic atomic file mechanism. No widget dependency is required to test the storage driver itself.

`qt_volume_settings_test` and `qt_equalizer_settings_test` qualify typed QSettings restoration and persistence through the portable playback service/control API. They use a supplied unavailable media driver for idle controls and a controlled decoder for the narrow-stream case. The targets link QtCore settings and the portable service directly, without Qt Widgets/Gui, FFmpeg, the application runtime or a source-root include.

Failure cases write real QSaveFile temporary output before simulating a short write or failed primary commit. They verify exact preservation of primary/backup bytes and retry through the same store. JSON cases reject every byte-prefix truncation, including multibyte text, without changing the accepted snapshot; invalid later legacy records cannot expose partially defaulted data. Concurrent edit admission and save retry remain covered by the portable persistence owner.
