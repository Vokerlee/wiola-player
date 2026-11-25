# Qt application adapters

QObject application adapters own Qt event-loop delivery, player/session preferences, library/workspace publishers and runtime composition. They adapt the portable app/library/playback operations without owning widget state. PlayerApplication owns pumping and shutdown independently of a window, so recreating a view preserves the same player/library services.

Shared QueuedDelivery lives in the [Qt owner](../README.md); storage and asynchronous projection have their own complete sibling owners. The [application tests](tests) qualify preferences, publication, metadata transactions and host lifetime. Widget presentation is composed separately through `wiola::qt_widgets`.

Library persistence receives explicit revision, receipt and retained-payload capacities. Ordinary edits reserve one revision and receipt slot for physical file results. Payload accounting includes record/container capacity, retained text capacity and native path code units, with saturating arithmetic; it is a conservative queue budget rather than a process-memory or allocator-overhead guarantee. Invalid capacities are rejected before the worker starts.
