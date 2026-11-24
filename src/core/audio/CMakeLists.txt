include(GNUInstallDirs)

add_library(wiola_audio_output INTERFACE)
add_library(wiola::audio_output ALIAS wiola_audio_output)
set_target_properties(wiola_audio_output PROPERTIES EXPORT_NAME audio_output)
target_compile_features(wiola_audio_output INTERFACE cxx_std_23)
target_include_directories(
    wiola_audio_output
    INTERFACE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/public>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
target_link_libraries(
    wiola_audio_output
    INTERFACE wiola::borrowed_reference wiola::media
)

install(
    TARGETS wiola_audio_output
    EXPORT wiola_audio_targets
    COMPONENT wiola_audio
)
install(
    DIRECTORY public/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    COMPONENT wiola_audio
    FILES_MATCHING
    PATTERN "*.hpp"
)
install(
    EXPORT wiola_audio_targets
    FILE wiola_audio-targets.cmake
    NAMESPACE wiola::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/wiola_audio
    COMPONENT wiola_audio
)
install(
    FILES package.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/wiola_audio
    RENAME wiola_audio-config.cmake
    COMPONENT wiola_audio
)

if(WIOLA_BUILD_TESTS)
    add_subdirectory(tests)
endif()
