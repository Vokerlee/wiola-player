# Qt projection delivery

TrackProjection adapts the portable view pipeline to QtCore delivery. It owns one active query and one replaceable pending query; generations identify the newest request. Inputs own rows/search facts rather than borrowing a live repository or Qt item model. Completed results coalesce before delivery through the shared Qt endpoint.

`wiola::qt_projection` links portable presentation and QtCore delivery, without Widgets. The [focused tests](tests) cover replacing blocked work, owned input facts, cancellation and destruction without late receiver delivery. The `wiola_core_adapter_boundary` consumer also qualifies session/storage adapters without GUI/Widgets definitions. Widget table binding remains in the widgets owner.
