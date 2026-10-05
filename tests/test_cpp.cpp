#include "flux/result.h"
#include "flux/ring.h"
#include "flux/pool.h"
#include "flux/latency.h"
#include "flux/time.h"
#include "check.h"

#include <cstdlib>
#include <cstdint>

int main()
{
    void *state = std::malloc(flux_ring_state_size());
    CHECK(state != nullptr);
    int storage[2];
    flux_ring_t *ring = nullptr;
    CHECK(flux_ring_init(state, flux_ring_state_size(), storage, sizeof(storage), sizeof(int),
                         &ring) == FLUX_OK);
    int input = 42, output = 0;
    CHECK(flux_ring_try_push(ring, &input));
    CHECK(flux_ring_try_pop(ring, &output) && output == input);
    std::free(state);

    alignas(int) unsigned char blocks[2 * sizeof(int)];
    flux_pool_slot_t slots[2];
    flux_pool_t pool;
    CHECK(flux_pool_init(&pool, blocks, sizeof(blocks), sizeof(int), alignof(int), slots, 2) ==
          FLUX_OK);
    void *block = flux_pool_acquire(&pool);
    CHECK(block != nullptr && flux_pool_release(&pool, block));
    std::uint64_t buckets[4];
    flux_latency_t latency;
    CHECK(flux_latency_init(&latency, buckets, 4, 10) == FLUX_OK);
    CHECK(flux_latency_record(&latency, 15) == FLUX_OK);
    std::uint64_t ns = 0;
    CHECK(flux_latency_percentile(&latency, 50, &ns) == FLUX_OK && ns == 15);
    CHECK(flux_time_now_ns(nullptr) == FLUX_INVALID_ARGUMENT);
    return 0;
}
