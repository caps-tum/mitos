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

void save_virtual_address_offset(std::string filename) {
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
    std::string file_path = "/tmp/" + filename;
    std::cout << "Output: " << file_path << std::endl;
    f_addr.open(file_path);
    f_addr << (long long)link_map->l_addr << std::endl;
    f_addr.close();
    // -------------------
}

#endif //SAMPLING_VIRTUAL_ADDRESS_WRITER_H
