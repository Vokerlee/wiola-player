include(GNUInstallDirs)

add_library(wiola_media_core STATIC private/audio_decoder.cpp)
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
# Build controls are optional for an independent source consumer. An instrumented archive still
# exports its selected runtime options, never the project's private build targets.
foreach(build_control IN ITEMS wiola_coverage wiola_sanitizer wiola_warnings)
    if(TARGET ${build_control})
        target_link_libraries(
            wiola_media_core
            PRIVATE $<BUILD_INTERFACE:${build_control}>
        )
        get_target_property(
            runtime_options
            ${build_control}
            INTERFACE_LINK_OPTIONS
        )
        if(runtime_options)
            target_link_options(
                wiola_media_core
                INTERFACE "$<INSTALL_INTERFACE:${runtime_options}>"
            )
        endif()
    endif()
endforeach()

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
