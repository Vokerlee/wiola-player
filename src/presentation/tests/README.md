# Portable presentation contracts

These tests exercise the presentation layer without Qt, widgets, audio hardware or a filesystem scan. They use the actual in-memory catalog and playback queue. Tests stay beside the portable policy so another frontend can reuse the same behavior.

| Target | Contract |
| --- | --- |
| `presentation_track_view_test` | Owned metadata, Unicode filename fallback, normalized filters, search snapshot/index precedence, cancellation and document command capabilities. |
| `presentation_queue_rows_test` | Distinct queue occurrences, structural and selection updates, incremental metadata patches, catalog identity discovery and patch coalescing. |
| `presentation_saved_playlist_rows_test` | Duplicate saved occurrences share metadata while retaining separate identities; reconciliation rebuilds lookup indexes. |
| `presentation_workspace_rows_test` | Scan generation admission, duplicate suppression, completion-only pruning, cancellation, availability and relocation. |
| `presentation_track_sort_test` | Typed metadata/custom-tag sort values and missing facts. |
| `presentation_track_sorter_test` | Multi-key ordering, missing durations, deterministic identity tie breaks and bounded value preparation. |
| `presentation_session_documents_test` | Document identity, owned browser snapshots and queue occurrence remapping. |
| `presentation_track_projection_test` | Worker cancellation, coalesced owner-thread delivery, observer lifetime, failure and reentrancy. |

Build the target for the policy being changed and run its discovered CTest cases. Frontend model, selection, painting and journey tests remain with their bindings; they check behavior beyond these portable contracts.
