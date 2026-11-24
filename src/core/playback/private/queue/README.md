# Playback queue

Playlist owns queue occurrences, indexed metadata and separate structural/metadata/selection revisions. The public QueueEntry value owns immutable occurrence identity/location and its learned metadata observations. Playlist converts input paths into entries and fallback titles; the prepared batch and next identity cursor are returned together before commit. PlaybackOrder owns permutation, repeat policy and the current position. These classes share one queue invariant and live together; none opens files, decodes, starts a worker or touches a device/frontend.

```mermaid
flowchart LR
    Session[Session commands] --> Playlist
    Metadata[Accepted metadata] --> Playlist
    Playlist --> Entry[Public QueueEntry value]
    Playlist --> Order[PlaybackOrder]
    Observers[Presentation observations] --> View[Public QueueView port]
    Playlist --> View
```

Observers borrow one immutable metadata value instead of maintaining a forwarding method for every possible field. This keeps metadata evolution separate from QueueEntry identity/ownership. A nonempty PlaybackOrder always has a valid selected place; its own mutations preserve that invariant.

A queue occurrence is not its resource path or row index. Replacement allocates new identities; duplicates remain distinct. Metadata lookup uses occurrence identity, and unrelated revisions remain independent. Removing a selected occurrence invalidates selection even if its successor occupies the same row; removing a later occurrence preserves selection identity/revision. Set/append prepare their fallible state before committing so an allocation failure preserves the accepted queue and current selection.

Queue creation and accepted asynchronous metadata share one fallback-title rule. Session forwards the complete metadata value without interpreting its individual fields. A batch prepares every valid occurrence's description before changing any entry, artwork or revision; the commit requires nothrow metadata move assignment, enforced in C++. A failed preparation leaves the whole batch unchanged. Explicit `describe`/`illustrate` mutations retain their supplied-value semantics.

The scoped internal target imports public media/playback values only. It has no transport, worker, DSP, codec, Qt or global source-root dependency. Playlist implements the public live QueueView port directly; no copied container or forwarding adapter sits beside it. Application/frontend observations name QueueView, not this private type. Tests constructing this owner declare its direct dependency. Queue implementation headers are not installed as public playback API.

Borrowed entry spans/pointers are valid only until the next mutation attempt. A failed append can reserve new storage before a later allocation fails, even though accepted facts and revisions remain unchanged. Readers reacquire observations on each control turn; owned entry copies remain independent. QueueView keeps no storage or worker, and its lifetime is bounded by the owning Session.
