#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <linux/mman.h>
#include <sched.h>
#include <unistd.h>
#include <errno.h>
#include <inttypes.h>
#include <algorithm>
#include <numeric>
#include <cmath>
#include <x86intrin.h>

uint64_t operator""_GB(const unsigned long long x)
{ 
    return 1024ULL * 1024ULL * 1024ULL * x;
}

int main(int argc, char **argv)
{
    int opt;
    uint64_t default_nsamples = 1_GB / sizeof(uint64_t);
    size_t nsamples = default_nsamples;
    uint64_t print_threshold = 100000;
    uint64_t nlargest_to_print = 0;
    int32_t ncpu = 0;
    char *end;
    while ((opt = getopt(argc, argv, "n:c:t:l:")) != -1)
    {
        switch(opt)
        {
            case 'n':
                nsamples = strtol(optarg, &end, 10);
                break;
            case 'c':
                ncpu = strtol(optarg, &end, 10);
                break;
            case 't':
                print_threshold = strtol(optarg, &end, 10);
                break;
            case 'l':
                nlargest_to_print = strtol(optarg, &end, 10);
                break;
        }
        if (end == optarg || *end != '\0' || errno == ERANGE)
        {
                fprintf(stderr, "Invalid argument: %c = %s\n", opt, optarg);
                exit(EXIT_FAILURE);
        }
    }

    if (nsamples == 0)
    {
        fprintf(stderr, "Number of samples must be greater than zero\n");
        exit(EXIT_FAILURE);
    }

    if (nsamples > default_nsamples)
    {
        fprintf(stderr, "Number of samples too large\n");
        exit(EXIT_FAILURE);
    }

    // Pin process to assigned CPU.
    cpu_set_t set;
    CPU_ZERO(&set);
    CPU_SET(ncpu, &set);
    if (sched_setaffinity(0, sizeof(set), &set) != 0)
    {
        perror("Failed to set processor affinity");
        exit(EXIT_FAILURE);
    }

    // Set priority.
    int max_priority = sched_get_priority_max(SCHED_FIFO);
    struct sched_param params;
    params.sched_priority = max_priority;
    if (sched_setscheduler(0,SCHED_FIFO,&params) != 0)
    {
        perror("Failed to set scheduler priority");
        exit(EXIT_FAILURE);
    }

    // Reserve memory for samples.
    const size_t nbytes = nsamples * sizeof(uint64_t);
    auto mem = mmap(
        NULL,
        nbytes,
        PROT_READ | PROT_WRITE,
        // NOTE: MAP_UNINITIALIZED is only available if the kernel has been configured
        //       with CONFIG_MMAP_ALLOW_UNINITIALIZED, which is usually not the case.
        MAP_PRIVATE | MAP_ANONYMOUS | MAP_HUGETLB | MAP_HUGE_1GB | MAP_LOCKED | MAP_POPULATE | MAP_NORESERVE, // MAP_UNINITIALIZED,
        -1,
        0
    );
    if (mem == MAP_FAILED)
    {
        perror("Failed to allocate memory");
        exit(EXIT_FAILURE);
    }
    uint64_t *samples = static_cast<uint64_t*>(mem);

    // Inform kernel that we are going to use the memory sequentially.
    if (madvise(samples, nbytes, MADV_SEQUENTIAL | MADV_WILLNEED) != 0)
    {
        perror("Failed to call madvise");
        exit(EXIT_FAILURE);
    }

    // Avoid page faults and TLB shootdowns when saving samples.
    if (mlockall(MCL_CURRENT | MCL_FUTURE) != 0)
    {
        perror("Failed to lock memory");
        exit(EXIT_FAILURE);
    }

    // Run jitter measurement loop.
    uint64_t ts1, ts2;
    uint32_t tmp;
    for (size_t i = 0; i < nsamples; ++i)
    {
        _mm_mfence();
        _mm_lfence();
        ts1 = __rdtsc();
        /////////////////////////////
        // Code to profile goes here.
        /////////////////////////////
        ts2 = __rdtscp(&tmp);
        _mm_lfence();
        samples[i] = ts2 - ts1;
    }

    // Ignore first few measurements while loop is warming up.
    const uint64_t warmup_buffer = static_cast<uint64_t>(nsamples * 0.25);
    samples = samples + warmup_buffer;
    const uint64_t nsamples_used = nsamples - warmup_buffer;

    // Print diffs above threshold as well as surrounding diffs.
    for(uint64_t i = 0; i < nsamples_used; ++i)
    {
        if(samples[i] > print_threshold)
        {
            if (i >= 2)
                printf("%ld:\t%ld\n", i-2, samples[i-2]);
            if (i >= 1)
                printf("%ld:\t%ld\n", i-1, samples[i-1]);
            printf("%ld:\t%ld\n", i+0, samples[i+0]);
            if (i+1 < nsamples_used)
                printf("%ld:\t%ld\n", i+1, samples[i+1]);
            if (i+2 < nsamples_used)
                printf("%ld:\t%ld\n", i+2, samples[i+2]);
            printf("***********\n");
        }
    }

    // Calculate stats.
    std::sort(samples, samples + nsamples_used);
    const uint64_t total = std::accumulate(samples, samples + nsamples_used, 0ULL);
    const double mean = total / static_cast<double>(nsamples_used);
    auto variance_func = [&mean, &nsamples_used](double accumulator, const uint64_t& val)
    {
    return accumulator + ((val - mean) * (val - mean)) / static_cast<double>(nsamples_used - 1);
    };
    const double stddev = std::sqrt(std::accumulate(samples, samples + nsamples_used, 0.0, variance_func));

    // Print summary.
    printf("cpu\tmin\tmean\tstddev\tmedian\tpct95\tpct99.7\tpct99.999\tpct99.99999\tmax\tsamples\tbuffer\n");
    printf(
        "%d\t%ld\t%f\t%f\t%ld\t%ld\t%ld\t%ld\t%ld\t%ld\t%ld\t%ld\n",
        ncpu,
        samples[0],
        mean,
        stddev,
        samples[static_cast<int32_t>((nsamples_used) * 0.5)],
        samples[static_cast<int32_t>((nsamples_used) * 0.95)],
        samples[static_cast<int32_t>((nsamples_used) * 0.997)],
        samples[static_cast<int32_t>((nsamples_used) * 0.99999)],
        samples[static_cast<int32_t>((nsamples_used) * 0.9999999)],
        samples[nsamples_used - 1],
        nsamples_used,
        warmup_buffer
    );

    // Print largest diffs.
    for(uint64_t i = 1; i <= nlargest_to_print; ++i)
    {
        printf("%ld\n", samples[nsamples_used - i]);
    }

    return 0;
}
