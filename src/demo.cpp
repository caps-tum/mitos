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
#include <omp.h>
#include <set>

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
static mitos_output mout1, mout2;
static std::vector<mitos_output> mouts;
SymtabAPI::Symtab *symtab_obj;
SymtabCodeSource *symtab_code_src;
int sym_success = 0;

void openInputFile (std::ifstream &inputFile, std::string &bin_name, std::string &dir_path,
            std::string &dir_prefix, std::string &dir_first_dir_prefix);

int populate_dirs(std::vector<std::string> &output_dirs, const std::string& dir_path, 
            const std::string& dir_prefix, const std::string& dir_first_dir_prefix);

int process_mout(mitos_output *mout, const char *prefix_name);

void demo_write_samples_header(std::ofstream& fproc);

int demo_openFile(const char *bin_name, mitos_output *mout);

int demo_post_process(const char *bin_name, mitos_output *mout, std::set<std::string>& src_files);

int demo_merge_files(const std::string& dir_prefix, const std::string& dir_first_dir_prefix, const std::set<std::string>& src_files);

int main(int argc, char* argv[])
{
    // Check if input file is provided as command-line argument
    if (argc != 2) {
        std::cerr << "Usage: " << argv[0] << " <input_file>\n";
        return 1;
    }

    // Open the input file
    std::ifstream inputFile(argv[1]);

    // Check if the file is opened successfully
    if (!inputFile.is_open()) {
        std::cerr << "Error opening file\n";
        return 1;
    }

    // Variables to store values
    std::string bin_name, dir_path, dir_prefix, dir_first_dir_prefix;
    openInputFile(inputFile, bin_name, dir_path,
            dir_prefix, dir_first_dir_prefix);
    
    // Output the variables
    std::cout << "bin_name: " << bin_name << '\n';
    std::cout << "dir_path: " << dir_path << '\n';
    std::cout << "dir_prefix: " << dir_prefix << '\n';
    std::cout << "dir_first_dir_prefix: " << dir_first_dir_prefix << '\n';
    std::vector<std::string> output_dirs;
    output_dirs.reserve(omp_get_num_threads());
    
    //Implement using lambda
    populate_dirs(output_dirs, dir_path, dir_prefix, dir_first_dir_prefix);
    mouts.resize(output_dirs.size());
    std::cout << "Printing dirs name:\n";
    for (auto i = 0; i <output_dirs.size(); i++){
        std::cout<<output_dirs[i] << "\n";
    }
    for (auto i = 0; i < output_dirs.size(); i++){
        process_mout(&mouts[i], output_dirs[i].c_str());    
    }

    demo_openFile(bin_name.c_str(), &mouts[0]);
    
    std::set<std::string> src_files;

    for (auto i = 0; i < output_dirs.size(); i++){
       demo_post_process(bin_name.c_str(), &mouts[i], src_files);
    }

    std::cout << "Following source files found:\n";
    for (auto& str : src_files) {
        std::cout << str << "\n";
    }

    /* Examples:
    dir:prefix: 1708705259_openmp_distr_mon, 
    dir_first_dir_prefix: 1708705259_openmp_distr_mon_663246
    */
    demo_merge_files(dir_prefix, dir_first_dir_prefix, src_files);
}

void openInputFile (std::ifstream &inputFile, std::string &bin_name, std::string &dir_path,
            std::string &dir_prefix, std::string &dir_first_dir_prefix){
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
            else if (variableName == "dir_first_dir_prefix")
                dir_first_dir_prefix = value;
        }
    }
}


