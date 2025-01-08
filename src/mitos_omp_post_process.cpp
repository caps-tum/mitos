#include <sys/stat.h>

#undef PTHREAD_STACK_MIN
#define PTHREAD_STACK_MIN 16384

#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <ctime>

#include "hwloc_dump.h"

#include "Mitos.h"
#include <vector>
#include <cstdlib>

#ifndef __has_include
static_assert(false, "__has_include not supported");
#else
#  if __cplusplus >= 201703L && __has_include(<filesystem>)
#    include <filesystem>
namespace fs = std::filesystem;
#  elif __has_include(<experimental/filesystem>)
#    include <experimental/filesystem>
     namespace fs = std::experimental::filesystem;
#  elif __has_include(<boost/filesystem.hpp>)
#    include <boost/filesystem.hpp>
     namespace fs = boost::filesystem;
#  endif
#endif // __has_include

#ifdef USE_DYNINST
#include <CodeObject.h> // parseAPI
#include <InstructionDecoder.h> // instructionAPI
#include <Module.h>
using namespace Dyninst;
using namespace SymtabAPI;
using namespace InstructionAPI;
using namespace ParseAPI;

#include "x86_util.h" // getReadSize using instructionAPI

#endif // USE_DYNINST

using namespace std;
static mitos_output mouts;
SymtabAPI::Symtab *symtab_obj;
SymtabCodeSource *symtab_code_src;
int sym_success = 0;

int main(int argc, char* argv[])
{
    // Check if input file is provided as command-line argument
    if (argc != 3) {
        std::cerr << "Usage: " << argv[0] << " <Executable path> <Name of Results Directory> \n";
        std::cerr << "For example: ./demo_post_process /u/home/ubuntu/bin/matmul 1710533830_openmp_distr_monresult \n";
        return 1;
    }

    // Variables to store values
    std::string bin_name, dir_prefix;
    // Output the variables
    bin_name = argv[1];
    dir_prefix = argv[2];
    
    std::cout << "Executable Name: " << bin_name << '\n';
    std::cout << "Results directory: " << dir_prefix << '\n';
    
    Mitos_set_result_mout(&mouts, dir_prefix);    
    Mitos_process_binary(bin_name.c_str(), &mouts);
    Mitos_post_process(bin_name.c_str(), &mouts, dir_prefix);

}