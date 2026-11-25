# Catalog tests

These cases test the [library owner](../README.md): distinct identities and occurrences, metadata/tag rules, query matching, repository transactions and location reconciliation. Relocation cases supply digest/location facts directly; native fingerprint integration belongs to the local media tests. These cases link the catalog target and use scoped files only where native observations are still part of the location-identity compatibility contract.

The owner registers these tests when `WIOLA_BUILD_TESTS` is enabled. Shared portable collaborators remain under the repository test root; Qt storage/schema tests stay with the [Qt storage adapter](../../qt/storage/tests).

Transaction recovery cases reuse the shared C++ allocation-failure fixture to fail each allocation until an update/commit succeeds. They check original records and the persistence-acceptance boundary, including publication failure after durable acceptance. Native TSan supplies its own allocation replacements, so these scoped fault cases run in Debug/ASan/coverage while ordinary lifecycle cases run in every sanitizer mode. No production failure hook is required.
