/**
 * @file gif_mem_builtin.c
 * @brief Single fixed-pool TLSF backend for giflib-embedded.
 *
 * Copyright (c) 2026 Steven Zhu
 * SPDX-License-Identifier: MIT
 *
 * This backend never allocates from or falls back to a C library heap. It is
 * intentionally single-pool and not internally synchronized; applications
 * must serialize simultaneous decoder activity when this backend is selected.
 */

#include "gif_mem_builtin.h"

#include "gif_config.h"
#include "gif_tlsf.h"

#include <stdint.h>

#if defined(__GNUC__) || defined(__clang__)
#define GIF_MEM_POOL_ALIGN(bytes) __attribute__((aligned(bytes)))
#define GIF_MEM_POOL_DECLARE(bytes, name) \
    static unsigned char name[GIF_MEM_POOL_SIZE] GIF_MEM_POOL_ALIGN(bytes)
#elif defined(_MSC_VER)
#define GIF_MEM_POOL_ALIGN(bytes) __declspec(align(bytes))
#define GIF_MEM_POOL_DECLARE(bytes, name) \
    GIF_MEM_POOL_ALIGN(bytes) static unsigned char name[GIF_MEM_POOL_SIZE]
#else
#define GIF_MEM_POOL_ALIGN(bytes)
#define GIF_MEM_POOL_DECLARE(bytes, name) \
    static unsigned char name[GIF_MEM_POOL_SIZE]
#endif

/** @brief State of the one-time fixed-pool initialization attempt. */
typedef enum GifMemBuiltinState {
    GIF_MEM_BUILTIN_UNINITIALIZED = 0,
    GIF_MEM_BUILTIN_READY,
    GIF_MEM_BUILTIN_FAILED
} GifMemBuiltinState;

/** @brief Static storage whose address is explicitly aligned for TLSF. */
GIF_MEM_POOL_DECLARE(GIF_MEM_POOL_ALIGNMENT, gif_mem_builtin_pool);

static gif_tlsf_t gif_mem_builtin_tlsf;
static GifMemBuiltinState gif_mem_builtin_state =
    GIF_MEM_BUILTIN_UNINITIALIZED;

/**
 * @brief Construct TLSF over the fixed pool once, recording a safe failure.
 *
 * @return Non-zero only when the backend is ready for allocations.
 */
static int gif_mem_builtin_initialize(void) {
    size_t required_size;

    if (gif_mem_builtin_state != GIF_MEM_BUILTIN_UNINITIALIZED) {
        return gif_mem_builtin_state == GIF_MEM_BUILTIN_READY;
    }

    required_size = gif_tlsf_size() + gif_tlsf_pool_overhead() +
                    gif_tlsf_block_size_min();
    if (GIF_MEM_POOL_ALIGNMENT < gif_tlsf_align_size() ||
        (uintptr_t)gif_mem_builtin_pool % gif_tlsf_align_size() != 0U ||
        GIF_MEM_POOL_SIZE < required_size) {
        gif_mem_builtin_state = GIF_MEM_BUILTIN_FAILED;
        return 0;
    }

    gif_mem_builtin_tlsf = gif_tlsf_create(gif_mem_builtin_pool);
    if (gif_mem_builtin_tlsf == NULL ||
        gif_tlsf_add_pool(gif_mem_builtin_tlsf,
                          gif_mem_builtin_pool + gif_tlsf_size(),
                          GIF_MEM_POOL_SIZE - gif_tlsf_size()) == NULL) {
        gif_mem_builtin_tlsf = NULL;
        gif_mem_builtin_state = GIF_MEM_BUILTIN_FAILED;
        return 0;
    }

    gif_mem_builtin_state = GIF_MEM_BUILTIN_READY;
    return 1;
}

/** @brief Allocate a non-zero block from the fixed TLSF pool. */
void *gif_mem_builtin_malloc(size_t size) {
    if (!gif_mem_builtin_initialize()) {
        return NULL;
    }
    return gif_tlsf_malloc(gif_mem_builtin_tlsf, size);
}

/** @brief Resize a non-zero fixed-pool allocation without a libc fallback. */
void *gif_mem_builtin_realloc(void *pointer, size_t new_size) {
    if (!gif_mem_builtin_initialize()) {
        return NULL;
    }
    return gif_tlsf_realloc(gif_mem_builtin_tlsf, pointer, new_size);
}

/** @brief Release a fixed-pool allocation. */
void gif_mem_builtin_free(void *pointer) {
    if (pointer != NULL && gif_mem_builtin_initialize()) {
        (void)gif_tlsf_free(gif_mem_builtin_tlsf, pointer);
    }
}

/**
 * @brief Check fixed-pool consistency for internal diagnostics and tests.
 *
 * @return Zero when TLSF reports a consistent pool, otherwise non-zero.
 */
int gif_mem_builtin_check_integrity(void) {
    if (!gif_mem_builtin_initialize()) {
        return 1;
    }
    return gif_tlsf_check(gif_mem_builtin_tlsf);
}
