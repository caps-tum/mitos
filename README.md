# Mitos

Mitos is a library and a tool for collecting sampled memory
performance data to view with
[MemAxes](https://github.com/scalability-llnl/MemAxes)

----

# Quick Start

## Requirements

Mitos requires:

* A Linux kernel with perf_events support for memory
  sampling.  This originated in the 3.10 Linux kernel, but is backported
  to some versions of [RHEL6.6](https://www.redhat.com/promo/Red_Hat_Enterprise_Linux6/).

* [Dyninst](http://www.dyninst.org) version 12.3.0 or higher.

* [hwloc](http://www.open-mpi.org/projects/hwloc/)

## Building

1. Make sure that Dyninst is installed and its location is added to the
   `CMAKE_PREFIX_PATH` environment variable.

2. Run the following commands from the root of the MemAxes source:
   ```bash
   $> mkdir build && cd build
   $> cmake -DCMAKE_INSTALL_PREFIX=/path/to/install/location ..
   $> make
   $> make install
   ```

## Running

### Mitosrun

1. Find the `mitosrun` command in the `bin` directory in the install
   directory.

2. Make sure that the path to the installation location of `mitos` and `Dyninst` is added to the `LD_LIBRARY_PATH`. For example:

   ```bash
   $> export LD_LIBRARY_PATH=/path/to/mitos/install/location/lib/
   $> export LD_LIBRARY_PATH=/path/to/dyninst/lib/:$LD_LIBRARY_PATH
   ```

3. Run any binary with `mitosrun` like this to generate a folder of
   mitos output data. For example:

   ```bash
   $> mitosrun ./examples/matmul
   ```

   The above command will run the matmul example and create a folder
   called mitos_###, where ### is the number of seconds since the
   epoch. The folder will contain:

   ```bash
   mitos_###/
      data/
         samples.csv
      src/
         <empty>
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
       -s top folder of source code to copy
   ```
   **See the section on `Source Code Attribution` below for more details on how to save the source code information with the samples collected while running your application.**

## IBS (AMD) Configuration
The default installation of `mitos` will be configured for Intel based Precise Event Based Sampling (PEBS). Additionally, `mitos` supports AMD based Instruction Based Sampling (IBS). Configure CMAKE with IBS depending on the chosen executable and configure environment variables if necessary:
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

## Mitoshooks with OpenMP Usage
### 1. Requirements

   Compiler with OMPT support such as Clang (OpenMP feature since version 5.0) is required. if clang is not the default compiler:
   
   ```bash
   $> export CC=/path/to/clang*
   $> export CXX=/path/to/clang++*
   ```
### 2. CMake Configuration
   
   OpenMP (version 5.0 and later) must be installed. If OpenMP is not installed at the default loaction, make sure to specify the paths so that `cmake` finds it.
   
   Enable `MITOSHOOKS_OPEN_MP` option by `-DMITOSHOOKS_OPEN_MP=ON`

### 3. Compilation
   * Build and install `mitos` and `mitoshooks`
   * Depending on the build method (cmake/make/command-line), link the application with `mitos` and `mitoshooks` by specifying their paths.
   * See the section on `Source Code Attribution` below for more details on how to save the source code information with the samples collected while running your application.
   
### 4. Execution
   
   Mitoshooks uses the OMPT interface to collect samples while each thread runs. Configure the environment variable `OMP_TOOL_LIBRARIES` that points to mitoshooks-library and use one of these methods to launch the OpenMP-application.

   **Recommended method:** 
   ```bash
   $> env OMP_TOOL_LIBRARIES=/path/to/mitos-inst-dir/lib/libmitoshooks.so ./omp_example
   ```
   ***The following method should also work but is not recommended. Exporting the variable `OMP_TOOL_LIBRARIES` for the global enviornment may break other OpenMP applications that do not intend to use hooks.***
   ```bash
   $> export OMP_TOOL_LIBRARIES=/path/to/mitos-inst-dir/lib/libmitoshooks.so 
   
   $> ./omp_example
   ```
   `Mitoshooks` can be fine-tuned by setting these parameters:

   ```bash
   env MITOS_SAMPLING_PERIOD=1000 MITOS_LATENCY_THRESHOLD=10
   ```
   Default value of sampling period is 4000 and that of sample latency threshold is 3. When setting these values, the application can be executed by:

   ```bash
   $> env OMP_TOOL_LIBRARIES=/path/to/mitos-inst-dir/lib/libmitoshooks.so MITOS_SAMPLING_PERIOD=1000 MITOS_LATENCY_THRESHOLD=10 ./omp_example
   ```

# Source Code Attribution

## Requirements
* [Dyninst](http://www.dyninst.org) version 12.3.0 or higher.
* The application must be compiled in Debug mode (only `-g` compiler flag sufficient).

## Saving the virtual address

The source code of the executable must save the virtual addess offset when the executable starts runnnig. This can be done by including the [virtual_address_writer.h](src/virtual_address_writer.h) and calling the function [save_virtual_address_offset("/tmp/mitos_virt_address.txt")](src/virtual_address_writer.h#L18).

See [matmul.cpp](examples/matmul.cpp) for reference.

This saves the virtual address offset to `/tmp/mitos_virt_address.txt`. Dyninst will access this file and attribute the source code information when the samples are saved.

When running the application with `mitosrun`, another location can also be specified. When doing this, use `-l` option with the `mitosrun` to specify the location of the file.

For instance, if the application saves virtual address by calling `save_virtual_address_offset("/myPath/mitos_virt_address.txt")`, `mitosrun` can be run as:

```shell
./mitosrun -l /myPath/mitos_virt_address.txt ./myApplication
```

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