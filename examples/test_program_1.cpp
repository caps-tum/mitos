
// Created by maximilian on 13.07.23.
//

#include <csignal>
#include <thread>
#include <iostream>
#include "virtual_address_writer.h"
#include <unistd.h>

#define ROW_MAJOR(x,y,width) y*width+x

void init_matrices(int N, double **a, double **b, double **c)
{
    int i,j,k;

    *a = new double[N*N];
    *b = new double[N*N];
    *c = new double[N*N];

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

    int randx = N*((float)rand() / (float)RAND_MAX);
    int randy = N*((float)rand() / (float)RAND_MAX);
    //std::cout << c[ROW_MAJOR(randx,randy,N)] << std::endl;
}

double *a,*b,*c;

int main() {
    pid_t curpid = getpid();
    save_virtual_address_offset("virt_address.txt");
    clock_t start = clock();
    cpu_set_t  mask;
    CPU_ZERO(&mask);
    CPU_SET(3, &mask);
    sched_setaffinity(curpid, sizeof(mask), &mask);
    // small array

    int N = 32;

    for (int i = 0; i < 100000; i++){
        init_matrices(N,&a,&b,&c);
        matmul(N,a,b,c);
    }
    // larger scenario
    CPU_ZERO(&mask);
    CPU_SET(7, &mask);
    sched_setaffinity(curpid, sizeof(mask), &mask);
    N = 1024;
    std::cout << "----------\n";
    for (int i = 0; i < 2; i++){
        init_matrices(N,&a,&b,&c);
        matmul(N,a,b,c);
    }
    clock_t end = clock();
    float seconds_update = (float) (end - start) / CLOCKS_PER_SEC;
    std::cerr << "Duration: " << seconds_update << "\n";
    return 0;

}