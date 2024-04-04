//
// Created by maximilian on 07.03.23.
//

#ifndef SAMPLING_VIRTUAL_ADDRESS_WRITER_H
#define SAMPLING_VIRTUAL_ADDRESS_WRITER_H
#include <iostream>
#include "dlfcn.h"
#include "link.h"
#include "cassert"
#include "fstream"

/* This function saves the virtual address offset for the 
    executable (being run for sampling). 
* This helps in locating the source code of the executable
    using the instruction pointers obtained through the samples.
* This function must be included in the source code of the executable */
void save_virtual_address_offset(std::string filename, bool verbose = false) {
    // ---------  get virtual address offset -------------------
    void * const handle = dlopen(NULL, RTLD_LAZY);
    assert(handle != 0);
    // ------ Get the link map
    const struct link_map* link_map = 0;
    const int ret = dlinfo(handle, RTLD_DI_LINKMAP, &link_map);
    const struct link_map * const loaded_link_map = link_map;
    assert(ret == 0);
    assert(link_map != nullptr);

    std::ofstream fproc;
    fproc << "link_map->l_addr " << (long long)link_map->l_addr << std::endl;

    fproc.close();
    std::ofstream f_addr;
    if(verbose)
        std::cout << "Output: " << filename << std::endl;
    f_addr.open(filename);
    f_addr << (long long)link_map->l_addr << std::endl;
    f_addr.close();
    // -------------------
}

#endif //SAMPLING_VIRTUAL_ADDRESS_WRITER_H
