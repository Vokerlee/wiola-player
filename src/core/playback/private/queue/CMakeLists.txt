# Queue ordering and metadata are independent of decoding, devices and worker scheduling.
add_library(
    wiola_playback_queue
    STATIC
    playback_order.cpp
    playlist.cpp
    track.cpp
)
target_compile_features(wiola_playback_queue PUBLIC cxx_std_23)
target_include_directories(
    wiola_playback_queue
    PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/..>
)
target_link_libraries(
    wiola_playback_queue
    PUBLIC wiola::media wiola::playback_api
)
if(COMMAND wiola_apply_build_controls)
    wiola_apply_build_controls(wiola_playback_queue)
endif()
