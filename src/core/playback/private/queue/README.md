# Playback queue

Playlist owns queue occurrences, indexed metadata and separate structural/metadata/selection revisions. The public QueueEntry value owns immutable occurrence identity/location and its learned metadata observations. Playlist converts input paths into entries and fallback titles; the prepared batch and next identity cursor are returned together before commit. PlaybackOrder owns permutation, repeat policy and the current position. These classes share one queue invariant and live together; none opens files, decodes, starts a worker or touches a device/frontend.

```mermaid
flowchart LR
    Session[Session commands] --> Playlist
    Metadata[Accepted metadata] --> Playlist
    Playlist --> Entry[Public QueueEntry value]
    Playlist --> Order[PlaybackOrder]
    Observers[Presentation observations] --> Playlist
```

Observers borrow one immutable metadata value instead of maintaining a forwarding method for every possible field. This keeps metadata evolution separate from QueueEntry identity/ownership. A nonempty PlaybackOrder always has a valid selected place; its own mutations preserve that invariant.

A queue occurrence is not its resource path or row index. Replacement allocates new identities; duplicates remain distinct. Metadata lookup uses occurrence identity, and unrelated revisions remain independent. Removing a selected occurrence invalidates selection even if its successor occupies the same row; removing a later occurrence preserves selection identity/revision. Set/append prepare their fallible state before committing so an allocation failure preserves the accepted queue and current selection.

The scoped internal target imports public media/playback values only. It has no transport, worker, DSP, codec, Qt or global source-root dependency. Session/presentation headers still name the private Playlist type until facade/presentation isolation is completed; they declare that real dependency directly. Queue implementation headers are not installed as public playback API.
