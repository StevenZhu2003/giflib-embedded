/**
 * @file test_porting.c
 * @brief Host-only in-memory implementation of the stable porting contract.
 *
 * Copyright (c) 2026 Steven Zhu
 * SPDX-License-Identifier: MIT
 */

#include "gif_porting.h"
#include "gif_porting_memory.h"

#include "test_porting.h"

#include <string.h>

/** @brief Dynamically allocated port state for one test source. */
typedef struct TestPortingHandle {
    MemorySource *source; /**< Application-owned source selected at open. */
} TestPortingHandle;

/** @copydoc gif_porting_open */
GifPortingStatus gif_porting_open(const void *source_identifier,
                                  GifPortingHandle *out_handle) {
    TestPortingHandle *handle;

    if (out_handle != NULL) {
        *out_handle = NULL;
    }
    if (source_identifier == NULL || out_handle == NULL) {
        return GIF_PORTING_IO_ERROR;
    }
    if (((const MemorySource *)source_identifier)->inject_open_error) {
        return GIF_PORTING_IO_ERROR;
    }

    handle = (TestPortingHandle *)gif_porting_mem_alloc(sizeof(*handle));
    if (handle == NULL) {
        return GIF_PORTING_OUT_OF_MEMORY;
    }

    handle->source = (MemorySource *)source_identifier;
    *out_handle = handle;
    return GIF_PORTING_OK;
}

/** @copydoc gif_porting_read */
GifPortingStatus gif_porting_read(GifPortingHandle handle,
                                  uint8_t *destination,
                                  size_t requested_bytes,
                                  size_t *actual_bytes) {
    TestPortingHandle *port_handle = (TestPortingHandle *)handle;
    MemorySource *source;
    size_t available;
    size_t amount;

    if (actual_bytes == NULL) {
        return GIF_PORTING_IO_ERROR;
    }
    *actual_bytes = 0;

    if (port_handle == NULL || destination == NULL || requested_bytes == 0) {
        return GIF_PORTING_IO_ERROR;
    }
    source = port_handle->source;
    if (source == NULL) {
        return GIF_PORTING_IO_ERROR;
    }

    source->read_calls++;
    if (requested_bytes > source->largest_requested) {
        source->largest_requested = requested_bytes;
    }
    if (source->inject_zero_ok && !source->zero_ok_emitted &&
        source->offset == source->zero_ok_offset) {
        source->zero_ok_emitted = true;
        return GIF_PORTING_OK;
    }
    if (source->inject_error && source->offset >= source->error_offset) {
        return GIF_PORTING_IO_ERROR;
    }
    if (source->offset >= source->size) {
        return GIF_PORTING_EOF;
    }

    available = source->size - source->offset;
    amount = requested_bytes;
    if (amount > available) {
        amount = available;
    }
    if (source->max_chunk != 0 && amount > source->max_chunk) {
        amount = source->max_chunk;
    }
    if (source->inject_error &&
        source->offset + amount > source->error_offset) {
        amount = source->error_offset - source->offset;
    }

    if (amount != 0) {
        memcpy(destination, source->data + source->offset, amount);
        source->offset += amount;
        *actual_bytes = amount;
    }

    if (source->inject_error && source->offset >= source->error_offset) {
        return GIF_PORTING_IO_ERROR;
    }
    if (source->eof_with_final_bytes && source->offset == source->size) {
        return GIF_PORTING_EOF;
    }
    return GIF_PORTING_OK;
}

/** @copydoc gif_porting_close */
void gif_porting_close(GifPortingHandle handle) {
    TestPortingHandle *port_handle = (TestPortingHandle *)handle;

    if (port_handle != NULL) {
        if (port_handle->source != NULL) {
            port_handle->source->close_calls++;
        }
        gif_porting_mem_free(port_handle);
    }
}
