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

// 512 should be enough for xeon-phi
#define MAX_THREADS 512
#define DEFAULT_PERIOD 4000
#define DEFAULT_LATENCY 4
#define DEFAULT_FREQ 4000

struct func_args
{
    void *(*func)(void*);
    void *args;
};


thread_local static mitos_output mout;
thread_local static char* virt_address;
long ts_output_prefix_omp;
long tid_omp_first;
static bool set_period = true;

void Mitos_get_environment_variables(int &sampling_period, int &latency_threshold, int &sampling_frequency){

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

void sample_handler(perf_event_sample *sample, void *args)
{
    LOG_HIGH("mitoshooks.cpp: sample_handler(), MPI handler sample: cpu= " << sample->cpu << " tid= "  << sample->tid);
    Mitos_write_sample(sample, &mout);
}

int MPI_Init(int *argc, char ***argv)
{
    fprintf(stderr, "[Mitos] MPI_Init hook\n");
    int ret = PMPI_Init(argc, argv);

    int mpi_rank;
    MPI_Comm_rank(MPI_COMM_WORLD, &mpi_rank);
    ts_output = std::time(NULL);
    MPI_Bcast(&ts_output, 1, MPI_LONG, 0, MPI_COMM_WORLD);
    // send timestamp from rank 0 to all others to synchronize folder prefix

    char rank_prefix[54];
    sprintf(rank_prefix, "mitos_%ld_rank_%d_", ts_output, mpi_rank);

    virt_address = new char[(strlen(rank_prefix) + strlen("/tmp/") + strlen("virt_address.txt") + 1)];
    strcpy(virt_address, "/tmp/");
    strcat(virt_address, rank_prefix);
    strcat(virt_address, "virt_address.txt");
    Mitos_save_virtual_address_offset(std::string(virt_address));
    // Take user inputs
    int sampling_period = DEFAULT_PERIOD;
    int latency_threshold = DEFAULT_LATENCY;
    int sampling_frequency = DEFAULT_FREQ;
    Mitos_get_environment_variables(sampling_period, latency_threshold, sampling_frequency);

    Mitos_create_output(&mout, rank_prefix);
    pid_t curpid = getpid();
    LOG_LOW("mitoshooks.cpp: MPI_Init(), Curpid: " << curpid << ", Rank: " << mpi_rank);

    Mitos_pre_process(&mout);
    Mitos_set_pid(curpid);

    Mitos_set_handler_fn(&sample_handler,NULL);
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
    
    std::cout << "[Mitos] Begin sampler, rank: " << mpi_rank << "\n";
    Mitos_begin_sampler();

    return ret;
}

int MPI_Init_thread(int *argc, char ***argv, int required, int *provided)
{
    fprintf(stderr, "[Mitos] MPI_Init_thread hook\n");
    int ret = PMPI_Init_thread(argc, argv, required, provided);

    int mpi_rank;
    MPI_Comm_rank(MPI_COMM_WORLD, &mpi_rank);

    char rank_prefix[38];
    sprintf(rank_prefix, "mitos_rank_%d", mpi_rank);
    
    // Take user inputs
    int sampling_period = DEFAULT_PERIOD;
    int latency_threshold = DEFAULT_LATENCY;
    int sampling_frequency = DEFAULT_FREQ;
    Mitos_get_environment_variables(sampling_period, latency_threshold, sampling_frequency);

    Mitos_create_output(&mout, rank_prefix);
    Mitos_pre_process(&mout);

    Mitos_set_handler_fn(&sample_handler,NULL);
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
    Mitos_begin_sampler();

    return ret;
}

int MPI_Finalize()
{
    std::cout << "[Mitos] MPI Finalize\n";
    int mpi_rank;
    MPI_Comm_rank(MPI_COMM_WORLD, &mpi_rank);
    Mitos_end_sampler();
    fflush(mout.fout_raw); // flush raw samples stream before post processing starts
    MPI_Barrier(MPI_COMM_WORLD);
    LOG_LOW("mitoshooks.cpp: MPI_Finalize(), Flushed raw samples, rank no.: " << mpi_rank);
    Mitos_add_offsets(virt_address, &mout);
    // merge files
    if (mpi_rank == 0) {
        int ret_val = Mitos_merge_files(std::string("mitos_") + std::to_string(ts_output) + "_rank_", std::string("mitos_") + std::to_string(ts_output) + "_rank_0");
        Mitos_openFile("/proc/self/exe", &mout);
        mitos_output result_mout;
        std::string result_dir = "mitos_" + std::to_string(ts_output) + "_rank_result";
        Mitos_set_result_mout(&result_mout, result_dir.c_str());    
        Mitos_post_process("/proc/self/exe", &result_mout, result_dir);
    }
    MPI_Barrier(MPI_COMM_WORLD);
    delete[] virt_address;
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
    sprintf(rank_prefix, "mitos_%ld_openmp_distr_mon_%d_", ts_output_prefix_omp, tid);
    Mitos_create_output(&mout, rank_prefix);
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
    Mitos_get_environment_variables(sampling_period, latency_threshold, sampling_frequency);

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
    Mitos_merge_files("mitos_" + std::to_string(ts_output_prefix_omp) + "_openmp_distr_mon", "mitos_" + std::to_string(ts_output_prefix_omp) + "_openmp_distr_mon_" + std::to_string(tid_omp_first));
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
        std::cout << "./mitos_omp_post_process " <<bin_name(getpid()) << " mitos_" + std::to_string(ts_output_prefix_omp) + "_openmp_distr_monresult\n";                    
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