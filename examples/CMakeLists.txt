include_directories("..")

set(CMAKE_BUILD_TYPE Debug)
#set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O3")

add_executable(mitos_api_matmul mitos_api_matmul.cpp)
add_executable(matmul matmul.cpp)

target_link_libraries(mitos_api_matmul mitos mitoshooks)
target_link_libraries(matmul mitos mitoshooks dl)

if(Dyninst_FOUND)
    include_directories(${DYNINST_INCLUDE_DIR})
endif()

if(MPI_FOUND)
    include_directories(${MPI_INCLUDE_PATH})
    add_executable(mpi_matmul mpi_matmul.cpp)
    add_executable(mpi_jacobi mpi_jacobi.cpp)
    target_link_libraries(mpi_matmul mitos mitoshooks ${MPI_LIBRARIES})
    target_link_libraries(mpi_jacobi mitos mitoshooks ${MPI_LIBRARIES})
endif()

if(OpenMP_CXX_FOUND)
    # env OMP_TOOL_LIBRARIES=$(PWD)/libinit.so
    # OMP_TOOL_LIBRARIES=./../src/libmitoshooks.so
    include_directories(./../src)
    message(STATUS "Linking examples with mitos, mitoshooks and OpenMP")
    add_executable(omp_example omp_example.cpp)
    target_link_libraries(omp_example mitos mitoshooks OpenMP::OpenMP_CXX)
endif ()