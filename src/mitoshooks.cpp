#include "mitoshooks.h"

#include "Mitos.h"
#include "virtual_address_writer.h"

#include <stdio.h>
#include <dlfcn.h>
#include <cstdlib>
#include <unistd.h>
#include <sys/ptrace.h>
#include <sys/wait.h>
#include <sys/syscall.h>
#include <vector>
#include <fstream>
#include <sstream>
// #define _GNU_SOURCE // sched_getcpu(3) is glibc-specific (see the man page)
#include <sched.h>
#include <cassert>
#include <ctime>
#include <set>
#include <chrono>

// PAPI START
#include <papi.h>
// PAPI END

// 512 should be enough for xeon-phi
#define MAX_THREADS 512
#define DEFAULT_PERIOD 4000
#define DEFAULT_LATENCY 4
#define DEFAULT_FREQ 4000

#define MITOS_MPI_TRACING


thread_local static mitos_output mout;
thread_local static char* virt_address;
long ts_output_prefix_omp;
long tid_omp_first;
static bool set_period = true;

void read_mitos_envs(int &sampling_period, int &latency_threshold, int &sampling_frequency){

    const char* latency_input = std::getenv("MITOS_LATENCY_THRESHOLD");
    latency_threshold = DEFAULT_LATENCY;
    if (latency_input != nullptr) {
        latency_threshold = std::atoi(latency_input);
    } 

    const char* freq_input = std::getenv("MITOS_SAMPLING_FREQUENCY");
    sampling_frequency = DEFAULT_FREQ;
    if (freq_input != nullptr) {
        sampling_frequency = std::atoi(freq_input);
        set_period = false;
    }

    const char* sampling_input = std::getenv("MITOS_SAMPLING_PERIOD");
    if (sampling_input != nullptr) {
        sampling_period = std::atoi(sampling_input);
        set_period = true;
    }
}

#ifdef USE_MPI
// MPI hooks
long ts_output = 0;

int tracing_mpi_rank;

void sample_handler(perf_event_sample *sample, void *args)
{
    LOG_HIGH("mitoshooks.cpp: sample_handler(), MPI handler sample: cpu= " << sample->cpu << " tid= "  << sample->tid);
    auto ts = std::chrono::high_resolution_clock::now();
    uint64_t ull_ts = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(ts.time_since_epoch()).count());
    sample->time = ull_ts;
    sample->pid = tracing_mpi_rank;
    Mitos_write_sample(sample, &mout);
}

