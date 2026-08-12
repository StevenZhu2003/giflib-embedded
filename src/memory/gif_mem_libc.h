/**
 * @file gif_mem_libc.h
 * @brief C library heap primitives for the private LIBC memory backend.
 *
 * Copyright (c) 2026 Steven Zhu
 * SPDX-License-Identifier: MIT
 */

#ifndef GIF_MEM_LIBC_H
#define GIF_MEM_LIBC_H

#include <stddef.h>

void *gif_mem_libc_malloc(size_t size);
void *gif_mem_libc_realloc(void *pointer, size_t new_size);
void gif_mem_libc_free(void *pointer);

#endif /* GIF_MEM_LIBC_H */
