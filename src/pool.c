#include "flux/pool.h"
#include "realtime.h"

#include <stdint.h>

flux_result_t flux_pool_storage_size(size_t block_size, size_t block_alignment, size_t capacity,
                                     size_t *out)
{
    if (out == NULL || block_size == 0 || capacity == 0 || block_alignment == 0 ||
        (block_alignment & (block_alignment - 1)) != 0)
        return FLUX_INVALID_ARGUMENT;
    if (block_size > SIZE_MAX - (block_alignment - 1))
        return FLUX_OVERFLOW;

    size_t stride = (block_size + block_alignment - 1) & ~(block_alignment - 1);
    if (capacity > SIZE_MAX / stride || capacity > SIZE_MAX / sizeof(flux_pool_slot_t))
        return FLUX_OVERFLOW;
    *out = stride * capacity;
    return FLUX_OK;
}

flux_result_t flux_pool_init(flux_pool_t *pool, void *storage, size_t storage_size,
                             size_t block_size, size_t block_alignment, flux_pool_slot_t *slots,
                             size_t capacity)
{
    if (pool == NULL || storage == NULL || slots == NULL)
        return FLUX_INVALID_ARGUMENT;

    size_t required;
    flux_result_t result = flux_pool_storage_size(block_size, block_alignment, capacity, &required);
    if (result != FLUX_OK)
        return result;
    if (storage_size < required || (uintptr_t)storage % block_alignment != 0)
        return FLUX_INVALID_ARGUMENT;

    for (size_t i = 0; i < capacity; ++i) {
        slots[i].next = i + 1;
        slots[i].in_use = false;
    }
    /* capacity is the end-of-list sentinel, never a valid block index. */
    pool->storage = storage;
    pool->slots = slots;
    pool->stride = required / capacity;
    pool->capacity = capacity;
    pool->free_head = 0;
    pool->available = capacity;
    return FLUX_OK;
}

FLUX_NONBLOCKING void *flux_pool_acquire(flux_pool_t *pool)
{
    if (pool == NULL || pool->available == 0)
        return NULL;

    size_t index = pool->free_head;
    pool->free_head = pool->slots[index].next;
    pool->slots[index].in_use = true;
    --pool->available;
    return pool->storage + index * pool->stride;
}

FLUX_NONBLOCKING bool flux_pool_release(flux_pool_t *pool, void *block)
{
    if (pool == NULL || block == NULL)
        return false;

    /* Integer addresses avoid undefined subtraction/comparison of unrelated
     * pointers while rejecting foreign and interior addresses. */
    uintptr_t base = (uintptr_t)pool->storage;
    uintptr_t address = (uintptr_t)block;
    if (address < base)
        return false;
    uintptr_t offset = address - base;
    if (offset >= pool->stride * pool->capacity || offset % pool->stride != 0)
        return false;

    size_t index = (size_t)(offset / pool->stride);
    if (!pool->slots[index].in_use)
        return false;
    pool->slots[index].in_use = false;
    pool->slots[index].next = pool->free_head;
    pool->free_head = index;
    ++pool->available;
    return true;
}

FLUX_NONBLOCKING size_t flux_pool_available(const flux_pool_t *pool)
{
    return pool == NULL ? 0 : pool->available;
}
