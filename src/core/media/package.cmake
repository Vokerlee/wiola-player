include(CMakeFindDependencyMacro)
find_dependency(wiola_quantities CONFIG)
include("${CMAKE_CURRENT_LIST_DIR}/wiola_media-targets.cmake")
