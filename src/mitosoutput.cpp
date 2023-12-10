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
//#include <LineInformation.h> // symtabAPI
#include <CodeObject.h> // parseAPI
#include <InstructionDecoder.h> // instructionAPI
#include <Module.h>
using namespace Dyninst;
using namespace SymtabAPI;
using namespace InstructionAPI;
using namespace ParseAPI;

#include "x86_util.h" // getReadSize using instructionAPI

#endif // USE_DYNINST

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

    //copy over source code to mitos output folder
    if (!mout->dname_srcdir_orig.empty())
    {
        if(!fs::exists(mout->dname_srcdir_orig))
        {
            std::cerr << "Mitos: Source code path " << mout->dname_srcdir_orig << "does not exist!\n";
            return 1;
        }
        std::error_code ec;
        fs::copy(mout->dname_srcdir_orig, mout->dname_srcdir, ec);
        if(ec)
        {
            std::cerr << "Mitos: Source code path " << mout->dname_srcdir_orig << "was not copied. Error " << ec.value() << ".\n";
            return 1;
        }
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

    std::string fname_lshw = std::string(mout->dname_hwdatadir) + "/lshw.xml";
    std::string lshw_cmd = "lshw -c memory -xml > " + fname_lshw;
    err = system(lshw_cmd.c_str());
    if(err)
    {
        std::cerr << "Mitos: Failed to create hardware topology file!\n";
        return 1;
    }

    return 0;
}

int Mitos_write_sample(perf_event_sample *sample, mitos_output *mout)
{
    if(!mout->ok)
        return 1;

    Mitos_resolve_symbol(sample);
  
    fprintf(mout->fout_raw,
            "%llu,%s,%llu,%llu,%llu,%llu,%llu,%u,%u,%llu,%llu,%u,%llu,%s,%s,%s,%s,%s,%d",
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
            sample->weight,
            sample->mem_lvl,
            sample->mem_hit,
            sample->mem_op,
            sample->mem_snoop,
            sample->mem_tlb,
            sample->numa_node);
#ifdef USE_IBS_FETCH
//        std::string ibs_l1_tlb_pg_size = "error";
//        switch(sample->ibs_fetch_ctl.reg.ibs_l1_tlb_pg_sz){
//            case 0:
//                ibs_l1_tlb_pg_size ="4KB";
//                break;
//            case 1:
//                ibs_l1_tlb_pg_size = "2MB";
//                break;
//            case 2:
//                ibs_l1_tlb_pg_size = "1GB";
//                break;
//            default:
//                break;
//        }
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

void Mitos_write_samples_header(std::ofstream& fproc) {
    // Write header for processed samples
    //std::ofstream fproc = fproc_pointer;
    fproc << "source,line,instruction,bytes,ip,variable,buffer_size,dims,xidx,yidx,zidx,pid,tid,time,addr,cpu,latency,level,hit_type,op_type,snoop_mode,tlb_access,numa";
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

int Mitos_post_process(const char *bin_name, mitos_output *mout)
{
    int err = 0;
    fflush(mout->fout_raw); // flush raw samples stream before post processing starts

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
#else // NO DYNINST
    // Open Symtab object and code source object
    SymtabAPI::Symtab *symtab_obj;
    SymtabCodeSource *symtab_code_src;
    
    int sym_success = SymtabAPI::Symtab::openFile(symtab_obj,bin_name);
    if(!sym_success)
    {
        std::cerr << "Mitos: Failed to open Symtab object for " << bin_name << std::endl;
        std::cerr << "Saving raw data (no source/instruction attribution)" << std::endl;

        fraw.close();
        fproc.close();


        err = rename(mout->fname_raw, mout->fname_processed);
        if(err)
        {
            std::cerr << "Mitos: Failed to rename raw output to " << mout->fname_processed << std::endl;
        }



        return 1;
    }

    symtab_code_src = new SymtabCodeSource(strdup(bin_name));

    // Get machine information
    unsigned int inst_length = InstructionDecoder::maxInstructionLength;
    Architecture arch = symtab_obj->getArchitecture();

    Mitos_write_samples_header(fproc);

    //get base (.text) virtual address of the measured process
    //std::ifstream foffset("/u/home/vanecek/sshfs/sv_mitos/build/test3.txt");
    // TODO: Replace line
    std::ifstream foffset("/tmp/virt_address.txt");
    long long offsetAddr = 0;
    string str_offset;
    if(std::getline(foffset, str_offset).good())
    {
        offsetAddr = strtoll(str_offset.c_str(),NULL,0);
    }
    foffset.close();
    cout << "offset: " << offsetAddr << endl;

    // Read raw samples one by one and get attribute from ip
    Dyninst::Offset ip;
    size_t ip_endpos;
    std::string line, ip_str;
    int tmp_line = 0;
    LOG_MEDIUM("mitosoutput.cpp:Mitos_post_process(), reading raw samples...");

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
            cout << "ip: " << ip <<"\n";
        // Parse ip for source line info
        std::vector<SymtabAPI::Statement::Ptr> stats;
        sym_success = symtab_obj->getSourceLines(stats, ip);

        // if(tmp_line%100==0)
        // {
        //     Offset addressInRange = ip;
        //     std::vector<SymtabAPI::Statement::Ptr> lines = stats;
        //
        //     unsigned int originalSize = lines.size();
        //     std::set<Module*> mods_for_offset;
        //     symtab_obj->findModuleByOffset(mods_for_offset, addressInRange);
        //
        //     cout << "mods found: " << mods_for_offset.size() << endl;
        //     for(auto i = mods_for_offset.begin();
        //     i != mods_for_offset.end();
        //     ++i)
        //     {
        //         (*i)->getSourceLines(lines, addressInRange);
        //     }
        //
        //     cout << "line " << tmp_line << "sym_success " << sym_success << endl;
        //
        //     // const_iterator start_addr_valid = project<SymtabAPI::Statement::addr_range>(get<SymtabAPI::Statement::upper_bound>().lower_bound(addressInRange ));
        //     // const_iterator end_addr_valid = impl_t::upper_bound(addressInRange );
        //     // cout << start_addr_valid << endl << end_addr_valid << endl;
        //     // while(start_addr_valid != end_addr_valid && start_addr_valid != end())
        //     // {
        //     //     if(*(*start_addr_valid) == addressInRange)
        //     //     {
        //     //         lines.push_back(*start_addr_valid);
        //     //     }
        //     //     ++start_addr_valid;
        //     // }
        //
        //     cout << std::hex << "Statement: < [" << stats[0]->startAddr() << ", " << stats[0]->endAddr() << "): "
        //    << std::dec << stats[0]->getFile() << ":" << stats[0]->getLine() << " >" << endl;
        // }

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

#endif // USE_DYNINST

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
        LOG_LOW("mitosoutput.cpp:Mitos_merge_files(), First Dir Found, copy Files From " << path_first_dir << " to result folder: ./" << dir_prefix << "result");
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
    //fs::copy(path_first_dir + "/data", path_dir_result + "/data");
    //fs::copy(path_first_dir + "/hwdata", path_dir_result + "/hwdata");
    //fs::copy(path_first_dir + "/src", path_dir_result + "/src");
    // delete first folder
    fs::remove_all(path_first_dir);

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
                LOG_LOW("mitosoutput.cpp:Mitos_merge_files(), Move Data " << dir_entry.path() << " to Result Folder...");
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
                    fs::remove_all(dir_entry.path().u8string());
                }
            }
        } // END LOOP
        file_samples_out.close();
    }
    // TODO Copy Raw samples
    std::cout << "Merge successfully completed\n";
    return 0;
}