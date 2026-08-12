/**
 * @file gif_mem.h
 * @brief Private allocation facade shared by the decoder and retained giflib.
 *
 * Copyright (c) 2026 Steven Zhu
 * SPDX-License-Identifier: MIT
 */

#ifndef GIF_MEM_H
#define GIF_MEM_H

#include <stddef.h>

void *gif_mem_malloc(size_t size);
void *gif_mem_calloc(size_t count, size_t size);
void *gif_mem_realloc(void *pointer, size_t new_size);
void *gif_mem_realloc_array(void *pointer, size_t count, size_t element_size);
void gif_mem_free(void *pointer);

#endif /* GIF_MEM_H */
