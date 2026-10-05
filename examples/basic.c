#include "flux/ring.h"
#include "flux/pool.h"
#include "flux/latency.h"
#include "flux/time.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>

int main(void)
{
    /* Explicit one-time control allocation; push/pop never allocate. */
    void *state = malloc(flux_ring_state_size());
    if (state == NULL)
        return EXIT_FAILURE;
    unsigned int storage[8];
    flux_ring_t *ring;
    if (flux_ring_init(state, flux_ring_state_size(), storage, sizeof(storage), sizeof(storage[0]),
                       &ring) != FLUX_OK) {
        free(state);
        return EXIT_FAILURE;
    }

    unsigned int input = 42, output;
    if (!flux_ring_try_push(ring, &input) || !flux_ring_try_pop(ring, &output)) {
        free(state);
        return EXIT_FAILURE;
    }
    printf("Queue: %u (capacity %zu)\n", output, flux_ring_capacity(ring));
    free(state); /* No users remain; no ring destruction is necessary. */

    _Alignas(max_align_t) unsigned char blocks[4 * 32];
    flux_pool_slot_t slots[4];
    flux_pool_t pool;
    if (flux_pool_init(&pool, blocks, sizeof(blocks), 32, _Alignof(max_align_t), slots, 4) !=
        FLUX_OK)
        return EXIT_FAILURE;
    void *block = flux_pool_acquire(&pool);
    if (block == NULL || !flux_pool_release(&pool, block))
        return EXIT_FAILURE;
    printf("Pool: %zu available blocks\n", flux_pool_available(&pool));

    uint64_t buckets[16];
    flux_latency_t latency;
    uint64_t p95;
    if (flux_latency_init(&latency, buckets, 16, 100) != FLUX_OK)
        return EXIT_FAILURE;
    for (uint64_t sample = 100; sample <= 1000; sample += 100)
        if (flux_latency_record(&latency, sample) != FLUX_OK)
            return EXIT_FAILURE;
    if (flux_latency_percentile(&latency, 95, &p95) != FLUX_OK)
        return EXIT_FAILURE;
    printf("Latency: %" PRIu64 " samples, p95 <= %" PRIu64 " ns\n", latency.count, p95);

    uint64_t now;
    flux_result_t result = flux_time_now_ns(&now);
    if (result == FLUX_OK)
        printf("Monotonic clock: %" PRIu64 " ns\n", now);
    else if (result != FLUX_NOT_SUPPORTED)
        return EXIT_FAILURE;
    return EXIT_SUCCESS;
}
