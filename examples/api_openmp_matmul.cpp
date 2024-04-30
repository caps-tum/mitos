#include <iostream>
#include <fstream>
#include <vector>
#include <cstdlib>
#include <unistd.h>
#include "../src/Mitos.h"
#include "src/virtual_address_writer.h"

thread_local static mitos_output mout;
thread_local static char* virt_address;
long tid_omp_first;
static long ts_output_prefix_omp;

#include <omp.h>

void sample_handler(perf_event_sample *sample, void *args)
{
    //fprintf(stderr, "SAMP: cpu=%d, tid=%d\n", sample->cpu, sample->tid);
    Mitos_write_sample(sample, &mout);
}

#define ROW_MAJOR(x,y,width) y*width+x

void init_matrices(int N, double **a, double **b, double **c)
{
    int i,j,k;

    *a = new double[N*N];
    *b = new double[N*N];
    *c = new double[N*N];

    size_t dims[2];
    dims[0] = N; //static_cast<size_t>(N);
    dims[1] = N; //static_cast<size_t>(N);
    Mitos_add_symbol("a",*a,sizeof(double),dims,2);
    Mitos_add_symbol("b",*b,sizeof(double),dims,2);
    Mitos_add_symbol("c",*c,sizeof(double),dims,2);

    for(i=0; i<N; ++i)
    {
        for(j=0; j<N; ++j)
        {
            (*a)[ROW_MAJOR(i,j,N)] = (double)rand();
            (*b)[ROW_MAJOR(i,j,N)] = (double)rand();
            (*c)[ROW_MAJOR(i,j,N)] = 0;
        }
    }
}

void matmul(int N, double *a, double *b, double *c)
{
    ts_output_prefix_omp = time(NULL);
    #pragma omp parallel
    {
        printf("Hello from thread %i of %i!\n", omp_get_thread_num(),
               omp_get_num_threads());
        
        /* Setting up mitos for every thread*/
        pid_t tid = gettid();
        if(omp_get_thread_num() == 0){
            tid_omp_first = tid;
        }

        /* Rank specific directory name*/
        
        // Unique directory name for every rank
        //char[] mitos_prexix_path = “/tmp/“ + mitos_unique_id + “/virt.txt”;
        // char rank_prefix[54];
        // sprintf(rank_prefix, "mitos_%ld_openmp_%d_", ts_output_prefix_omp, tid);

        Mitos_create_output(&mout, ts_output_prefix_omp, tid);
        // Create output directories and get the location of the virtual address file to be created
        // auto virt_address = Mitos_create_api_output(&mout, rank_prefix);
        std::string virt_address = "/tmp/" + std::to_string(ts_output_prefix_omp) + "_virt_address.txt";
        Mitos_save_virtual_address_offset(virt_address);

        Mitos_pre_process(&mout);
        Mitos_set_pid(tid);

        Mitos_set_handler_fn(&sample_handler,NULL);
        Mitos_set_sample_latency_threshold(3);
        Mitos_set_sample_time_frequency(4000);

        std::cout << "[Mitos] Beginning sampler: " << omp_get_thread_num() <<"\n";
        Mitos_begin_sampler();
        
        /* Main computation*/
        #pragma omp for
        for(int i=0; i<N; ++i)
        {
            for(int j=0; j<N; ++j)
            {
                for(int k=0; k<N; ++k)
                {
                    c[ROW_MAJOR(i,j,N)] += a[ROW_MAJOR(i,k,N)]*b[ROW_MAJOR(k,j,N)];
                }
            }
        }

        /*End the thread-specific sampler and flush the raw samples*/
        Mitos_end_sampler();
        Mitos_add_offsets(virt_address.c_str(), &mout); 
    }
    int randx = N*((float)rand() / (float)RAND_MAX+1);
    int randy = N*((float)rand() / (float)RAND_MAX+1);
    std::cout << c[ROW_MAJOR(randx,randy,N)] << std::endl;

    std::cout << "[Mitos] End sampler\n";
}

int main(int argc, char **argv)
{

    int N = (argc == 2) ? atoi(argv[1]) : 1024;

    double *a,*b,*c;
    init_matrices(N,&a,&b,&c); 
    
    // Sampling done inside this function
    matmul(N,a,b,c);
    
    // Post-processing of raw samples (to be done by the primary thread)
    
    /* Merge and copy the thread-local raw samples into results directory*/
    
    // Set name of the directories (where samples are stored)
    std::string dir_prefix = "mitos_" + std::to_string(ts_output_prefix_omp) + "_out_";
    std::string prefix_first_thread = dir_prefix + std::to_string(tid_omp_first);
    std::string result_dir = dir_prefix + "result";
    
    // Merges all the raw samples into a single raw_samples.csv file
    Mitos_merge_files(dir_prefix, prefix_first_thread);
    
    // Store result information
    mitos_output result_mout;
    Mitos_set_result_mout(&result_mout, result_dir.c_str());     
    
    // Read the binary for symbols
    if(Mitos_process_binary(argv[0], &result_mout))
    {
        std::cerr << "Error opening binary file!" << std::endl;
        return 1;
    }
    
    // Finalize post-processing
    if(Mitos_post_process(argv[0], &result_mout, result_dir)){
        std::cerr << "Error post processing!" << std::endl;
        return 1;
    }
    return 0;
}