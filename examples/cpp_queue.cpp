#include "flux/ring.hpp"

#include <cstdio>
#include <cstdlib>

int main()
{
    using queue_type = flux::spsc_queue<int>;
    void *state = std::malloc(queue_type::state_size());
    if (state == nullptr)
        return EXIT_FAILURE;

    int result = EXIT_FAILURE;
    int storage[8];
    {
        queue_type queue;
        if (queue.init(state, queue_type::state_size(), storage, sizeof(storage)) == FLUX_OK) {
            int received;
            if (queue.try_push(42) && queue.try_pop(received)) {
                std::printf("Received %d; capacity %zu\n", received, queue.capacity());
                result = EXIT_SUCCESS;
            }
        }
    }
    std::free(state);
    return result;
}
