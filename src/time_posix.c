#define _POSIX_C_SOURCE 200809L
#include "flux/time.h"

#include <time.h>

flux_result_t flux_time_now_ns(uint64_t *out_ns)
{
    if (out_ns == NULL)
        return FLUX_INVALID_ARGUMENT;
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0)
        return FLUX_SYSTEM_ERROR;
    if (now.tv_sec < 0 || (uintmax_t)now.tv_sec > UINT64_MAX / UINT64_C(1000000000))
        return FLUX_OVERFLOW;
    uint64_t seconds = (uint64_t)now.tv_sec * UINT64_C(1000000000);
    if ((uint64_t)now.tv_nsec > UINT64_MAX - seconds)
        return FLUX_OVERFLOW;
    *out_ns = seconds + (uint64_t)now.tv_nsec;
    return FLUX_OK;
}
