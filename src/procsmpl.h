#ifndef PROCSMPL_H
#define PROCSMPL_H

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <inttypes.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <linux/perf_event.h>
#include <asm/unistd.h>
#include <pthread.h>
#include <signal.h>
#include <vector>
#include <mutex>

#include "Mitos.h"

class procsmpl;
class threadsmpl;

/*
    * This struct is used to store the data of a single sample.
    * It contains the file descriptor (fd) of the perf event,
    * the perf_event_attr structure (attr) that defines the event attributes,
    * the mmap buffer (mmap_buf) used for reading the event data,
    * and a flag (running) indicating if the event is currently running.
*/
struct perf_event_container
{
    int fd;
    struct perf_event_attr attr;
    struct perf_event_mmap_page *mmap_buf;
    int running;
};

/* Process-wide sampler
This is supposed to contain the attributes of all the sampling modes 
Depending on these sampling modes, each executing thread runs the sampler. */
class procsmpl
{
    friend class threadsmpl;
    friend class perf_event_sample;
    friend void thread_sighandler(int sig, siginfo_t *info, void *extra);

public:
    procsmpl();
    ~procsmpl();

    /* Initializes the process-wide sampler.
    Calls and intializes every thread-local sampler.*/
    int begin_sampling();
    
    /* Terminates the process-wide sampler by calling every thread-local sampler.*/ 
    void end_sampling();
    
    /*Saves the PID of the target application in target_pid.*/
    void set_pid(pid_t p) 
        { target_pid = p; }
    /*Sets the sampling frequency.*/
    void set_sample_time_frequency(uint64_t p) 
        { use_frequency = 1; sample_frequency = p; }
    
    /*Sets the sampling period.*/
    void set_sample_event_period(uint64_t p) 
        { use_frequency = 0; sample_period = p; }
    
    /*Sets the sampling latency threshold.*/
    void set_sample_latency_threshold(uint64_t t) 
        { sample_latency_threshold = t; }

    /*Sets the user-defined handler function.
    To be used for dumping and writing samples.*/
    void set_handler_fn(sample_handler_fn_t h, void* args) 
        { handler_fn = h; handler_fn_args = args; }

    void add_event(int tid, mitos_output* mout);

    pid_t target_pid;
private:
    /* Set up perf_event_attr (to be used by thread-local samplers)*/
    void init_attrs();
    void init_attrs_ibs();
    void init_attrs_pebs();
private:
    /* Configure perf events*/
    int num_attrs;
    struct perf_event_attr *attrs;

    uint64_t use_frequency;

    uint64_t sample_period;
    uint64_t sample_frequency;
    uint64_t sample_latency_threshold;
    uint64_t sample_type;

    size_t mmap_pages;
    size_t mmap_size;
    size_t pgsz;
    size_t pgmsk;

    /* user-defined handler*/
    sample_handler_fn_t handler_fn;
    void *handler_fn_args;

    /* Identifies if sampler has already started.*/
    bool first_time;

};

/* Thread-local Sampler
Takes the attributes and sampling modes stored by the process. 
The sampler is initiated on the executing thread. */
class threadsmpl
{
    friend class procsmpl;
    friend void thread_sighandler(int sig, siginfo_t *info, void *extra);

public:
    /*Initializes sampler on the calling thread.*/
    int begin_sampling();

    /*Terminates sampler on the calling thread.*/
    void end_sampling();

    ~threadsmpl(){
        std::destroy(vec_events.begin(), vec_events.end());
    }

    /*Initialize perf_events and setup thread specific signal handler.*/
    int init(procsmpl *parent);
    int init_perf_events(struct perf_event_attr *attrs, int num_attrs, size_t mmap_size);
    int init_thread_sighandler();

    /* Parent process calling the thread.*/
    procsmpl *proc_parent;

    /* Identifies if the thread-local sampling is setup.*/
    int ready;

    int num_events;
    int counter_update;
    struct perf_event_container *events;

    bool* core_occupied;
    int core_count;
    std::vector<perf_event_container> vec_events;
    std::mutex m;

    perf_event_sample pes;

    /*IBS specific functions for enabling and disabling perf events.*/
    int enable_event(int event_id);
    void disable_event(int event_id);

};

#endif
