include(CMakeFindDependencyMacro)
find_dependency(Threads)
find_dependency(wiola_media CONFIG)
include("${CMAKE_CURRENT_LIST_DIR}/wiola_library-targets.cmake")
