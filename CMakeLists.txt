cmake_minimum_required(VERSION 3.15)
project(Mitos C CXX)

set (CMAKE_CXX_STANDARD 17)
set (CXX_STANDARD_REQUIRED ON)

# Version info
set(MITOS_MAJOR_VERSION 0)
set(MITOS_MINOR_VERSION 8)
set(MITOS_PATCH_VERSION 1)
set(MITOS_VERSION ${MITOS_MAJOR_VERSION}.${MITOS_MINOR_VERSION}.${MITOS_PATCH_VERSION})
set(IBS_TYPE "OFF" "IBS_FETCH" "IBS_OP" CACHE STRING "Enabling Instruction-Based Sampling")
option(MITOSHOOKS_OPEN_MP "Enable Mitoshooks OpenMP-Routing" OFF)
set_property(CACHE IBS_TYPE PROPERTY STRINGS
        "OFF" "IBS_FETCH" "IBS_OP")

set(IBS_SAMPLING_MODE "ALL_ON" "THREAD_MIGRATION"  CACHE STRING "ALL_ON: Run sampling on all cores; THREAD_MIGRATION: Run single sampler that migrates core after detecting a core migration; ")
set_property(CACHE IBS_SAMPLING_MODE PROPERTY STRINGS
        "ALL_ON" "ALL_SELECTIVE_ON" "THREAD_MIGRATION")

if ((IBS_TYPE STREQUAL "IBS_FETCH") OR (IBS_TYPE STREQUAL "IBS_OP"))
    # define sampling mode for IBS
    if (IBS_SAMPLING_MODE STREQUAL "ALL_ON")
        message(STATUS "IBS Sampling Mode ALL_ON Selected: Monitor all cores")
        add_definitions(-DUSE_IBS_ALL_ON)
    elseif (IBS_SAMPLING_MODE STREQUAL "THREAD_MIGRATION")
        message(STATUS "IBS Sampling Mode THREAD_MIGRATION Selected: Monitor one thread; Migrate sampler at runtime from one core to another")
        add_definitions(-DUSE_IBS_THREAD_MIGRATION)
    else()
        message(STATUS "IBS not set")
    endif ()
endif ()

if (IBS_TYPE STREQUAL "IBS_FETCH")
    message(STATUS "Switch IBS_FETCH ON")
    add_definitions(-DUSE_IBS_FETCH)
elseif (IBS_TYPE STREQUAL "IBS_OP")
    message(STATUS "Switch IBS_OP On")
    add_definitions(-DUSE_IBS_OP)
else()
    message(STATUS "IBS not set")
endif ()



#set_property(CACHE IBS_MODE PROPERTY STRINGS
#        "OFF" "IBS_FETCH" "IBS_OP")

# Included cmake files
set(CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake;${CMAKE_MODULE_PATH}")

# Packages required
find_package(Threads REQUIRED)
find_package(PerfEvents REQUIRED)

# Non-essential packages
find_package(OpenMP)
find_package(MPI)
find_package(Dyninst QUIET COMPONENTS SymtabAPI InstructionAPI ParseAPI)
if(Dyninst_FOUND)
    message(STATUS "Found Dyninst: ${DYNINST_INCLUDE_DIR}")
else()
    message(STATUS "Could NOT find Dyninst")
endif()

# C-Defines for non-essential packages
if(MPI_FOUND)
    add_definitions(-DUSE_MPI)
endif()


if(OPENMP_FOUND)
    message(STATUS "Found OpenMP")
    if (MITOSHOOKS_OPEN_MP)
        message(STATUS "Mitoshooks OpenMP Enabled")
        add_definitions(-DUSE_OPEN_MP)
    else()
        message(STATUS "Mitoshooks OpenMP Disabled")
    endif ()
else()
    message(STATUS "OpenMP not Found")
endif ()

if(Dyninst_FOUND)
    add_definitions(-DUSE_DYNINST)
endif()

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${OpenMP_C_FLAGS}")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${OpenMP_CXX_FLAGS}")

# Subdirectories
add_subdirectory(src)
add_subdirectory(examples)

# Export the package
export(PACKAGE Mitos)

# Install cmake export
install(EXPORT mitos-targets DESTINATION lib/Mitos)
install(FILES mitos-config.cmake DESTINATION lib/Mitos)
