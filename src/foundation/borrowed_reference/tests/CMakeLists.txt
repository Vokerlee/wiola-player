cmake_minimum_required(VERSION 3.28)
project(borrowed_reference_consumer LANGUAGES CXX)
find_package(wiola_borrowed_reference CONFIG REQUIRED)
add_library(public_consumer OBJECT consumer.cpp)
target_link_libraries(public_consumer PRIVATE wiola::borrowed_reference)
add_library(private_consumer OBJECT consumer.cpp)
target_link_libraries(private_consumer PRIVATE wiola::borrowed_reference)
target_compile_definitions(
    private_consumer
    PRIVATE WIOLA_PRIVATE_HEADER="${WIOLA_PRIVATE_HEADER}"
)
