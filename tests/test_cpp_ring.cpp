#include "flux/ring.hpp"
#include "check.h"

#include <cstdint>
#include <cstdlib>
#include <type_traits>
#include <utility>

#if defined(FLUX_TEST_CPP_THREADS)
#include <atomic>
#include <thread>
#endif

using int_queue = flux::spsc_queue<int>;
static_assert(!std::is_copy_constructible<int_queue>::value, "Queue must not be copied");
static_assert(!std::is_copy_assignable<int_queue>::value, "Queue must not be copy-assigned");
static_assert(!std::is_move_constructible<int_queue>::value, "Queue must not be moved");
static_assert(!std::is_move_assignable<int_queue>::value, "Queue must not be move-assigned");
static_assert(std::is_nothrow_default_constructible<int_queue>::value, "No throwing constructor");
static_assert(noexcept(std::declval<int_queue &>().try_push(std::declval<const int &>())),
              "No throwing push");
static_assert(noexcept(std::declval<int_queue &>().try_pop(std::declval<int &>())),
              "No throwing pop");

struct alignas(64) message {
    std::uint64_t sequence;
    std::uint64_t inverse;
    unsigned char payload[13];
};

struct custom_address {
    int value;

    custom_address *operator&() noexcept
    {
        return nullptr;
    }

    const custom_address *operator&() const noexcept
    {
        return nullptr;
    }
};

static void check_custom_address()
{
    using queue_type = flux::spsc_queue<custom_address>;
    void *state = std::malloc(queue_type::state_size());
    CHECK(state != nullptr);
    custom_address storage[2];
    {
        queue_type queue;
        CHECK(queue.init(state, queue_type::state_size(), storage, sizeof(storage)) == FLUX_OK);
        custom_address input = {42}, output = {0};
        CHECK(queue.try_push(input));
        CHECK(queue.try_pop(output) && output.value == 42);
    }
    std::free(state);
}

static void check_values()
{
    int_queue queue;
    int output = 99;
    CHECK(queue.capacity() == 0);
    CHECK(!queue.try_push(42));
    CHECK(!queue.try_pop(output) && output == 99);
    CHECK(int_queue::state_size() == flux_ring_state_size());
    CHECK(int_queue::state_alignment() == flux_ring_state_alignment());

    void *state = std::malloc(int_queue::state_size());
    CHECK(state != nullptr);
    int storage[8];
    CHECK(queue.init(nullptr, int_queue::state_size(), storage, sizeof(storage)) ==
          FLUX_INVALID_ARGUMENT);
    CHECK(queue.capacity() == 0);
    CHECK(queue.init(state, 0, storage, sizeof(storage)) == FLUX_INVALID_ARGUMENT);
    CHECK(queue.init(state, int_queue::state_size(), nullptr, sizeof(storage)) ==
          FLUX_INVALID_ARGUMENT);
    CHECK(queue.init(state, int_queue::state_size(), storage, 0) == FLUX_INVALID_ARGUMENT);
    CHECK(queue.init(state, int_queue::state_size(), storage, 3 * sizeof(int)) ==
          FLUX_INVALID_ARGUMENT);

    for (std::size_t capacity = 1; capacity <= 8; capacity *= 2) {
        CHECK(queue.init(state, int_queue::state_size(), storage, capacity * sizeof(int)) ==
              FLUX_OK);
        CHECK(queue.capacity() == capacity);
        for (int round = 0; round < 1000; ++round) {
            for (std::size_t i = 0; i < capacity; ++i) {
                int input = round * 8 + static_cast<int>(i);
                CHECK(queue.try_push(input));
            }
            CHECK(!queue.try_push(-1));
            for (std::size_t i = 0; i < capacity; ++i) {
                CHECK(queue.try_pop(output));
                CHECK(output == round * 8 + static_cast<int>(i));
            }
            output = 99;
            CHECK(!queue.try_pop(output) && output == 99);
        }
        CHECK(queue.try_push(42));
        CHECK(queue.init(state, 0, storage, sizeof(storage)) == FLUX_INVALID_ARGUMENT);
        CHECK(queue.capacity() == capacity);
        CHECK(queue.try_pop(output) && output == 42);
    }
    std::free(state);
}

