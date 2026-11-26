# Qt Widgets frontend

This owner contains the concrete window, controls, documents, track bindings, layout and theme rendering. `wiola::qt_widgets` adapts portable application/presentation values and the QtCore projection service to widgets. Runtime pumping, persistence completion and decoder/device construction belong to their supplied owners.

The [widget tests](tests) exercise binding, selection, accessibility, painting, layouts and process journeys beside this frontend. Widget declarations should use the smallest complete types or forward declarations they require; implementation includes belong in the corresponding cpp when only a borrowed collaborator is declared. Source guides under each mechanism describe additional local lifetime rules.

SavedPlaylistManager presents an already accepted command outcome. Its binding guards widget lifetime after command completion, navigation changes and receipt observation. Closing the view during those callbacks preserves the command result and stops further presentation. An active changed observer retains its callable state until it returns, including when it closes the manager itself.
