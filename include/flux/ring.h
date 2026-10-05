#ifndef FLUX_RING_H
#define FLUX_RING_H

#include <stdbool.h>
#include <stddef.h>
#include "flux/result.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Bounded FIFO for exactly one producer and one consumer (SPSC).
 * Copies fixed-size elements; no ownership of pointers inside an element is
 * transferred. Both control memory and element storage remain caller-owned.
 *
 * The opaque state keeps C11 atomic types out of the C++ ABI. Supply raw memory
 * with no declared type (e.g. malloc storage), at least state_size() bytes and
 * aligned to state_alignment(). malloc provides sufficient alignment here.
 * This memory and the element storage must be disjoint and remain valid until
 * both threads have stopped. Do not copy or move initialized control memory.
 * There is no destroy operation: no resources beyond supplied memory are held.
 *
 * All non-NULL handles passed to operations must be successfully initialized.
 * Initialization/reinitialization requires exclusive access. It rejects targets
 * whose size_t atomics are not lock-free. Push/pop use a fixed number of atomic
 * operations and one element copy, without allocation, locks, syscalls, or retry
 * loops. Their algorithm is bounded; this is not an OS scheduling guarantee.
 * Each side must be serialized independently. Switching the producer/consumer
 * thread requires external synchronization. Storage must not be modified by
 * callers after initialization, except through the queue API.
 */
typedef struct flux_ring flux_ring_t;

size_t flux_ring_state_size(void);
size_t flux_ring_state_alignment(void);

/*
 * Capacity is storage_size / element_size, must be a nonzero power of two,
 * and cannot exceed SIZE_MAX / 2. All capacity slots are usable; trailing bytes
 * are unused. Element storage has no alignment requirement. Failure leaves
 * *out and the supplied memory unchanged. out must not overlap either buffer.
 */
flux_result_t flux_ring_init(void *state, size_t state_size, void *storage, size_t storage_size,
                             size_t element_size, flux_ring_t **out);

/* False for full/empty respectively, or NULL arguments; output is unchanged
 * on failure. Element/output ranges must not overlap any queue storage/state.
 * A successful push copies the element before returning. */
bool flux_ring_try_push(flux_ring_t *ring, const void *element);
bool flux_ring_try_pop(flux_ring_t *ring, void *element);

/* Immutable after initialization; may be queried by either side. NULL -> 0. */
size_t flux_ring_capacity(const flux_ring_t *ring);

#ifdef __cplusplus
}
#endif
#endif
