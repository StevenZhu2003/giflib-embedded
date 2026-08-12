/**
 * @file gif_mem.c
 * @brief Backend-independent allocation semantics for giflib-embedded.
 *
 * Copyright (c) 2026 Steven Zhu
 * SPDX-License-Identifier: MIT
 */

#include "gif_mem.h"

#include "gif_config.h"

#include <stdint.h>
#include <string.h>

#if GIF_MEM_BACKEND == GIF_MEM_USE_BUILTIN
#include "gif_mem_builtin.h"
#elif GIF_MEM_BACKEND == GIF_MEM_USE_PRIVATE
#include "gif_mem_private.h"
#elif GIF_MEM_BACKEND == GIF_MEM_USE_LIBC
#include "gif_mem_libc.h"
#endif

/**
 * @brief Multiply two allocation dimensions without overflowing size_t.
 *
 * @param[in] count        Element count.
 * @param[in] element_size Bytes per element.
 * @param[out] total_size  Result when multiplication is valid.
 * @return Non-zero when the multiplication is valid.
 */
static int gif_mem_multiply(size_t count, size_t element_size,
                            size_t *total_size) {
    if (count == 0U || element_size == 0U ||
        count > SIZE_MAX / element_size) {
        return 0;
    }

    *total_size = count * element_size;
    return 1;
}

/** @brief Allocate a non-zero byte count through the selected backend. */
void *gif_mem_malloc(size_t size) {
    if (size == 0U) {
        return NULL;
    }

#if GIF_MEM_BACKEND == GIF_MEM_USE_BUILTIN
    return gif_mem_builtin_malloc(size);
#elif GIF_MEM_BACKEND == GIF_MEM_USE_PRIVATE
    return gif_mem_private_malloc(size);
#else
    return gif_mem_libc_malloc(size);
#endif
}

/** @brief Allocate and clear a checked, non-zero element array. */
void *gif_mem_calloc(size_t count, size_t size) {
    size_t total_size;
    void *pointer;

    if (!gif_mem_multiply(count, size, &total_size)) {
        return NULL;
    }

    pointer = gif_mem_malloc(total_size);
    if (pointer != NULL) {
        memset(pointer, 0, total_size);
    }
    return pointer;
}

/** @brief Resize an allocation while preserving portable zero-size semantics. */
void *gif_mem_realloc(void *pointer, size_t new_size) {
    if (pointer == NULL) {
        return gif_mem_malloc(new_size);
    }
    if (new_size == 0U) {
        gif_mem_free(pointer);
        return NULL;
    }

#if GIF_MEM_BACKEND == GIF_MEM_USE_BUILTIN
    return gif_mem_builtin_realloc(pointer, new_size);
#elif GIF_MEM_BACKEND == GIF_MEM_USE_PRIVATE
    return gif_mem_private_realloc(pointer, new_size);
#else
    return gif_mem_libc_realloc(pointer, new_size);
#endif
}

/** @brief Resize an array after an overflow-safe size calculation. */
void *gif_mem_realloc_array(void *pointer, size_t count, size_t element_size) {
    size_t total_size;

    if (!gif_mem_multiply(count, element_size, &total_size)) {
        /* Preserve giflib's retained OpenBSD reallocarray zero-size behavior:
         * return NULL without changing a non-null original allocation. */
        return NULL;
    }
    return gif_mem_realloc(pointer, total_size);
}

/** @brief Release an allocation; a null pointer is ignored. */
void gif_mem_free(void *pointer) {
    if (pointer == NULL) {
        return;
    }

#if GIF_MEM_BACKEND == GIF_MEM_USE_BUILTIN
    gif_mem_builtin_free(pointer);
#elif GIF_MEM_BACKEND == GIF_MEM_USE_PRIVATE
    gif_mem_private_free(pointer);
#else
    gif_mem_libc_free(pointer);
#endif
}
