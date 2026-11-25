# Catalog tests

These cases test the [library owner](../README.md): distinct identities and occurrences, metadata/tag rules, query matching, repository transactions and location reconciliation. Relocation cases supply digest/location facts directly; native fingerprint integration belongs to the local media tests. These cases link the catalog target and use scoped files only where native observations are still part of the location-identity compatibility contract.

The owner registers these tests when `WIOLA_BUILD_TESTS` is enabled. Shared portable collaborators remain under the repository test root; Qt storage/schema tests stay with the [Qt storage adapter](../../qt/storage/tests).
