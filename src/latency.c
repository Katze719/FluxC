#include "flux/latency.h"
#include "realtime.h"

#include <string.h>

flux_result_t flux_latency_init(flux_latency_t *latency, uint64_t *buckets, size_t bucket_count,
                                uint64_t bucket_width_ns)
{
    if (latency == NULL || buckets == NULL || bucket_count == 0 || bucket_width_ns == 0)
        return FLUX_INVALID_ARGUMENT;
    if (bucket_count > SIZE_MAX / sizeof(*buckets))
        return FLUX_OVERFLOW;

    latency->buckets = buckets;
    latency->bucket_count = bucket_count;
    latency->bucket_width_ns = bucket_width_ns;
    flux_latency_reset(latency);
    return FLUX_OK;
}

void flux_latency_reset(flux_latency_t *latency)
{
    if (latency == NULL)
        return;
    memset(latency->buckets, 0, latency->bucket_count * sizeof(*latency->buckets));
    latency->count = 0;
    latency->sum_ns = 0;
    latency->min_ns = UINT64_MAX;
    latency->max_ns = 0;
}

FLUX_NONBLOCKING flux_result_t flux_latency_record(flux_latency_t *latency, uint64_t ns)
{
    if (latency == NULL)
        return FLUX_INVALID_ARGUMENT;
    if (latency->count == UINT64_MAX || ns > UINT64_MAX - latency->sum_ns)
        return FLUX_OVERFLOW;

    uint64_t index = ns / latency->bucket_width_ns;
    size_t bucket = index >= latency->bucket_count ? latency->bucket_count - 1 : (size_t)index;
    ++latency->buckets[bucket];
    ++latency->count;
    latency->sum_ns += ns;
    if (ns < latency->min_ns)
        latency->min_ns = ns;
    if (ns > latency->max_ns)
        latency->max_ns = ns;
    return FLUX_OK;
}

flux_result_t flux_latency_percentile(const flux_latency_t *latency, unsigned int percent,
                                      uint64_t *out_ns)
{
    if (latency == NULL || out_ns == NULL || percent > 100 || latency->count == 0)
        return FLUX_INVALID_ARGUMENT;
    if (percent == 0 || percent == 100) {
        *out_ns = percent == 0 ? latency->min_ns : latency->max_ns;
        return FLUX_OK;
    }

    /* ceil(count * percent / 100), without overflowing the multiplication. */
    uint64_t rank =
        (latency->count / 100) * percent + ((latency->count % 100) * percent + 99) / 100;
    uint64_t cumulative = 0;
    for (size_t i = 0; i < latency->bucket_count; ++i) {
        cumulative += latency->buckets[i];
        if (cumulative < rank)
            continue;
        uint64_t upper = latency->max_ns;
        if (i != latency->bucket_count - 1 && i + 1 <= UINT64_MAX / latency->bucket_width_ns) {
            uint64_t edge = (uint64_t)(i + 1) * latency->bucket_width_ns - 1;
            if (edge < upper)
                upper = edge;
        }
        *out_ns = upper;
        return FLUX_OK;
    }
    /* Unreachable for valid, unmodified histogram metadata. */
    return FLUX_INVALID_ARGUMENT;
}
