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

include(GoogleTest)

foreach(control IN ITEMS volume equalizer)
    add_executable(qt_${control}_settings_test ${control}_settings_test.cpp)

    target_link_libraries(
        qt_${control}_settings_test
        PRIVATE wiola::qt_settings wiola::playback_service GTest::gtest_main
    )

    target_include_directories(
        qt_${control}_settings_test
        PRIVATE "${PROJECT_SOURCE_DIR}/tests"
    )

    wiola_apply_build_controls(qt_${control}_settings_test)

    gtest_discover_tests(qt_${control}_settings_test)
endforeach()
