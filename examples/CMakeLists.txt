include_directories("..")

set(CMAKE_BUILD_TYPE Debug)
#set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O3")

add_executable(api_matmul api_matmul.cpp)
add_executable(matmul matmul.cpp)

target_link_libraries(api_matmul mitos mitoshooks)

if(Dyninst_FOUND)
    include_directories(${DYNINST_INCLUDE_DIR})
endif()

if(MPI_FOUND)
    include_directories(${MPI_INCLUDE_PATH})
    add_executable(mpi_matmul mpi_matmul.cpp)
    target_link_libraries(mpi_matmul mitos mitoshooks ${MPI_LIBRARIES})
endif()

if(OpenMP_CXX_FOUND)
    # env OMP_TOOL_LIBRARIES=$(PWD)/libinit.so
    # OMP_TOOL_LIBRARIES=./../src/libmitoshooks.so
    include_directories(./../src)
    message(STATUS "Linking examples with mitos, mitoshooks and OpenMP")
    add_executable(openmp_matmul openmp_matmul.cpp)
    target_link_libraries(openmp_matmul mitos mitoshooks OpenMP::OpenMP_CXX)
endif ()