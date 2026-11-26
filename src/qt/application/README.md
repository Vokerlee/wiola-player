# Qt application adapters

QObject application adapters own Qt event-loop delivery, player/session preferences, library/workspace publishers and runtime composition. They adapt the portable app/library/playback operations without owning widget state. PlayerApplication owns pumping and shutdown independently of a window, so recreating a view preserves the same player/library services.

Shared QueuedDelivery lives in the [Qt owner](../README.md); storage and asynchronous projection have their own complete sibling owners. The [application tests](tests) qualify preferences, publication, metadata transactions and host lifetime. Widget presentation is composed separately through `wiola::qt_widgets`.

The portable [application persistence workflow](../../app/library/README.md) receives explicit revision, receipt and retained-payload capacities. Ordinary edits reserve one revision and receipt slot for physical file results. Payload accounting includes record/container capacity, retained text capacity and native path code units, with arithmetic bounded by the supplied byte capacity; it is a conservative queue budget rather than a process-memory or allocator-overhead guarantee. Invalid capacities are rejected before the worker starts.

The Qt composition owns a queued receiver and a snapshot writer adapter, supplying both to portable LibraryPersistence. `library/library_persistence_delivery.hpp` translates the queued receiver into its narrow owner-delivery contract. Persistence policy and worker state live together under `app/library`; Qt files/serialization stay under `qt/storage`.

Saved-playlist commands and their name policy live in the [portable application owner](../../app/library/README.md). LibrarySession supplies persistence and exposes those commands; Qt storage integration remains here. Command results own their commit/receipt, rather than requiring a later mutable-status lookup.

Catalog metadata edits use the [portable metadata workflow](../../app/metadata/README.md). LibrarySession supplies the native fingerprint reader at composition. Bindings send a captured edit session and an explicit conflict policy; application observers run after transaction closure and receive later durability updates only while their service remains alive.

PlayerApplication owns shutdown ordering: stop timers and playback admission, drain the library, then save preferences. A nested shutdown reports that completion is still pending; scoped state permits explicit retry after an exception or failed save. LibrarySession caches an optional completed result only after its flush, worker stop and settings synchronization finish. An interrupted shutdown cannot inherit a default successful outcome.

SessionState combines portable [SessionDocuments](../../presentation/documents/session_documents.hpp) with Qt window geometry and storage-facing layout values. SessionPreferences schedules and writes that composed snapshot; it delegates occurrence remapping to the portable value. Track browser widgets depend directly on BrowserViewState, rather than importing the Qt session codec's header.

The executable [main.cpp](../main.cpp) selects the concrete media/device drivers and supplies PlayerRuntimeConfig. PlayerApplication requires that explicit configuration and owns runtime policy rather than choosing FFmpeg or miniaudio. Controlled application tests supply their own output factory.

MetadataWriteService has explicit running, closing, retry and closed phases. It joins staging/commit workers before draining owner-thread observations, continues essential cleanup after an observer failure, and permits a failed final flush to be retried. Active observation retains callback identity. A terminal shutdown observer can release the service because active draining retains only its private lifetime sentinel and owned result.

Metadata binding callbacks may release their coordinator. Submission and commit continuations retain owned outcomes and check the private lifetime sentinel before touching the coordinator again. Handoff/durability/flush and persistence callables retain their identity through invocation or replacement. Required media and persistence drivers are validated before worker construction; cancellation captures tokens before invoking callbacks that can change owner lifetime.