int EventSet = PAPI_NULL;
unsigned long long ull_papi_start = 0;
int pmpi_start_papi(const char* env_MITOS_MEASURE_PAPI)
{
    // PAPI START: Initialize PAPI library
    int retval;
    // long long values[4];  // Array to hold PAPI event values
    // int EventSet = PAPI_NULL;  // PAPI Event Set
    if ((retval = PAPI_library_init(PAPI_VER_CURRENT)) != PAPI_VER_CURRENT) {
        fprintf(stderr, "PAPI library init error!\n");
        MPI_Abort(MPI_COMM_WORLD, retval);
    }
    // Create an EventSet
    if (PAPI_create_eventset(&EventSet) != PAPI_OK) {
        fprintf(stderr, "Error creating EventSet\n");
        MPI_Abort(MPI_COMM_WORLD, -1);
    }
    // Add the load instructions event (PAPI_LD_INS) to the EventSet
    if (PAPI_add_event(EventSet, PAPI_LD_INS) != PAPI_OK) {
        fprintf(stderr, "Error adding PAPI_LD_INS\n");
        MPI_Abort(MPI_COMM_WORLD, -1);
    }
    if (PAPI_add_event(EventSet, PAPI_L1_LDM) != PAPI_OK) {
        fprintf(stderr, "Error adding PAPI_L1_LDM\n");
        MPI_Abort(MPI_COMM_WORLD, -1);
    }
    if (PAPI_add_event(EventSet, PAPI_TOT_CYC) != PAPI_OK) {
        fprintf(stderr, "Error adding PAPI_TOT_CYC\n");
        MPI_Abort(MPI_COMM_WORLD, -1);
    }
    if (PAPI_add_event(EventSet, PAPI_L3_LDM) != PAPI_OK) {
        fprintf(stderr, "Error adding PAPI_L3_LDM\n");
        MPI_Abort(MPI_COMM_WORLD, -1);
    }
    if (PAPI_start(EventSet) != PAPI_OK) {
        fprintf(stderr, "Error starting PAPI counters\n");
        MPI_Abort(MPI_COMM_WORLD, -1);
    }
    // PAPI END

    auto start = std::chrono::high_resolution_clock::now();
    ull_papi_start = static_cast<unsigned long long>(std::chrono::duration_cast<std::chrono::nanoseconds>(start.time_since_epoch()).count());

    return 0;
}
int pmpi_end_papi(const char* env_MITOS_MEASURE_PAPI)
{
    int r;
    MPI_Comm_rank(MPI_COMM_WORLD, &r);
    long long values[4];  // Array to hold PAPI event values
    // PAPI START: Stop the PAPI counters
    if (PAPI_stop(EventSet, values) != PAPI_OK) {
        fprintf(stderr, "Error stopping PAPI counters\n");
        MPI_Abort(MPI_COMM_WORLD, -1);
    }
    auto end = std::chrono::high_resolution_clock::now();
    unsigned long long ull_papi_end = static_cast<unsigned long long>(std::chrono::duration_cast<std::chrono::nanoseconds>(end.time_since_epoch()).count());
    unsigned long long papi_diration_ns = ull_papi_end - ull_papi_start;

    long long LD_INS = values[0];
    long long L1_LDM = values[1];
    long long TOT_CYC = values[2];
    long long L3_LDM = values[3];
    double LD_density = (double)LD_INS/(double)TOT_CYC;
    long long L1_miss_density = LD_INS/L1_LDM;
    long long mem_hit_density = LD_INS/L3_LDM;
    std::cout << std::fixed << "PAPI_LD_INS," << LD_INS << ",  LD_density," << LD_density << ",  L1_miss_density," << L1_miss_density << ",  mem_hit_density," << mem_hit_density << ",  PAPI_L1_LDM," << L1_LDM << ",  PAPI_L3_LDM," << L3_LDM << ",  PAPI_TOT_CYC," << TOT_CYC << ",  time," << papi_diration_ns <<std::endl;
    // printf("Rank %d - PAPI_LD_INS: %lld   PAPI_L1_LDM: %lld   PAPI_TOT_CYC: %lld   PAPI_L3_LDM: %lld\n", r, values[0],values[1],values[2],values[3]);
    PAPI_shutdown();
    // PAPI END

    //create papi_measurements
    char* papi_values_filename = strdup(std::string(std::string(env_MITOS_MEASURE_PAPI) + "/data/papi_measurements_" + std::to_string(r) + ".csv").c_str());
    FILE* papi_values_file = fopen(papi_values_filename,"w");
    if(!papi_values_file)
    {
        std::cerr << "Mitos: Failed to create papi_values_file file!\n";
        return 1;
    }
    std::string output = "PAPI_LD_INS," + std::to_string(LD_INS) + "\n";
    output += "LD_density," + std::to_string(LD_density) + "\n";
    output += "L1_miss_density," + std::to_string(L1_miss_density) + "\n";
    output += "mem_hit_density," + std::to_string(mem_hit_density) + "\n";
    output += "PAPI_L1_LDM," + std::to_string(L1_LDM) + "\n";
    output += "PAPI_L3_LDM," + std::to_string(L3_LDM) + "\n";
    output += "PAPI_TOT_CYC," + std::to_string(TOT_CYC) + "\n";
    output += "time," + std::to_string(papi_diration_ns) + "\n";
    if (fputs(output.c_str(), papi_values_file) == EOF) {perror("Error writing to file");}
        return 0;
    // papi_values_file->close();

    return 0;
}
int pmpi_init_mitos()
{
    // send timestamp from rank 0 to all others to synchronize folder prefix
    int mpi_rank;
    MPI_Comm_rank(MPI_COMM_WORLD, &mpi_rank);
    ts_output = std::time(NULL);
    MPI_Bcast(&ts_output, 1, MPI_LONG, 0, MPI_COMM_WORLD);

    char rank_prefix[54];
    sprintf(rank_prefix, "mitos_%ld_out_%d_", ts_output, mpi_rank);

    //TODO string 
    virt_address = new char[(strlen(rank_prefix) + strlen("/tmp/") + strlen("virt_address.txt") + 1)];
    strcpy(virt_address, "/tmp/");
    strcat(virt_address, rank_prefix);
    strcat(virt_address, "virt_address.txt");
    Mitos_save_virtual_address_offset(std::string(virt_address));
    
    // Take user inputs
    int sampling_period = DEFAULT_PERIOD;
    int latency_threshold = DEFAULT_LATENCY;
    int sampling_frequency = DEFAULT_FREQ;
    read_mitos_envs(sampling_period, latency_threshold, sampling_frequency);
    
    // configure Mitos sampling
    Mitos_set_sample_latency_threshold(latency_threshold);
    if (mpi_rank == 0)
        std::cout << "[Mitos] Mitos sampling parameters: Latency threshold = " << latency_threshold << ", ";
    if(set_period){
        Mitos_set_sample_event_period(sampling_period);
        if (mpi_rank == 0)
            std::cout << "Sampling period: " << sampling_period <<"\n";
    }
    else {
        Mitos_set_sample_time_frequency(sampling_frequency);
        if (mpi_rank == 0)
            std::cout << "Sampling frequency: " << sampling_frequency <<"\n";
    }
    Mitos_set_handler_fn(&sample_handler,NULL);
    pid_t curpid = getpid();
    Mitos_set_pid(curpid);

    Mitos_create_output(&mout, ts_output, mpi_rank);
    LOG_LOW("mitoshooks.cpp: MPI_Init(), Curpid: " << curpid << ", Rank: " << mpi_rank);
    Mitos_pre_process(&mout);

    std::cout << "[Mitos] Begin sampler, rank: " << mpi_rank << "\n";
    Mitos_begin_sampler();

    return 0;
}



