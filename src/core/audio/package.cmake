include(CMakeFindDependencyMacro)
find_dependency(wiola_borrowed_reference CONFIG)
find_dependency(wiola_media CONFIG)
include("${CMAKE_CURRENT_LIST_DIR}/wiola_audio-targets.cmake")
