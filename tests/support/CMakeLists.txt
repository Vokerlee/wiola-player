# TSan owns strong allocation replacements. Its normal behavior cases still run; scoped
# allocation-failure cases use these standard C++ replacements in other configurations.
if(ENABLE_TSAN)
    add_library(wiola_test_allocation_failure INTERFACE)

    target_compile_definitions(
        wiola_test_allocation_failure
        INTERFACE WIOLA_TEST_ALLOCATOR_REPLACEMENT=0
    )
else()
    add_library(wiola_test_allocation_failure STATIC allocation_failure.cpp)

    wiola_apply_build_controls(wiola_test_allocation_failure)

    target_compile_definitions(
        wiola_test_allocation_failure
        INTERFACE WIOLA_TEST_ALLOCATOR_REPLACEMENT=1
    )
endif()

target_include_directories(
    wiola_test_allocation_failure
    INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}"
)
