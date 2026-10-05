#include "flux/time.h"
#include "check.h"

int main(void)
{
    CHECK(flux_time_now_ns(NULL) == FLUX_INVALID_ARGUMENT);
    uint64_t first = 123;
#if FLUX_HAS_POSIX_TIME
    CHECK(flux_time_now_ns(&first) == FLUX_OK);
    for (size_t i = 0; i < 1000; ++i) {
        uint64_t next;
        CHECK(flux_time_now_ns(&next) == FLUX_OK);
        CHECK(next >= first);
        first = next;
    }
#else
    CHECK(flux_time_now_ns(&first) == FLUX_NOT_SUPPORTED);
    CHECK(first == 123);
#endif
    return 0;
}
