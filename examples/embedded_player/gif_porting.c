/**
 * @file gif_porting.c
 * @brief Memory-backed byte-source port for the embedded player example.
 *
 * Every open decoder receives a dynamically allocated cursor through the
 * port-only memory bridge. The implementation performs only forward
 * sequential reads and is independent of filesystems and platform SDKs.
 *
 * Copyright (c) 2026 Steven Zhu
 * SPDX-License-Identifier: MIT
 */

#include "gif_porting.h"
#include "gif_porting_memory.h"

#include "memory_source.h"

#include <string.h>

/** @brief Mutable cursor for one independently open example memory stream. */
typedef struct GifMemoryHandle {
    const uint8_t *next; /**< Address of the next unread byte. */
    size_t remaining;    /**< Number of bytes remaining in the stream. */
} GifMemoryHandle;

/** @copydoc gif_porting_open */
GifPortingStatus gif_porting_open(const void *source_identifier,
                                  GifPortingHandle *out_handle) {
    const GifMemorySource *source =
        (const GifMemorySource *)source_identifier;
    GifMemoryHandle *handle;

    if (out_handle == NULL) {
        return GIF_PORTING_IO_ERROR;
    }
    *out_handle = NULL;

    if (source == NULL || source->data == NULL) {
        return GIF_PORTING_IO_ERROR;
    }

    handle = (GifMemoryHandle *)gif_porting_mem_alloc(sizeof(*handle));
    if (handle == NULL) {
        return GIF_PORTING_OUT_OF_MEMORY;
    }

    handle->next = source->data;
    handle->remaining = source->size;
    *out_handle = handle;
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

    if (memory == NULL || destination == NULL || requested_bytes == 0) {
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

    if (memory != NULL) {
        memory->next = NULL;
        memory->remaining = 0;
        gif_porting_mem_free(memory);
    }
}