int pmpi_init_mpi_tracing()
{
#ifndef MITOS_MPI_TRACING
    return 0;
#endif 
    MPI_Comm_rank(MPI_COMM_WORLD, &tracing_mpi_rank);
    // std::cout << "pmpi_init_mpi_tracing: tracing_mpi_rank = " << tracing_mpi_rank << std::endl;
    return 0;
}

int MPI_Isend(const void *buf, int count, MPI_Datatype datatype, int dest, int tag, MPI_Comm comm, MPI_Request *request)
{
    auto start = std::chrono::high_resolution_clock::now();
    unsigned long long ull_start = static_cast<unsigned long long>(std::chrono::duration_cast<std::chrono::nanoseconds>(start.time_since_epoch()).count());

    int ret = PMPI_Isend(buf, count, datatype, dest, tag, comm, request);
    if(std::getenv("MITOS_MEASURE_PAPI") != nullptr)
        return ret;
    
    auto end = std::chrono::high_resolution_clock::now();
    unsigned long long ull_end = static_cast<unsigned long long>(std::chrono::duration_cast<std::chrono::nanoseconds>(end.time_since_epoch()).count());


    std::string dtype;
    if(datatype == MPI_DOUBLE)
        dtype = "MPI_DOUBLE";
    else if(datatype == MPI_INT)
        dtype = "MPI_INT";
    else
        dtype = std::to_string(reinterpret_cast<std::uintptr_t>(datatype));
    std::string mpi_comm;
    if(comm == MPI_COMM_WORLD)
        mpi_comm = "MPI_COMM_WORLD";
    else
        mpi_comm = std::to_string(reinterpret_cast<std::uintptr_t>(comm));
    
    std::string trace = std::to_string(tracing_mpi_rank) + ";MPI_Isend;";
    trace += std::to_string(reinterpret_cast<std::uintptr_t>(__builtin_return_address(0))) + ";";    trace += std::to_string(ull_start) + ";" + std::to_string(ull_end) + ";";
    trace += std::to_string(reinterpret_cast<std::uintptr_t>(buf)) + ";" + std::to_string(count) + ";" + dtype + ";";
    trace += std::to_string(dest) + ";" + mpi_comm + ";";
    trace += std::to_string(reinterpret_cast<std::uintptr_t>(request))+ ";";
    trace += std::to_string(tag) + ";";
    trace += "\n";
    if (fputs(trace.c_str(), mout.fout_mpi_traces) == EOF) {perror("Error writing to file");}
    return ret;
}

