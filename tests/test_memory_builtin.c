/**
 * @file test_memory_builtin.c
 * @brief Fixed-pool stress tests for the BUILTIN allocator backend.
 *
 * Copyright (c) 2026 Steven Zhu
 * SPDX-License-Identifier: MIT
 */

#include "gif_mem.h"
#include "gif_mem_builtin.h"

#include <stdio.h>
#include <string.h>

static int failures;

#define CHECK(condition)                                                       \
    do {                                                                       \
        if (!(condition)) {                                                    \
            fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__,          \
                    __LINE__, #condition);                                    \
            failures++;                                                        \
        }                                                                      \
    } while (0)

/** @brief Fragment, coalesce, and exhaust the actual single TLSF pool. */
static void test_fixed_pool_stress(void) {
    void *blocks[256] = {0};
    size_t count = 0U;
    size_t index;

    while (count < 256U) {
        blocks[count] = gif_mem_malloc(128U + (count % 7U) * 31U);
        if (blocks[count] == NULL) {
            break;
        }
        count++;
    }
    CHECK(count > 0U);
    CHECK(gif_mem_builtin_check_integrity() == 0);

    for (index = 0U; index < count; index += 2U) {
        gif_mem_free(blocks[index]);
        blocks[index] = NULL;
    }
    CHECK(gif_mem_builtin_check_integrity() == 0);

    for (index = 0U; index < count; index += 2U) {
        blocks[index] = gif_mem_malloc(96U);
        CHECK(blocks[index] != NULL);
    }
    CHECK(gif_mem_builtin_check_integrity() == 0);

    for (index = 0U; index < count; index++) {
        gif_mem_free(blocks[index]);
    }
    CHECK(gif_mem_builtin_check_integrity() == 0);
}

/** @brief Exercise in-place and moved realloc paths under pool pressure. */
static void test_realloc_stress(void) {
    unsigned char *pointer = gif_mem_malloc(128U);
    unsigned char *replacement;

    CHECK(pointer != NULL);
    if (pointer == NULL) {
        return;
    }
    memset(pointer, 0x3c, 128U);
    replacement = gif_mem_realloc(pointer, 2048U);
    CHECK(replacement != NULL);
    if (replacement != NULL) {
        CHECK(replacement[0] == 0x3cU);
        CHECK(replacement[127] == 0x3cU);
        pointer = replacement;
    }
    replacement = gif_mem_realloc(pointer, 64U);
    CHECK(replacement != NULL);
    if (replacement != NULL) {
        pointer = replacement;
    }
    gif_mem_free(pointer);
    CHECK(gif_mem_builtin_check_integrity() == 0);
}

/** @brief Verify a failed fixed-pool resize preserves the original block. */
static void test_realloc_failure_preserves_original(void) {
    unsigned char *pointer = gif_mem_malloc(32U);
    void *replacement;

    CHECK(pointer != NULL);
    if (pointer == NULL) {
        return;
    }
    pointer[0] = 0x7eU;
    replacement = gif_mem_realloc(pointer, (size_t)-1);
    CHECK(replacement == NULL);
    CHECK(pointer[0] == 0x7eU);
    gif_mem_free(pointer);
    CHECK(gif_mem_builtin_check_integrity() == 0);
}

int main(void) {
    test_fixed_pool_stress();
    test_realloc_stress();
    test_realloc_failure_preserves_original();

    if (failures != 0) {
        fprintf(stderr, "%d builtin memory check(s) failed\n", failures);
        return 1;
    }
    return 0;
}
