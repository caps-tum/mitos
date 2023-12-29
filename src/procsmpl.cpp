#include "procsmpl.h"
#include "mmap_processor.h"

#include <poll.h>
#include <fcntl.h>
#include <errno.h>
#include <iostream>
#include <sstream>
#include <regex>

thread_local static threadsmpl tsmp;

// get thread id of the current thread (if using single thread. returns the process id).
pid_t gettid(void)
{
	return (pid_t)syscall(__NR_gettid);
}


int get_psr() {
    // ps -L -o psr -p <pid>
    // -L flag: list running cores for all threads belonging to the given pid

    std::stringstream  str_cmd;
    str_cmd << "ps -o psr " << tsmp.proc_parent->target_pid;

    char buf[1035];
    FILE *fp;
    // execute command
    if ((fp = popen(str_cmd.str().c_str(), "r")) == NULL) {
        printf("Error opening pipe!\n");
        return -1;
    }
    // read command output
    std::stringstream output;
    while (fgets(buf, 1035, fp) != NULL) {
        // Do whatever you want here...
        output << buf;
    }
    if (pclose(fp)) {
        printf("Command not found or exited with error status\n");
        return -1;
    }
    // convert command output to core_id
    std::string str_output_regex = std::regex_replace(
            output.str(),
            std::regex("[^0-9]*"),
            std::string("$1")
    );
    if (str_output_regex == "") {
        return -1;
    }
    //std::cout << str_output_regex << std::endl;
    return std::stoi( str_output_regex );
}

// perf_event_open system call
int perf_event_open(struct perf_event_attr *attr,
                    pid_t pid, int cpu, int group_fd,
                    unsigned long flags)
{
#if  defined(USE_IBS_FETCH) || defined(USE_IBS_OP)
    return syscall(__NR_perf_event_open, attr, -1, cpu, group_fd, flags); // for IBS: monitor all processes, single process monitoring unavailable
#else // PEBS (Intel)
    return syscall(__NR_perf_event_open, attr, pid, cpu, group_fd, flags);
#endif // USE_IBS_FETCH || USE_IBS_OP
}

void update_sampling_events() {
    int ret = 1;
    // TODO: Fix case where thread migration fails
    // Option A: stop monitoring for this process
    // Option B: stay in loop until core can be monitored (--> infinite loop problem)
    // idea: find where process is running
    // - try to enable new running core
    // - try to disable old monitored core
    // function completes if enable thread was successful
    // possible disadvantage: function only completes if enable_event has been successful (no other IBS process runs on same core)
#if CURRENT_VERBOSITY >= VERBOSE_HIGH
   int has_switch = 0;
   struct timespec tp;
   struct timespec tp2;
   clockid_t clk_id = CLOCK_PROCESS_CPUTIME_ID;
   int t_start2 = clock_gettime(clk_id, &tp);
#endif // CURRENT_VERBOSITY >= VERBOSE_HIGH
    while(ret != 0) {
        int active_core = get_psr(); // OLD function
        //int active_core = sched_getcpu(); // this function only works on mitoshooks, monitoring occurs on same thread as computation
        LOG_HIGH ("procsmpl.cpp:update_sampling_events(), Core: " << active_core << ", Events: " << tsmp.num_events);
        LOG_HIGH ("procsmpl.cpp:update_sampling_events(), Method called: "<< active_core);
        if (active_core < 0) {
            std::cout << "No Core active\n";
            return;
        }
        for (int i = 0; i < tsmp.num_events; i++) {
            if (active_core == i) {
            #if CURRENT_VERBOSITY >= VERBOSE_HIGH
               has_switch =  !(tsmp.events[i].running);
            #endif // CURRENT_VERBOSITY >= VERBOSE_HIGH
                ret = tsmp.enable_event(active_core);
            }else {
                tsmp.disable_event(i);
            }
        }
    }
#if CURRENT_VERBOSITY >= VERBOSE_HIGH
    clock_t t_end = clock();
    float seconds = (float)(t_end - t_start) / CLOCKS_PER_SEC;
   int t_end2 = clock_gettime(clk_id, &tp2);
   LOG_HIGH ("procsmpl.cpp:update_sampling_events(), has_switch: " << has_switch << "," << (tp2.tv_nsec - tp.tv_nsec));
   LOG_HIGH ("procsmpl.cpp:update_sampling_events(), has_switch: " << has_switch << "," << seconds);
    float seconds_update = (float) (time_update_sample_end - time_process_end) / CLOCKS_PER_SEC;
#endif // CURRENT_VERBOSITY >= VERBOSE_HIGH
}

