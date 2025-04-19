// SPDX-License-Identifier: Apache-2.0
#include <sys/time.h>
#include <windows.h>

int clock_gettime(clockid_t id, struct timespec *ts)
{
    LARGE_INTEGER epoch = {};

    switch (id) {
        case CLOCK_REALTIME:
            GetSystemTimePreciseAsFileTime((LPFILETIME)&epoch);
            break;
        case CLOCK_MONOTONIC:
            QueryPerformanceCounter(&epoch);
            break;
    }

    ts->tv_sec = epoch.QuadPart / 10000000;
    ts->tv_nsec = (epoch.QuadPart % 10000000) * 100;

    return 0;
}
