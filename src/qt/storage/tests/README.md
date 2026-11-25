# Qt storage contracts

The JSON, library-storage and repository-restoration suites live beside their adapter. They qualify round trips, stored-schema compatibility, invalid/future/oversized input, atomic output failures, verified backup recovery and stable restored identities. Tests use scoped Qt temporary directories and supplied C++ output implementations.

These targets link the QtCore library-store adapter. Theme, layout and session tests separately exercise consumers of the generic atomic file mechanism. No widget dependency is required to test the storage driver itself.
