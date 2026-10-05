#ifndef FLUX_LATENCY_H
#define FLUX_LATENCY_H

#include <stddef.h>
#include <stdint.h>
#include "flux/result.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Fixed-width nanosecond histogram with exact count/sum/min/max. The caller
 * supplies bucket_count uint64_t counters; the last bucket catches all values
 * at or above (bucket_count - 1) * bucket_width_ns. One bucket is supported.
 *
 * No allocation, clocks, locks, or syscalls. Not thread-safe; external
 * serialization is required. Recording is bounded O(1); init/reset/percentile
 * are O(bucket_count). Buckets and state must be disjoint and remain valid.
 * Fields are exposed for stack allocation/statistics, not a stable ABI. Do not
 * mutate fields/counters outside this API or copy a histogram while in use.
 */
typedef struct {
    uint64_t *buckets;
    size_t bucket_count;
    uint64_t bucket_width_ns;
    uint64_t count;
    uint64_t sum_ns;
    uint64_t min_ns; /* UINT64_MAX while empty. */
    uint64_t max_ns; /* Zero while empty. */
} flux_latency_t;

/* Rejects NULL, zero sizes and bucket byte-size overflow; unchanged on failure.
 * Bucket boundaries may exceed UINT64_MAX: such buckets remain unused. */
flux_result_t flux_latency_init(flux_latency_t *latency, uint64_t *buckets, size_t bucket_count,
                                uint64_t bucket_width_ns);

/* Initialized state required. NULL -> no operation. Clears counters. */
void flux_latency_reset(flux_latency_t *latency);

/* FLUX_OVERFLOW if count/sum would overflow; no sample is recorded on failure.
 * NULL -> FLUX_INVALID_ARGUMENT. */
flux_result_t flux_latency_record(flux_latency_t *latency, uint64_t ns);

/* Nearest-rank percentile for integer percent 0..100. Returns an upper bound
 * on the selected sample from the bucket's inclusive upper edge, capped by the
 * observed maximum. 0/100 return the exact minimum/maximum. Empty, NULL, or an
 * invalid percent returns FLUX_INVALID_ARGUMENT without changing *out_ns.
 * out_ns must not point into the histogram or its buckets. */
flux_result_t flux_latency_percentile(const flux_latency_t *latency, unsigned int percent,
                                      uint64_t *out_ns);

#ifdef __cplusplus
}
#endif
#endif
