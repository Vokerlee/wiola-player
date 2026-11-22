wiola_add_test(gui_track_sort_test SOURCES track_sort_test.cpp LIBS wiola_gui)
wiola_add_test(
    gui_track_sorter_test
    SOURCES track_sorter_test.cpp
    LIBS wiola_gui
)
wiola_add_test(
    gui_track_search_panel_test
    SOURCES track_search_panel_test.cpp
    LIBS wiola_gui
)
wiola_add_test(
    gui_tag_filter_panel_test
    SOURCES tag_filter_panel_test.cpp
    LIBS wiola_gui
)
wiola_add_test(
    gui_track_table_model_test
    SOURCES track_table_model_test.cpp
    LIBS wiola_gui
)
wiola_add_test(
    gui_track_table_view_test
    SOURCES track_table_view_test.cpp
    LIBS wiola_gui Qt6::Test
)
wiola_add_test(
    gui_now_playing_document_test
    SOURCES now_playing_document_test.cpp
    LIBS wiola_gui
)
wiola_add_test(
    gui_library_navigation_test
    SOURCES library_navigation_test.cpp
    LIBS wiola_gui wiola_library
)
wiola_add_test(
    gui_metadata_conflict_panel_test
    SOURCES metadata_conflict_panel_test.cpp
    LIBS wiola_gui
)
wiola_add_test(
    gui_standard_metadata_editor_test
    SOURCES standard_metadata_editor_test.cpp
    LIBS wiola_gui
)
wiola_add_test(
    gui_custom_tag_editor_test
    SOURCES custom_tag_editor_test.cpp
    LIBS wiola_gui
)
wiola_add_test(
    gui_track_inspector_test
    SOURCES track_inspector_test.cpp
    LIBS wiola_gui
)
wiola_add_test(
    gui_saved_playlist_storage_test
    SOURCES saved_playlist_storage_test.cpp
    LIBS wiola_gui wiola_library
)
wiola_add_test(
    gui_saved_playlist_manager_test
    SOURCES saved_playlist_manager_test.cpp
    LIBS wiola_gui wiola_saved_playlists
)
wiola_add_test(
    gui_workspace_document_test
    SOURCES workspace_document_test.cpp
    LIBS wiola_gui wiola_workspace_scans
)
wiola_add_test(
    gui_saved_playlist_document_test
    SOURCES saved_playlist_document_test.cpp
    LIBS wiola_gui wiola_library
)
wiola_add_test(
    gui_playlist_control_test
    SOURCES playlist_control_test.cpp
    LIBS wiola_gui
)
wiola_add_test(
    gui_equalizer_control_test
    SOURCES equalizer_control_test.cpp
    LIBS wiola_gui
)
wiola_add_test(
    gui_volume_control_test
    SOURCES volume_control_test.cpp
    LIBS wiola_gui
)
wiola_add_test(gui_main_window_test SOURCES main_window_test.cpp LIBS wiola_gui)

if(ENABLE_TSAN)
    target_compile_definitions(
        gui_track_table_view_test
        PRIVATE WIOLA_TEST_TSAN
    )
    target_compile_definitions(gui_main_window_test PRIVATE WIOLA_TEST_TSAN)
endif()
wiola_add_test(
    gui_player_presenter_test
    SOURCES player_presenter_test.cpp
    LIBS wiola_gui
)
wiola_add_test(
    gui_player_actions_test
    SOURCES player_actions_test.cpp
    LIBS wiola_gui
)
wiola_add_test(
    gui_library_json_test
    SOURCES library_json_test.cpp
    LIBS wiola_gui wiola_library
)
wiola_add_test(
    gui_library_storage_test
    SOURCES library_storage_test.cpp
    LIBS wiola_gui wiola_library
)
wiola_add_test(gui_theme_test SOURCES theme_test.cpp LIBS wiola_gui)
wiola_add_test(
    gui_theme_editor_test
    SOURCES theme_editor_test.cpp
    LIBS wiola_gui
)
wiola_add_test(
    gui_layout_document_test
    SOURCES layout_document_test.cpp
    LIBS wiola_gui
)
wiola_add_test(
    gui_button_layout_editor_test
    SOURCES button_layout_editor_test.cpp
    LIBS wiola_gui
)
if(ENABLE_TSAN)
    target_compile_definitions(
        gui_button_layout_editor_test
        PRIVATE WIOLA_TEST_TSAN
    )
endif()
wiola_add_test(
    gui_layout_editor_test
    SOURCES layout_editor_test.cpp
    LIBS wiola_gui
)
wiola_add_test(
    gui_layout_builder_test
    SOURCES layout_builder_test.cpp
    LIBS wiola_gui
)
wiola_add_test(
    gui_main_window_shell_test
    SOURCES main_window_shell_test.cpp
    LIBS wiola_gui
)
