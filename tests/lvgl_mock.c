/**
 * @file lvgl_mock.c
 * @brief Deterministic implementation of the public LVGL allocator test API.
 *
 * Copyright (c) 2026 Steven Zhu
 * SPDX-License-Identifier: MIT
 */

#include "lvgl.h"

#include <stdlib.h>

/** @brief Maximum simultaneously tracked allocations in a test process. */
#define LVGL_MOCK_MAX_TRACKED_ALLOCATIONS 8192U

/** @brief Active allocation pointers returned through the mocked LVGL API. */
static void *lvgl_mock_tracked[LVGL_MOCK_MAX_TRACKED_ALLOCATIONS];
/** @brief Number of active entries in lvgl_mock_tracked. */
static size_t lvgl_mock_tracked_count;
/** @brief Successful allocations permitted before deterministic failure. */
static size_t lvgl_mock_allocations_before_failure;
/** @brief Non-zero while deterministic allocation failure is enabled. */
static int lvgl_mock_failure_enabled;

/** @brief Determine whether the current allocation operation must fail. */
static int lvgl_mock_should_fail(void) {
    if (!lvgl_mock_failure_enabled) {
        return 0;
    }
    if (lvgl_mock_allocations_before_failure == 0U) {
        return 1;
    }
    lvgl_mock_allocations_before_failure--;
    return 0;
}

/** @brief Track an active allocation pointer. */
static void lvgl_mock_track(void *pointer) {
    size_t index;

    if (pointer == NULL) {
        return;
    }
    for (index = 0U; index < lvgl_mock_tracked_count; index++) {
        if (lvgl_mock_tracked[index] == pointer) {
            return;
        }
    }
    if (lvgl_mock_tracked_count < LVGL_MOCK_MAX_TRACKED_ALLOCATIONS) {
        lvgl_mock_tracked[lvgl_mock_tracked_count++] = pointer;
    }
}

/** @brief Remove one active allocation pointer from the tracking set. */
static void lvgl_mock_untrack(void *pointer) {
    size_t index;

    if (pointer == NULL) {
        return;
    }
    for (index = 0U; index < lvgl_mock_tracked_count; index++) {
        if (lvgl_mock_tracked[index] == pointer) {
            lvgl_mock_tracked_count--;
            lvgl_mock_tracked[index] =
                lvgl_mock_tracked[lvgl_mock_tracked_count];
            lvgl_mock_tracked[lvgl_mock_tracked_count] = NULL;
            return;
        }
    }
}

/** @brief Implement a public LVGL allocation operation for tests. */
static void *lvgl_mock_malloc(size_t size) {
    void *pointer;

    if (lvgl_mock_should_fail()) {
        return NULL;
    }
    pointer = malloc(size);
    lvgl_mock_track(pointer);
    return pointer;
}

/** @brief Implement a public LVGL resize operation for tests. */
static void *lvgl_mock_realloc(void *pointer, size_t new_size) {
    void *replacement;

    if (lvgl_mock_should_fail()) {
        return NULL;
    }
    replacement = realloc(pointer, new_size);
    if (replacement != NULL) {
        lvgl_mock_untrack(pointer);
        lvgl_mock_track(replacement);
    }
    return replacement;
}

/** @brief Implement a public LVGL free operation for tests. */
static void lvgl_mock_free(void *pointer) {
    lvgl_mock_untrack(pointer);
    free(pointer);
}

void *lv_mem_alloc(size_t size) {
    return lvgl_mock_malloc(size);
}

void *lv_mem_realloc(void *pointer, size_t new_size) {
    return lvgl_mock_realloc(pointer, new_size);
}

void lv_mem_free(void *pointer) {
    lvgl_mock_free(pointer);
}

void *lv_malloc(size_t size) {
    return lvgl_mock_malloc(size);
}

void *lv_realloc(void *pointer, size_t new_size) {
    return lvgl_mock_realloc(pointer, new_size);
}

void lv_free(void *pointer) {
    lvgl_mock_free(pointer);
}

size_t giflib_test_outstanding_allocations(void) {
    return lvgl_mock_tracked_count;
}

void giflib_test_fail_allocation_after(size_t successful_allocations) {
    lvgl_mock_allocations_before_failure = successful_allocations;
    lvgl_mock_failure_enabled = 1;
}

void giflib_test_disable_allocation_failure(void) {
    lvgl_mock_failure_enabled = 0;
    lvgl_mock_allocations_before_failure = 0U;
}
