#ifndef FLUX_TIME_H
#define FLUX_TIME_H

#include <stdint.h>
#include "flux/result.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * POSIX monotonic time in nanoseconds, with an unspecified epoch. Thread-safe,
 * allocation-free within FluxC, but clock_gettime may enter the kernel; no
 * bounded/wait-free scheduling guarantee is made. Not a wall-clock timestamp.
 * No state or memory is retained; the caller owns the output.
 *
 * Disabled at build time -> FLUX_NOT_SUPPORTED. NULL -> FLUX_INVALID_ARGUMENT.
 * Clock failure -> FLUX_SYSTEM_ERROR (preserves clock_gettime's errno).
 * Conversion overflow -> FLUX_OVERFLOW. Output is unchanged on failure.
 */
flux_result_t flux_time_now_ns(uint64_t *out_ns);

#ifdef __cplusplus
}
#endif
#endif
