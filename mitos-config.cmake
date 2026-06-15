cmake_minimum_required(VERSION 3.20)
include(CMakeFindDependencyMacro)

find_dependency(Threads REQUIRED)
find_dependency(Dyninst 13 COMPONENTS symtabAPI instructionAPI parseAPI)
find_dependency(PAPI REQUIRED)
find_dependency(sys-sage REQUIRED)
# Only if Mitos was built with MPI support:
find_dependency(MPI)

include("${CMAKE_CURRENT_LIST_DIR}/mitos-targets.cmake")
