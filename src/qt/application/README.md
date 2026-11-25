# Qt application adapters

QObject application adapters own Qt event-loop delivery, player/session preferences, library/workspace publishers and runtime composition. They adapt the portable app/library/playback operations without owning widget state. PlayerApplication owns pumping and shutdown independently of a window, so recreating a view preserves the same player/library services.

Shared QueuedDelivery lives in the [Qt owner](../README.md); storage and asynchronous projection have their own complete sibling owners. The [application tests](tests) qualify preferences, publication, metadata transactions and host lifetime. Widget presentation is composed separately through `wiola::qt_widgets`.

The portable [application persistence workflow](../../app/library/README.md) receives explicit revision, receipt and retained-payload capacities. Ordinary edits reserve one revision and receipt slot for physical file results. Payload accounting includes record/container capacity, retained text capacity and native path code units, with arithmetic bounded by the supplied byte capacity; it is a conservative queue budget rather than a process-memory or allocator-overhead guarantee. Invalid capacities are rejected before the worker starts.

The Qt composition owns a queued receiver and a snapshot writer adapter, supplying both to portable LibraryPersistence. `library/library_persistence_delivery.hpp` translates the queued receiver into its narrow owner-delivery contract. Persistence policy and worker state live together under `app/library`; Qt files/serialization stay under `qt/storage`.

Saved-playlist commands and their name policy live in the [portable application owner](../../app/library/README.md). LibrarySession supplies persistence and exposes those commands; Qt storage integration remains here. Command results own their commit/receipt, rather than requiring a later mutable-status lookup.
