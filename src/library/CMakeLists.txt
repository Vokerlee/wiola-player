include(GNUInstallDirs)
include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/headers.cmake")
find_package(Threads REQUIRED)

add_library(
    wiola_library
    STATIC
    commit_receipt.cpp
    in_memory_repository.cpp
    library_changes.cpp
    metadata.cpp
    metadata_patch.cpp
    relocation.cpp
    repository_transaction.cpp
    search.cpp
    tags.cpp
)
add_library(wiola::library ALIAS wiola_library)
set_target_properties(
    wiola_library
    PROPERTIES EXPORT_NAME library CXX_EXTENSIONS OFF
)
target_compile_features(wiola_library PUBLIC cxx_std_23)
wiola_export_headers(
    wiola_library
    wiola/library
    wiola_library
    HEADERS
        commit_receipt.hpp
        identity.hpp
        in_memory_repository.hpp
        library_changes.hpp
        metadata.hpp
        metadata_patch.hpp
        records.hpp
        relocation.hpp
        repository.hpp
        repository_transaction.hpp
        revision_delta.hpp
        search.hpp
        snapshot.hpp
        tags.hpp
)
target_link_libraries(wiola_library PUBLIC wiola::media Threads::Threads)
if(COMMAND wiola_apply_build_controls)
    wiola_apply_build_controls(wiola_library)
endif()

install(
    TARGETS wiola_library
    EXPORT wiola_library_targets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR} COMPONENT wiola_library
)
install(
    EXPORT wiola_library_targets
    FILE wiola_library-targets.cmake
    NAMESPACE wiola::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/wiola_library
    COMPONENT wiola_library
)
install(
    FILES package.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/wiola_library
    RENAME wiola_library-config.cmake
    COMPONENT wiola_library
)

if(WIOLA_BUILD_TESTS)
    add_subdirectory(tests)
endif()
