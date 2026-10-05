#ifndef FLUX_RESULT_H
#define FLUX_RESULT_H

/* Shared return codes. No allocation, blocking, or global error state. */
typedef enum {
    FLUX_OK = 0,
    FLUX_INVALID_ARGUMENT,
    FLUX_NOT_SUPPORTED,
    FLUX_OVERFLOW,
    FLUX_SYSTEM_ERROR
} flux_result_t;

#endif
