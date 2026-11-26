# Qt projection delivery

`make_track_projection` supplies a QtCore receiver to the portable TrackProjection workflow. The receiver is owned until the worker stops; closing its QObject context disables delivery. The factory verifies thread affinity before constructing Qt objects. Generations, cancellation, snapshots, worker execution and coalescing belong to `presentation/tracks`, with no second policy implementation here.

`wiola::qt_projection` links portable presentation and QtCore delivery, without Widgets. The [focused tests](tests) cover replacing blocked work, retained callback identity, context closure and destruction without late delivery. The `wiola_core_adapter_boundary` consumer also qualifies session/storage adapters without GUI/Widgets definitions. Widget table binding remains in the widgets owner.
