/**
 * @file gif_mem_libc.c
 * @brief C library heap primitives for the private LIBC memory backend.
 *
 * Copyright (c) 2026 Steven Zhu
 * SPDX-License-Identifier: MIT
 */

#include "gif_mem_libc.h"

#include <stdlib.h>

/** @brief Allocate a non-zero byte count from the C library heap. */
void *gif_mem_libc_malloc(size_t size) {
    return malloc(size);
}

/** @brief Resize a non-zero allocation through the C library heap. */
void *gif_mem_libc_realloc(void *pointer, size_t new_size) {
    return realloc(pointer, new_size);
}

/** @brief Release a C library heap allocation. */
void gif_mem_libc_free(void *pointer) {
    free(pointer);
}
