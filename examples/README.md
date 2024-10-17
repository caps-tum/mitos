# Examples

The examples in this directory show usage for different ways of using mitos. The executables will be installed in the `build/examples/` directory.

1. `matmul.cpp`: For Running with `mitosrun` 

    ```bash
    $> ./mitosrun ./matmul
    ```

2. `openmp_matmul.cpp`: For running with `mitoshooks` (OpenMP)

    ```bash
    $> env OMP_TOOL_LIBRARIES=/path/to/lib/libmitoshooks.so ./openmp_matmul
    ```

3. `mpi_matmul.cpp`: For running with `mitoshooks` (MPI)

    ```bash
    $> mpirun -np 4 ./mpi_matmul
    ```
    
4. `api_matmul.cpp`: For single-threaded API usage

    ```bash
    $> ./api_matmul
    ```

5. `api_openmp_matmul.cpp`: For OpenMP based API usage

    ```bash
    $> ./api_openmp_matmul
    ```

6. `api_mpi_matmul.cpp`: For MPI based API usage

    ```bash
    $> ./api_mpi_matmul
    ```
