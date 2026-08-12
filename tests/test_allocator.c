/**
 * @file test_allocator.c
 * @brief PRIVATE-backend provider with deterministic failure and leak tracking.
 *
 * Copyright (c) 2026 Steven Zhu
 * SPDX-License-Identifier: MIT
 */

#include <stddef.h>
#include <stdlib.h>

/** @brief Maximum simultaneously tracked allocations in a test process. */
#define MAX_TRACKED_ALLOCATIONS 8192U

/** @brief Active allocation pointers returned by the test provider. */
static void *tracked_allocations[MAX_TRACKED_ALLOCATIONS];
/** @brief Number of entries currently stored in tracked_allocations. */
static size_t tracked_count;
/** @brief Successful allocations allowed before an injected failure. */
static size_t allocations_before_failure;
/** @brief Non-zero while deterministic allocation failure is enabled. */
static int allocation_failure_enabled;

/**
 * @brief Advance the failure-injection counter for one allocation attempt.
 *
 * @return Non-zero when the current allocation must fail, otherwise zero.
 */
static int should_fail_allocation(void) {
    if (!allocation_failure_enabled) {
        return 0;
    }
    if (allocations_before_failure == 0U) {
        return 1;
    }
    allocations_before_failure--;
    return 0;
}

/** @brief Record a returned allocation if it is not already tracked. */
static void track_pointer(void *pointer) {
    size_t index;

    if (pointer == NULL) {
        return;
    }
    for (index = 0U; index < tracked_count; index++) {
        if (tracked_allocations[index] == pointer) {
            return;
        }
    }
    if (tracked_count < MAX_TRACKED_ALLOCATIONS) {
        tracked_allocations[tracked_count++] = pointer;
    }
}

/** @brief Stop tracking an allocation before or after its provider release. */
static void untrack_pointer(void *pointer) {
    size_t index;

    if (pointer == NULL) {
        return;
    }
    for (index = 0U; index < tracked_count; index++) {
        if (tracked_allocations[index] == pointer) {
            tracked_count--;
            tracked_allocations[index] = tracked_allocations[tracked_count];
            tracked_allocations[tracked_count] = NULL;
            return;
        }
    }
}

/** @brief Implement the PRIVATE backend's non-zero allocation primitive. */
void *gif_mem_private_malloc(size_t size) {
    void *pointer;

    if (should_fail_allocation()) {
        return NULL;
    }
    pointer = malloc(size);
    track_pointer(pointer);
    return pointer;
}

/** @brief Implement the PRIVATE backend's non-zero resize primitive. */
void *gif_mem_private_realloc(void *pointer, size_t new_size) {
    void *replacement;

    if (should_fail_allocation()) {
        return NULL;
    }
    replacement = realloc(pointer, new_size);
    if (replacement != NULL) {
        untrack_pointer(pointer);
        track_pointer(replacement);
    }
    return replacement;
}

/** @brief Implement the PRIVATE backend's release primitive. */
void gif_mem_private_free(void *pointer) {
    untrack_pointer(pointer);
    free(pointer);
}

/** @brief Report the number of allocations not yet released. */
size_t giflib_test_outstanding_allocations(void) {
    return tracked_count;
}

/** @brief Enable failure after a selected number of successful allocations. */
void giflib_test_fail_allocation_after(size_t successful_allocations) {
    allocations_before_failure = successful_allocations;
    allocation_failure_enabled = 1;
}

/** @brief Disable deterministic allocation failure injection. */
void giflib_test_disable_allocation_failure(void) {
    allocation_failure_enabled = 0;
    allocations_before_failure = 0U;
}
