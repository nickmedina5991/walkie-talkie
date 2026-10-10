// Minimal test helpers so the host tests need nothing but a C compiler.
#pragma once
#include <stdio.h>
#include <stdlib.h>

extern int g_failures;
extern int g_checks;

#define CHECK(cond)                                                                 \
    do {                                                                            \
        g_checks++;                                                                 \
        if (!(cond)) {                                                              \
            g_failures++;                                                           \
            printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                \
        }                                                                           \
    } while (0)

#define CHECK_EQ(a, b)                                                              \
    do {                                                                            \
        g_checks++;                                                                 \
        long long _a = (long long)(a), _b = (long long)(b);                         \
        if (_a != _b) {                                                             \
            g_failures++;                                                           \
            printf("  FAIL %s:%d: %s == %s (%lld vs %lld)\n", __FILE__, __LINE__, #a, \
                   #b, _a, _b);                                                     \
        }                                                                           \
    } while (0)

#define RUN(test)                \
    do {                         \
        printf("%s\n", #test);   \
        test();                  \
    } while (0)
