/**
 * @file lvgl.h
 * @brief Minimal public-LVGL allocator declaration set for backend tests.
 *
 * Copyright (c) 2026 Steven Zhu
 * SPDX-License-Identifier: MIT
 */

#ifndef LVGL_H
#define LVGL_H

#include <stddef.h>

#ifndef LVGL_VERSION_MAJOR
#define LVGL_VERSION_MAJOR 9
#endif

#ifndef LVGL_VERSION_MINOR
#define LVGL_VERSION_MINOR 0
#endif

void *lv_mem_alloc(size_t size);
void *lv_mem_realloc(void *pointer, size_t new_size);
void lv_mem_free(void *pointer);

void *lv_malloc(size_t size);
void *lv_realloc(void *pointer, size_t new_size);
void lv_free(void *pointer);

#endif /* LVGL_H */
