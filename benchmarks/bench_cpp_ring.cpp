#include "flux/ring.hpp"
#include "flux/time.h"

#include <cinttypes>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sys/utsname.h>

static void require(bool condition)
{
    if (!condition) {
        std::fputs("C++ queue benchmark failed\n", stderr);
        std::exit(EXIT_FAILURE);
    }
}

static std::uint64_t now_ns()
{
    std::uint64_t ns;
    require(flux_time_now_ns(&ns) == FLUX_OK);
    return ns;
}

struct sample {
    std::uint64_t sequence;
    std::uint64_t padding[7];
};

template <typename T> static void bench_pairs()
{
    const std::uint64_t iterations = 1000000;
    using queue_type = flux::spsc_queue<T>;
    void *state = std::malloc(queue_type::state_size());
    void *storage = std::malloc(64 * sizeof(T));
    require(state != nullptr && storage != nullptr);
    {
        queue_type queue;
        for (unsigned int round = 0; round < 3; ++round) {
            for (unsigned int wrapped = 0; wrapped < 2; ++wrapped) {
                flux_ring_t *raw = nullptr;
                if (wrapped)
                    require(queue.init(state, queue_type::state_size(), storage, 64 * sizeof(T)) ==
                            FLUX_OK);
                else
                    require(flux_ring_init(state, queue_type::state_size(), storage, 64 * sizeof(T),
                                           sizeof(T), &raw) == FLUX_OK);
                std::uint64_t checksum = 0;
                T output = {};
                std::uint64_t start = now_ns();
                for (std::uint64_t i = 0; i < iterations; ++i) {
                    T input = {};
                    std::memcpy(&input, &i, sizeof(i));
                    if (wrapped) {
                        require(queue.try_push(input));
                        require(queue.try_pop(output));
                    } else {
                        require(flux_ring_try_push(raw, &input));
                        require(flux_ring_try_pop(raw, &output));
                    }
                    /* Inspect byte representation equally for both paths. */
                    checksum += reinterpret_cast<const unsigned char *>(&output)[0];
                }
                std::uint64_t elapsed = now_ns() - start;
                std::printf("%s, %zu-byte elements, round %u: %.2f ns/pair, checksum=%" PRIu64 "\n",
                            wrapped ? "C++ wrapper" : "C API baseline", sizeof(T), round + 1,
                            static_cast<double>(elapsed) / static_cast<double>(iterations),
                            checksum);
            }
        }
    }
    std::free(state);
    std::free(storage);
}

int main()
{
    struct utsname environment;
    if (uname(&environment) == 0)
        std::printf("%s %s %s\n", environment.sysname, environment.release, environment.machine);
    std::printf("FluxC %s, compiler %s, build %s\n", FLUX_VERSION, __VERSION__, FLUX_BUILD_TYPE);
#if defined(__OPTIMIZE__)
    std::puts("Compiler optimization enabled");
#else
    std::puts("Compiler optimization disabled; use Release for measurements");
#endif
    std::puts("Single-thread push/pop pairs; capacity 64, 1000000 iterations per round.");
    bench_pairs<std::uint64_t>();
    bench_pairs<sample>();
    return EXIT_SUCCESS;
}
