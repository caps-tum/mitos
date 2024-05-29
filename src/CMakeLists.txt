# Include dirs required
include_directories(${MPI_INCLUDE_PATH})
include_directories(${DYNINST_INCLUDE_DIR})

# If VERBOSITY is not defined via command line, use default
if(DEFINED VERBOSITY)
    set(VERBOSITY ${VERBOSITY} CACHE STRING "Set verbosity level")
    message(STATUS "VERBOSITY set to ${VERBOSITY}")
    add_definitions(-DVERBOSITY=${VERBOSITY})
endif()

# Source and header files
set(MITOS_SRC_FILES procsmpl.cpp mmap_processor.cpp mattr.cpp mitosoutput.cpp Mitos.cpp)
set(MITOS_HDR_FILES procsmpl.h mmap_processor.h mattr.h Mitos.h)
set(MITOS_HDR_PUBLIC Mitos.h)
set(MITOSHOOKS_SRC_FILES mitoshooks.cpp)

# Libraries (created)
add_library(mitos SHARED ${MITOS_SRC_FILES})
add_library(mitoshooks SHARED ${MITOSHOOKS_SRC_FILES} ${MITOS_SRC_FILES})

# Binaries
add_executable(mitosrun mitosrun.cpp)
add_executable(mitos_omp_post_process mitos_omp_post_process.cpp)

# Linkage
target_link_libraries(mitos mitoshooks numa hwloc ${CMAKE_THREAD_LIBS_INIT} stdc++fs)
target_link_libraries(mitosrun mitos)
target_link_libraries(mitos_omp_post_process mitos common)
target_link_libraries(mitoshooks dl)

if(MPI_FOUND)
    target_link_libraries(mitos ${MPI_LIBRARIES})
endif()

if(Dyninst_FOUND)
    target_link_libraries(mitos symtabAPI instructionAPI parseAPI)
endif()


# Install
install(FILES ${MITOS_HDR_PUBLIC}
        DESTINATION "include/Mitos")
install(TARGETS mitosrun
        EXPORT mitos-targets
        DESTINATION bin)
install(TARGETS mitos_omp_post_process
        EXPORT mitos-targets
        DESTINATION bin) 
install(TARGETS mitos
        EXPORT mitos-targets
        DESTINATION lib)
install(TARGETS mitoshooks
        EXPORT mitos-targets
        DESTINATION lib)
