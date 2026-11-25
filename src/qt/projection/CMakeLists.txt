wiola_add_library(wiola_qt_projection SOURCES track_projection.cpp)
add_library(wiola::qt_projection ALIAS wiola_qt_projection)
target_link_libraries(
    wiola_qt_projection
    PUBLIC Qt6::Core wiola::presentation wiola::qt_delivery
)

add_executable(wiola_core_adapter_boundary EXCLUDE_FROM_ALL boundary.cpp)
target_link_libraries(
    wiola_core_adapter_boundary
    PRIVATE
        wiola::qt_projection
        wiola_session_preferences
        wiola::qt_library_store
        wiola_sanitizer
        wiola_coverage
)
