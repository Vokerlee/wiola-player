# Projection lifecycle contracts

`qt_track_projection_test` uses QCoreApplication and explicit worker gates. It checks that a new query supersedes blocked work and that shutdown releases owned inputs without delivering to a destroyed receiver. It links the QtCore projection target directly; a widget application is not needed.