void thread_sighandler(int sig, siginfo_t *info, void *extra)
{
    double t_start = (double) ((double) clock())/ CLOCKS_PER_SEC;
    int i;
    int fd = info->si_fd;
    // Iterate over all the threads
    for(i=0; i<tsmp.num_events; i++)
    {
        if(tsmp.events[i].fd == fd)
        {

            process_sample_buffer(&tsmp.pes,
                                  tsmp.events[i].attr.sample_type,
                                  tsmp.proc_parent->handler_fn,
                                  tsmp.proc_parent->handler_fn_args,
                                  tsmp.events[i].mmap_buf,
                                  tsmp.proc_parent->pgmsk);
        }
    }

    ioctl(fd, PERF_EVENT_IOC_REFRESH, 1);
#if CURRENT_VERBOSITY >= VERBOSE_HIGH
   clock_t time_process_end = clock();
#endif // CURRENT_VERBOSITY >= VERBOSE_HIGH
#if defined( USE_IBS_THREAD_MIGRATION)
    tsmp.counter_update++;
    if(tsmp.counter_update >= 250) {
        tsmp.counter_update = 0;
        update_sampling_events();
    }
#endif // USE_IBS_THREAD_MIGRATION
#if CURRENT_VERBOSITY >= VERBOSE_HIGH
    double t_end = ((double) clock())/ CLOCKS_PER_SEC;
    float seconds = (float)(time_process_end - start) / CLOCKS_PER_SEC;
    float seconds_update = (float) (time_update_sample_end - time_process_end) / CLOCKS_PER_SEC;
    LOG_HIGH ("procsmpl.cpp:thread_sighandler(), t_start: " t_start <<", t_end: " << t_end << ", gettid(): " << gettid() << ", fd: " << fd << ", sched_getcpu(): " << sched_getcpu());
#endif // CURRENT_VERBOSITY >= VERBOSE_HIGH
}


procsmpl::procsmpl()
{
    // Defaults
    mmap_pages = 1;
    use_frequency = 1;
    sample_frequency = 4000;
    sample_latency_threshold = 8;
    pgsz = sysconf(_SC_PAGESIZE);
    mmap_size = (mmap_pages+1)*pgsz;
    pgmsk = mmap_pages*pgsz-1;

    handler_fn = NULL;

    first_time = true;
}

procsmpl::~procsmpl()
{
}

void procsmpl::init_attrs()
{
#if  defined(USE_IBS_FETCH) || defined(USE_IBS_OP)
    procsmpl::init_attrs_ibs();
    return;
#endif // USE_IBS_FETCH || USE_IBS_OP
    num_attrs = 2;
    attrs = (struct perf_event_attr*)malloc(num_attrs*sizeof(struct perf_event_attr));
    num_attrs = 1;

    struct perf_event_attr attr;
    memset(&attr, 0, sizeof(struct perf_event_attr));
    attr.size = sizeof(struct perf_event_attr);

    attr.mmap = 1;
    attr.mmap_data = 1;
    attr.comm = 1;
    attr.exclude_user = 0;
    attr.exclude_kernel = 0;
    attr.exclude_hv = 0;
    attr.exclude_idle = 0;
    attr.exclude_host = 0;
    attr.exclude_guest = 1;
    attr.exclusive = 0;
    attr.pinned = 0;
    attr.sample_id_all = 0;
    attr.wakeup_events = 1;

    if(use_frequency)
    {
        attr.sample_freq = sample_frequency;
        attr.freq = 1;
    }
    else
    {
        attr.sample_period = sample_period;
        attr.freq = 0;
    }

    attr.sample_type =
        PERF_SAMPLE_IP |
        PERF_SAMPLE_STREAM_ID |
        PERF_SAMPLE_TIME |
        PERF_SAMPLE_TID |
        PERF_SAMPLE_CPU |
        PERF_SAMPLE_ADDR |
        PERF_SAMPLE_WEIGHT |
        PERF_SAMPLE_TRANSACTION |
        PERF_SAMPLE_DATA_SRC;

    attr.type = PERF_TYPE_RAW;

    // Set up load sampling
    attr.type = PERF_TYPE_RAW;
    attr.config = 0x5101cd;          // MEM_TRANS_RETIRED:LATENCY_THRESHOLD
    attr.config1 = sample_latency_threshold;
    attr.precise_ip = 2;
    attr.disabled = 1;              // Event group leader starts disabled

    attrs[0] = attr;

    // Set up store sampling
    attr.config = 0x5382d0;         // MEM_UOPS_RETIRED:ALL_STORES
    attr.config1 = 0;
    attr.precise_ip = 2;
    attr.disabled = 0;              // Event group follower starts enabled

    attrs[1] = attr;
}

