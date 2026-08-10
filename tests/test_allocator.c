#include <stddef.h>

#define MAX_TRACKED_ALLOCATIONS 8192

static void *tracked_allocations[MAX_TRACKED_ALLOCATIONS];
static size_t tracked_count;

void *__real_malloc(size_t size);
void *__real_calloc(size_t count, size_t size);
void *__real_realloc(void *pointer, size_t size);
void __real_free(void *pointer);

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

void *__wrap_malloc(size_t size) {
	void *pointer = __real_malloc(size);
	track_pointer(pointer);
	return pointer;
}

void *__wrap_calloc(size_t count, size_t size) {
	void *pointer = __real_calloc(count, size);
	track_pointer(pointer);
	return pointer;
}

void *__wrap_realloc(void *pointer, size_t size) {
	void *replacement = __real_realloc(pointer, size);

	if (replacement != NULL) {
		untrack_pointer(pointer);
		track_pointer(replacement);
	}

	return replacement;
}

void __wrap_free(void *pointer) {
	untrack_pointer(pointer);
	__real_free(pointer);
}

size_t giflib_test_outstanding_allocations(void) {
	return tracked_count;
}