int MPI_Irecv(void *buf, int count, MPI_Datatype datatype, int source, int tag, MPI_Comm comm, MPI_Request *request)
{
    auto start = std::chrono::high_resolution_clock::now();
    unsigned long long ull_start = static_cast<unsigned long long>(std::chrono::duration_cast<std::chrono::nanoseconds>(start.time_since_epoch()).count());

    int ret = PMPI_Irecv(buf, count, datatype, source, tag, comm, request);
    if(std::getenv("MITOS_MEASURE_PAPI") != nullptr)
        return ret;

    auto end = std::chrono::high_resolution_clock::now();
    unsigned long long ull_end = static_cast<unsigned long long>(std::chrono::duration_cast<std::chrono::nanoseconds>(end.time_since_epoch()).count());


    
    std::string dtype;
    if(datatype == MPI_DOUBLE)
        dtype = "MPI_DOUBLE";
    else if(datatype == MPI_INT)
        dtype = "MPI_INT";
    else
        dtype = std::to_string(reinterpret_cast<std::uintptr_t>(datatype));
    std::string mpi_comm;
    if(comm == MPI_COMM_WORLD)
        mpi_comm = "MPI_COMM_WORLD";
    else
        mpi_comm = std::to_string(reinterpret_cast<std::uintptr_t>(comm));

    std::string trace = std::to_string(tracing_mpi_rank) + ";MPI_Irecv;";
    trace += std::to_string(reinterpret_cast<std::uintptr_t>(__builtin_return_address(0))) + ";";
    trace += std::to_string(ull_start) + ";" + std::to_string(ull_end) + ";";
    trace += std::to_string(reinterpret_cast<std::uintptr_t>(buf)) + ";" + std::to_string(count) + ";" + dtype + ";";
    trace += std::to_string(source) + ";" + mpi_comm + ";";
    trace += std::to_string(reinterpret_cast<std::uintptr_t>(request))+ ";";
    trace += std::to_string(tag) + ";";
    trace += "\n";
    if (fputs(trace.c_str(), mout.fout_mpi_traces) == EOF) {perror("Error writing to file");}
    return ret;
}

int MPI_Waitall(int count, MPI_Request array_of_requests[], MPI_Status array_of_statuses[])
{
    auto start = std::chrono::high_resolution_clock::now();
    unsigned long long ull_start = static_cast<unsigned long long>(std::chrono::duration_cast<std::chrono::nanoseconds>(start.time_since_epoch()).count());

    int ret = PMPI_Waitall(count, array_of_requests, array_of_statuses);
    if(std::getenv("MITOS_MEASURE_PAPI") != nullptr)
        return ret;

    auto end = std::chrono::high_resolution_clock::now();
    unsigned long long ull_end = static_cast<unsigned long long>(std::chrono::duration_cast<std::chrono::nanoseconds>(end.time_since_epoch()).count());

    std::string trace = std::to_string(tracing_mpi_rank) + ";MPI_Waitall;";
    trace += std::to_string(reinterpret_cast<std::uintptr_t>(__builtin_return_address(0))) + ";";
    trace += std::to_string(ull_start) + ";" + std::to_string(ull_end) + ";";
    trace += std::to_string(reinterpret_cast<std::uintptr_t>(&(array_of_requests[0]))) + ";" + std::to_string(count) + ";";
    trace += "\n";
    if (fputs(trace.c_str(), mout.fout_mpi_traces) == EOF) {perror("Error writing to file");}
    return ret;
}