// get total number of cores
int get_num_cores() {
    long numCPU = sysconf(_SC_NPROCESSORS_ONLN);
    return (int) numCPU;
}

#if defined(USE_IBS_FETCH) || defined(USE_IBS_OP)

void init_attr_ibs(struct perf_event_attr* attr, __u64 sample_period) {
    memset(attr, 0, sizeof(struct perf_event_attr));
    attr->size = sizeof(struct perf_event_attr);

    attr->sample_period = sample_period;
#ifdef USE_IBS_FETCH
    attr->type = 8; // IBS_Fetch
    attr->config = (1ULL<<57);
#endif // USE_IBS_FETCH
#ifdef USE_IBS_OP
    attr->type = 9;
            // Setting this bit in config enables sampling every sample_period ops.
            // Leaving it unset will take an IBS sample every sample_period cycles
            // https://github.com/jlgreathouse/AMD_IBS_Toolkit/blob/master/ibs_with_perf_events.txt#L151
            attr->config = 0; // (1ULL<<19);
#endif // USE_IBS_OP
    attr->read_format = 0;
    attr->sample_type = PERF_SAMPLE_RAW
                       | PERF_SAMPLE_CPU
                       | PERF_SAMPLE_IP
                       | PERF_SAMPLE_TID
                       | PERF_SAMPLE_STREAM_ID
                       | PERF_SAMPLE_TIME
                       | PERF_SAMPLE_PERIOD
                       | PERF_SAMPLE_ADDR
                       | PERF_SAMPLE_WEIGHT;
    //                 | PERF_SAMPLE_DATA_SRC;
    attr->disabled = 1;
    attr->inherit = 1;
    attr->precise_ip = 2;
    attr->sample_id_all = 1;
    attr->pinned = 0;
    attr->exclusive = 0;
    attr->exclude_user = 0;
    attr->exclude_kernel = 0;
    attr->exclude_hv = 0;
    attr->exclude_idle = 0;
    attr->mmap = 1;
    attr->comm_exec = 1;
    attr->comm = 1;
    attr->task = 1;
    attr->freq = 0;
}

void procsmpl::init_attrs_ibs() {
    num_attrs = 1;

#if defined(USE_IBS_ALL_ON) || defined(USE_IBS_THREAD_MIGRATION)
    // if ALL_ON or Selective On
    num_attrs = get_num_cores();
    LOG_HIGH("procsmpl.cpp:init_attrs_ibs(), Amount CPUs: " << num_attrs);

#endif // USE_IBS_ALL_ON || USE_IBS_THREAD_MIGRATION
    attrs = (struct perf_event_attr*)malloc(num_attrs*sizeof(struct perf_event_attr));
    for (int i = 0; i< num_attrs; i++) {
        LOG_HIGH("procsmpl.cpp:init_attrs_ibs(), perf_event_attr: " << i);
        struct perf_event_attr attr;
        //memset(&attr, 0, sizeof(struct perf_event_attr));
        init_attr_ibs(&attr, sample_period);
        attrs[i] = attr;
    }
}
#endif // USE_IBS_FETCH || USE_IBS_OP

int procsmpl::begin_sampling()
{
    // Initialize perf_event attributes if coming here for the first time
    if(first_time)
        init_attrs();

    first_time = false;

    // Initialize thread-local sampler
    // Using the perf_event attributes, the perf_event call for each thread is initialized
    int ret = tsmp.init(this);
    if(ret)
        return ret;

    return tsmp.begin_sampling();
}

void procsmpl::end_sampling()
{
    tsmp.end_sampling();
}

int threadsmpl::init(procsmpl *parent)
{
    int ret;
    
    ready = 0;

    // Set parent process
    proc_parent = parent;

    // initialize perf_event calls using parent's attributes 
    ret = init_perf_events(proc_parent->attrs, proc_parent->num_attrs, proc_parent->mmap_size);
    if(ret)
        return ret;
    ret = init_thread_sighandler();
    if(ret)
        return ret;
    // Success
    ready = 1;

    return 0;
}

