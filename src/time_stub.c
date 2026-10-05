#include "flux/time.h"

#include <stddef.h>

flux_result_t flux_time_now_ns(uint64_t *out_ns)
{
    return out_ns == NULL ? FLUX_INVALID_ARGUMENT : FLUX_NOT_SUPPORTED;
}
