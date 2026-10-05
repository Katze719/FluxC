#ifndef FLUX_RING_HPP
#define FLUX_RING_HPP

#include "flux/ring.h"

#include <cstddef>
#include <memory>
#include <type_traits>

namespace flux
{

/*
 * Typed, non-owning C++11 wrapper around the C SPSC queue. Exactly one producer
 * may call try_push and one consumer may call try_pop concurrently on the same
 * instance. Initialization and destruction require both sides to be stopped.
 *
 * No allocation, deallocation, locks, or exceptions. The caller provides raw
 * allocated control memory and an element buffer, following ring.h's alignment,
 * lifetime and disjoint-storage requirements. These buffers and this wrapper
 * must remain alive until both threads have stopped. Destruction does not free
 * them. Pointed-to resources inside T remain the caller's responsibility.
 *
 * Elements are copied as bytes, without constructors or assignment operators.
 * Source/output objects must remain valid and exclusively accessible during the
 * respective call, and must not overlap queue storage. Hot-path behavior is the
 * same as the C implementation: bounded and lock-free after successful init.
 */
template <typename T> class spsc_queue
{
    static_assert(std::is_trivially_copyable<T>::value,
                  "flux::spsc_queue requires a trivially copyable element type");
    static_assert(!std::is_const<T>::value && !std::is_volatile<T>::value,
                  "flux::spsc_queue requires an unqualified element type");

  public:
    spsc_queue() noexcept = default;
    ~spsc_queue() = default;

    spsc_queue(const spsc_queue &) = delete;
    spsc_queue &operator=(const spsc_queue &) = delete;
    spsc_queue(spsc_queue &&) = delete;
    spsc_queue &operator=(spsc_queue &&) = delete;

    static std::size_t state_size() noexcept
    {
        return flux_ring_state_size();
    }

    static std::size_t state_alignment() noexcept
    {
        return flux_ring_state_alignment();
    }

    /* Capacity is storage_size / sizeof(T), with the C API's power-of-two
     * restriction. Failure leaves the existing binding and buffers unchanged.
     * Never initialize/reinitialize while either thread is using this instance. */
    flux_result_t init(void *state, std::size_t state_size, void *storage,
                       std::size_t storage_size) noexcept
    {
        return flux_ring_init(state, state_size, storage, storage_size, sizeof(T), &ring_);
    }

    /* False if full or not initialized. Copies value before returning. */
    bool try_push(const T &value) noexcept
    {
        return flux_ring_try_push(ring_, std::addressof(value));
    }

    /* False if empty or not initialized; value remains unchanged on failure. */
    bool try_pop(T &value) noexcept
    {
        return flux_ring_try_pop(ring_, std::addressof(value));
    }

    /* Immutable after initialization; zero before successful initialization. */
    std::size_t capacity() const noexcept
    {
        return flux_ring_capacity(ring_);
    }

  private:
    flux_ring_t *ring_ = nullptr;
};

} // namespace flux

#endif
