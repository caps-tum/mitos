#include <sys/stat.h>

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

void openInputFile (std::ifstream &inputFile, std::string &bin_name, std::string &dir_path,
            std::string &dir_prefix);

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
    std::string output_dirs = dir_prefix;
    
    Mitos_set_result_mout(&mouts, output_dirs.c_str());    
    Mitos_openFile(bin_name.c_str(), &mouts);
    
    std::set<std::string> src_files;

    Mitos_post_process(bin_name.c_str(), &mouts, src_files);

    std::cout << "Following source files found:\n";
    for (auto& str : src_files) {
        std::cout << str << "\n";
    }

    /* Examples:
    dir:prefix: 1708705259_openmp_distr_monresult
    */
    //copy_source_files(dir_prefix, src_files);
    Mitos_copy_sources(dir_prefix, src_files);
}

void openInputFile (std::ifstream &inputFile, std::string &bin_name, std::string &dir_path,
            std::string &dir_prefix){
    std::string line;
    while (std::getline(inputFile, line)) {
        // Find the position of '=' character
        size_t pos = line.find('=');
        if (pos != std::string::npos) {
            // Extract variable name and value
            std::string variableName = line.substr(0, pos);
            std::string value = line.substr(pos + 1);

            // Remove leading/trailing spaces from variable name and value
            size_t start = variableName.find_first_not_of(" \t");
            size_t end = variableName.find_last_not_of(" \t");
            if (start != std::string::npos && end != std::string::npos)
                variableName = variableName.substr(start, end - start + 1);

            start = value.find_first_not_of(" \t");
            end = value.find_last_not_of(" \t");
            if (start != std::string::npos && end != std::string::npos)
                value = value.substr(start, end - start + 1);

            // Store values in respective variables
            if (variableName == "bin_name")
                bin_name = value;
            else if (variableName == "dir_path")
                dir_path = value;
            else if (variableName == "dir_prefix")
                dir_prefix = value;
        }
    }
}