# Library catalog

This owner contains catalog identities, track/playlist/workspace records, metadata/tag rules, queries and the repository contract/implementation. MediaId identifies a catalog track; PlaylistEntryId identifies one saved occurrence. Playback QueueEntryId and provider ResourceReference have their own separate meanings.

```mermaid
flowchart LR
    Workflow[Application workflow] --> Repository[LibraryRepository]
    Repository --> Records[Tracks / saved occurrences / workspaces / tags]
    Repository --> Transaction[Transactions / revisions / receipts]
    Query[Search / metadata resolution] --> Records
    Storage[Snapshot adapter] --> Records
```

InMemoryRepository implements the declared mutations and observations. Borrowed record pointers observe repository state; callers should not retain them across repository changes. Value snapshots and projected rows own the facts they retain. Metadata patches distinguish inherit, set and clear; saved duplicates keep separate occurrence identities.

Location/fingerprint helpers currently include native file observation and reconciliation, called explicitly by their consumers. Qt JSON/storage is implemented by the [Qt storage adapter](../qt/storage/README.md). Focused catalog cases live in [tests](tests/README.md), with workflow cases under [application tests](../app/tests/README.md). No Qt types are used by this source owner.
