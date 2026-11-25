include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/headers.cmake")

add_library(wiola_media_core STATIC audio_decoder.cpp byte_stream.cpp)

add_library(wiola::media ALIAS wiola_media_core)

set_target_properties(wiola_media_core PROPERTIES CXX_EXTENSIONS OFF)

target_compile_features(wiola_media_core PUBLIC cxx_std_23)

wiola_target_headers(
    wiola_media_core
    wiola/media
    HEADERS
        artwork_reference.hpp
        audio_decoder.hpp
        byte_stream.hpp
        error.hpp
        metadata.hpp
        operation_context.hpp
        provider.hpp
        resource.hpp
        result.hpp
        stream_spec.hpp
)

target_link_libraries(wiola_media_core PUBLIC wiola::quantities)

# A standalone source consumer supplies its own build options.
if(COMMAND wiola_apply_build_controls)
    wiola_apply_build_controls(wiola_media_core)
endif()

if(WIOLA_BUILD_TESTS)
    add_subdirectory(tests)
endif()

if(NOT WIOLA_COMPONENT_BUILD)
    add_subdirectory(local)

    add_subdirectory(codecs)
endif()
