include("${CMAKE_CURRENT_LIST_DIR}/../../../cmake/headers.cmake")

add_library(wiola_quantities INTERFACE)

add_library(wiola::quantities ALIAS wiola_quantities)

target_compile_features(wiola_quantities INTERFACE cxx_std_23)

wiola_target_headers(wiola_quantities wiola HEADERS units.hpp)

if(WIOLA_BUILD_TESTS)
    add_subdirectory(tests)
endif()
