wiola_add_test(
    compatibility_corpus_test
    SOURCES
        main.cpp
        library_test.cpp
        session_test.cpp
        layout_test.cpp
        theme_test.cpp
        settings_test.cpp
    DATA "${CMAKE_CURRENT_SOURCE_DIR}/data"
    LIBS
        wiola::qt_library_store
        wiola_session_preferences
        wiola::qt_widgets
        wiola_qt_settings
)
