#include "ring_internal.h"
#include "realtime.h"

#include <stdint.h>
#include <string.h>

_Static_assert(_Alignof(flux_ring_t) <= _Alignof(max_align_t),
               "ring control must fit the alignment guaranteed by malloc");

size_t flux_ring_state_size(void)
{
    return sizeof(flux_ring_t);
}

size_t flux_ring_state_alignment(void)
{
    return _Alignof(flux_ring_t);
}

flux_result_t flux_ring_init(void *state, size_t state_size, void *storage, size_t storage_size,
                             size_t element_size, flux_ring_t **out)
{
    if (state == NULL || storage == NULL || out == NULL || element_size == 0)
        return FLUX_INVALID_ARGUMENT;
    if (state_size < sizeof(flux_ring_t) || (uintptr_t)state % _Alignof(flux_ring_t) != 0)
        return FLUX_INVALID_ARGUMENT;

    size_t capacity = storage_size / element_size;
    if (capacity == 0 || (capacity & (capacity - 1)) != 0 || capacity > SIZE_MAX / 2)
        return FLUX_INVALID_ARGUMENT;

    atomic_size_t probe;
    atomic_init(&probe, 0);
    if (!atomic_is_lock_free(&probe))
        return FLUX_NOT_SUPPORTED;

    flux_ring_t *ring = state;
    ring->storage = storage;
    ring->element_size = element_size;
    ring->capacity = capacity;
    ring->mask = capacity - 1;
    atomic_init(&ring->write_index, 0);
    atomic_init(&ring->read_index, 0);
    *out = ring;
    return FLUX_OK;
}

FLUX_NONBLOCKING bool flux_ring_try_push(flux_ring_t *ring, const void *element)
{
    if (ring == NULL || element == NULL)
        return false;

    /* Only this producer writes write_index, so its own load can be relaxed. */
    size_t write = atomic_load_explicit(&ring->write_index, memory_order_relaxed);
    /* Acquire pairs with the consumer's release, before reusing a read slot. */
    size_t read = atomic_load_explicit(&ring->read_index, memory_order_acquire);
    if (write - read == ring->capacity)
        return false;

    memcpy(ring->storage + (write & ring->mask) * ring->element_size, element, ring->element_size);
    /* Release publishes the element to the consumer's acquire load. Unsigned
     * counter wrap is intentional; power-of-two capacity preserves indexing. */
    atomic_store_explicit(&ring->write_index, write + 1, memory_order_release);
    return true;
}

FLUX_NONBLOCKING bool flux_ring_try_pop(flux_ring_t *ring, void *element)
{
    if (ring == NULL || element == NULL)
        return false;

    /* Only this consumer writes read_index. */
    size_t read = atomic_load_explicit(&ring->read_index, memory_order_relaxed);
    /* Acquire makes the producer's element copy visible before reading it. */
    size_t write = atomic_load_explicit(&ring->write_index, memory_order_acquire);
    if (read == write)
        return false;

    memcpy(element, ring->storage + (read & ring->mask) * ring->element_size, ring->element_size);
    /* Release makes the completed copy visible before the producer reuses it. */
    atomic_store_explicit(&ring->read_index, read + 1, memory_order_release);
    return true;
}

FLUX_NONBLOCKING size_t flux_ring_capacity(const flux_ring_t *ring)
{
    return ring == NULL ? 0 : ring->capacity;
}
