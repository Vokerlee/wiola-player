include(headers)

add_library(wiola_qt_settings STATIC settings.cpp)

add_library(wiola::qt_settings ALIAS wiola_qt_settings)

set_target_properties(wiola_qt_settings PROPERTIES CXX_EXTENSIONS OFF)

target_link_libraries(
    wiola_qt_settings
    PUBLIC Qt6::Core wiola::audio_dsp wiola::playback_preferences_api
    PRIVATE wiola::qt_paths
)

wiola_target_headers(wiola_qt_settings wiola/qt/storage HEADERS settings.hpp)

wiola_apply_build_controls(wiola_qt_settings)

wiola_add_library(
    wiola_qt_files
    SOURCES atomic_file_store.cpp
    LIBS wiola::qt_paths
)

add_library(wiola::qt_files ALIAS wiola_qt_files)

target_link_libraries(wiola_qt_files PUBLIC Qt6::Core)

wiola_add_library(
    wiola_qt_library_store
    SOURCES library_json.cpp library_store.cpp snapshot_writer.cpp
    LIBS wiola::qt_paths
)

add_library(wiola::qt_library_store ALIAS wiola_qt_library_store)

target_link_libraries(
    wiola_qt_library_store
    PUBLIC Qt6::Core wiola::qt_files wiola::library wiola::snapshot_writer
)
