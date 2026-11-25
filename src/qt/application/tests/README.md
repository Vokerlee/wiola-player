# Qt application contracts

These suites qualify the QObject workflow adapters beside their implementations: library/workspace publication, saved-playlist and queue restoration, preferences, metadata writes, session persistence and player lifetime. Shared event-delivery tests live with the Qt primitive rather than inside a workflow suite. Artifact locations are supplied by CMake; process and real encoded-media journeys use scoped fixtures.
