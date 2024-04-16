#include <iostream>
#include <fstream>
#include <vector>
#include <cstdlib>
#include <unistd.h>
#include "../src/Mitos.h"
#include "src/virtual_address_writer.h"

thread_local static mitos_output mout;

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

    printf("Hello from thread %i of %i!\n", omp_get_thread_num(),
               omp_get_num_threads());
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


    int randx = N*((float)rand() / (float)RAND_MAX+1);
    int randy = N*((float)rand() / (float)RAND_MAX+1);
    std::cout << c[ROW_MAJOR(randx,randy,N)] << std::endl;
}

int main(int argc, char **argv)
{
    save_virtual_address_offset("/tmp/mitos_virt_address.txt");
    int N = (argc == 2) ? atoi(argv[1]) : 1024;

    double *a,*b,*c;
    init_matrices(N,&a,&b,&c);
    long ts_output_prefix_omp = time(NULL);
    #pragma omp parallel
    {
        //mitos_output mout;
        pid_t tid = gettid();
//         uint64_t tid_omp = thread_data->value = my_next_id();
// #ifdef SYS_gettid
//         pid_t tid = syscall(SYS_gettid);
// #else
// #error "SYS_gettid unavailable on this system"
//         exit(1);
// #endif // SYS_gettid

        
        char rank_prefix[54];
        sprintf(rank_prefix, "mitos_%ld_openmp_distr_mon_%d_", ts_output_prefix_omp, tid);

        Mitos_create_output(&mout, rank_prefix);
        Mitos_pre_process(&mout);
        Mitos_set_pid(tid);

        Mitos_set_handler_fn(&sample_handler,NULL);
        Mitos_set_sample_latency_threshold(3);
        Mitos_set_sample_time_frequency(4000);

        std::cout << "[Mitos] Beginning sampler: " << omp_get_thread_num() <<"\n";
        Mitos_begin_sampler();
        matmul(N,a,b,c);
        Mitos_end_sampler();
        fflush(mout.fout_raw);
        std::cout << "[Mitos] End sampler\n";
        //Mitos_add_offsets("/tmp/mitos_virt_address.txt", &mout);
    }
    // std::set<std::string> src_files;    
    // if(Mitos_openFile(argv[0], &mout))
    // {
    //     std::cerr << "Error opening binary file!" << std::endl;
    //     return 1;
    // }
    // if(Mitos_post_process(argv[0],&mout, src_files)){
    //     std::cerr << "Error post processing!" << std::endl;
    //     return 1;
    // }
    // Mitos_copy_sources(mout.dname_topdir, src_files);
}