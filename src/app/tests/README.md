# Application workflow tests

The folders follow the [workflow owner](../README.md): catalog-derived search/artwork observations, metadata editing and replacement, playback commands and workspace reconciliation. Cases exercise acceptance, failure, cancellation and retained state through controlled collaborators, independently of the Qt event loop.

The owner registers these tests when `WIOLA_BUILD_TESTS` is enabled. Shared portable fakes and scoped temporary directories remain under the repository test root; Qt delivery, settings and window behavior are tested with the [Qt application adapters](../../qt/application/tests/README.md) and frontend. Codec media fixtures are supplied through the declared CMake test-data path.