int MPI_Wait(MPI_Request *request, MPI_Status *status)
{
    auto start = std::chrono::high_resolution_clock::now();
    unsigned long long ull_start = static_cast<unsigned long long>(std::chrono::duration_cast<std::chrono::nanoseconds>(start.time_since_epoch()).count());

    int ret = PMPI_Wait(request, status);
    if(std::getenv("MITOS_MEASURE_PAPI") != nullptr)
        return ret;

    auto end = std::chrono::high_resolution_clock::now();
    unsigned long long ull_end = static_cast<unsigned long long>(std::chrono::duration_cast<std::chrono::nanoseconds>(end.time_since_epoch()).count());


    std::string trace = std::to_string(tracing_mpi_rank) + ";MPI_Wait;";
    trace += std::to_string(reinterpret_cast<std::uintptr_t>(__builtin_return_address(0))) + ";";
    trace += std::to_string(ull_start) + ";" + std::to_string(ull_end) + ";";
    trace += std::to_string(reinterpret_cast<std::uintptr_t>(request)) + ";";
    trace += "\n";
    if (fputs(trace.c_str(), mout.fout_mpi_traces) == EOF) {perror("Error writing to file");}
    return ret;
}

int MPI_Init(int *argc, char ***argv)
{
    fprintf(stderr, "[Mitos] MPI_Init hook\n");

    auto start = std::chrono::high_resolution_clock::now();
    unsigned long long ull_start = static_cast<unsigned long long>(std::chrono::duration_cast<std::chrono::nanoseconds>(start.time_since_epoch()).count());

    int ret = PMPI_Init(argc, argv);

    auto end = std::chrono::high_resolution_clock::now();
    unsigned long long ull_end = static_cast<unsigned long long>(std::chrono::duration_cast<std::chrono::nanoseconds>(end.time_since_epoch()).count());

    const char* env_MITOS_MEASURE_PAPI = std::getenv("MITOS_MEASURE_PAPI");
    if(env_MITOS_MEASURE_PAPI != nullptr)
    {
        std::cout << "[Mitos-PAPI] MPI_Init\n";
        pmpi_start_papi(env_MITOS_MEASURE_PAPI);
    }
    else
    {
        pmpi_init_mitos();

        pmpi_init_mpi_tracing();

        std::string trace = std::to_string(tracing_mpi_rank) + ";MPI_Init;";
        trace += std::to_string(reinterpret_cast<std::uintptr_t>(__builtin_return_address(0))) + ";";
        trace += std::to_string(ull_start) + ";" + std::to_string(ull_end) + ";";
        trace += "\n";
        if (fputs(trace.c_str(), mout.fout_mpi_traces) == EOF) {perror("Error writing to file");}
    }

    return ret;
}

int MPI_Init_thread(int *argc, char ***argv, int required, int *provided)
{
    fprintf(stderr, "[Mitos] MPI_Init_thread hook\n");
    int ret = PMPI_Init_thread(argc, argv, required, provided);

    pmpi_init_mitos();

    return ret;
}

int MPI_Finalize()
{
    const char* env_MITOS_MEASURE_PAPI = std::getenv("MITOS_MEASURE_PAPI");
    if(env_MITOS_MEASURE_PAPI != nullptr)
    {
        std::cout << "[Mitos-PAPI] MPI Finalize\n";
        pmpi_end_papi(env_MITOS_MEASURE_PAPI);
    }
    else
    {
        std::cout << "[Mitos] MPI Finalize\n";
        int mpi_rank;
        MPI_Comm_rank(MPI_COMM_WORLD, &mpi_rank);
        Mitos_end_sampler();
        LOG_LOW("mitoshooks.cpp: MPI_Finalize(), Flushed raw samples, rank no.: " << mpi_rank);
        Mitos_add_offsets(virt_address, &mout);
        MPI_Barrier(MPI_COMM_WORLD);
        // merge files
        if (mpi_rank == 0) {
            std::string result_dir;
            Mitos_merge_files(ts_output, result_dir);
            
            mitos_output result_mout;
            Mitos_set_result_mout(&result_mout, result_dir);    
            Mitos_process_binary("/proc/self/exe", &result_mout);
            Mitos_post_process("/proc/self/exe", &result_mout, result_dir);
        }
        MPI_Barrier(MPI_COMM_WORLD);
        delete[] virt_address;
    }
    return PMPI_Finalize();
}
#endif // USE_MPI


