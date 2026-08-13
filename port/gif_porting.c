/**
 * @file gif_porting.c
 * @brief Single user-editable platform integration point for GIF input.
 *
 * This template deliberately has no stdio, filesystem, RTOS, or device-driver
 * dependency. A platform port replaces the three function bodies below and
 * adds any required platform includes in this file only. Do not add storage
 * glue to `main()`, `gif_decoder.c`, or the hidden decoder core.
 *
 * Copyright (c) 2026 Steven Zhu
 * SPDX-License-Identifier: MIT
 */

#include "gif_porting.h"
#include "gif_porting_memory.h"

/**
 * @brief Open the application-selected source on the target platform.
 *
 * Porting checklist:
 * - interpret `source_identifier` without exposing platform types elsewhere;
 * - use gif_porting_mem_alloc() to create one dynamic per-stream handle;
 *   return GIF_PORTING_OUT_OF_MEMORY if that allocation fails;
 * - set `*out_handle` only after the source is ready for sequential reads;
 * - map every platform failure to `GIF_PORTING_IO_ERROR`.
 *
 * @copydetails gif_porting_open
 */
GifPortingStatus gif_porting_open(const void *source_identifier,
                                  GifPortingHandle *out_handle) {
    if (out_handle != NULL) {
        *out_handle = NULL;
    }

    (void)source_identifier;

    /* TODO(port): Open the selected source and return its non-NULL handle. */
    return GIF_PORTING_IO_ERROR;
}

/**
 * @brief Read forward-only bytes using the target platform's storage API.
 *
 * Porting checklist:
 * - clear `*actual_bytes` before attempting the read;
 * - report the platform's exact byte count, including legal short reads;
 * - return `GIF_PORTING_EOF` only when no future byte can be produced;
 * - never seek, rewind, inspect file size, or access decoder internals.
 *
 * @copydetails gif_porting_read
 */
GifPortingStatus gif_porting_read(GifPortingHandle handle,
                                  uint8_t *destination,
                                  size_t requested_bytes,
                                  size_t *actual_bytes) {
    if (actual_bytes != NULL) {
        *actual_bytes = 0;
    }

    (void)handle;
    (void)destination;
    (void)requested_bytes;

    /* TODO(port): Replace this stub with the platform sequential read. */
    return GIF_PORTING_IO_ERROR;
}

/**
 * @brief Close the target source and release only port-owned resources.
 *
 * The decoder calls this function exactly once for every successfully opened
 * source transferred to it. Calling it with `NULL` must remain harmless.
 * Every dynamic handle allocated with gif_porting_mem_alloc() must be
 * released here after its platform source is closed.
 *
 * @copydetails gif_porting_close
 */
void gif_porting_close(GifPortingHandle handle) {
    (void)handle;

    /* TODO(port): Close the platform source and release its dynamic handle. */
}
