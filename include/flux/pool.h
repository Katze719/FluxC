#ifndef FLUX_POOL_H
#define FLUX_POOL_H

#include <stdbool.h>
#include <stddef.h>
#include "flux/result.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Fixed-size, caller-owned block pool. Not thread-safe: serialize all access
 * externally. Non-NULL pool arguments require successful initialization before
 * acquire/release/available. Init is O(capacity); acquire/release are bounded
 * O(1), allocation-free, and nonblocking. No memory is cleared on acquire/release.
 *
 * Public structures permit stack allocation without hidden control allocation;
 * their fields are private by convention, and their layout is not a stable ABI.
 * Storage, slots and pool must be disjoint and live as long as the pool. Do not
 * copy a live pool or modify its metadata. Acquired blocks remain caller-owned;
 * release invalidates access until a later acquire. A stale pointer to a block
 * acquired again cannot be distinguished from its current owner's pointer.
 */
typedef struct {
    size_t next;
    bool in_use;
} flux_pool_slot_t;

typedef struct {
    unsigned char *storage;
    flux_pool_slot_t *slots;
    size_t stride;
    size_t capacity;
    size_t free_head;
    size_t available;
} flux_pool_t;

/* Computes padded bytes for capacity blocks. Alignment must be a nonzero power
 * of two. Rejects zero sizes/capacity and overflow; leaves *out unchanged on
 * failure. The caller must supply storage aligned to block_alignment. */
flux_result_t flux_pool_storage_size(size_t block_size, size_t block_alignment, size_t capacity,
                                     size_t *out);

/* slots must contain capacity entries. Only successful initialization writes
 * pool/slots. Validates storage size and alignment; blocks have padded stride. */
flux_result_t flux_pool_init(flux_pool_t *pool, void *storage, size_t storage_size,
                             size_t block_size, size_t block_alignment, flux_pool_slot_t *slots,
                             size_t capacity);

/* NULL if exhausted or pool is NULL. pool otherwise must be initialized. */
void *flux_pool_acquire(flux_pool_t *pool);

/* False for NULL, foreign/interior pointers, or a block already free. */
bool flux_pool_release(flux_pool_t *pool, void *block);

/* Not safe during concurrent mutation. NULL -> 0. */
size_t flux_pool_available(const flux_pool_t *pool);

#ifdef __cplusplus
}
#endif
#endif
