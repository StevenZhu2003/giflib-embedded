/**
 * @file gif_porting_memory.h
 * @brief Port-only allocation bridge for dynamic byte-source handles.
 *
 * Include this header only from `gif_porting.c` or files owned by that port.
 * It is deliberately outside the installed public API and is not an
 * application memory service.
 *
 * Copyright (c) 2026 Steven Zhu
 * SPDX-License-Identifier: MIT
 */

#ifndef GIF_PORTING_MEMORY_H
#define GIF_PORTING_MEMORY_H

#include "gif_mem.h"

#include <stddef.h>

/**
 * @brief Allocate a non-zero port-owned object through the selected GIF backend.
 *
 * The returned storage shares the decoder's allocator domain. A port must
 * release it with gif_porting_mem_free() from gif_porting_close(). In BUILTIN
 * mode, its live size contributes to GIF_MEM_POOL_SIZE.
 *
 * @param[in] size Number of bytes to allocate; zero returns `NULL`.
 * @return A port-owned allocation, or `NULL` when the selected backend is full.
 */
static inline void *gif_porting_mem_alloc(size_t size) {
    return gif_mem_malloc(size);
}

/**
 * @brief Release one allocation created by gif_porting_mem_alloc().
 *
 * @param[in] pointer Port-owned allocation, or `NULL`.
 */
static inline void gif_porting_mem_free(void *pointer) {
    gif_mem_free(pointer);
}

#endif /* GIF_PORTING_MEMORY_H */
