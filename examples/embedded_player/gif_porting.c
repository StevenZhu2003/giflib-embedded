/**
 * @file gif_porting.c
 * @brief Memory-backed byte-source port for the embedded player example.
 *
 * This implementation supports one active decoder and performs only forward
 * sequential reads. It is independent of filesystems and platform SDKs.
 *
 * Copyright (c) 2026 Steven Zhu
 * SPDX-License-Identifier: MIT
 */

#include "gif_porting.h"

#include "memory_source.h"

#include <string.h>

/** @brief Mutable cursor for the example's single active memory stream. */
typedef struct GifMemoryHandle {
    const uint8_t *next; /**< Address of the next unread byte. */
    size_t remaining;    /**< Number of bytes remaining in the stream. */
    int active;          /**< Non-zero while a decoder owns the handle. */
} GifMemoryHandle;

/** @brief Storage for the example's one supported active decoder. */
static GifMemoryHandle gif_memory_handle;

/** @copydoc gif_porting_open */
GifPortingStatus gif_porting_open(const void *source_identifier,
                                  GifPortingHandle *out_handle) {
    const GifMemorySource *source =
        (const GifMemorySource *)source_identifier;

    if (out_handle == NULL) {
        return GIF_PORTING_IO_ERROR;
    }
    *out_handle = NULL;

    if (source == NULL || source->data == NULL || gif_memory_handle.active) {
        return GIF_PORTING_IO_ERROR;
    }

    gif_memory_handle.next = source->data;
    gif_memory_handle.remaining = source->size;
    gif_memory_handle.active = 1;
    *out_handle = &gif_memory_handle;
    return GIF_PORTING_OK;
}

/** @copydoc gif_porting_read */
GifPortingStatus gif_porting_read(GifPortingHandle handle,
                                  uint8_t *destination,
                                  size_t requested_bytes,
                                  size_t *actual_bytes) {
    GifMemoryHandle *memory = (GifMemoryHandle *)handle;
    size_t count;

    if (actual_bytes == NULL) {
        return GIF_PORTING_IO_ERROR;
    }
    *actual_bytes = 0;

    if (memory != &gif_memory_handle || !memory->active ||
        destination == NULL || requested_bytes == 0) {
        return GIF_PORTING_IO_ERROR;
    }

    count = requested_bytes < memory->remaining
                ? requested_bytes
                : memory->remaining;
    if (count != 0) {
        memcpy(destination, memory->next, count);
        memory->next += count;
        memory->remaining -= count;
        *actual_bytes = count;
    }

    return memory->remaining == 0 ? GIF_PORTING_EOF : GIF_PORTING_OK;
}

/** @copydoc gif_porting_close */
void gif_porting_close(GifPortingHandle handle) {
    GifMemoryHandle *memory = (GifMemoryHandle *)handle;

    if (memory == &gif_memory_handle) {
        memory->next = NULL;
        memory->remaining = 0;
        memory->active = 0;
    }
}
