# Qt application contracts

These suites qualify the QObject workflow adapters beside their implementations: library/workspace publication, saved-playlist and queue restoration, preferences, metadata writes, session persistence and player lifetime. Shared event-delivery tests live with the Qt primitive rather than inside a workflow suite. Artifact locations are supplied by CMake; process and real encoded-media journeys use scoped fixtures.

Metadata durability-lifetime tests hold real staging behind explicit acknowledgments. They cover a conflict after intent persistence and cancellation of already prepared jobs during shutdown. A later job's validation delivery acts as a FIFO staging barrier; tests use no sleeps or private queue inspection.
