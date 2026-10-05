#include "flux/pool.h"
#include "check.h"

#include <stdint.h>
#include <string.h>

int main(void)
{
    _Alignas(64) unsigned char storage[64];
    flux_pool_slot_t slots[8];
    flux_pool_t pool;
    size_t bytes = 123;
    CHECK(flux_pool_storage_size(7, 8, 8, &bytes) == FLUX_OK && bytes == 64);
    CHECK(flux_pool_storage_size(0, 8, 8, &bytes) == FLUX_INVALID_ARGUMENT);
    CHECK(flux_pool_storage_size(7, 0, 8, &bytes) == FLUX_INVALID_ARGUMENT);
    CHECK(flux_pool_storage_size(7, 3, 8, &bytes) == FLUX_INVALID_ARGUMENT);
    CHECK(flux_pool_storage_size(7, 8, 0, &bytes) == FLUX_INVALID_ARGUMENT);
    CHECK(flux_pool_storage_size(7, 8, 8, NULL) == FLUX_INVALID_ARGUMENT);
    CHECK(flux_pool_storage_size(SIZE_MAX, 8, 1, &bytes) == FLUX_OVERFLOW);
    CHECK(flux_pool_storage_size(8, 8, SIZE_MAX / 8 + 1, &bytes) == FLUX_OVERFLOW);
    CHECK(flux_pool_storage_size(1, 1, SIZE_MAX, &bytes) == FLUX_OVERFLOW);
    CHECK(bytes == 64);
    CHECK(flux_pool_init(NULL, storage, 64, 7, 8, slots, 8) == FLUX_INVALID_ARGUMENT);
    CHECK(flux_pool_init(&pool, NULL, 64, 7, 8, slots, 8) == FLUX_INVALID_ARGUMENT);
    CHECK(flux_pool_init(&pool, storage, 64, 7, 8, NULL, 8) == FLUX_INVALID_ARGUMENT);
    CHECK(flux_pool_init(&pool, storage + 1, 63, 7, 8, slots, 8) == FLUX_INVALID_ARGUMENT);
    CHECK(flux_pool_init(&pool, storage, 63, 7, 8, slots, 8) == FLUX_INVALID_ARGUMENT);
    CHECK(flux_pool_init(&pool, storage, 64, 7, 8, slots, 8) == FLUX_OK);
    CHECK(flux_pool_available(NULL) == 0);
    CHECK(flux_pool_acquire(NULL) == NULL);
    CHECK(!flux_pool_release(NULL, storage));
    CHECK(!flux_pool_release(&pool, NULL));
    CHECK(!flux_pool_release(&pool, storage));
    void *blocks[8];
    for (size_t i = 0; i < 8; ++i) {
        blocks[i] = flux_pool_acquire(&pool);
        CHECK(blocks[i] == storage + i * 8);
        CHECK((uintptr_t)blocks[i] % 8 == 0);
        memset(blocks[i], (int)i, 7);
        CHECK(flux_pool_available(&pool) == 7 - i);
    }
    CHECK(flux_pool_acquire(&pool) == NULL);
    CHECK(!flux_pool_release(&pool, storage + 1));
    CHECK(!flux_pool_release(&pool, storage + 64));
    int foreign = 0;
    CHECK(!flux_pool_release(&pool, &foreign));
    CHECK(flux_pool_release(&pool, blocks[3]));
    CHECK(!flux_pool_release(&pool, blocks[3]));
    CHECK(flux_pool_acquire(&pool) == blocks[3]);
    CHECK(((unsigned char *)blocks[3])[0] == 3); /* Release does not clear. */
    for (size_t i = 0; i < 8; ++i)
        CHECK(flux_pool_release(&pool, blocks[i]));
    CHECK(flux_pool_available(&pool) == 8);

    /* Compare arbitrary acquire/release order against an independent live set. */
    for (size_t i = 0; i < 8; ++i)
        blocks[i] = NULL;
    uint32_t random = 12345;
    size_t live = 0;
    for (size_t iteration = 0; iteration < 50000; ++iteration) {
        random = random * UINT32_C(1664525) + UINT32_C(1013904223);
        size_t index = (random >> 16) % 8;
        if (blocks[index] != NULL) {
            CHECK(flux_pool_release(&pool, blocks[index]));
            blocks[index] = NULL;
            --live;
        } else {
            void *block = flux_pool_acquire(&pool);
            CHECK(block != NULL);
            for (size_t i = 0; i < 8; ++i)
                CHECK(blocks[i] != block);
            blocks[index] = block;
            ++live;
        }
        CHECK(flux_pool_available(&pool) == 8 - live);
    }
    for (size_t i = 0; i < 8; ++i)
        if (blocks[i] != NULL)
            CHECK(flux_pool_release(&pool, blocks[i]));
    CHECK(flux_pool_init(&pool, storage, 64, 64, 64, slots, 1) == FLUX_OK);
    CHECK(flux_pool_acquire(&pool) == storage);
    CHECK(flux_pool_acquire(&pool) == NULL);
    CHECK(flux_pool_release(&pool, storage));
    return 0;
}
