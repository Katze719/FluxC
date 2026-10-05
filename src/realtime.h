#ifndef FLUX_REALTIME_INTERNAL_H
#define FLUX_REALTIME_INTERNAL_H

/* Only diagnostic builds add function-entry RTSan instrumentation. Keep this
 * Clang extension private so public C11/C++ headers and normal builds stay
 * portable. The attribute checks blocking/allocation, not scheduling deadlines. */
#if defined(FLUX_ENABLE_RTSAN)
#define FLUX_NONBLOCKING __attribute__((nonblocking))
#else
#define FLUX_NONBLOCKING
#endif

#endif
