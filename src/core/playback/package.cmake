include(CMakeFindDependencyMacro)
find_dependency(wiola_media CONFIG)
include("${CMAKE_CURRENT_LIST_DIR}/wiola_playback-targets.cmake")
