/**
 * @file test_allocator.c
 * @brief GNU linker-wrap allocator tracking and fault injection for tests.
 *
 * Copyright (c) 2026 Steven Zhu
 * SPDX-License-Identifier: MIT
 */

#include <stddef.h>

/** @brief Maximum simultaneously tracked allocations in a test process. */
#define MAX_TRACKED_ALLOCATIONS 8192

/** @brief Active allocation pointers observed through linker wrappers. */
static void *tracked_allocations[MAX_TRACKED_ALLOCATIONS];
/** @brief Number of entries currently stored in `tracked_allocations`. */
static size_t tracked_count;
/** @brief Successful allocations allowed before an injected failure. */
static size_t allocations_before_failure;
/** @brief Non-zero while deterministic allocation failure is enabled. */
static int allocation_failure_enabled;

/** @name Real allocation functions supplied by the GNU linker wrapper. */
/** @{ */
void *__real_malloc(size_t size);
void *__real_calloc(size_t count, size_t size);
void *__real_realloc(void *pointer, size_t size);
void __real_free(void *pointer);
/** @} */

/**
 * @brief Advance the failure-injection counter for one allocation attempt.
 *
 * @return Non-zero when the current allocation must fail, otherwise zero.
 */
static int should_fail_allocation(void) {
	if (!allocation_failure_enabled) {
		return 0;
	}
	if (allocations_before_failure == 0) {
		return 1;
	}
	allocations_before_failure--;
	return 0;
}

/**
 * @brief Add a non-null allocation to the active-pointer table.
 *
 * @param[in] pointer Allocation returned by the real allocator.
 */
static void track_pointer(void *pointer) {
	size_t index;

	if (pointer == NULL) {
		return;
	}

	for (index = 0; index < tracked_count; index++) {
		if (tracked_allocations[index] == pointer) {
			return;
		}
	}

	if (tracked_count < MAX_TRACKED_ALLOCATIONS) {
		tracked_allocations[tracked_count++] = pointer;
	}
}

/**
 * @brief Remove a released allocation from the active-pointer table.
 *
 * @param[in] pointer Allocation passed to `free()` or replaced by `realloc()`.
 */
static void untrack_pointer(void *pointer) {
	size_t index;

	if (pointer == NULL) {
		return;
	}

	for (index = 0; index < tracked_count; index++) {
		if (tracked_allocations[index] == pointer) {
			tracked_count--;
			tracked_allocations[index] =
			    tracked_allocations[tracked_count];
			tracked_allocations[tracked_count] = NULL;
			return;
		}
	}
}

/**
 * @brief Wrapped `malloc()` with deterministic failure and leak tracking.
 *
 * @param[in] size Requested allocation size.
 * @return Allocated pointer or `NULL`.
 */
void *__wrap_malloc(size_t size) {
	if (should_fail_allocation()) {
		return NULL;
	}
	void *pointer = __real_malloc(size);
	track_pointer(pointer);
	return pointer;
}

/**
 * @brief Wrapped `calloc()` with deterministic failure and leak tracking.
 *
 * @param[in] count Number of elements.
 * @param[in] size  Size of each element.
 * @return Zero-initialized allocation or `NULL`.
 */
void *__wrap_calloc(size_t count, size_t size) {
	if (should_fail_allocation()) {
		return NULL;
	}
	void *pointer = __real_calloc(count, size);
	track_pointer(pointer);
	return pointer;
}

/**
 * @brief Wrapped `realloc()` that preserves tracking on failed replacement.
 *
 * @param[in] pointer Existing allocation or `NULL`.
 * @param[in] size    Requested replacement size.
 * @return Replacement pointer or `NULL`.
 */
void *__wrap_realloc(void *pointer, size_t size) {
	if (should_fail_allocation()) {
		return NULL;
	}
	void *replacement = __real_realloc(pointer, size);

	if (replacement != NULL) {
		untrack_pointer(pointer);
		track_pointer(replacement);
	}

	return replacement;
}

/**
 * @brief Wrapped `free()` that removes the pointer from leak tracking.
 *
 * @param[in] pointer Allocation to release, or `NULL`.
 */
void __wrap_free(void *pointer) {
	untrack_pointer(pointer);
	__real_free(pointer);
}

/**
 * @brief Report the number of allocations not yet released.
 *
 * @return Current active allocation count.
 */
size_t giflib_test_outstanding_allocations(void) {
	return tracked_count;
}

/**
 * @brief Enable failure after a selected number of successful allocations.
 *
 * @param[in] successful_allocations Number of successes allowed before failure.
 */
void giflib_test_fail_allocation_after(size_t successful_allocations) {
	allocations_before_failure = successful_allocations;
	allocation_failure_enabled = 1;
}

/** @brief Disable deterministic allocation failure injection. */
void giflib_test_disable_allocation_failure(void) {
	allocation_failure_enabled = 0;
	allocations_before_failure = 0;
}
