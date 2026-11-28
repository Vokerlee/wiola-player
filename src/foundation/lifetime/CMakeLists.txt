include("${CMAKE_CURRENT_LIST_DIR}/../../../cmake/headers.cmake")

find_package(Threads REQUIRED)

add_library(wiola_lifetime INTERFACE)

add_library(wiola::lifetime ALIAS wiola_lifetime)

target_compile_features(wiola_lifetime INTERFACE cxx_std_23)

target_link_libraries(wiola_lifetime INTERFACE Threads::Threads)

wiola_target_headers(
    wiola_lifetime
    wiola/foundation
    HEADERS
        lease.hpp
        lease_handle.hpp
        owner_thread_lifetime.hpp
        prepared_change.hpp
)

if(WIOLA_BUILD_TESTS)
    wiola_add_test(
        lifetime_test
        SOURCES
            tests/lease_test.cpp
            tests/owner_thread_lifetime_test.cpp
            tests/prepared_change_test.cpp
        LIBS wiola::lifetime wiola_test_allocation_failure
    )
endif()

if(PROJECT_IS_TOP_LEVEL AND BUILD_TESTING)
    add_executable(lifetime_consumer_test tests/consumer.cpp)

    target_link_libraries(lifetime_consumer_test PRIVATE wiola::lifetime)

    wiola_apply_build_controls(lifetime_consumer_test)

    add_test(NAME LifetimeGraph.Consumer COMMAND lifetime_consumer_test)
endif()
