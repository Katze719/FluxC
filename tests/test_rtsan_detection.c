#include <stdlib.h>

/* Deliberate violation: this executable must fail with an RTSan malloc report.
 * Keep the allocation observable even in optimized builds. No diagnostics are
 * suppressed; the CTest driver verifies both failure and the actual report. */
static __attribute__((nonblocking, noinline)) void *unsafe_allocate(void)
{
    void *block = malloc(32);
    if (block != NULL)
        *(volatile unsigned char *)block = 1;
    return block;
}

int main(void)
{
    void *block = unsafe_allocate();
    int result = block == NULL ? EXIT_FAILURE : EXIT_SUCCESS;
    free(block);
    return result;
}
