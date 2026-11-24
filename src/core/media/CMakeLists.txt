include(GNUInstallDirs)

add_library(
    wiola_media_core
    STATIC
    private/audio_decoder.cpp
    private/byte_stream.cpp
)
add_library(wiola::media ALIAS wiola_media_core)
set_target_properties(
    wiola_media_core
    PROPERTIES EXPORT_NAME media CXX_EXTENSIONS OFF
)
target_compile_features(wiola_media_core PUBLIC cxx_std_23)
target_include_directories(
    wiola_media_core
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/public>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
target_link_libraries(wiola_media_core PUBLIC wiola::quantities)
# A standalone source consumer supplies its own build options.
if(COMMAND wiola_apply_build_controls)
    wiola_apply_build_controls(wiola_media_core)
endif()

install(
    TARGETS wiola_media_core
    EXPORT wiola_media_targets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR} COMPONENT wiola_media
)
install(
    DIRECTORY public/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    COMPONENT wiola_media
    FILES_MATCHING
    PATTERN "*.hpp"
)
install(
    EXPORT wiola_media_targets
    FILE wiola_media-targets.cmake
    NAMESPACE wiola::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/wiola_media
    COMPONENT wiola_media
)
install(
    FILES package.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/wiola_media
    RENAME wiola_media-config.cmake
    COMPONENT wiola_media
)

if(WIOLA_BUILD_TESTS)
    add_subdirectory(tests)
endif()
