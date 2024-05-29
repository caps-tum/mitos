#include <unistd.h>
#include <sys/ptrace.h>
#include <sys/wait.h>

#include <iostream>
#include <fstream>
#include <vector>
#include <cstdlib>

#include "Mitos.h"

size_t bufsz;
uint64_t period;
uint64_t thresh;
uint64_t freq;

#define DEFAULT_BUFSZ       4096
#define DEFAULT_THRESH      4
#define DEFAULT_PERIOD      4000
#define DEFAULT_FREQ      4000
mitos_output mout;
std::vector<perf_event_sample> samples;
pid_t child_pid;
std::string address_file;
static bool use_period  = true;
static bool set_period  = false;
static bool set_frequency  = false;
/* Helper function for writing samples.*/
void dump_samples()
{
    LOG_LOW("mitosrun.cpp:dump_samples(), Total " << samples.size() << " sample(s)");
    for(size_t i=0; i<samples.size(); i++)
        Mitos_write_sample(&samples.at(i), &mout);
    samples.clear();
}

/*Write samples information in the output file.*/
void sample_handler(perf_event_sample *sample, void *args)
{
#if defined(USE_IBS_FETCH) || defined(USE_IBS_OP)
    if (sample->pid == child_pid){
        samples.push_back(*sample);
    }
#else // PEBS (Intel)
    samples.push_back(*sample);
#endif // USE_IBS_FETCH || USE_IBS_OP

    if(samples.size() >= bufsz)
        dump_samples();
}

/* Prints usage information.*/
void usage(char **argv)
{
    std::cerr << "Usage:" << std::endl;
    std::cerr << argv[0] << " [options] <cmd> [args]" << std::endl;
    std::cerr << "    [options]:" << std::endl;
    std::cerr << "        -b sample buffer size (default 4096)" << std::endl;
    std::cerr << "        -p sample period (default 4000)" << std::endl;
    std::cerr << "        -t sample latency threshold (default 4)" << std::endl;
    std::cerr << "        -f sample frequency (default 4000)" << std::endl;
    std::cerr << "        -l location of virtual address file (default /tmp/mitos_virt_address.txt)" << std::endl;
    std::cerr << "    <cmd>: command to sample on (required)" << std::endl;
    std::cerr << "    [args]: command arguments" << std::endl;
}

/* Sets default values for the sampler.*/
void set_defaults()
{
    bufsz = DEFAULT_BUFSZ;
    period = DEFAULT_PERIOD;
    thresh = DEFAULT_THRESH;
    freq = DEFAULT_FREQ;
    address_file = "/tmp/mitos_virt_address.txt";
}

/* Parses command line arguments.*/
int parse_args(int argc, char **argv)
{
    set_defaults();

    int c;
    while((c=getopt(argc, argv, "b:p:t:f:l:")) != -1)
    {
        switch(c)
        {
            case 'b':
                bufsz = atoi(optarg);
                break;
            case 'p':
                period = atoi(optarg);
                set_period = true;
                use_period = true;
                break;
            case 't':
                thresh = atoi(optarg);
                break;
            case 'f':
                freq = atoi(optarg);
                set_frequency = true;
                use_period = false;
                break;
            case 'l':
                address_file = optarg;
                break;    
            case '?':
                usage(argv);
                return 1;
            default:
                abort();
        }
    }

    return 0;
}

/* Finds the index of the command to execute.*/
int findCmdArgId(int argc, char **argv)
{
    // case 1: argv[0] -f1000 cmd
    // case 2: argv[0] -f 1000 cmd
    int cmdarg = -1;
    bool isarg = false;
    for(int i=1; i<argc; i++)
    {
        if(argv[i][0] != '-')
        {
            if(isarg)
                isarg = false;
            else
                return i;
        }
        else
        {
            if(strlen(argv[i]) > 2)
                isarg = false;
            else
                isarg = true;
        }
    }
    return cmdarg;
}

int main(int argc, char **argv)
{
    int cmdarg = findCmdArgId(argc,argv);

    if(cmdarg == -1)
    {
        usage(argv);
        return 1;
    }

    if(parse_args(cmdarg,argv))
        return 1;

    pid_t child = fork();

    if(child == 0)
    {
        ptrace(PTRACE_TRACEME,0,0,0);
        int err = execvp(argv[cmdarg],&argv[cmdarg]);
        if(err)
        {
            perror("execvp");
        }
    }
    else if(child < 0)
    {
        std::cerr << "Error forking!" << std::endl;
    }
    else
    {
        int status;
        wait(&status);
#if defined(USE_IBS_FETCH) || defined(USE_IBS_OP)
        child_pid = child;
#endif // USE_IBS_FETCH || USE_IBS_OP

        auto unique_id = time(NULL);
        int err = Mitos_create_output(&mout, unique_id);
        if(err)
        {
            kill(child, SIGKILL);
            return 1;
        }

        err = Mitos_pre_process(&mout);
        if(err)
        {
            kill(child, SIGKILL);
            return 1;
        }
        Mitos_set_pid(child);
        LOG_MEDIUM("mitosrun.cpp:main(), pid: " << child);

        Mitos_set_sample_latency_threshold(thresh);
        std::cout << "[Mitos] Mitos sampling parameters: Latency threshold = " << thresh << ", ";
        if(set_period)
        {
            Mitos_set_sample_event_period(period);
            std::cout << "Sampling period: " << period <<"\n";
        } else{
            if(use_period)
            {
                Mitos_set_sample_event_period(period);
                std::cout << "Sampling period: " << period <<"\n";
            }   
            else
            {
                Mitos_set_sample_time_frequency(freq);
                std::cout << "Sampling frequency: " << freq <<"\n";
            }
                
        }

        Mitos_set_handler_fn(&sample_handler,NULL);

        std::cout << "[Mitos] Beginning sampler\n";
        Mitos_begin_sampler();
        {
            ptrace(PTRACE_CONT,child,0,0);

            // Wait until process exits
            do { wait(&status); }
            while(!WIFEXITED(status));
        }
        Mitos_end_sampler();
        std::cout << "[Mitos] End sampler\n";
        LOG_LOW("mitosrun.cpp:main(), Dumping leftover samples...");
        dump_samples(); // anything left over
        std::cout << "[Mitos] Command completed! Processing samples..." <<  "\n";
        std::cout << "[Mitos] Bin Name: " << argv[cmdarg] <<  "\n";
        
        Mitos_add_offsets(address_file.c_str(), &mout);
        if(Mitos_process_binary(argv[cmdarg], &mout))
        {
            std::cerr << "Error opening binary file!" << std::endl;
            return 1;
        }
        if(Mitos_post_process(argv[cmdarg],&mout, mout.dname_topdir)){
            std::cerr << "Error post processing!" << std::endl;
            return 1;
        }
    }

    return 0;
}