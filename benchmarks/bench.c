#define _POSIX_C_SOURCE 200809L
#include "flux/ring.h"
#include "flux/pool.h"
#include "flux/latency.h"
#include "flux/time.h"

#include <errno.h>
#include <inttypes.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/utsname.h>

#if defined(__GNUC__) || defined(__clang__)
#define NOINLINE __attribute__((noinline))
#else
#define NOINLINE
#endif

static void require(bool condition)
{
    if (!condition) {
        fputs("Benchmark failed\n", stderr);
        exit(EXIT_FAILURE);
    }
}

static uint64_t now_ns(void)
{
    uint64_t ns;
    require(flux_time_now_ns(&ns) == FLUX_OK);
    return ns;
}

static void report(const char *name, uint64_t iterations, uint64_t elapsed, uint64_t checksum)
{
    printf("%-32s %9.2f ns/iteration %10.2f M iterations/s  checksum=%" PRIu64 "\n", name,
           (double)elapsed / (double)iterations,
           elapsed == 0 ? 0.0 : (double)iterations * 1000.0 / (double)elapsed, checksum);
}

/* Separate calls preserve the copy/allocation baselines under optimization. */
static NOINLINE void raw_copy(void *to, const void *from)
{
    memcpy(to, from, sizeof(uint64_t));
}

static NOINLINE void *baseline_allocate(void)
{
    return malloc(32);
}

static NOINLINE void baseline_free(void *block)
{
    free(block);
}

static void bench_local(uint64_t iterations)
{
    uint64_t storage[64], output = 0, checksum = 0;
    void *state = malloc(flux_ring_state_size());
    require(state != NULL);
    flux_ring_t *ring;
    require(flux_ring_init(state, flux_ring_state_size(), storage, sizeof(storage),
                           sizeof(storage[0]), &ring) == FLUX_OK);
    uint64_t start = now_ns();
    for (uint64_t i = 0; i < iterations; ++i) {
        raw_copy(storage, &i);
        raw_copy(&output, storage);
        checksum += output;
    }
    report("two 8-byte copies (baseline)", iterations, now_ns() - start, checksum);
    checksum = 0;
    start = now_ns();
    for (uint64_t i = 0; i < iterations; ++i) {
        require(flux_ring_try_push(ring, &i));
        require(flux_ring_try_pop(ring, &output));
        checksum += output;
    }
    report("ring push+pop, capacity 64", iterations, now_ns() - start, checksum);
    free(state);

    _Alignas(max_align_t) unsigned char blocks[64 * 32];
    flux_pool_slot_t slots[64];
    flux_pool_t pool;
    require(flux_pool_init(&pool, blocks, sizeof(blocks), 32, _Alignof(max_align_t), slots, 64) ==
            FLUX_OK);
    checksum = 0;
    start = now_ns();
    for (uint64_t i = 0; i < iterations; ++i) {
        volatile unsigned char *block = baseline_allocate();
        require(block != NULL);
        *block = (unsigned char)i;
        checksum += *block;
        baseline_free((void *)block);
    }
    report("malloc+free 32 bytes (baseline)", iterations, now_ns() - start, checksum);
    checksum = 0;
    start = now_ns();
    for (uint64_t i = 0; i < iterations; ++i) {
        volatile unsigned char *block = flux_pool_acquire(&pool);
        require(block != NULL);
        *block = (unsigned char)i;
        checksum += *block;
        require(flux_pool_release(&pool, (void *)block));
    }
    report("pool acquire+release 32 bytes", iterations, now_ns() - start, checksum);

    uint64_t buckets[256];
    flux_latency_t latency;
    require(flux_latency_init(&latency, buckets, 256, 10) == FLUX_OK);
    volatile uint64_t sum = 0;
    start = now_ns();
    for (uint64_t i = 0; i < iterations; ++i)
        sum += i & 1023;
    report("sum only (baseline)", iterations, now_ns() - start, sum);
    start = now_ns();
    for (uint64_t i = 0; i < iterations; ++i)
        require(flux_latency_record(&latency, i & 1023) == FLUX_OK);
    report("histogram record", iterations, now_ns() - start, latency.sum_ns);

    start = now_ns();
    uint64_t previous = start;
    for (uint64_t i = 0; i < iterations; ++i)
        previous = now_ns();
    report("monotonic clock", iterations, now_ns() - start, previous);
}

