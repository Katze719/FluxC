#include "flux/ring.h"
#include "../src/ring_internal.h"
#include "check.h"

#include <stdint.h>
#include <string.h>

int main(void)
{
    size_t state_size = flux_ring_state_size();
    unsigned char *state = malloc(state_size + flux_ring_state_alignment());
    CHECK(state != NULL);
    unsigned char storage[32];
    flux_ring_t *ring = NULL;
    CHECK(flux_ring_capacity(NULL) == 0);
    CHECK(!flux_ring_try_push(NULL, storage));
    CHECK(!flux_ring_try_pop(NULL, storage));
    CHECK(flux_ring_init(NULL, state_size, storage, 8, 1, &ring) == FLUX_INVALID_ARGUMENT);
    CHECK(flux_ring_init(state, 0, storage, 8, 1, &ring) == FLUX_INVALID_ARGUMENT);
    CHECK(flux_ring_init(state, state_size, NULL, 8, 1, &ring) == FLUX_INVALID_ARGUMENT);
    CHECK(flux_ring_init(state, state_size, storage, 8, 1, NULL) == FLUX_INVALID_ARGUMENT);
    CHECK(flux_ring_init(state, state_size, storage, 8, 0, &ring) == FLUX_INVALID_ARGUMENT);
    CHECK(flux_ring_init(state, state_size, storage, 0, 1, &ring) == FLUX_INVALID_ARGUMENT);
    CHECK(flux_ring_init(state, state_size, storage, 7, 1, &ring) == FLUX_INVALID_ARGUMENT);
    CHECK(flux_ring_init(state, state_size, storage, 1, 2, &ring) == FLUX_INVALID_ARGUMENT);
    CHECK(flux_ring_init(state, state_size, storage, SIZE_MAX / 2 + 1, 1, &ring) ==
          FLUX_INVALID_ARGUMENT);
    if (flux_ring_state_alignment() > 1)
        CHECK(flux_ring_init(state + 1, state_size, storage, 8, 1, &ring) == FLUX_INVALID_ARGUMENT);
    CHECK(ring == NULL);

    for (size_t capacity = 1; capacity <= 8; capacity *= 2) {
        /* Three-byte elements and one trailing byte exercise unaligned copies. */
        CHECK(flux_ring_init(state, state_size, storage + 1, capacity * 3 + 1, 3, &ring) ==
              FLUX_OK);
        CHECK(flux_ring_capacity(ring) == capacity);
        unsigned char output[3] = {99, 99, 99};
        CHECK(!flux_ring_try_pop(ring, output));
        CHECK(output[0] == 99);
        CHECK(!flux_ring_try_push(ring, NULL));
        CHECK(!flux_ring_try_pop(ring, NULL));
        for (unsigned int round = 0; round < 1000; ++round) {
            for (size_t i = 0; i < capacity; ++i) {
                unsigned char input[3] = {(unsigned char)round, (unsigned char)i, 0xa5};
                CHECK(flux_ring_try_push(ring, input));
                memset(input, 0, sizeof(input)); /* Queue owns its copy. */
            }
            CHECK(!flux_ring_try_push(ring, output));
            for (size_t i = 0; i < capacity; ++i) {
                CHECK(flux_ring_try_pop(ring, output));
                CHECK(output[0] == (unsigned char)round && output[1] == i && output[2] == 0xa5);
            }
            CHECK(!flux_ring_try_pop(ring, output));
        }

        /* No threads are active. Exercise actual unsigned counter rollover,
         * including nonzero slot indices, rather than only buffer wraparound. */
        atomic_store_explicit(&ring->write_index, SIZE_MAX - 2, memory_order_relaxed);
        atomic_store_explicit(&ring->read_index, SIZE_MAX - 2, memory_order_relaxed);
        for (size_t i = 0; i < capacity; ++i) {
            unsigned char input[3] = {(unsigned char)i, 1, 2};
            CHECK(flux_ring_try_push(ring, input));
        }
        CHECK(!flux_ring_try_push(ring, output));
        for (size_t i = 0; i < capacity; ++i) {
            CHECK(flux_ring_try_pop(ring, output));
            CHECK(output[0] == i && output[1] == 1 && output[2] == 2);
        }
        CHECK(!flux_ring_try_pop(ring, output));
        for (size_t i = 0; i < 32; ++i) {
            unsigned char input[3] = {(unsigned char)i, 1, 2};
            CHECK(flux_ring_try_push(ring, input));
            CHECK(flux_ring_try_pop(ring, output));
            CHECK(memcmp(input, output, 3) == 0);
        }
        /* Failed reinitialization preserves the usable queue. */
        CHECK(flux_ring_init(state, state_size, storage, 7, 1, &ring) == FLUX_INVALID_ARGUMENT);
        CHECK(flux_ring_capacity(ring) == capacity);
    }
    free(state);
    return 0;
}