int populate_dirs(std::vector<std::string> &output_dirs, const std::string& dir_path, const std::string& dir_prefix, const std::string& dir_first_dir_prefix)
{
    fs::path path_root{"."};
    bool first_dir_found = false;
    std::string path_first_dir{""};
    for (auto const& dir_entry : std::filesystem::directory_iterator{path_root}) {
        if (dir_entry.path().u8string().rfind("./" + dir_first_dir_prefix) == 0) {
            path_first_dir = dir_entry.path().u8string();
            first_dir_found = true;
        }
    }
    if(first_dir_found) {
        LOG_LOW("demo.cpp:populate_dirs, First Dir Found: " << path_first_dir);
        //output_dirs.push_back("/u/home/mishrad/mitos-master/inst-dir/bin"+ path_first_dir.substr(1));
        output_dirs.push_back(dir_path+ path_first_dir.substr(1));
    }else {
        LOG_LOW("demo.cpp:populate_dirs, First Dir not found");
        // error, directory not found
        return 1;
    }
    std::string path_dir_result = "./"+ dir_prefix + "result";
    // for other directories, copy samples to dest dir
    for (auto const& dir_entry : std::filesystem::directory_iterator{path_root})
    {
        if (dir_entry.path().u8string().rfind("./"+ dir_prefix) == 0
        && dir_entry.path().u8string() != path_first_dir
        && dir_entry.path().u8string() != path_dir_result) {
            //std::cout << "dir_entry.path(): " <<  dir_entry.path() <<"\n";
            LOG_LOW("demo.cpp:populate_dirs, Directory found " << dir_entry.path());
            // output_dirs.push_back("/u/home/mishrad/mitos-master/inst-dir/bin" + std::string(dir_entry.path()).substr(1));
            output_dirs.push_back( dir_path + std::string(dir_entry.path()).substr(1));
        }
    } // END LOOP

    return 0;
}
int process_mout(mitos_output *mout, const char *prefix_name)
{
    memset(mout,0,sizeof(struct mitos_output));

    // Set top directory name
    std::stringstream ss_dname_topdir;
    //ss_dname_topdir << prefix_name << "_" << std::time(NULL);
    ss_dname_topdir << prefix_name;
    mout->dname_topdir = strdup(ss_dname_topdir.str().c_str());

    // Set data directory name
    std::stringstream ss_dname_datadir;
    ss_dname_datadir << ss_dname_topdir.str() << "/data";
    mout->dname_datadir = strdup(ss_dname_datadir.str().c_str());

    // Set src directory name
    std::stringstream ss_dname_srcdir;
    ss_dname_srcdir << ss_dname_topdir.str() << "/src";
    mout->dname_srcdir = strdup(ss_dname_srcdir.str().c_str());

    // Set hwdata directory name
    std::stringstream ss_dname_hwdatadir;
    ss_dname_hwdatadir << ss_dname_topdir.str() << "/hwdata";
    mout->dname_hwdatadir = strdup(ss_dname_hwdatadir.str().c_str());

    mout->fname_raw = strdup(std::string(std::string(mout->dname_datadir) + "/raw_samples.csv").c_str());
    mout->fname_processed = strdup(std::string(std::string(mout->dname_datadir) + "/samples.csv").c_str());

    return 0;
}


void demo_write_samples_header(std::ofstream& fproc) {
    // Write header for processed samples
    fproc << "source,line,instruction,bytes,ip,variable,buffer_size,dims,xidx,yidx,zidx,pid,tid,time,addr,cpu,latency,";
#if !defined(USE_IBS_FETCH) && !defined(USE_IBS_OP)
    fproc << "level,hit_type,op_type,snoop_mode,tlb_access,";
#endif
    fproc << "numa";
#ifdef USE_IBS_FETCH
    fproc << ",ibs_fetch_max_cnt,ibs_fetch_cnt,ibs_fetch_lat,ibs_fetch_en,ibs_fetch_val,ibs_fetch_comp,ibs_ic_miss,ibs_phy_addr_valid,ibs_l1_tlb_pg_sz,ibs_l1_tlb_miss,ibs_l2_tlb_miss,ibs_rand_en,ibs_fetch_l2_miss,";
    fproc << "ibs_fetch_lin_addr,ibs_fetch_phy_addr,ibs_fetch_control_extended";
#endif // USE_IBS_FETCH
#ifdef USE_IBS_OP
    fproc << ",ibs_op_max_cnt,ibs_op_en,ibs_op_val,ibs_op_cnt_ctl,ibs_op_max_cnt_upper,ibs_op_cur_cnt,";
        fproc << "ibs_op_rip,";
        // ibs_op_data_1
        fproc << "ibs_comp_to_ret_ctr,ibs_tag_to_ret_ctr,ibs_op_brn_resync,ibs_op_misp_return,ibs_op_return,ibs_op_brn_taken,ibs_op_brn_misp,";
        fproc << "ibs_op_brn_ret,ibs_rip_invalid,ibs_op_brn_fuse,ibs_op_microcode,";
        // ibs_op_data_2
        fproc << "ibs_nb_req_src,ibs_nb_req_dst_node,ibs_nb_req_cache_hit_st,";
        // ibs_op_data_3
        fproc << "ibs_ld_op,ibs_st_op,ibs_dc_l1_tlb_miss,ibs_dc_l2_tlb_miss,ibs_dc_l1_tlb_hit_2m,ibs_dc_l1_tlb_hit_1g,ibs_dc_l2_tlb_hit_2m,";
        fproc << "ibs_dc_miss,ibs_dc_miss_acc,ibs_dc_ld_bank_con,ibs_dc_st_bank_con,ibs_dc_st_to_ld_fwd,ibs_dc_st_to_ld_can,";
        fproc << "ibs_dc_wc_mem_acc,ibs_dc_uc_mem_acc,ibs_dc_locked_op,ibs_dc_no_mab_alloc,ibs_lin_addr_valid,ibs_phy_addr_valid,";
        fproc << "ibs_dc_l2_tlb_hit_1g,ibs_l2_miss,ibs_sw_pf,ibs_op_mem_width,ibs_op_dc_miss_open_mem_reqs,ibs_dc_miss_lat,ibs_tlb_refill_lat,";
        // lin and phy address
        fproc << "ibs_op_phy,ibs_op_lin,";
        // ibs brs target address
        fproc << "ibs_branch_target";
#endif // USE_IBS_OP
    fproc << "\n";
}

