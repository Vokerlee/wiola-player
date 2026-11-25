include(GoogleTest)

function(wiola_library_add_test name)
    cmake_parse_arguments(PARSE_ARGV 1 arg "" "" "SOURCES;LIBS")

    add_executable(${name} ${arg_SOURCES})

    target_link_libraries(
        ${name}
        PRIVATE
            wiola::library
            GTest::gtest_main
            wiola_coverage
            wiola_sanitizer
            wiola_warnings
            ${arg_LIBS}
    )

    set_target_properties(
        ${name}
        PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    )

    gtest_discover_tests(${name})
endfunction()

wiola_library_add_test(
    library_metadata_test
    SOURCES metadata_test.cpp
)

wiola_library_add_test(
    library_identity_test
    SOURCES identity_test.cpp
)

wiola_library_add_test(library_records_test SOURCES records_test.cpp)

wiola_library_add_test(library_tags_test SOURCES tags_test.cpp)

wiola_library_add_test(
    library_search_test
    SOURCES search_test.cpp
    LIBS wiola_test_allocation_failure
)

wiola_library_add_test(
    library_in_memory_repository_test
    SOURCES in_memory_repository_test.cpp
)

wiola_library_add_test(
    library_relocation_test
    SOURCES relocation_test.cpp
)

wiola_library_add_test(
    library_repository_transaction_test
    SOURCES repository_transaction_test.cpp
    LIBS wiola_test_allocation_failure
)

wiola_library_add_test(
    library_metadata_patch_test
    SOURCES metadata_patch_test.cpp
)

wiola_library_add_test(
    library_changes_test
    SOURCES library_changes_test.cpp
)

wiola_library_add_test(
    library_commit_receipt_test
    SOURCES commit_receipt_test.cpp
)

wiola_library_add_test(
    library_repository_restore_test
    SOURCES repository_restore_test.cpp
)

add_executable(library_consumer_test "${CMAKE_CURRENT_SOURCE_DIR}/consumer.cpp")

target_link_libraries(library_consumer_test PRIVATE wiola::library)

wiola_apply_build_controls(library_consumer_test)

add_test(NAME Library.Consumer COMMAND library_consumer_test)
