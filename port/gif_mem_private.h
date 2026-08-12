/**
 * @file gif_mem_private.h
 * @brief Application-owned primitives for the PRIVATE memory backend.
 *
 * Copyright (c) 2026 Steven Zhu
 * SPDX-License-Identifier: MIT
 */

#ifndef GIF_MEM_PRIVATE_H
#define GIF_MEM_PRIVATE_H

#include <stddef.h>

/**
 * @brief Allocate a non-zero number of bytes from the application's allocator.
 *
 * The implementation is selected only when `GIF_MEM_BACKEND` is
 * `GIF_MEM_USE_PRIVATE`. It must return `NULL` on failure and must share an
 * allocation domain with gif_mem_private_realloc() and gif_mem_private_free().
 */
void *gif_mem_private_malloc(size_t size);

/**
 * @brief Resize a non-zero allocation from the application's allocator.
 *
 * On failure, the original allocation must remain valid and unchanged.
 * Thread safety is the responsibility of the application implementation.
 */
void *gif_mem_private_realloc(void *pointer, size_t new_size);

/**
 * @brief Release an allocation created by the application's allocator.
 */
void gif_mem_private_free(void *pointer);

#endif /* GIF_MEM_PRIVATE_H */
