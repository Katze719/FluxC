#include "flux/latency.h"
#include "check.h"

int main(void)
{
    flux_latency_t latency;
    uint64_t buckets[5];
    uint64_t value = 999;
    CHECK(flux_latency_init(NULL, buckets, 5, 10) == FLUX_INVALID_ARGUMENT);
    CHECK(flux_latency_init(&latency, NULL, 5, 10) == FLUX_INVALID_ARGUMENT);
    CHECK(flux_latency_init(&latency, buckets, 0, 10) == FLUX_INVALID_ARGUMENT);
    CHECK(flux_latency_init(&latency, buckets, 5, 0) == FLUX_INVALID_ARGUMENT);
    CHECK(flux_latency_init(&latency, buckets, SIZE_MAX / sizeof(uint64_t) + 1, 1) ==
          FLUX_OVERFLOW);
    CHECK(flux_latency_init(&latency, buckets, 5, 10) == FLUX_OK);
    CHECK(latency.count == 0 && latency.min_ns == UINT64_MAX);
    CHECK(flux_latency_percentile(&latency, 50, &value) == FLUX_INVALID_ARGUMENT);
    CHECK(value == 999);
    CHECK(flux_latency_record(NULL, 1) == FLUX_INVALID_ARGUMENT);
    flux_latency_reset(NULL);
    uint64_t samples[] = {0, 9, 10, 19, 20, 29, 30, 39, 40, 100};
    for (size_t i = 0; i < 10; ++i)
        CHECK(flux_latency_record(&latency, samples[i]) == FLUX_OK);
    for (size_t i = 0; i < 5; ++i)
        CHECK(buckets[i] == 2);
    CHECK(latency.count == 10 && latency.sum_ns == 296 && latency.min_ns == 0 &&
          latency.max_ns == 100);
    CHECK(flux_latency_percentile(NULL, 50, &value) == FLUX_INVALID_ARGUMENT);
    CHECK(flux_latency_percentile(&latency, 50, NULL) == FLUX_INVALID_ARGUMENT);
    CHECK(flux_latency_percentile(&latency, 101, &value) == FLUX_INVALID_ARGUMENT);
    CHECK(flux_latency_percentile(&latency, 0, &value) == FLUX_OK && value == 0);
    CHECK(flux_latency_percentile(&latency, 1, &value) == FLUX_OK && value == 9);
    CHECK(flux_latency_percentile(&latency, 50, &value) == FLUX_OK && value == 29);
    CHECK(flux_latency_percentile(&latency, 80, &value) == FLUX_OK && value == 39);
    CHECK(flux_latency_percentile(&latency, 95, &value) == FLUX_OK && value == 100);
    CHECK(flux_latency_percentile(&latency, 100, &value) == FLUX_OK && value == 100);
    flux_latency_reset(&latency);
    CHECK(latency.count == 0 && latency.sum_ns == 0 && latency.max_ns == 0);
    for (size_t i = 0; i < 5; ++i)
        CHECK(buckets[i] == 0);

    CHECK(flux_latency_record(&latency, UINT64_MAX) == FLUX_OK);
    CHECK(flux_latency_record(&latency, 1) == FLUX_OVERFLOW);
    CHECK(latency.count == 1 && latency.sum_ns == UINT64_MAX && buckets[4] == 1);
    CHECK(flux_latency_record(&latency, 0) == FLUX_OK);
    CHECK(flux_latency_percentile(&latency, 50, &value) == FLUX_OK && value == 9);

    CHECK(flux_latency_init(&latency, buckets, 5, UINT64_MAX) == FLUX_OK);
    CHECK(flux_latency_record(&latency, UINT64_MAX) == FLUX_OK);
    CHECK(flux_latency_percentile(&latency, 50, &value) == FLUX_OK && value == UINT64_MAX);
    CHECK(flux_latency_init(&latency, buckets, 1, 1) == FLUX_OK);
    CHECK(flux_latency_record(&latency, 123) == FLUX_OK);
    CHECK(flux_latency_percentile(&latency, 50, &value) == FLUX_OK && value == 123);

    /* Synthetic valid zero-valued population exercises rank/count arithmetic
     * beyond practical test runtimes without weakening production validation. */
    flux_latency_reset(&latency);
    latency.count = UINT64_MAX;
    latency.min_ns = 0;
    buckets[0] = UINT64_MAX;
    CHECK(flux_latency_percentile(&latency, 99, &value) == FLUX_OK && value == 0);
    CHECK(flux_latency_record(&latency, 0) == FLUX_OVERFLOW);
    CHECK(latency.count == UINT64_MAX && buckets[0] == UINT64_MAX);
    return 0;
}