int threadsmpl::init_perf_events(struct perf_event_attr *attrs, int num_attrs, size_t mmap_size)
{
#if defined(USE_IBS_FETCH) || defined(USE_IBS_OP)
    num_events = num_attrs;
    events = (struct perf_event_container*)malloc(num_events*sizeof(struct perf_event_container));

    #if defined( USE_IBS_THREAD_MIGRATION)
        for(int i=0; i < num_events; i++) {
            events[i].running = 0;
            // initailize one event for each core
            events[i].fd = -1;
            events[i].attr = attrs[i];
        }
        return 0;
    #endif // USE_IBS_THREAD_MIGRATION
    #ifdef USE_IBS_ALL_ON
        // Case IBS USE_IBS_ALL_ON
        for(int i=0; i < num_events; i++)
        {
            events[i].running = 0;
            // initailize one event for each core
            events[i].fd = -1;
            events[i].attr = attrs[i];
           LOG_HIGH ("procsmpl.cpp:init_perf_events(), Init Event " << i);
            // Create attr according to sample mode
            // defines which core is monitored by this event
            LOG_HIGH("procsmpl.cpp:init_perf_events(), gettid(): " << gettid() << ", tsmp.proc_parent->target_pid: " << tsmp.proc_parent->target_pid);
            events[i].fd = perf_event_open(&events[i].attr, gettid(), i, events[i].fd, 0);
            //fprintf(stderr, "i: %d : thread: %d : events[0].fd: %d : returned %d\n", i, gettid(), events[0].fd, errno);

            if(events[i].fd == -1)
            {
                perror("perf_event_open");
                std::cout << "Error: " << i << std::endl;
                return 1;
            }

            // Create mmap buffer for samples
            events[i].mmap_buf = (struct perf_event_mmap_page*)
                    mmap(NULL, mmap_size, PROT_READ | PROT_WRITE, MAP_SHARED, events[i].fd, 0);

            if(events[i].mmap_buf == MAP_FAILED)
            {
                perror("mmap");
                return 1;
            }
        }
        return 0;
    #endif // USE_IBS_ALL_ON
#else // PEBS (Intel)
    int i;

    num_events = num_attrs;
    events = (struct perf_event_container*)malloc(num_events*sizeof(struct perf_event_container));

    events[0].fd = -1;
    LOG_HIGH("procsmpl.cpp:init_perf_events(), Total " << num_events << " event(s)");
    // Iterate over all the threads
    for(i=0; i<num_events; i++)
    {
        // Create attr according to sample mode
        events[i].attr = attrs[i];
        LOG_MEDIUM("procsmpl.cpp:init_perf_events(), tsmp.proc_parent->target_pid: " << tsmp.proc_parent->target_pid);

        // Initialize perf_event call
        events[i].fd = perf_event_open(&events[i].attr, tsmp.proc_parent->target_pid, -1, events[0].fd, 0);
        std::cout << "event no.(i): " << i << ", file descriptor(fd): "<< events[i].fd << "\n";

        if(events[i].fd == -1)
        {
            perror("perf_event_open");
            return 1;
        }

        // Using the mmap system call to create a memory-mapped buffer for perf_event samples. 
        events[i].mmap_buf = (struct perf_event_mmap_page*)
            mmap(NULL, mmap_size, PROT_READ | PROT_WRITE, MAP_SHARED, events[i].fd, 0);

        if(events[i].mmap_buf == MAP_FAILED)
        {
            perror("mmap");
            return 1;
        }
    }
    return 0;
#endif // USE_IBS_FETCH || USE_IBS_OP
}

