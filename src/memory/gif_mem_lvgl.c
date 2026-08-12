/**
 * @file gif_mem_lvgl.c
 * @brief Version-isolated bridge to LVGL's public allocator API.
 *
 * This backend never initializes or deinitializes LVGL. The application must
 * keep LVGL initialized, and must preserve the LVGL allocator domain, for the
 * entire lifetime of every active GIF decoder using this backend.
 *
 * Copyright (c) 2026 Steven Zhu
 * SPDX-License-Identifier: MIT
 */

#include "gif_mem_lvgl.h"

#include "lvgl.h"

#ifndef LVGL_VERSION_MAJOR
#error "giflib-embedded requires LVGL_VERSION_MAJOR from the public lvgl.h"
#endif

#if LVGL_VERSION_MAJOR == 8
#ifndef LVGL_VERSION_MINOR
#error "giflib-embedded requires LVGL_VERSION_MINOR for LVGL 8 validation"
#elif LVGL_VERSION_MINOR < 4
#error "giflib-embedded supports LVGL 8.4 or later, and LVGL 9.x"
#endif
#elif LVGL_VERSION_MAJOR < 9
#error "giflib-embedded supports LVGL 8.4 or later, and LVGL 9.x"
#endif

/** @brief Allocate a non-zero byte count through LVGL's public API. */
void *gif_mem_lvgl_malloc(size_t size) {
#if LVGL_VERSION_MAJOR >= 9
    return lv_malloc(size);
#else
    return lv_mem_alloc(size);
#endif
}

/** @brief Resize a non-zero allocation through LVGL's public API. */
void *gif_mem_lvgl_realloc(void *pointer, size_t new_size) {
#if LVGL_VERSION_MAJOR >= 9
    return lv_realloc(pointer, new_size);
#else
    return lv_mem_realloc(pointer, new_size);
#endif
}

/** @brief Release an allocation through LVGL's public API. */
void gif_mem_lvgl_free(void *pointer) {
#if LVGL_VERSION_MAJOR >= 9
    lv_free(pointer);
#else
    lv_mem_free(pointer);
#endif
}
