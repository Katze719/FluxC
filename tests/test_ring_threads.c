#include "flux/ring.h"
#include "check.h"

#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>

typedef struct {
    uint64_t sequence;
    uint64_t inverse;
    unsigned char payload[13];
} message_t;

typedef struct {
    flux_ring_t *ring;
    uint64_t iterations;
    atomic_bool start;
    atomic_bool failed;
} context_t;

static void *produce(void *argument)
{
    context_t *context = argument;
    /* Acquire receives the main thread's release start signal. */
    while (!atomic_load_explicit(&context->start, memory_order_acquire)) {
    }
    for (uint64_t i = 0; i < context->iterations; ++i) {
        message_t message = {0};
        message.sequence = i;
        message.inverse = ~i;
        for (size_t j = 0; j < sizeof(message.payload); ++j)
            message.payload[j] = (unsigned char)(i + j);
        while (!flux_ring_try_push(context->ring, &message)) {
            /* Cancellation only; no payload is published through failed. */
            if (atomic_load_explicit(&context->failed, memory_order_relaxed))
                return NULL;
        }
    }
    return NULL;
}

static void *consume(void *argument)
{
    context_t *context = argument;
    while (!atomic_load_explicit(&context->start, memory_order_acquire)) {
    }
    for (uint64_t i = 0; i < context->iterations; ++i) {
        message_t message;
        while (!flux_ring_try_pop(context->ring, &message)) {
        }
        bool correct = message.sequence == i && message.inverse == ~i;
        for (size_t j = 0; j < sizeof(message.payload); ++j)
            correct = correct && message.payload[j] == (unsigned char)(i + j);
        if (!correct) {
            atomic_store_explicit(&context->failed, true, memory_order_relaxed);
            return NULL;
        }
    }
    return NULL;
}

int main(void)
{
    size_t capacities[] = {1, 2, 64, 1024};
    void *state = malloc(flux_ring_state_size());
    CHECK(state != NULL);
    for (size_t i = 0; i < sizeof(capacities) / sizeof(capacities[0]); ++i) {
        size_t bytes = capacities[i] * sizeof(message_t);
        void *storage = malloc(bytes);
        CHECK(storage != NULL);
        context_t context;
        context.iterations = 300000;
        atomic_init(&context.start, false);
        atomic_init(&context.failed, false);
        CHECK(flux_ring_init(state, flux_ring_state_size(), storage, bytes, sizeof(message_t),
                             &context.ring) == FLUX_OK);
        pthread_t producer, consumer;
        CHECK(pthread_create(&producer, NULL, produce, &context) == 0);
        CHECK(pthread_create(&consumer, NULL, consume, &context) == 0);
        /* Publish initialization to both workers without timing-based sleeps. */
        atomic_store_explicit(&context.start, true, memory_order_release);
        CHECK(pthread_join(producer, NULL) == 0);
        CHECK(pthread_join(consumer, NULL) == 0);
        CHECK(!atomic_load_explicit(&context.failed, memory_order_relaxed));
        message_t output;
        CHECK(!flux_ring_try_pop(context.ring, &output));
        free(storage);
    }
    free(state);
    return 0;
}
