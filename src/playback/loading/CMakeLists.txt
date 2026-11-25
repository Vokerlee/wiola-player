add_library(wiola_playback_loading STATIC loader.cpp)

target_compile_features(wiola_playback_loading PUBLIC cxx_std_23)

target_include_directories(
    wiola_playback_loading
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_link_libraries(
    wiola_playback_loading
    PUBLIC wiola::media wiola::playback_api
    PRIVATE Threads::Threads
)

if(COMMAND wiola_apply_build_controls)
    wiola_apply_build_controls(wiola_playback_loading)
endif()
