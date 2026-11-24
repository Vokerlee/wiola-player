include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/headers.cmake")

# Application clients can consume values without linking transport/worker implementation.
add_library(wiola_playback_api INTERFACE)
add_library(wiola::playback_api ALIAS wiola_playback_api)
set_target_properties(wiola_playback_api PROPERTIES EXPORT_NAME playback_api)
target_compile_features(wiola_playback_api INTERFACE cxx_std_23)
wiola_export_headers(
    wiola_playback_api
    wiola/playback
    wiola_playback
    HEADERS
        buffering/buffering_config.hpp
        error.hpp
        loading/open_result.hpp
        operation_outcome.hpp
        pipeline/output_factory.hpp
        queue/queue_entry.hpp
        queue/queue_entry_id.hpp
        queue/queue_view.hpp
        queue/repeat_mode.hpp
        session.hpp
        snapshot.hpp
        transport/state.hpp
        transport/transport_config.hpp
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
    EXPORT wiola_playback_targets
    FILE wiola_playback-targets.cmake
    NAMESPACE wiola::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/wiola_playback
    COMPONENT wiola_playback
)
configure_package_config_file(
    package.cmake.in
    "${CMAKE_CURRENT_BINARY_DIR}/wiola_playback-config.cmake"
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/wiola_playback
    NO_SET_AND_CHECK_MACRO
)
install(
    FILES "${CMAKE_CURRENT_BINARY_DIR}/wiola_playback-config.cmake"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/wiola_playback
    COMPONENT wiola_playback
)

# PCM transport storage and its render bridge are private playback implementation.
add_library(
    wiola_playback_buffer
    STATIC
    buffering/buffer_source.cpp
    buffering/relay.cpp
    buffering/shaped_source.cpp
)
set_target_properties(
    wiola_playback_buffer
    PROPERTIES EXPORT_NAME playback_buffer
)
target_compile_features(wiola_playback_buffer PUBLIC cxx_std_23)
target_include_directories(
    wiola_playback_buffer
    PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
)
target_link_libraries(
    wiola_playback_buffer
    PUBLIC wiola::audio_output wiola::borrowed_reference wiola::media
)
if(COMMAND wiola_apply_build_controls)
    wiola_apply_build_controls(wiola_playback_buffer)
endif()

find_package(Threads REQUIRED)
add_subdirectory(transport)
add_subdirectory(loading)
add_subdirectory(queue)
add_subdirectory(metadata)

add_subdirectory(pipeline)

add_library(wiola_playback STATIC session.cpp session_impl.cpp)
add_library(wiola::playback ALIAS wiola_playback)
set_target_properties(wiola_playback PROPERTIES EXPORT_NAME playback)
target_compile_features(wiola_playback PUBLIC cxx_std_23)
target_include_directories(
    wiola_playback
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}"
)
target_link_libraries(
    wiola_playback
    PUBLIC wiola::playback_api wiola::audio_output
    PRIVATE
        wiola_playback_pipeline
        wiola_playback_transport
        wiola_playback_loading
        wiola_playback_metadata
        wiola_playback_queue
)
if(COMMAND wiola_apply_build_controls)
    wiola_apply_build_controls(wiola_playback)
endif()

# Static implementation archives belong to this one runtime export; their headers stay private.
install(
    TARGETS
        wiola_playback
        wiola_playback_pipeline
        wiola_playback_transport
        wiola_playback_buffer
        wiola_playback_loading
        wiola_playback_metadata
        wiola_playback_queue
    EXPORT wiola_playback_runtime_targets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR} COMPONENT wiola_playback_runtime
)
install(
    EXPORT wiola_playback_runtime_targets
    FILE wiola_playback-runtime-targets.cmake
    NAMESPACE wiola::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/wiola_playback
    COMPONENT wiola_playback_runtime
)

if(WIOLA_BUILD_TESTS)
    add_subdirectory(tests)
endif()