#ifdef USE_OPEN_MP


void sample_handler_omp(perf_event_sample *sample, void *args)
{
    LOG_HIGH("mitoshooks.cpp: sample_handler_omp(), MPI handler sample: cpu= " << sample->cpu << " tid= "  << sample->tid);
    Mitos_write_sample(sample, &mout);
}


#define register_callback_t(name, type)                                        \
  do {                                                                         \
    type f_##name = &on_##name;                                                \
    if (ompt_set_callback(name, (ompt_callback_t)f_##name) == ompt_set_never)  \
      printf("0: Could not register callback '" #name "'\n");                  \
  } while (0)

#define register_callback(name) register_callback_t(name, name##_t)

static uint64_t my_next_id() {
    static uint64_t ID = 0;
    uint64_t ret = __sync_fetch_and_add(&ID, 1);
    assert(ret < MAX_THREADS &&
           "Maximum number of allowed threads is limited by MAX_THREADS");
    return ret;
}

static void on_ompt_callback_thread_begin(ompt_thread_t thread_type,
                                          ompt_data_t *thread_data) {
    uint64_t tid_omp = thread_data->value = my_next_id();
#ifdef SYS_gettid
    pid_t tid = syscall(SYS_gettid);
#else
#error "SYS_gettid unavailable on this system"
    exit(1);
#endif // SYS_gettid
    if (tid_omp_first == -1) {
        tid_omp_first = tid;
    }
#if VERBOSITY >= VERBOSE_MEDIUM
    int cpu_num = sched_getcpu();
    LOG_MEDIUM("mitoshooks.cpp: on_ompt_callback_thread_begin(), Start Thread OMP:= " << getpid()
         << " tid= "  << tid << " omp_tid= "  << tid_omp << " cpu_id= "  << cpu_num);
#endif
    char rank_prefix[54];
    sprintf(rank_prefix, "mitos_%ld_out_%d_", ts_output_prefix_omp, tid);
    Mitos_create_output(&mout, ts_output_prefix_omp, tid);
#if VERBOSITY >= VERBOSE_MEDIUM
   pid_t curpid = getpid();
   LOG_MEDIUM("mitoshooks.cpp: on_ompt_callback_thread_begin(), Curpid:= " << curpid);
#endif
    virt_address = new char[(strlen(rank_prefix) + strlen("/tmp/") + strlen("virt_address.txt") + 1)];
    strcpy(virt_address, "/tmp/");
    strcat(virt_address, rank_prefix);
    strcat(virt_address, "virt_address.txt");
    Mitos_save_virtual_address_offset(std::string(virt_address));
    // Take user inputs
    int sampling_period = DEFAULT_PERIOD;
    int latency_threshold = DEFAULT_LATENCY;
    int sampling_frequency = DEFAULT_FREQ;
    read_mitos_envs(sampling_period, latency_threshold, sampling_frequency);

    Mitos_pre_process(&mout);
    Mitos_set_pid(tid);

    Mitos_set_handler_fn(&sample_handler_omp,NULL);
    Mitos_set_sample_latency_threshold(latency_threshold);
    if (omp_get_thread_num() == 0){
        std::cout << "Mitos sampling parameters: Latency threshold = " << latency_threshold << ", ";
    }
    if(set_period){
        Mitos_set_sample_event_period(sampling_period);
        if (omp_get_thread_num() == 0)
            std::cout << "Sampling period: " << sampling_period <<"\n";
    }
    else {
        Mitos_set_sample_time_frequency(sampling_frequency);
        if (omp_get_thread_num() == 0)
            std::cout << "Sampling frequency: " << sampling_frequency <<"\n";
    }

    Mitos_begin_sampler();
    LOG_LOW("mitoshooks.cpp: on_ompt_callback_thread_begin(), Begin sampling, thread id = " << omp_get_thread_num());
}


static void on_ompt_callback_thread_end(ompt_data_t *thread_data) {
    uint64_t tid_omp = thread_data->value;
#ifdef SYS_gettid
    pid_t tid = syscall(SYS_gettid);
#else
#error "SYS_gettid unavailable on this system"
#endif // SYS_gettid
    LOG_MEDIUM("mitoshooks.cpp: on_ompt_callback_thread_end(), End Thread OMP:= " << getpid()
         << " tid= "  << tid << " omp_tid= "  << tid_omp );
    Mitos_end_sampler();
    LOG_LOW("mitoshooks.cpp: on_ompt_callback_thread_end(), Flushed raw samples, thread id = " << omp_get_thread_num());
    Mitos_add_offsets(virt_address, &mout);
    LOG_LOW("mitoshooks.cpp: on_ompt_callback_thread_end(), Thread End: " << omp_get_thread_num());
}

int ompt_initialize(ompt_function_lookup_t lookup, int initial_device_num,
                    ompt_data_t *tool_data) {
    LOG_LOW("mitoshooks.cpp: ompt_initialize(), libomp init time: " 
            << omp_get_wtime() - *(double *) (tool_data->ptr));
    *(double *) (tool_data->ptr) = omp_get_wtime();
    // initialize callback
    ompt_set_callback_t ompt_set_callback =
            (ompt_set_callback_t)lookup("ompt_set_callback");
    printf("[Mitos] Beginning sampler\n");
    register_callback(ompt_callback_thread_begin);
    register_callback(ompt_callback_thread_end);

    ts_output_prefix_omp = std::time(NULL);

    tid_omp_first = -1;

    return 1; // success: activates tool
}

void ompt_finalize(ompt_data_t *tool_data) {
    LOG_LOW("mitoshooks.cpp: ompt_finalize(), application runtime: " 
            << omp_get_wtime() - *(double *) (tool_data->ptr));

    printf("[Mitos] End Sampler...\n");
    std::string result_dir;
    Mitos_merge_files(ts_output_prefix_omp, result_dir);
    {
        auto bin_name = [](pid_t pid) -> std::string {    
            char buffer[1024];
            memset(buffer, 0, sizeof(buffer));

            // Create the path to the symbolic link
            snprintf(buffer, sizeof(buffer), "/proc/%d/exe", pid);

            // Read the symbolic link
            ssize_t len = readlink(buffer, buffer, sizeof(buffer) - 1);

            if (len != -1) {
                buffer[len] = '\0';
                return std::string(buffer);
            } else {
                // Handle error (e.g., process not found, insufficient permissions)
                return "";
            }
        };   
        std::cout << "\n*******************************************************************\n\n";
        std::cout << "Samples collected and written as raw data. Run the following command for post-processing the samples: \n ";
        std::cout << "./mitos_omp_post_process " << bin_name(getpid()) << " " << result_dir <<"\n";                    
        std::cout << "\n*******************************************************************\n\n";    
    }
        
    delete[] virt_address;

}

#ifdef __cplusplus
extern "C" {
#endif
ompt_start_tool_result_t *ompt_start_tool(unsigned int omp_version,
                                          const char *runtime_version) {
    static double time = 0; // static defintion needs constant assigment
    time = omp_get_wtime();
    printf("[Mitos] Initiating OMP Hooks start tool: %u \n", getpid());

    static ompt_start_tool_result_t ompt_start_tool_result = {
            &ompt_initialize, &ompt_finalize, {.ptr = &time}};
    return &ompt_start_tool_result; // success: registers tool
}
#ifdef __cplusplus
}
#endif

#endif // USE_OPEN_MP