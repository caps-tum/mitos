//
// Created by maximilian
//

#include <iostream>
#include <omp.h>
#include "src/virtual_address_writer.h"

void do_something_single_thread() {
    std::cout << ("Do something in single thread...\n");
    int b = 15;
    for (int i = 0; i < 10000; i++) {
        b += 3;
        if (b % 15 == 0) {
            b -= 5;
        }
    }
    std::cout << ("Single thread activity done\n");
}

int main() {
    save_virtual_address_offset("virt_address.txt");
    do_something_single_thread();
    #pragma omp parallel default(none) num_threads(8)
    {
        printf("Hello from thread %i of %i!\n", omp_get_thread_num(),
               omp_get_num_threads());
        int a = 0;
        for(int i = 0; i < 10000000; i++) {
            a += 2%3;
            if (a % 5 == 0) {
                a += 7;
            }
        }
    }
    return 0;
}