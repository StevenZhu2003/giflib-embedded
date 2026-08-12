/**
 * @file gif_mem_lvgl.h
 * @brief LVGL allocator primitives for the private LVGL memory backend.
 *
 * Copyright (c) 2026 Steven Zhu
 * SPDX-License-Identifier: MIT
 */

#ifndef GIF_MEM_LVGL_H
#define GIF_MEM_LVGL_H

#include <stddef.h>

void *gif_mem_lvgl_malloc(size_t size);
void *gif_mem_lvgl_realloc(void *pointer, size_t new_size);
void gif_mem_lvgl_free(void *pointer);

#endif /* GIF_MEM_LVGL_H */
