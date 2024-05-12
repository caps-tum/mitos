# Mitos

Mitos is a library and a tool for collecting sampled memory
performance data to view with
[MemAxes](https://github.com/scalability-llnl/MemAxes)

----

Mitos offers three interfaces for collecting samples:
1. Mitosrun for single threaded applications.
   - The application needs to be run alongside `mitosrun`
2. Mitoshooks for multi-threaded OpenMP or MPI codes.
   - The application needs to be linked with `mitos` and `mitoshooks`.
3. API calls
   - The application can directly make API calls to collect the samples.

# Requirements

Mitos requires:

* A Linux kernel with perf_events support for memory
  sampling.  This originated in the 3.10 Linux kernel, but is backported
  to some versions of [RHEL6.6](https://www.redhat.com/promo/Red_Hat_Enterprise_Linux6/).

* [Dyninst](http://www.dyninst.org) version 12.3.0 or higher.

* [hwloc](http://www.open-mpi.org/projects/hwloc/)

* For OpenMP code, Clang C/C++ compiler is required.

* In order for source attribution to work (see below), the application must be compiled in Debug mode (only `-g` compiler flag sufficient)

# Building

1. Make sure that Dyninst is installed and its location is added to the
   `CMAKE_PREFIX_PATH` environment variable.

2. Run the following commands from the root of the MemAxes source:
   ```bash
   $> mkdir build && cd build
   $> cmake -DCMAKE_INSTALL_PREFIX=/path/to/install/location ..
   $> make
   $> make install
   ```

The default installation of `mitos` will be configured for Intel based **Precise Event Based Sampling (PEBS)**. Additionally, `mitos` supports AMD based **Instruction Based Sampling (IBS)**.

## IBS (AMD) Configuration
 Configure CMAKE with IBS depending on the chosen executable and configure environment variables if necessary:
* `IBS_TYPE` 
  * Use IBS_FETCH or IBS_OP depending on the profiling use case (requires AMD processor with IBS support)
  * IBS is not supported on Intel processors. By default, the variable is set to OFF.
* `IBS_SAMPLING_MODE`
  * Mitosrun (with or without OpenMP): `IBS_ALL_ON` or `IBS_THREAD_MIGRATION`
  * Mitoshooks with OpenMP: 
    * `IBS_THREAD_MIGRATION`, requires Clang due to omp-tools.h dependency
    * Enables OpenMP code by setting `MITOSHOOKS_OPEN_MP` CMake variable to `ON`
    * Configure environment variable `OMP_TOOL_LIBRARIES` that points to mitoshooks-library:
      * OMP_TOOL_LIBRARIES=./../src/libmitoshooks.so
  * Mitoshooks with MPI: `IBS_THREAD_MIGRATION`
  * NOTE: `IBS_ALL_ON` might also work, but this sampling method is not recommended.

## Mitoshooks

### 1. Mitoshooks with OpenMP
#### A. Requirements

   Compiler with OMPT support such as Clang (OpenMP feature since version 5.0) is required. If clang is not the default compiler:
   
   ```bash
   $> export CC=/path/to/clang*
   $> export CXX=/path/to/clang++*
   ```

#### B. CMake Configuration
   
   OpenMP (version 5.0 and later) must be installed. If OpenMP is not installed at the default loaction, make sure to specify the paths so that `cmake` finds it.
   
   Enable `MITOSHOOKS_OPEN_MP` option by `-DMITOSHOOKS_OPEN_MP=ON`

#### C. Building
   **Note:** If using `IBS_THREAD_MIGRATION`, modify the source code
            and set active_core = sched_getcpu() in procsmpl.cpp(L#91)

   * Build and install `mitos` and `mitoshooks`
   * Depending on the build method (cmake/make/command-line), link the application with `mitos` and `mitoshooks` by specifying their paths.
   * See the section on `Source Code Attribution` below for more details on how to save the source code information with the samples collected while running your application.

### 2. Mitoshooks with MPI

   * Build and install `mitos` and `mitoshooks`
   * Depending on the build method (cmake/make/command-line), link the application with `mitos` and `mitoshooks` by specifying their paths.
   * See the section on `Source Code Attribution` below for more details on how to save the source code information with the samples collected while running your application.

## API
   **Note:** If using `IBS_THREAD_MIGRATION` for collecting samples for an OpenMP application, modify the source code and set active_core = sched_getcpu() in procsmpl.cpp(L#91)
   
   * Build and install `mitos`
   * Depending on the build method (cmake/make/command-line), link the application with `mitos` by specifying their paths.
   * See the section on `Source Code Attribution` below for more details on how to save the source code information with the samples collected while running your application.

# Execution

This section highlights different ways of executing `mitos`. For example usage, see [examples/README.md](examples/README.md). 

## Mitosrun

1. Find the `mitosrun` executable in the `bin` directory in the install
   directory.

2. Make sure that the path to the installation location of `mitos` and `Dyninst` can be located by the linker. 

3. Run any binary with `mitosrun` like this to generate a folder of
   mitos output data. For example:

   ```bash
   $> ./mitosrun ./examples/matmul
   ```

   The above command will run the matmul example and create a folder
   called mitos_###, where ### is the number of seconds since the
   epoch. The folder will contain:

   ```bash
   mitos_###/
      data/
         samples.csv
      src/
         <source files>
      hwdata/
         <>   
      hardware.xml
   ```

   Where `samples.csv` contains a comma-separated list of memory
   samples, hardware.xml describes the hardware topology (using hwloc)
   and src is an empty directory where you can put the program source
   files for use in MemAxes.

   `mitosrun` can also be fine-tuned with the following parameters:

   ```bash
   [options]:
       -b sample buffer size (default 4096)
       -p sample period (default 4000)
       -t sample latency threshold (default 10)
       -f sample frequency (default 4000)
       -l location of virtual address file (default /tmp/mitos_virt_address.txt)
   ```
   The sampling parameters are chosen in this order of preference :
   1. By default, use the sampling period
   2. if sampling frequency is defined, use the sampling frequency
   3. if sampling period is defined, use the sampling period (even if both the period and frequency are defined)


   **See the section on `Source Code Attribution` below for more details on how to save the source code information with the samples collected while running your application.**


## Mitoshooks 

### 1. Mitoshooks with OpenMP Usage
   
   Mitoshooks uses the OMPT interface to collect samples while each thread runs. Configure the environment variable `OMP_TOOL_LIBRARIES` that points to mitoshooks-library and use one of these methods to launch the OpenMP-application.


   ```bash
   $> env OMP_TOOL_LIBRARIES=/path/to/mitos-inst-dir/lib/libmitoshooks.so ./omp_example
   ```
   OR

   ```bash
   $> export OMP_TOOL_LIBRARIES=/path/to/mitos-inst-dir/lib/libmitoshooks.so 
   
   $> ./omp_example
   ```

   For OpenMP hooks, the post-processing needs to be done after the execution is finished and all the samples have been collected. If the execution was successful, `mitos` will give the instruction on how to do the sampling:

   ```bash
   Initiating OMP Hooks start tool: 16887
   Beginning sampler
Mitos sampling parameters: Latency threshold = 4, Sampling period: 4000

// Application outputs and messages

End Sampler...

*******************************************************************

Samples collected and written as raw data. Run the following command for post-processing the samples: 
 ./mitos_omp_post_process /path/to/mitos/inst-dir/bin/myExecutable mitos_###_openmp_distr_monresult

*******************************************************************
   ```

Copy the above command and run:

```bash
$> ./mitos_omp_post_process /path/to/mitos/inst-dir/bin/myExecutable mitos_###_openmp_distr_monresult
```

The results will be saved in mitos_###_openmp_distr_monresult, where ### is the number of seconds since the epoch.

### 2. Mitoshooks with MPI Usage

There are no special instructions for MPI usage. If the application is successfully linked to `mitos` and `mitoshooks`, running it on multiple nodes will work. For example,

```bash
$> mpirun -np 4 ./myExecutable
```
The results will be saved in mitos_###_rank_result, where ### is the number of seconds since the epoch.

### Fine-tuning Mitoshooks

`Mitoshooks` can be fine-tuned by setting these parameters:

   ```bash
   $> export MITOS_SAMPLING_PERIOD=1000 
   $> export MITOS_LATENCY_THRESHOLD=10 
   $> export MITOS_SAMPLING_FREQUENCY=3000
   ```
   Default value of sampling period is 4000 and that of sample latency threshold is 4. However, the sampling parameters are chosen in this order of preference:
   1. By default, use the sampling period
   2. if sampling frequency is defined, use the sampling frequency
   3. if sampling period is defined, use the sampling period (even if both the period and frequency are defined)
   
   For example, when setting these values, the OpenMP application can be executed by:

   ```bash
   $> env OMP_TOOL_LIBRARIES=/path/to/mitos-inst-dir/lib/libmitoshooks.so MITOS_SAMPLING_PERIOD=1000 MITOS_LATENCY_THRESHOLD=10 ./omp_example
   ```

## API

See [examples/api_matmul.cpp](examples/api_matmul.cpp), [examples/api_openmp_matmul.cpp](examples/api_openmp_matmul.cpp), and [examples/api_mpi_matmul.cpp](examples/api_mpi_matmul.cpp) for the sample usage.

# Source Code Attribution

## Requirements
* [Dyninst](http://www.dyninst.org) version 12.3.0 or higher.
* The application must be compiled in Debug mode (only `-g` compiler flag sufficient).

## Saving the virtual address

The source code of the executable must save the virtual addess offset when the executable starts runnnig. This can be done by including the [virtual_address_writer.h](src/virtual_address_writer.h) and calling the function [Mitos_save_virtual_address_offset("/tmp/mitos_virt_address.txt")](src/virtual_address_writer.h#L18).

See [matmul.cpp](examples/matmul.cpp) for reference.

This saves the virtual address offset to `/tmp/mitos_virt_address.txt`. Dyninst will access this file and attribute the source code information when the samples are saved.

When running the application with `mitosrun`, another location can also be specified. When doing this, use `-l` option with the `mitosrun` to specify the location of the file.

For instance, if the application saves virtual address by calling `Mitos_save_virtual_address_offset("/myPath/mitos_virt_address.txt")`, `mitosrun` can be run as:

```shell
./mitosrun -l /myPath/mitos_virt_address.txt ./myApplication
```

# Verbose debug outputs

By default, log messages will just show minimal information. In order to get better outputs, verbosity can be defined by setting `-DVERBOSITY=1|2|3` during compilation. `1` refers to the lowest verbsoiy and `3` to the highest.

# Authors

Mitos and MemAxes were originally written by Alfredo Gimenez.

Thanks to Todd Gamblin for suggestions and for giving Mitos a proper build setup.

# License

Mitos is distributed under the Apache-2.0 license with the LLVM exception.
All new contributions must be made under this license. Copyrights and patents
in the Mitos project are retained by contributors. No copyright assignment is
required to contribute to Mitos.

See [LICENSE](https://github.com/llnl/mitos/blob/develop/LICENSE) and
[NOTICE](https://github.com/llnl/mitos/blob/develop/NOTICE) for details.

SPDX-License-Identifier: (Apache-2.0 WITH LLVM-exception)

`LLNL-CODE-838491`