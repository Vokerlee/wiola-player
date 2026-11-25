wiola_add_test(
    gui_library_json_test
    SOURCES library_json_test.cpp
    LIBS wiola::qt_library_store
)
wiola_add_test(
    gui_library_storage_test
    SOURCES library_storage_test.cpp
    LIBS wiola::qt_library_store
)
wiola_add_test(
    gui_saved_playlist_storage_test
    SOURCES repository_storage_test.cpp
    LIBS wiola::qt_library_store
)

wiola_add_test(
    qt_snapshot_writer_test
    SOURCES snapshot_writer_test.cpp
    LIBS wiola::qt_library_store
)
