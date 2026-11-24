include(GNUInstallDirs)
include("${CMAKE_CURRENT_LIST_DIR}/../../../cmake/headers.cmake")

add_library(wiola_quantities INTERFACE)
add_library(wiola::quantities ALIAS wiola_quantities)
set_target_properties(wiola_quantities PROPERTIES EXPORT_NAME quantities)
target_compile_features(wiola_quantities INTERFACE cxx_std_23)
wiola_export_headers(wiola_quantities wiola wiola_quantities HEADERS units.hpp)
install(
    TARGETS wiola_quantities
    EXPORT wiola_quantities_targets
    COMPONENT wiola_quantities
)
install(
    EXPORT wiola_quantities_targets
    FILE wiola_quantities-config.cmake
    NAMESPACE wiola::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/wiola_quantities
    COMPONENT wiola_quantities
)

if(WIOLA_BUILD_TESTS)
    add_subdirectory(tests)
endif()
