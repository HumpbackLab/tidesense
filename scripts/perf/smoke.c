/* Deterministic CPU workload with visible frames for perf stat/record/report. */
#define _POSIX_C_SOURCE 200809L
#include <stdint.h>
#include <stdio.h>
#include <time.h>

static volatile uint64_t sink;

__attribute__((noinline)) static uint64_t mix(uint64_t value)
{
    for (unsigned i = 0; i < 100000; ++i) {
        value ^= value << 13;
        value ^= value >> 7;
        value ^= value << 17;
    }
    return value;
}

__attribute__((noinline)) static void workload(void)
{
    sink = mix(sink + 1);
}

static double now(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9;
}

int main(void)
{
    double start = now();
    while (now() - start < 3.0) workload();
    printf("checksum=%llu\n", (unsigned long long)sink);
    return 0;
}
