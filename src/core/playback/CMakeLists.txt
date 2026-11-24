include(GNUInstallDirs)

# Application clients can consume values without linking transport/worker implementation.
add_library(wiola_playback_api INTERFACE)
add_library(wiola::playback_api ALIAS wiola_playback_api)
set_target_properties(wiola_playback_api PROPERTIES EXPORT_NAME playback_api)
target_compile_features(wiola_playback_api INTERFACE cxx_std_23)
target_include_directories(
    wiola_playback_api
    INTERFACE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/public>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
target_link_libraries(
    wiola_playback_api
    INTERFACE wiola::media wiola::quantities
)
install(
    TARGETS wiola_playback_api
    EXPORT wiola_playback_targets
    COMPONENT wiola_playback
)
install(
    DIRECTORY public/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    COMPONENT wiola_playback
    FILES_MATCHING
    PATTERN "*.hpp"
)
install(
    EXPORT wiola_playback_targets
    FILE wiola_playback-targets.cmake
    NAMESPACE wiola::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/wiola_playback
    COMPONENT wiola_playback
)
install(
    FILES package.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/wiola_playback
    RENAME wiola_playback-config.cmake
    COMPONENT wiola_playback
)

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
if(COMMAND wiola_apply_build_controls)
    wiola_apply_build_controls(wiola_playback_buffer)
endif()

find_package(Threads REQUIRED)
add_subdirectory(private/transport)
add_subdirectory(private/loading)
add_subdirectory(private/queue)

if(WIOLA_BUILD_TESTS)
    add_subdirectory(tests)
endif()
