wiola_add_test(
    gui_playlist_model_test
    SOURCES playlist_model_test.cpp
    LIBS wiola_gui
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
    gui_qt_path_adapter_test
    SOURCES qt_path_adapter_test.cpp
    LIBS wiola_gui
)
wiola_add_test(
    gui_volume_control_test
    SOURCES volume_control_test.cpp
    LIBS wiola_gui
)
wiola_add_test(gui_main_window_test SOURCES main_window_test.cpp LIBS wiola_gui)

if(ENABLE_TSAN)
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
wiola_add_test(
    gui_main_window_shell_test
    SOURCES main_window_shell_test.cpp
    LIBS wiola_gui
)
