# PCM transport storage and its render bridge are private playback implementation.
add_library(wiola_playback_buffer STATIC private/buffering/buffer_source.cpp)
target_compile_features(wiola_playback_buffer PUBLIC cxx_std_23)
target_include_directories(
    wiola_playback_buffer
    PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/private>
)
target_link_libraries(
    wiola_playback_buffer
    PUBLIC wiola::audio_output wiola::borrowed_reference wiola::media
)
foreach(build_control IN ITEMS wiola_coverage wiola_sanitizer wiola_warnings)
    if(TARGET ${build_control})
        target_link_libraries(wiola_playback_buffer PRIVATE ${build_control})
    endif()
endforeach()

if(WIOLA_BUILD_TESTS)
    add_subdirectory(tests/buffering)
endif()