typedef struct {
    pthread_mutex_t mutex;
    uint64_t *storage;
    size_t capacity;
    size_t write;
    size_t read;
    size_t count;
} mutex_queue_t;

static bool mutex_push(mutex_queue_t *queue, uint64_t value)
{
    require(pthread_mutex_lock(&queue->mutex) == 0);
    bool success = queue->count != queue->capacity;
    if (success) {
        queue->storage[queue->write] = value;
        queue->write = (queue->write + 1) & (queue->capacity - 1);
        ++queue->count;
    }
    require(pthread_mutex_unlock(&queue->mutex) == 0);
    return success;
}

static bool mutex_pop(mutex_queue_t *queue, uint64_t *value)
{
    require(pthread_mutex_lock(&queue->mutex) == 0);
    bool success = queue->count != 0;
    if (success) {
        *value = queue->storage[queue->read];
        queue->read = (queue->read + 1) & (queue->capacity - 1);
        --queue->count;
    }
    require(pthread_mutex_unlock(&queue->mutex) == 0);
    return success;
}

typedef struct {
    flux_ring_t *ring;
    mutex_queue_t *mutex_queue;
    atomic_bool start;
    uint64_t iterations;
    uint64_t producer_retries;
    uint64_t consumer_retries;
    uint64_t checksum;
} transfer_t;

static void *producer(void *argument)
{
    transfer_t *transfer = argument;
    /* Acquire receives main's release start signal, outside the timed loop. */
    while (!atomic_load_explicit(&transfer->start, memory_order_acquire)) {
    }
    for (uint64_t i = 0; i < transfer->iterations; ++i)
        while (transfer->ring != NULL ? !flux_ring_try_push(transfer->ring, &i)
                                      : !mutex_push(transfer->mutex_queue, i))
            ++transfer->producer_retries;
    return NULL;
}

static void *consumer(void *argument)
{
    transfer_t *transfer = argument;
    while (!atomic_load_explicit(&transfer->start, memory_order_acquire)) {
    }
    uint64_t checksum = 0;
    for (uint64_t i = 0; i < transfer->iterations; ++i) {
        uint64_t value;
        while (transfer->ring != NULL ? !flux_ring_try_pop(transfer->ring, &value)
                                      : !mutex_pop(transfer->mutex_queue, &value))
            ++transfer->consumer_retries;
        require(value == i);
        checksum += value;
    }
    transfer->checksum = checksum;
    return NULL;
}

static void bench_transfer(size_t capacity, uint64_t iterations, bool use_mutex)
{
    uint64_t *storage = malloc(capacity * sizeof(*storage));
    void *state = malloc(flux_ring_state_size());
    require(storage != NULL && state != NULL);
    flux_ring_t *ring;
    require(flux_ring_init(state, flux_ring_state_size(), storage, capacity * sizeof(*storage),
                           sizeof(*storage), &ring) == FLUX_OK);
    mutex_queue_t queue;
    require(pthread_mutex_init(&queue.mutex, NULL) == 0);
    queue.storage = storage;
    queue.capacity = capacity;
    queue.write = queue.read = queue.count = 0;
    transfer_t transfer = {0};
    transfer.ring = use_mutex ? NULL : ring;
    transfer.mutex_queue = &queue;
    transfer.iterations = iterations;
    atomic_init(&transfer.start, false);
    pthread_t produce_thread, consume_thread;
    require(pthread_create(&produce_thread, NULL, producer, &transfer) == 0);
    require(pthread_create(&consume_thread, NULL, consumer, &transfer) == 0);
    uint64_t start = now_ns();
    atomic_store_explicit(&transfer.start, true, memory_order_release);
    require(pthread_join(produce_thread, NULL) == 0);
    require(pthread_join(consume_thread, NULL) == 0);
    uint64_t elapsed = now_ns() - start;
    char name[80];
    snprintf(name, sizeof(name), "%s transfer, capacity %zu", use_mutex ? "mutex" : "SPSC",
             capacity);
    report(name, iterations, elapsed, transfer.checksum);
    printf("  retries producer=%" PRIu64 " consumer=%" PRIu64 "\n", transfer.producer_retries,
           transfer.consumer_retries);
    require(pthread_mutex_destroy(&queue.mutex) == 0);
    free(state);
    free(storage);
}

