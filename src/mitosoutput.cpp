#include <sys/stat.h>

#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <ctime>

#include "hwloc_dump.h"

#include "Mitos.h"

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
SymtabAPI::Symtab *symtab_obj;
SymtabCodeSource *symtab_code_src;
int sym_success = 0;

int Mitos_create_output(mitos_output *mout, const char *prefix_name)
{
    memset(mout,0,sizeof(struct mitos_output));

    // Set top directory name
    std::stringstream ss_dname_topdir;
    ss_dname_topdir << prefix_name << "_" << std::time(NULL);
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

    // Create the directories
    int err;
    err = mkdir(mout->dname_topdir,0777);
    err |= mkdir(mout->dname_datadir,0777);
    err |= mkdir(mout->dname_srcdir,0777);
    err |= mkdir(mout->dname_hwdatadir,0777);

    if(err)
    {
        std::cerr << "Mitos: Failed to create output directories!\n";
        return 1;
    }

    // Create file for raw sample output
    mout->fname_raw = strdup(std::string(std::string(mout->dname_datadir) + "/raw_samples.csv").c_str());
    mout->fout_raw = fopen(mout->fname_raw,"w");
    if(!mout->fout_raw)
    {
        std::cerr << "Mitos: Failed to create raw output file!\n";
        return 1;
    }

    // Create file for processed sample output
    mout->fname_processed = strdup(std::string(std::string(mout->dname_datadir) + "/samples.csv").c_str());
    mout->fout_processed = fopen(mout->fname_processed,"w");
    if(!mout->fout_processed)
    {
        std::cerr << "Mitos: Failed to create processed output file!\n";
        return 1;
    }


    mout->ok = true;

    return 0;
}

int Mitos_pre_process(mitos_output *mout)
{
    // Create hardware topology file for current hardware
    std::string fname_hardware = std::string(mout->dname_hwdatadir) + "/hwloc.xml";
    int err = dump_hardware_xml(fname_hardware.c_str());
    if(err)
    {
        std::cerr << "Mitos: Failed to create hardware topology file!\n";
        return 1;
    }

    // hwloc puts the file in the current directory, need to move it
    std::string fname_hardware_final = std::string(mout->dname_topdir) + "/hardware.xml";
    err = rename(fname_hardware.c_str(), fname_hardware_final.c_str());
    if(err)
    {
        std::cerr << "Mitos: Failed to move hardware topology file to output directory!\n";
        return 1;
    }

    // std::string fname_lshw = std::string(mout->dname_hwdatadir) + "/lshw.xml";
    // std::string lshw_cmd = "lshw -c memory -xml > " + fname_lshw;
    // err = system(lshw_cmd.c_str());
    // if(err)
    // {
    //     std::cerr << "Mitos: Failed to create hardware topology file!\n";
    //     return 1;
    // }

    return 0;
}

int Mitos_set_result_mout(mitos_output *mout, const char *prefix_name)
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

