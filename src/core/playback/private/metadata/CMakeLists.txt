add_library(
    wiola_playback_metadata
    STATIC
    tag_reader.cpp
    metadata_coordinator.cpp
)
set_target_properties(
    wiola_playback_metadata
    PROPERTIES EXPORT_NAME playback_metadata
)
target_compile_features(wiola_playback_metadata PUBLIC cxx_std_23)
target_include_directories(
    wiola_playback_metadata
    PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/..>
)
target_link_libraries(
    wiola_playback_metadata
    PUBLIC wiola::media wiola::playback_api
    PRIVATE Threads::Threads
)
if(COMMAND wiola_apply_build_controls)
    wiola_apply_build_controls(wiola_playback_metadata)
endif()