int threadsmpl::init_thread_sighandler()
{
    int i, ret;
    struct f_owner_ex fown_ex;
    struct sigaction sact;

    // Set up signal handler
    memset(&sact, 0, sizeof(sact));
    sact.sa_sigaction = &thread_sighandler;
    sact.sa_flags = SA_SIGINFO;

    ret = sigaction(SIGIO, &sact, NULL);
    if(ret)
    {
        perror("sigaction");
        return ret;
    }

    // Unblock SIGIO signal if necessary
    sigset_t sold, snew;
    sigemptyset(&sold);
    sigemptyset(&snew);
    sigaddset(&snew, SIGIO);

    ret = sigprocmask(SIG_SETMASK, NULL, &sold);
    if(ret)
    {
        perror("sigaction");
        return 1;
    }

	if(sigismember(&sold, SIGIO))
    {
		ret = sigprocmask(SIG_UNBLOCK, &snew, NULL);
        if(ret)
        {
            perror("sigaction");
            return 1;
        }
    } 

    for(i=0; i<num_events; i++)
    {
        // Set perf event events[i].fd to signal
        ret = fcntl(events[i].fd, F_SETSIG, SIGIO);
        if(ret)
        {
            perror("fcntl SIG1");
            return 1;
        }
        ret = fcntl(events[i].fd, F_SETFL, O_NONBLOCK | O_ASYNC);
        if(ret)
        {
            perror("fcntl SIG2");
            return 1;
        }
        // Set owner to current thread
        fown_ex.type = F_OWNER_TID;
        fown_ex.pid = gettid();
        ret = fcntl(events[i].fd, F_SETOWN_EX, (unsigned long)&fown_ex);
        if(ret)
        {
            perror("fcntl SIG2");
            return 1;
        } 
    }

    return 0;
}


int threadsmpl::begin_sampling()
{
    int i, ret;

    if(!ready)
    {
        fprintf(stderr, "Not ready to begin sampling!\n");
        return 1;
    }

#if defined(USE_IBS_FETCH) || defined(USE_IBS_OP)
    #if defined( USE_IBS_THREAD_MIGRATION)
        update_sampling_events();
        return 0;
    #endif // USE_IBS_THREAD_MIGRATION
    #ifdef USE_IBS_ALL_ON
        for (i = 0; i <num_events; i++) {
            ret = ioctl(events[i].fd, PERF_EVENT_IOC_RESET, 0);
            if(ret)
                perror("ioctl");

            ret = ioctl(events[i].fd, PERF_EVENT_IOC_ENABLE, 0);
            if(ret) {
                perror("ioctl");
            }
            events[i].running = 1;
        }
        return ret;
    #endif // USE_IBS_ALL_ON
#else // PEBS (Intel)

    /* This `ioctl` call resets the counter associated with the perf_event. 
    Thus, counter is set back to its initial value, and counting begins anew.*/
    ret = ioctl(events[0].fd, PERF_EVENT_IOC_RESET, 0);
    if(ret)
        perror("ioctl");
    
    /* This `ioctl` call starts the counting of the specified event. 
    The counter is now actively monitoring the associated performance event.*/
    ret = ioctl(events[0].fd, PERF_EVENT_IOC_ENABLE, 0);
    if(ret)
        perror("ioctl");

    return ret;
#endif // USE_IBS_FETCH ||  USE_IBS_OP
}

void threadsmpl::end_sampling()
{
    int i, ret;
#if defined(USE_IBS_FETCH) || defined(USE_IBS_OP)
    #ifdef USE_IBS_ALL_ON
        for (i = 0; i < num_events; i++) {
            if (events[i].fd != -1) {
                ret = ioctl(events[i].fd, PERF_EVENT_IOC_DISABLE, 0);
                if(ret)
                    perror("ioctl END SAMPLING");
            }
        }
    #elif defined(USE_IBS_THREAD_MIGRATION)
        for (i = 0; i < num_events; i++) {
            tsmp.disable_event(i);
        }
    #endif  // USE_IBS_ALL_ON || USE_IBS_THREAD_MIGRATION
#else // PEBS (Intel)

    /* This `ioctl` call disable the performance monitoring event. When disabled, 
    the event is no longer actively counting or generating samples.
    Disabling the event before processing the sample buffer ensures that 
    no new samples are collected, while the samples from the current period are being processed. 
    This helps in preventing interference during the processing of performance data.*/
    ret = ioctl(events[0].fd, PERF_EVENT_IOC_DISABLE, 0);
    if(ret)
        perror("ioctl");
#endif // USE_IBS_FETCH ||  USE_IBS_OP

    // Iterate over all the threads
    for(i=0; i<num_events; i++)
    {
        // Flush out remaining samples
        if (events[i].fd != -1) {
#if defined(USE_IBS_FETCH) or defined(USE_IBS_OP)
            if (events[i].running == 0) {
                continue;
            }
#endif // USE_IBS_FETCH ||  USE_IBS_OP
            /* After the sampling has completed, reads the related info in the mmap buffers. 
            process_sample_buffer() also takes the handler_fn as an argument which writes the information
            stored in the mmap buffer to readable text files.*/
            process_sample_buffer(&pes,
                              events[i].attr.type,
                              proc_parent->handler_fn,
                              proc_parent->handler_fn_args,
                              events[i].mmap_buf, 
                              proc_parent->pgmsk);
        }
    }

}


