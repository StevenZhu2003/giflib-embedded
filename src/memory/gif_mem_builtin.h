/**
 * @file gif_mem_builtin.h
 * @brief Primitive interface of the library-owned fixed-pool backend.
 *
 * Copyright (c) 2026 Steven Zhu
 * SPDX-License-Identifier: MIT
 */

#ifndef GIF_MEM_BUILTIN_H
#define GIF_MEM_BUILTIN_H

#include <stddef.h>

void *gif_mem_builtin_malloc(size_t size);
void *gif_mem_builtin_realloc(void *pointer, size_t new_size);
void gif_mem_builtin_free(void *pointer);
int gif_mem_builtin_check_integrity(void);

#endif /* GIF_MEM_BUILTIN_H */