int Mitos_write_sample(perf_event_sample *sample, mitos_output *mout)
{
    if(!mout->ok)
        return 1;

    Mitos_resolve_symbol(sample);
  
    fprintf(mout->fout_raw,
            "%llu,%s,%llu,%llu,%llu,%llu,%llu,%u,%u,%llu,%llu,%u,%llu,",
            sample->ip,
            sample->data_symbol,
            sample->data_size,
            sample->num_dims,
            sample->access_index[0],
            sample->access_index[1],
            sample->access_index[2],
            sample->pid,
            sample->tid,
            sample->time,
            sample->addr,
            sample->cpu,
            sample->weight);
#if !defined(USE_IBS_FETCH) && !defined(USE_IBS_OP)
    fprintf(mout->fout_raw,
            "%s,%s,%s,%s,%s,",
            sample->mem_lvl,
            sample->mem_hit,
            sample->mem_op,
            sample->mem_snoop,
            sample->mem_tlb);
#endif            
    fprintf(mout->fout_raw, "%d",
            sample->numa_node);
#ifdef USE_IBS_FETCH

        fprintf(mout->fout_raw,
                ",%u,%u,%u, %u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%lx,%lx,%lx",
                sample->ibs_fetch_ctl.reg.ibs_fetch_max_cnt,
                sample->ibs_fetch_ctl.reg.ibs_fetch_cnt,
                sample->ibs_fetch_ctl.reg.ibs_fetch_lat,

                sample->ibs_fetch_ctl.reg.ibs_fetch_en,
                sample->ibs_fetch_ctl.reg.ibs_fetch_val,
                sample->ibs_fetch_ctl.reg.ibs_fetch_comp,
                sample->ibs_fetch_ctl.reg.ibs_ic_miss,
                sample->ibs_fetch_ctl.reg.ibs_phy_addr_valid,
                sample->ibs_fetch_ctl.reg.ibs_l1_tlb_pg_sz,
                //ibs_l1_tlb_pg_size.c_str(),
                sample->ibs_fetch_ctl.reg.ibs_l1_tlb_miss,
                sample->ibs_fetch_ctl.reg.ibs_l2_tlb_miss,
                sample->ibs_fetch_ctl.reg.ibs_rand_en,
                sample->ibs_fetch_ctl.reg.ibs_fetch_l2_miss,

                sample->ibs_fetch_lin,
                sample->ibs_fetch_phy.reg.ibs_fetch_phy_addr,
                sample->ibs_fetch_ext
        );
#endif // USE_IBS_FETCH
#ifdef USE_IBS_OP
        // op_ctl
        fprintf(mout->fout_raw,
                ",%u,%u,%u,%u,%u,%u,",
                sample->ibs_op_ctl.reg.ibs_op_max_cnt,
                sample->ibs_op_ctl.reg.ibs_op_en,
                sample->ibs_op_ctl.reg.ibs_op_val,
                sample->ibs_op_ctl.reg.ibs_op_cnt_ctl,
                sample->ibs_op_ctl.reg.ibs_op_max_cnt_upper,
                sample->ibs_op_ctl.reg.ibs_op_cur_cnt
        );
        // op_rip
        fprintf(mout->fout_raw,
                "%lx,", sample->ibs_op_rip);
        // op_data_1
        fprintf(mout->fout_raw,
                "%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,",
                sample->ibs_op_data_1.reg.ibs_comp_to_ret_ctr,
                sample->ibs_op_data_1.reg.ibs_tag_to_ret_ctr,
                sample->ibs_op_data_1.reg.ibs_op_brn_resync,
                sample->ibs_op_data_1.reg.ibs_op_misp_return,
                sample->ibs_op_data_1.reg.ibs_op_return,
                sample->ibs_op_data_1.reg.ibs_op_brn_taken,
                sample->ibs_op_data_1.reg.ibs_op_brn_misp,
                sample->ibs_op_data_1.reg.ibs_op_brn_ret,
                sample->ibs_op_data_1.reg.ibs_rip_invalid,
                sample->ibs_op_data_1.reg.ibs_op_brn_fuse,
                sample->ibs_op_data_1.reg.ibs_op_microcode
        );
        // op_data_2
        fprintf(mout->fout_raw,
                "%u,%u,%u,",
                sample->ibs_op_data_2.reg.ibs_nb_req_src,
                sample->ibs_op_data_2.reg.ibs_nb_req_dst_node,
                sample->ibs_op_data_2.reg.ibs_nb_req_cache_hit_st
        );
        // op_data_3
        fprintf(mout->fout_raw,
                "%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,",
                sample->ibs_op_data_3.reg.ibs_ld_op,
                sample->ibs_op_data_3.reg.ibs_st_op,
                sample->ibs_op_data_3.reg.ibs_dc_l1_tlb_miss,
                sample->ibs_op_data_3.reg.ibs_dc_l2_tlb_miss,
                sample->ibs_op_data_3.reg.ibs_dc_l1_tlb_hit_2m,
                sample->ibs_op_data_3.reg.ibs_dc_l1_tlb_hit_1g,
                sample->ibs_op_data_3.reg.ibs_dc_l2_tlb_hit_2m,
                sample->ibs_op_data_3.reg.ibs_dc_miss,
                sample->ibs_op_data_3.reg.ibs_dc_miss_acc,
                sample->ibs_op_data_3.reg.ibs_dc_ld_bank_con,
                sample->ibs_op_data_3.reg.ibs_dc_st_bank_con,
                sample->ibs_op_data_3.reg.ibs_dc_st_to_ld_fwd,
                sample->ibs_op_data_3.reg.ibs_dc_st_to_ld_can,
                sample->ibs_op_data_3.reg.ibs_dc_wc_mem_acc,
                sample->ibs_op_data_3.reg.ibs_dc_uc_mem_acc
        );
        fprintf(mout->fout_raw,
                "%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,",
                sample->ibs_op_data_3.reg.ibs_dc_locked_op,
                sample->ibs_op_data_3.reg.ibs_dc_no_mab_alloc,
                sample->ibs_op_data_3.reg.ibs_lin_addr_valid,
                sample->ibs_op_data_3.reg.ibs_phy_addr_valid,
                sample->ibs_op_data_3.reg.ibs_dc_l2_tlb_hit_1g,
                sample->ibs_op_data_3.reg.ibs_l2_miss,
                sample->ibs_op_data_3.reg.ibs_sw_pf,
                sample->ibs_op_data_3.reg.ibs_op_mem_width,
                sample->ibs_op_data_3.reg.ibs_op_dc_miss_open_mem_reqs,
                sample->ibs_op_data_3.reg.ibs_dc_miss_lat,
                sample->ibs_op_data_3.reg.ibs_tlb_refill_lat
        );
        //phy, lin and brs target
        fprintf(mout->fout_raw,
                "%lx,%lx,%lx",
                sample->ibs_op_phy.reg.ibs_dc_phys_addr,
                sample->ibs_op_lin,
                sample->ibs_op_brs_target
        );
#endif // USE_IBS_OP
    fprintf(mout->fout_raw, "\n");
    return 0;
}