static void check_storage_lifetime()
{
    using message_queue = flux::spsc_queue<message>;
    void *state = std::malloc(message_queue::state_size());
    CHECK(state != nullptr);
    /* The element buffer may be unaligned even for an over-aligned T. */
    unsigned char storage[2 * sizeof(message) + 1];
    {
        message_queue queue;
        CHECK(queue.init(state, message_queue::state_size(), storage + 1, 2 * sizeof(message)) ==
              FLUX_OK);
        message input = {7, ~UINT64_C(7), {1, 2, 3}};
        CHECK(queue.try_push(input));
        input.sequence = 99;
        input.payload[0] = 99;
        message output = {};
        CHECK(queue.try_pop(output));
        CHECK(output.sequence == 7 && output.inverse == ~UINT64_C(7));
        CHECK(output.payload[0] == 1 && output.payload[1] == 2 && output.payload[2] == 3);
    }
    /* The wrapper never owns/frees either buffer. Rebind after its destruction. */
    {
        message_queue queue;
        CHECK(queue.init(state, message_queue::state_size(), storage + 1, 2 * sizeof(message)) ==
              FLUX_OK);
        CHECK(queue.capacity() == 2);
    }
    std::free(state);
}

#if defined(FLUX_TEST_CPP_THREADS)
static void check_threads()
{
    using message_queue = flux::spsc_queue<message>;
    const std::size_t capacities[] = {1, 64, 1024};
    for (std::size_t capacity : capacities) {
        void *state = std::malloc(message_queue::state_size());
        void *storage = std::malloc(capacity * sizeof(message));
        CHECK(state != nullptr && storage != nullptr);
        {
            message_queue queue;
            CHECK(queue.init(state, message_queue::state_size(), storage,
                             capacity * sizeof(message)) == FLUX_OK);
            std::atomic<bool> start(false);
            std::atomic<bool> failed(false);
            const std::uint64_t iterations = 100000;
            std::thread producer([&] {
                /* Acquire receives the main thread's release start signal. */
                while (!start.load(std::memory_order_acquire)) {
                }
                for (std::uint64_t i = 0; i < iterations; ++i) {
                    message input = {};
                    input.sequence = i;
                    input.inverse = ~i;
                    for (std::size_t j = 0; j < sizeof(input.payload); ++j)
                        input.payload[j] = static_cast<unsigned char>(i + j);
                    while (!queue.try_push(input)) {
                        /* Cancellation only; no payload is published here. */
                        if (failed.load(std::memory_order_relaxed))
                            return;
                    }
                }
            });
            std::thread consumer([&] {
                while (!start.load(std::memory_order_acquire)) {
                }
                for (std::uint64_t i = 0; i < iterations; ++i) {
                    message output;
                    while (!queue.try_pop(output)) {
                    }
                    bool correct = output.sequence == i && output.inverse == ~i;
                    for (std::size_t j = 0; j < sizeof(output.payload); ++j)
                        correct = correct && output.payload[j] == static_cast<unsigned char>(i + j);
                    if (!correct) {
                        failed.store(true, std::memory_order_relaxed);
                        return;
                    }
                }
            });
            /* All setup completes before either worker enters the queue loop. */
            start.store(true, std::memory_order_release);
            producer.join();
            consumer.join();
            CHECK(!failed.load(std::memory_order_relaxed));
            message output;
            CHECK(!queue.try_pop(output));
        }
        std::free(state);
        std::free(storage);
    }
}
#endif

int main()
{
    check_values();
    check_storage_lifetime();
    check_custom_address();
#if defined(FLUX_TEST_CPP_THREADS)
    check_threads();
#endif
    return 0;
}
