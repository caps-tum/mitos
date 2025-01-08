#ifndef MITOS_H
#define MITOS_H

#include <string.h>
#include <string>
#include <stdlib.h>
#include <stdio.h>
#include <inttypes.h>
#include <set>
#include <map>
#include "ibs_types.h"

// Define verbosity levels
#define VERBOSE_LOW 1
#define VERBOSE_MEDIUM 2
#define VERBOSE_HIGH 3

// Set the current verbosity level (change this value to adjust verbosity)
#ifndef VERBOSITY 
#define VERBOSITY 0 
#endif

// Verbose output macros
#define LOG_LOW(message) do { if (VERBOSITY >= VERBOSE_LOW) std::cout << "[LOW] " << message << std::endl; } while(0)
#define LOG_MEDIUM(message) do { if (VERBOSITY >= VERBOSE_MEDIUM) std::cout << "[MEDIUM] " << message << std::endl; } while(0)
#define LOG_HIGH(message) do { if (VERBOSITY >= VERBOSE_HIGH) std::cout << "[HIGH] " << message << std::endl; } while(0)


struct mem_symbol;
struct perf_event_sample;
struct mitos_output;

typedef void (*sample_handler_fn_t)(struct perf_event_sample *sample, void *args);
typedef void (*end_fn_t)(void *args);

/*
 * Mitos:
 * All programs must invoke from these functions.
 */

#ifdef __cplusplus
extern "C" {
#endif

// Sampler configuration
void Mitos_set_pid(pid_t pid);
void Mitos_set_sample_time_frequency(uint64_t p);
void Mitos_set_sample_event_period(uint64_t p);
void Mitos_set_sample_latency_threshold(uint64_t t);
void Mitos_set_handler_fn(sample_handler_fn_t h, void *args);

// Sampler invocation
int Mitos_begin_sampler();
void Mitos_end_sampler();

// Memory attribution
void Mitos_add_symbol(const char* n, void *a, size_t s, size_t *dims, unsigned int ndims);
void Mitos_remove_symbol(const char* n);
int Mitos_resolve_symbol(struct perf_event_sample *s);

// Get friendly sample information
long Mitos_x_index(struct perf_event_sample *s);
long Mitos_y_index(struct perf_event_sample *s);
long Mitos_z_index(struct perf_event_sample *s);

// Output
int Mitos_create_output(struct mitos_output *mout, long uid, long tid = 0);
int Mitos_pre_process(struct mitos_output *mout);
int Mitos_set_result_mout(mitos_output *mout, std::string prefix_name);
int Mitos_write_sample(struct perf_event_sample *s, struct mitos_output *mout);
int Mitos_add_offsets(const char * virt_address, mitos_output *mout);
int Mitos_process_binary(const char *bin_name, mitos_output *mout);
int Mitos_post_process(const char *bin_name, mitos_output *mout, std::string dir_prefix = "");
int Mitos_merge_files(long unique_id, std::string &result_dir);
int Mitos_modify_samples(const std::string& dir_prefix, const std::map<std::string, std::string>& path_replacements);
int Mitos_copy_sources(const std::string& dir_prefix, const std::set<std::string>& src_files);
#ifdef __cplusplus
} // extern "C"
#endif

/*
 * perf_event_sample:
 * Struct containing all raw perf event information
 */

struct perf_event_sample
{
    uint64_t   sample_id;           /* if PERF_SAMPLE_IDENTIFIER */
    uint64_t   ip;                  /* if PERF_SAMPLE_IP */
    uint32_t   pid, tid;            /* if PERF_SAMPLE_TID */
    uint64_t   time;                /* if PERF_SAMPLE_TIME */
    uint64_t   addr;                /* if PERF_SAMPLE_ADDR */
    uint64_t   id;                  /* if PERF_SAMPLE_ID */
    uint64_t   stream_id;           /* if PERF_SAMPLE_STREAM_ID */
    uint32_t   cpu, res;            /* if PERF_SAMPLE_CPU */
    uint64_t   period;              /* if PERF_SAMPLE_PERIOD */
    uint32_t   raw_size;            /* if PERF_SAMPLE_RAW */
    char      *raw_data;            /* if PERF_SAMPLE_RAW */
    uint64_t   weight;              /* if PERF_SAMPLE_WEIGHT */
    uint64_t   data_src;            /* if PERF_SAMPLE_DATA_SRC */
    uint64_t   transaction;         /* if PERF_SAMPLE_TRANSACTION */

    size_t data_size;
    size_t num_dims;
    size_t access_index[3];
    const char *data_symbol;

    const char *mem_hit;
    const char *mem_lvl;
    const char *mem_op;
    const char *mem_snoop;
    const char *mem_lock;
    const char *mem_tlb;
    int numa_node;
#ifdef USE_IBS_FETCH
    ibs_fetch_ctl_t ibs_fetch_ctl;
    uint64_t ibs_fetch_lin;
    ibs_fetch_phys_addr ibs_fetch_phy;
    uint64_t ibs_fetch_ext;
#endif
#ifdef USE_IBS_OP
    ibs_op_ctl_t ibs_op_ctl;
    uint64_t ibs_op_rip;  // IBS Op Logical Address
    ibs_op_data1_t ibs_op_data_1;
    ibs_op_data2_t ibs_op_data_2;
    ibs_op_data3_t ibs_op_data_3;
    ibs_op_dc_phys_addr_t ibs_op_phy;
    uint64_t ibs_op_lin;
    uint64_t ibs_op_brs_target;
#endif

};

struct mitos_output
{
    int ok;

    char *dname_topdir;
    char *dname_datadir;
    char *dname_srcdir;
    char *dname_hwdatadir;

    char *fname_raw;
    char *fname_processed;

    FILE *fout_raw;
    FILE *fout_processed;

    char *fname_mpi_traces;
    FILE *fout_mpi_traces;
};

#endif // MITOS_H