int Mitos_add_offsets(const char * virt_address, mitos_output *mout){

    // Read the virtual address
    std::string loc = virt_address;
    std::ifstream foffset(loc);
    long long offsetAddr = 0;
    std::string str_offset;
    if(std::getline(foffset, str_offset).good())
    {
        offsetAddr = strtoll(str_offset.c_str(),NULL,0);
        str_offset += ",";
    }
    foffset.close();
    LOG_LOW("mitoshooks.cpp: add_offsets(), Raw file: "<< mout->fname_raw << ", virt_address_file: "<< loc <<", offset: " << offsetAddr);

    // Open the raw_samples.csv
    std::fstream fraw(mout->fname_raw, std::ios::in | std::ios::out); // Open the file for reading and writing

    if (!fraw.is_open()) {
        std::cerr << "Error opening: " << loc << "\n";
        return 1;
    }

    std::vector<std::string> lines; // Store lines in memory
    std::string line;

    // Read lines from the file into memory
    while (std::getline(fraw, line)) {
        if (!line.empty()) {
            line.insert(0, str_offset); // Insert '0,' at the beginning of the line
            lines.push_back(line);
        }
    }

    fraw.close(); // Close the file

    // Reopen the file in truncation mode
    fraw.open(mout->fname_raw, std::ios::out | std::ios::trunc);

    // Write modified lines back to the file
    for (const auto& modified_line : lines) {
        fraw << modified_line << std::endl;
    }

    fraw.close(); // Close the file
    LOG_LOW("mitoshooks.cpp: add_offsets(), Successfully added virtual address at the start of each line.");
    return 0;
}

void Mitos_write_samples_header(std::ofstream& fproc) {
    // Write header for processed samples
    fproc << "source,line,instruction,bytes,offset,ip,variable,buffer_size,dims,xidx,yidx,zidx,pid,tid,time,addr,cpu,latency,";
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

int Mitos_openFile(const char *bin_name, mitos_output *mout)
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
    LOG_MEDIUM("mitosoutput.cpp, Mitos_openFile(), bin_name: " << bin_name);
    sym_success = SymtabAPI::Symtab::openFile(symtab_obj,bin_name);
    LOG_MEDIUM("mitosoutput.cpp, Mitos_openFile(), sym_success: " << sym_success);
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
#endif // USE_DYNINST
}

