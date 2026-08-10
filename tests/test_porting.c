/**
 * @file test_porting.c
 * @brief Host-only in-memory implementation of the stable porting contract.
 */

#include "gif_porting.h"

#include "test_porting.h"

#include <string.h>

/** @copydoc gif_porting_open */
GifPortingStatus gif_porting_open(const void *source_identifier,
                                  GifPortingHandle *out_handle) {
    if (out_handle != NULL) {
        *out_handle = NULL;
    }
    if (source_identifier == NULL || out_handle == NULL) {
        return GIF_PORTING_IO_ERROR;
    }
    if (((const MemorySource *)source_identifier)->inject_open_error) {
        return GIF_PORTING_IO_ERROR;
    }

    *out_handle = (void *)source_identifier;
    return GIF_PORTING_OK;
}

/** @copydoc gif_porting_read */
GifPortingStatus gif_porting_read(GifPortingHandle handle,
                                  uint8_t *destination,
                                  size_t requested_bytes,
                                  size_t *actual_bytes) {
    MemorySource *source = (MemorySource *)handle;
    size_t available;
    size_t amount;

    if (actual_bytes == NULL) {
        return GIF_PORTING_IO_ERROR;
    }
    *actual_bytes = 0;

    if (source == NULL || destination == NULL || requested_bytes == 0) {
        return GIF_PORTING_IO_ERROR;
    }

    source->read_calls++;
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
    MemorySource *source = (MemorySource *)handle;

    if (source != NULL) {
        source->close_calls++;
    }
}