static void bench_latency(void)
{
    uint64_t storage[64], buckets[256];
    void *state = malloc(flux_ring_state_size());
    require(state != NULL);
    flux_ring_t *ring;
    require(flux_ring_init(state, flux_ring_state_size(), storage, sizeof(storage),
                           sizeof(*storage), &ring) == FLUX_OK);
    for (unsigned int baseline = 0; baseline < 2; ++baseline) {
        flux_latency_t latency;
        require(flux_latency_init(&latency, buckets, 256, 10) == FLUX_OK);
        for (uint64_t i = 0; i < 20000; ++i) {
            uint64_t start = now_ns();
            if (!baseline) {
                uint64_t output;
                require(flux_ring_try_push(ring, &i));
                require(flux_ring_try_pop(ring, &output) && output == i);
            }
            require(flux_latency_record(&latency, now_ns() - start) == FLUX_OK);
        }
        uint64_t p50, p95, p99;
        require(flux_latency_percentile(&latency, 50, &p50) == FLUX_OK);
        require(flux_latency_percentile(&latency, 95, &p95) == FLUX_OK);
        require(flux_latency_percentile(&latency, 99, &p99) == FLUX_OK);
        printf("%s: p50<=%" PRIu64 " p95<=%" PRIu64 " p99<=%" PRIu64 " ns, max=%" PRIu64 " ns\n",
               baseline ? "empty clock interval (baseline)" : "local ring pair + clock overhead",
               p50, p95, p99, latency.max_ns);
    }
    free(state);
}

int main(int argc, char **argv)
{
    uint64_t iterations = 200000;
    if (argc > 2) {
        fputs("Usage: flux_bench [iterations: 1..10000000]\n", stderr);
        return EXIT_FAILURE;
    }
    if (argc == 2) {
        char *end;
        errno = 0;
        unsigned long long parsed = strtoull(argv[1], &end, 10);
        if (errno != 0 || *end != '\0' || parsed == 0 || parsed > 10000000) {
            fputs("Invalid iteration count\n", stderr);
            return EXIT_FAILURE;
        }
        iterations = (uint64_t)parsed;
    }
    struct utsname environment;
    if (uname(&environment) == 0)
        printf("%s %s %s\n", environment.sysname, environment.release, environment.machine);
    FILE *cpuinfo = fopen("/proc/cpuinfo", "r");
    if (cpuinfo != NULL) {
        char line[256];
        while (fgets(line, sizeof(line), cpuinfo) != NULL)
            if (strncmp(line, "model name", 10) == 0 || strncmp(line, "Hardware", 8) == 0) {
                fputs(line, stdout);
                break;
            }
        fclose(cpuinfo);
    }
#ifdef __VERSION__
    printf("Compiler: %s\n", __VERSION__);
#endif
    printf("FluxC %s, build %s, iterations=%" PRIu64 "\n", FLUX_VERSION, FLUX_BUILD_TYPE,
           iterations);
#ifdef __OPTIMIZE__
    puts("Compiler optimization enabled");
#else
    puts("Compiler optimization disabled; use a Release build for measurements");
#endif
    puts("Concurrent tests use OS placement; transfer times include start signaling and joins.");
    bench_local(iterations);
    size_t capacities[] = {1, 64, 1024};
    for (size_t i = 0; i < sizeof(capacities) / sizeof(capacities[0]); ++i) {
        bench_transfer(capacities[i], iterations, true);
        bench_transfer(capacities[i], iterations, false);
    }
    bench_latency();
    return EXIT_SUCCESS;
}
