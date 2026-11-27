include("${CMAKE_CURRENT_LIST_DIR}/../../../cmake/headers.cmake")

add_library(wiola_shared_callback INTERFACE)

add_library(wiola::shared_callback ALIAS wiola_shared_callback)

target_compile_features(wiola_shared_callback INTERFACE cxx_std_23)

wiola_target_headers(
    wiola_shared_callback
    wiola/foundation
    HEADERS shared_callback.hpp
)

if(WIOLA_BUILD_TESTS)
    wiola_add_test(
        shared_callback_test
        SOURCES tests/shared_callback_test.cpp
        LIBS wiola::shared_callback
    )
endif()

if(PROJECT_IS_TOP_LEVEL AND BUILD_TESTING)
    add_executable(shared_callback_consumer_test tests/consumer.cpp)

    target_link_libraries(
        shared_callback_consumer_test
        PRIVATE wiola::shared_callback
    )

    wiola_apply_build_controls(shared_callback_consumer_test)

    add_test(NAME SharedCallback.Consumer COMMAND shared_callback_consumer_test)
endif()
