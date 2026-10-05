#ifndef FLUX_RING_INTERNAL_H
#define FLUX_RING_INTERNAL_H

#include <stdatomic.h>
#include "flux/ring.h"

/* Private layout, also used by the counter-rollover regression test. */
struct flux_ring {
    unsigned char *storage;
    size_t element_size;
    size_t capacity;
    size_t mask;
    atomic_size_t write_index;
    atomic_size_t read_index;
};

#endif