int demo_openFile(const char *bin_name, mitos_output *mout)
{
// Open input/output files
std::ifstream fraw(mout->fname_raw);
std::ofstream fproc(mout->fname_processed);
#ifndef USE_DYNINST
    // No Dyninst, no post-processing
    err = rename(mout->fname_raw, mout->fname_processed);
    if(err)
    {
        std::cerr << "Mitos: Failed to rename raw output to " << mout->fname_processed << std::endl;
    }

    fproc.close();
    fraw.close();

    return 0;
#else // USE DYNINST

    std::cout << "demo.cpp, demo_openFile(), bin_name: " << bin_name << "\n";
    sym_success = SymtabAPI::Symtab::openFile(symtab_obj,bin_name);
    std::cout << "demo.cpp, demo_openFile(), sym_success: " << sym_success <<"\n";
    if(!sym_success)
    {
        std::cerr << "Mitos: Failed to open Symtab object for " << bin_name << std::endl;
        std::cerr << "Saving raw data (no source/instruction attribution)" << std::endl;

        fraw.close();
        fproc.close();

        int err = rename(mout->fname_raw, mout->fname_processed);
        if(err)
        {
            std::cerr << "Mitos: Failed to rename raw output to " << mout->fname_processed << std::endl;
        }
        return 1;
    }
    return 0;
}

int demo_post_process(const char *bin_name, mitos_output *mout, std::set<std::string>& src_files)
{
    int err = 0;
    // Open input/output files
    std::cout <<"mout->fname_raw: " <<mout->fname_raw << "\n";
    std::ifstream fraw(mout->fname_raw);
    std::ofstream fproc(mout->fname_processed);
    
    symtab_code_src = new SymtabCodeSource(strdup(bin_name));

    // Get machine information
    unsigned int inst_length = InstructionDecoder::maxInstructionLength;
    Architecture arch = symtab_obj->getArchitecture();

    demo_write_samples_header(fproc);

    //get base (.text) virtual address of the measured process
    //std::ifstream foffset("/u/home/vanecek/sshfs/sv_mitos/build/test3.txt");
    // TODO: Replace line
    std::ifstream foffset("/tmp/virt_address.txt");
    long long offsetAddr = 0;
    std::string str_offset;
    if(std::getline(foffset, str_offset).good())
    {
        offsetAddr = strtoll(str_offset.c_str(),NULL,0);
    }
    foffset.close();
    std::cout << "offset: " << offsetAddr << std::endl;

    // Read raw samples one by one and get attribute from ip
    Dyninst::Offset ip;
    size_t ip_endpos;
    std::string line, ip_str;
    int tmp_line = 0;
    LOG_MEDIUM("demo.cpp:demo_post_process(), reading raw samples...");

    while(std::getline(fraw, line).good())
    {
        // Unknown values
        std::string source;
        std::stringstream line_num;
        std::stringstream instruction;
        std::stringstream bytes;

        // Extract ip
        size_t ip_endpos = line.find(',');
        std::string ip_str = line.substr(0,ip_endpos);
        ip = (Dyninst::Offset)(strtoull(ip_str.c_str(),NULL,0) - offsetAddr);
        if(tmp_line%4000==0)
            std::cout << "ip: " << ip <<"\n";
        // Parse ip for source line info
        std::vector<SymtabAPI::Statement::Ptr> stats;
        sym_success = symtab_obj->getSourceLines(stats, ip);

        if(sym_success)
        {
            source = (string)stats[0]->getFile();
            if (!mout->dname_srcdir_orig.empty())
            {
                std::size_t pos = source.find(mout->dname_srcdir_orig);
                if(pos == 0){
                    source = source.substr(mout->dname_srcdir_orig.length() + (mout->dname_srcdir_orig.back() == '/' ? 0 : 1)); //to remove slash if there is none in the string
                }

            }
            line_num << stats[0]->getLine();
        }
        if(!source.empty()){
            src_files.insert(source);
        }

        // Parse ip for instruction info
        void *inst_raw = NULL;
        if(symtab_code_src->isValidAddress(ip))
        {
            inst_raw = symtab_code_src->getPtrToInstruction(ip);

            if(inst_raw)
            {
                // Get instruction
                InstructionDecoder dec(inst_raw,inst_length,arch);
                Instruction inst = dec.decode();
                Operation op = inst.getOperation();
                entryID eid = op.getID();

                instruction << NS_x86::entryNames_IAPI[eid];

                // Get bytes read
                if(inst.readsMemory())
                    bytes << getReadSize(inst);
            }
        }
        LOG_HIGH("demo.cpp:demo_post_process(), writing out sample no. " << tmp_line + 1);
        // Write out the sample
        fproc << (source.empty()            ? "??" : source             ) << ","
              << (line_num.str().empty()    ? "??" : line_num.str()     ) << ","
              << (instruction.str().empty() ? "??" : instruction.str()  ) << ","
              << (bytes.str().empty()       ? "??" : bytes.str()        ) << ","
              << line << std::endl;

        tmp_line++;
    }
    fraw.close();
    fproc.close();


    err = remove(mout->fname_raw);
    if(err)
    {
        std::cerr << "Mitos: Failed to delete raw sample file!\n";
        return 1;
    }

#endif // USE_DYNINST

    return 0;
}


