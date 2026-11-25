if(NOT WIOLA_BUILD_GUI)
    return()
endif()

add_library(wiola_qt_paths INTERFACE)
add_library(wiola::qt_paths ALIAS wiola_qt_paths)
target_compile_features(wiola_qt_paths INTERFACE cxx_std_23)
target_include_directories(
    wiola_qt_paths
    INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/.."
)
target_link_libraries(wiola_qt_paths INTERFACE Qt6::Core)

wiola_add_library(wiola_qt_settings SOURCES settings.cpp LIBS wiola::qt_paths)
target_link_libraries(wiola_qt_settings PUBLIC Qt6::Core wiola::audio_dsp)

add_subdirectory(storage)

if(WIOLA_BUILD_TESTS)
    add_subdirectory(tests)
endif()
