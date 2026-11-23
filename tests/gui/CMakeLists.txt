wiola_add_test(
    gui_track_sort_test
    SOURCES tracks/track_sort_test.cpp
    LIBS wiola_gui
)
wiola_add_test(
    gui_track_sorter_test
    SOURCES tracks/track_sorter_test.cpp
    LIBS wiola_gui
)
wiola_add_test(
    gui_track_search_panel_test
    SOURCES tracks/track_search_panel_test.cpp
    LIBS wiola_gui Qt6::Test
)
wiola_add_test(
    gui_tag_filter_panel_test
    SOURCES tracks/tag_filter_panel_test.cpp
    LIBS wiola_gui Qt6::Test
)
wiola_add_test(
    gui_track_table_model_test
    SOURCES tracks/track_table_model_test.cpp
    LIBS wiola_gui Qt6::Test
)
wiola_add_test(
    gui_track_table_view_test
    SOURCES tracks/track_table_view_test.cpp
    LIBS wiola_gui Qt6::Test
)
wiola_add_test(
    gui_now_playing_document_test
    SOURCES documents/now_playing_document_test.cpp
    LIBS wiola_gui Qt6::Test
)
wiola_add_test(
    gui_library_navigation_test
    SOURCES shell/library_navigation_test.cpp
    LIBS wiola_gui wiola_library
)
wiola_add_test(
    gui_metadata_conflict_panel_test
    SOURCES metadata/metadata_conflict_panel_test.cpp
    LIBS wiola_gui
)
wiola_add_test(
    gui_standard_metadata_editor_test
    SOURCES metadata/standard_metadata_editor_test.cpp
    LIBS wiola_gui
)
wiola_add_test(
    gui_custom_tag_editor_test
    SOURCES metadata/custom_tag_editor_test.cpp
    LIBS wiola_gui
)
wiola_add_test(
    gui_track_inspector_test
    SOURCES metadata/track_inspector_test.cpp
    LIBS wiola_gui
)
wiola_add_test(
    gui_saved_playlist_manager_test
    SOURCES shell/saved_playlist_manager_test.cpp
    LIBS wiola_gui wiola_saved_playlists
)
wiola_add_test(
    gui_workspace_document_test
    SOURCES documents/workspace_document_test.cpp
    LIBS wiola_gui wiola_workspace_scans
)
wiola_add_test(
    gui_saved_playlist_document_test
    SOURCES documents/saved_playlist_document_test.cpp
    LIBS wiola_gui wiola_library
)

wiola_add_test(
    gui_main_window_test
    SOURCES shell/main_window_test.cpp
    LIBS wiola_gui
)

if(ENABLE_TSAN)
    target_compile_definitions(
        gui_track_table_view_test
        PRIVATE WIOLA_TEST_TSAN
    )
    target_compile_definitions(gui_main_window_test PRIVATE WIOLA_TEST_TSAN)
endif()
wiola_add_test(
    gui_player_actions_test
    SOURCES shell/player_actions_test.cpp
    LIBS wiola_gui
)
wiola_add_test(gui_theme_test SOURCES appearance/theme_test.cpp LIBS wiola_gui)
wiola_add_test(
    gui_theme_editor_test
    SOURCES appearance/theme_editor_test.cpp
    LIBS wiola_gui
)
wiola_add_test(
    gui_layout_document_test
    SOURCES layout/layout_document_test.cpp
    LIBS wiola_gui
)
wiola_add_test(
    gui_button_layout_editor_test
    SOURCES layout/editor/button_layout_editor_test.cpp
    LIBS wiola_gui Qt6::Test
)
if(ENABLE_TSAN)
    target_compile_definitions(
        gui_button_layout_editor_test
        PRIVATE WIOLA_TEST_TSAN
    )
endif()
wiola_add_test(
    gui_layout_editor_test
    SOURCES layout/layout_editor_test.cpp
    LIBS wiola_gui
)
wiola_add_test(
    gui_layout_builder_test
    SOURCES layout/layout_builder_test.cpp
    LIBS wiola_gui
)
wiola_add_test(
    gui_main_window_shell_test
    SOURCES main_window_shell_test.cpp
    LIBS wiola_gui wiola_player_application
)

wiola_add_test(
    gui_document_registry_test
    SOURCES documents/document_registry_test.cpp
    LIBS wiola_gui
)

wiola_add_test(
    gui_document_host_test
    SOURCES shell/document_host_test.cpp
    LIBS wiola_gui
)

wiola_add_test(
    gui_track_row_test
    SOURCES tracks/track_row_test.cpp
    LIBS wiola_gui
)

wiola_add_test(
    gui_track_browser_test
    SOURCES tracks/track_browser_test.cpp
    LIBS wiola_gui
)

wiola_add_test(
    gui_artwork_presentation_test
    SOURCES appearance/artwork_presentation_test.cpp
    LIBS wiola_gui
)

wiola_add_test(
    gui_layout_session_test
    SOURCES layout/layout_session_test.cpp
    LIBS wiola_gui
)

wiola_add_test(
    gui_browser_view_state_test
    SOURCES tracks/browser_view_state_test.cpp
    LIBS wiola_gui Qt6::Test
)

wiola_add_test(
    gui_theme_manager_test
    SOURCES appearance/theme_manager_test.cpp
    LIBS wiola_gui
)

wiola_add_test(
    gui_player_sections_test
    SOURCES controls/player_sections_test.cpp
    LIBS wiola_gui
)

wiola_add_test(
    gui_document_command_router_test
    SOURCES shell/document_command_router_test.cpp
    LIBS wiola_gui Qt6::Test
)

wiola_add_test(
    gui_track_projection_test
    SOURCES tracks/track_projection_test.cpp
    LIBS wiola_gui
)

# Explicit benchmark target: measurements are artifacts, not wall-clock CI pass/fail assertions.
add_executable(
    gui_projection_benchmark
    EXCLUDE_FROM_ALL
    benchmarks/projection_benchmark.cpp
)
target_link_libraries(gui_projection_benchmark PRIVATE wiola_gui)

wiola_add_test(
    gui_reader_contract_test
    SOURCES contracts/reader_contract_test.cpp
    LIBS wiola_gui wiola_session_preferences wiola_persistence_qt
)

foreach(scale IN ITEMS 1 1.25 1.5 2)
    add_test(
        NAME GuiScaling.LayoutEditor.${scale}
        COMMAND
            ${CMAKE_COMMAND} -E env QT_SCALE_FACTOR=${scale}
            $<TARGET_FILE:gui_button_layout_editor_test>
            --gtest_filter=ButtonLayoutEditor.TextEditingShortcutsDoNotDeleteComponentsAndSmallWindowsCanScroll
    )
endforeach()

wiola_add_test(
    gui_desktop_journey_test
    SOURCES journeys/desktop_journey_test.cpp
    LIBS wiola_gui wiola_player_application Qt6::Test
)
qt_finalize_target(gui_desktop_journey_test)

wiola_add_test(
    gui_interaction_fixture_test
    SOURCES contracts/interaction_fixture_test.cpp
    LIBS wiola_gui Qt6::Test
)