int threadsmpl::enable_event(int event_id) {
    if (!events[event_id].running) {
        LOG_HIGH("procsmpl.cpp:enable_event(), Enable event for " << event_id);
        clock_t start = clock();
        events[event_id].fd = -1;
        events[event_id].fd = perf_event_open(&events[event_id].attr, tsmp.proc_parent->target_pid, event_id, events[event_id].fd, 0);
        if(events[event_id].fd == -1)
        {
            perror("perf_event_open");
            LOG_HIGH("procsmpl.cpp:enable_event(), Core " << event_id << " could not be initialized");
            return 1;
        }else {
            LOG_HIGH("procsmpl.cpp:enable_event(), Perf Open Success: " << events[event_id].fd);
        }

        // Create mmap buffer for samples
        events[event_id].mmap_buf = (struct perf_event_mmap_page*)
                mmap(NULL, tsmp.proc_parent->mmap_size, PROT_READ | PROT_WRITE, MAP_SHARED, events[event_id].fd, 0);

        if(events[event_id].mmap_buf == MAP_FAILED)
        {
            perror("mmap");
            return 1;
        }
        // init sighandler
        int ret;
        struct f_owner_ex fown_ex;
        struct sigaction sact;

        // Set up signal handler
        memset(&sact, 0, sizeof(sact));
        sact.sa_sigaction = &thread_sighandler;
        sact.sa_flags = SA_SIGINFO;

        ret = sigaction(SIGIO, &sact, NULL);
        if(ret)
        {
            perror("sigaction");
            return ret;
        }

        // Unblock SIGIO signal if necessary
        sigset_t sold, snew;
        sigemptyset(&sold);
        sigemptyset(&snew);
        sigaddset(&snew, SIGIO);

        ret = sigprocmask(SIG_SETMASK, NULL, &sold);
        if(ret)
        {
            perror("sigaction");
            return 1;
        }

        if(sigismember(&sold, SIGIO))
        {
            ret = sigprocmask(SIG_UNBLOCK, &snew, NULL);
            if(ret)
            {
                perror("sigaction");
                return 1;
            }
        }

        ret = fcntl(events[event_id].fd, F_SETSIG, SIGIO);
        if(ret)
        {
            perror("fcntl 1");
            return 1;
        }
        ret = fcntl(events[event_id].fd, F_SETFL, O_NONBLOCK | O_ASYNC);
        if(ret)
        {
            perror("fcntl 2");
            return 1;
        }
        // Set owner to current thread
        fown_ex.type = F_OWNER_TID;
        fown_ex.pid = gettid();
        ret = fcntl(events[event_id].fd, F_SETOWN_EX, (unsigned long)&fown_ex);
        if(ret)
        {
            perror("fcntl 3");
            return 1;
        }

        ret = ioctl(events[event_id].fd, PERF_EVENT_IOC_RESET, 0);
        if(ret)
            perror("ioctl ST");

        ret = ioctl(events[event_id].fd, PERF_EVENT_IOC_ENABLE, 0);
        if (ret == 0) {
            events[event_id].running = 1;
        }
        clock_t end = clock();
        float seconds_update = (float) (end - start) / CLOCKS_PER_SEC;
        LOG_HIGH("procsmpl.cpp:enable_event()," <<seconds_update<< "," << ret);
        return ret;
    }
    return 0; // process already running, success
}


void threadsmpl::disable_event(int event_id) {
    if(events[event_id].running) {
        LOG_HIGH("procsmpl.cpp:disable_event(), Disable event for " << event_id);
        events[event_id].running = 0;
        if (events[event_id].fd != -1) {
            int ret = ioctl(events[event_id].fd, PERF_EVENT_IOC_DISABLE, 0);
            //close(events[event_id].fd);
            if(ret)
                perror("ioctl END");
                LOG_HIGH("procsmpl.cpp:disable_event(), Err ioctl END: " << events[event_id].fd <<", " << event_id);
        }

    }
}