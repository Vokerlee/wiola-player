# Catalog tests

These cases test the [library owner](../README.md): distinct identities and occurrences, metadata/tag rules, query matching, repository transactions and location reconciliation. Relocation cases supply digest/location facts directly; native fingerprint integration belongs to the local media tests. These cases link the portable `wiola::library` SDK target directly; native path identity and file observations are qualified by application and media-adapter tests.

The owner registers these tests when `WIOLA_BUILD_TESTS` is enabled. Shared portable collaborators remain under the repository test root; Qt storage/schema tests stay with the [Qt storage adapter](../../qt/storage/tests).

Transaction recovery cases reuse the shared C++ allocation-failure fixture to fail each allocation until an update/commit succeeds. They check original records and the persistence-acceptance boundary, including publication failure after durable acceptance. Native TSan supplies its own allocation replacements, so these scoped fault cases run in Debug/ASan/coverage while ordinary lifecycle cases run in every sanitizer mode. No production failure hook is required.

`repository_restore_test.cpp` qualifies atomic rejection of invalid/duplicate snapshot records, tag shape limits, missing identities, rollback of replaced/deleted records and collision-free identity allocation after restore. These cases use owned in-memory snapshots without storage or frontend dependencies. Metadata patch cases reject invalid earlier fields before any later field is applied.

`repository_contract.hpp` provides `verify_repository_contract` for a fresh `LibraryRepository` implementation. It checks opaque identity uniqueness, duplicate track occurrences, removal, tags and playlist lifecycle; backend-specific allocation and identity-format cases remain separate.