int demo_merge_files(const std::string& dir_prefix, const std::string& dir_first_dir_prefix, const std::set<std::string>& src_files) {
    // find exact directory name for mpi_rank 0
    fs::path path_root{"."};
    bool first_dir_found = false;
    std::string path_first_dir{""};
    for (auto const& dir_entry : std::filesystem::directory_iterator{path_root}) {
        if (dir_entry.path().u8string().rfind("./" + dir_first_dir_prefix) == 0) {
            path_first_dir = dir_entry.path().u8string();
            first_dir_found = true;
        }
    }
    if(first_dir_found) {
        LOG_LOW("demo.cpp:demo_merge_files(), First Dir Found, copy Files From " << path_first_dir << " to result folder: ./" << dir_prefix << "result");
    }else {
        LOG_LOW("demo.cpp:demo_merge_files(), First Dir not found");
        // error, directory not found
        return 1;
    }
    std::string path_dir_result = "./"+ dir_prefix + "result";
    // create directories
    fs::create_directory(path_dir_result);
    fs::create_directory(path_dir_result + "/data");
    fs::create_directory(path_dir_result + "/hwdata");
    fs::create_directory(path_dir_result + "/src");
    // copy first directory
    fs::copy(path_first_dir, path_dir_result, fs::copy_options::overwrite_existing | fs::copy_options::recursive);
    // delete first folder
    //fs::remove_all(path_first_dir);

    std::string path_samples_dest = path_dir_result + "/data/samples.csv";
    // check if file exist
    if (fs::exists(path_samples_dest)) {
        std::ofstream file_samples_out;
        file_samples_out.open(path_samples_dest, std::ios_base::app);
        // for other directories, copy samples to dest dir
        for (auto const& dir_entry : std::filesystem::directory_iterator{path_root})
        {
            if (dir_entry.path().u8string().rfind("./"+ dir_prefix) == 0
            && dir_entry.path().u8string() != path_first_dir
            && dir_entry.path().u8string() != path_dir_result) {
                LOG_LOW("demo.cpp:demo_merge_files(), Move Data " << dir_entry.path() << " to Result Folder...");
                // src file
                std::string path_samples_src = dir_entry.path().u8string() + "/data/samples.csv";
                if (fs::exists(path_samples_src)) {
                    // copy data
                    std::ifstream file_samples_in(path_samples_src);
                    std::string line;
                    bool is_first_line = true;
                    while (std::getline(file_samples_in, line))
                    {
                        if (is_first_line){
                            is_first_line = false;
                        }else{
                            file_samples_out << line << "\n";
                        }
                    }
                    file_samples_in.close();
                    // delete old folder
                    //fs::remove_all(dir_entry.path().u8string());
                }
            }
        } // END LOOP
        file_samples_out.close();
    }
    // copy source files

    std::cout << "Copying source files to result folder...\n";
    std::string path_src_dir = path_dir_result + "/src";
    for (auto& src_file : src_files) {
        try {
            // Check if source file exists
            if (!fs::exists(src_file)) {
                std::cerr << "Source file not accessible: " << src_file << "\n";
            }

            // Copy the file
            fs::copy(src_file, path_src_dir);

            std::cout << "File copied successfully." << "\n";
        } catch (const std::exception& ex) {
            std::cerr << "Error: " << ex.what() << "\n";
        }   
    }
    
    std::cout << "Merge successfully completed\n";
    return 0;
}
