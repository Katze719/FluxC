#ifndef FLUX_TEST_CHECK_H
#define FLUX_TEST_CHECK_H

#include <stdio.h>
#include <stdlib.h>

/* Keep checks active in Release builds, including expressions with side effects. */
#define CHECK(expression)                                                                          \
    do {                                                                                           \
        if (!(expression)) {                                                                       \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #expression);                       \
            exit(EXIT_FAILURE);                                                                    \
        }                                                                                          \
    } while (0)

#endif
