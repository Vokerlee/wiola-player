include(CMakeFindDependencyMacro)
find_dependency(Threads)
find_dependency(wiola_library CONFIG)
include("${CMAKE_CURRENT_LIST_DIR}/wiola_library_persistence-targets.cmake")
