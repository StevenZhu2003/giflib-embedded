/**
 * @file test_memory.c
 * @brief Semantic tests for the backend-independent memory facade.
 *
 * Copyright (c) 2026 Steven Zhu
 * SPDX-License-Identifier: MIT
 */

#include "gif_mem.h"

#include <stdint.h>
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

#ifdef GIFLIB_TEST_ALLOC_TRACKING
size_t giflib_test_outstanding_allocations(void);
void giflib_test_fail_allocation_after(size_t successful_allocations);
void giflib_test_disable_allocation_failure(void);
#endif

/** @brief Verify zero-size requests never enter a provider allocation domain. */
static void test_zero_size_semantics(void) {
    void *pointer = gif_mem_malloc(8U);

    CHECK(gif_mem_malloc(0U) == NULL);
    CHECK(gif_mem_calloc(0U, 1U) == NULL);
    CHECK(gif_mem_calloc(1U, 0U) == NULL);
    CHECK(pointer != NULL);
    CHECK(gif_mem_realloc(pointer, 0U) == NULL);
#ifdef GIFLIB_TEST_ALLOC_TRACKING
    CHECK(giflib_test_outstanding_allocations() == 0U);
#endif
    CHECK(gif_mem_realloc_array(NULL, 0U, 1U) == NULL);
    CHECK(gif_mem_realloc_array(NULL, 1U, 0U) == NULL);
}

/** @brief Verify calloc clearing, multiplication overflow, and null free. */
static void test_calloc_and_free(void) {
    uint8_t *pointer = gif_mem_calloc(16U, sizeof(*pointer));
    size_t index;

    CHECK(pointer != NULL);
    if (pointer != NULL) {
        for (index = 0U; index < 16U; index++) {
            CHECK(pointer[index] == 0U);
        }
    }
    CHECK(gif_mem_calloc(SIZE_MAX, 2U) == NULL);
    gif_mem_free(NULL);
    gif_mem_free(pointer);
#ifdef GIFLIB_TEST_ALLOC_TRACKING
    CHECK(giflib_test_outstanding_allocations() == 0U);
#endif
}

/** @brief Verify realloc growth, shrink, and failure preservation. */
static void test_realloc_semantics(void) {
    uint8_t *pointer = gif_mem_realloc(NULL, 8U);
    uint8_t *replacement;

    CHECK(pointer != NULL);
    if (pointer == NULL) {
        return;
    }
    memset(pointer, 0x5a, 8U);
    replacement = gif_mem_realloc(pointer, 32U);
    CHECK(replacement != NULL);
    if (replacement != NULL) {
        CHECK(replacement[0] == 0x5aU);
        CHECK(replacement[7] == 0x5aU);
        pointer = replacement;
    }
    replacement = gif_mem_realloc(pointer, 4U);
    CHECK(replacement != NULL);
    if (replacement != NULL) {
        CHECK(replacement[0] == 0x5aU);
        pointer = replacement;
    }

#ifdef GIFLIB_TEST_ALLOC_TRACKING
    giflib_test_fail_allocation_after(0U);
    replacement = gif_mem_realloc(pointer, 64U);
    giflib_test_disable_allocation_failure();
    CHECK(replacement == NULL);
    CHECK(pointer[0] == 0x5aU);
#endif
    gif_mem_free(pointer);
#ifdef GIFLIB_TEST_ALLOC_TRACKING
    CHECK(giflib_test_outstanding_allocations() == 0U);
#endif
}

/** @brief Verify reallocarray overflow and zero-size release behavior. */
static void test_realloc_array_semantics(void) {
    uint8_t *pointer = gif_mem_malloc(8U);

    CHECK(pointer != NULL);
    if (pointer == NULL) {
        return;
    }
    memset(pointer, 0xa5, 8U);
    CHECK(gif_mem_realloc_array(pointer, SIZE_MAX, 2U) == NULL);
    CHECK(pointer[0] == 0xa5U);
    CHECK(gif_mem_realloc_array(pointer, 0U, 4U) == NULL);
    CHECK(pointer[0] == 0xa5U);
    gif_mem_free(pointer);
#ifdef GIFLIB_TEST_ALLOC_TRACKING
    CHECK(giflib_test_outstanding_allocations() == 0U);
#endif
}

int main(void) {
    test_zero_size_semantics();
    test_calloc_and_free();
    test_realloc_semantics();
    test_realloc_array_semantics();

    if (failures != 0) {
        fprintf(stderr, "%d memory semantic check(s) failed\n", failures);
        return 1;
    }
    return 0;
}