int Mitos_post_process(const char *bin_name, mitos_output *mout, std::set<std::string>& src_files)
{
    int err = 0;
    // Open input/output files
    LOG_MEDIUM("mitosoutput.cpp, Mitos_post_process(), mout->fname_raw: " <<mout->fname_raw);
    std::ifstream fraw(mout->fname_raw);
    std::ofstream fproc(mout->fname_processed);
    
    symtab_code_src = new SymtabCodeSource(strdup(bin_name));

    // Get machine information
    unsigned int inst_length = InstructionDecoder::maxInstructionLength;
    Architecture arch = symtab_obj->getArchitecture();

    Mitos_write_samples_header(fproc);

    //get base (.text) virtual address of the measured process
    long long offsetAddr = 0;
    std::string str_offset;
    // Read raw samples one by one and get attribute from ip
    Dyninst::Offset ip;
    std::string line, ip_str;
    int tmp_line = 0;
    LOG_HIGH("mitosoutput.cpp: Mitos_post_process(), reading raw samples...");
    while(std::getline(fraw, line).good())
    {
        // Unknown values
        std::string source;
        std::stringstream line_num;
        std::stringstream instruction;
        std::stringstream bytes;

        // Extract offset     
        size_t offset_endpos = line.find(',');
        str_offset = line.substr(0,offset_endpos);
        offsetAddr = strtoll(str_offset.c_str(),NULL,0);
        // Extract ip
        size_t ip_endpos = (line.substr(offset_endpos+1)).find(',');
        std::string ip_str = line.substr(offset_endpos+1,ip_endpos);
        ip = (Dyninst::Offset)(strtoull(ip_str.c_str(),NULL,0) - offsetAddr);
        if(tmp_line%4000==0)
            LOG_MEDIUM("mitosoutput.cpp: Mitos_post_process(), Extracted instruction pointer (ip): " << ip);

        // Parse ip for source line info
        std::vector<SymtabAPI::Statement::Ptr> stats;
        sym_success = symtab_obj->getSourceLines(stats, ip);

        if(sym_success)
        {
            source = (string)stats[0]->getFile();
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
        LOG_HIGH("mitosoutput.cpp:Mitos_post_process(), writing out sample no. " << tmp_line + 1);
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
    std::cout << "Collected " << tmp_line << " samples\n";
    return 0;
}


int Mitos_merge_files(const std::string& dir_prefix, const std::string& dir_first_dir_prefix) {
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
        LOG_LOW("mitosoutput.cpp: Mitos_merge_files(), First Dir Found, copy Files From " << path_first_dir << " to result folder: ./" << dir_prefix << "result");
    }else {
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

    std::string path_samples_dest = path_dir_result + "/data/raw_samples.csv";
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
                LOG_LOW("mitosoutput.cpp: Mitos_merge_files(), Move Data " << dir_entry.path() << " to Result Folder...");
                // src file
                std::string path_samples_src = dir_entry.path().u8string() + "/data/raw_samples.csv";
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
    // TODO Copy Raw samples
    LOG_LOW("mitosoutput.cpp: Mitos_merge_files(), Merge successfully completed");
    return 0;
}

int Mitos_modify_samples(const std::string& dir_prefix, const std::map<std::string, std::string>& path_replacements){

    std::string samples_loc = "./"+ dir_prefix + "/data/samples.csv";

    // Open the raw_samples.csv
    std::fstream sample_file(samples_loc, std::ios::in | std::ios::out); // Open the file for reading and writing

    if (!sample_file.is_open()) {
        std::cerr << "Error opening: " << samples_loc << "\n";
        return 1;
    }

    std::vector<std::string> lines; // Store lines in memory
    std::string line;

    // Read lines from the file into memory
    while (std::getline(sample_file, line)) {
        if (!line.empty()) {
            size_t source_info = line.find(',');
            auto src = line.substr(0,source_info);
            auto it = path_replacements.find(src);
            if (it != path_replacements.end()) {
                src = path_replacements.at(src);
                src += ',';
                line = src + line.substr(source_info+1);
            }
            lines.push_back(line);
        }
    }

    sample_file.close(); // Close the file

    // Reopen the file in truncation mode
    sample_file.open(samples_loc, std::ios::out | std::ios::trunc);

    // Write modified lines back to the file
    for (const auto& modified_line : lines) {
        sample_file << modified_line << std::endl;
    }

    sample_file.close(); // Close the file
    LOG_LOW("mitoshooks.cpp: Mitos_modify_samples(), Successfully modified the locations of the samples.");
    return 0;
}

int Mitos_copy_sources(const std::string& dir_prefix, const std::set<std::string>& src_files) {
    
    std::string path_dir_result = "./"+ dir_prefix;
    // copy source files

    std::cout <<  "Copying following source files to result folder: \n";
    
    for (const auto &src : src_files)
    {
        std::cout << src;
    }
    
    
    
    std::string path_src_dir = path_dir_result + "/src";

    auto common_prefix = [&]() -> std::pair<std::string, int> {    
        auto iter = src_files.begin();
        std::string prefix = ((*iter).substr(0,4) == "/usr" ) ? "" : (*iter);
        int ans = prefix.length(), n = src_files.size();

        for(auto it = ++iter; it != src_files.end(); ++it){
            int j = 0;
            auto temp = *it;
            if(temp.substr(0,4) == "/usr") continue;

            while(j<temp.length() && temp[j] == prefix[j]) j++;
            ans = std::min(ans, j);
        }
        prefix = prefix.substr(0, ans);
        std::pair<std::string, int> commonPath;
        commonPath.first = "";
        commonPath.second = 0;
        if(prefix.length() > 1)
        {
            size_t lastSlashPos = prefix.find_last_of('/');

            if (lastSlashPos != std::string::npos) {
                // Extract the substring up to the last occurrence of '/'
                commonPath.first = prefix.substr(0, lastSlashPos + 1); // Include the '/' in the result
                commonPath.second = lastSlashPos + 1;
                LOG_LOW("mitosoutput.cpp: Mitos_copy_sources(), Common File Path" << commonPath.first);
                return commonPath;
            } else {
                LOG_LOW("mitosoutput.cpp: Mitos_copy_sources(), No '/' found in the path.");
                return commonPath;
            }
        }
        return commonPath;

    };

    auto common_path = common_prefix();
    std::map<std::string, std::string> path_replacements;
    for (auto& src_file : src_files) {
        if(src_file.substr(0,4) == "/usr") continue;
        path_replacements[src_file] = src_file.substr(common_path.second);
    }

    for (auto &path:path_replacements)
    {
        LOG_LOW("mitosoutput.cpp: Mitos_copy_sources(), Original filepath: " << path.first << ", Modfied filepath: " << path.second);
    }
    

    for (auto& src_file : src_files) {
        if(src_file.substr(0,4) == "/usr") continue;
        try {
            // Check if source file exists
            if (!fs::exists(src_file)) {
                std::cerr << "Source file not accessible: " << src_file << "\n";
            }

            auto temp = path_src_dir;
            size_t lastSlashPos = path_replacements[src_file].find_last_of('/');

            if (lastSlashPos != std::string::npos) {
                // Extract the substring up to the last occurrence of '/'
                temp += path_replacements[src_file].substr(0, lastSlashPos + 1); // Include the '/' in the result
                std::cout << "\nTemp Path: " << temp << std::endl;
            }

            if (!fs::exists(temp)) {
                fs::create_directories(temp);
            }
              
            // Copy the file
            fs::copy(src_file, temp);

        } catch (const std::exception& ex) {
            std::cerr << "Error: " << ex.what() << "\n";
        }   
    }

    std::cout << "Copied all the files. Post-processing finished.\n";
    Mitos_modify_samples(dir_prefix, path_replacements);
    return 0;
}
