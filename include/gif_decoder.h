/*
 * Copyright (c) 2026 Steven Zhu
 * SPDX-License-Identifier: MIT
 */

#ifndef GIF_DECODER_H
#define GIF_DECODER_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct GifDecoder GifDecoder;

typedef enum GifStatus {
    GIF_STATUS_OK = 0,
    GIF_STATUS_END_OF_STREAM = 1,
    GIF_STATUS_INVALID_ARGUMENT = 2,
    GIF_STATUS_OUT_OF_MEMORY = 3,
    GIF_STATUS_IO_ERROR = 4,
    GIF_STATUS_UNEXPECTED_EOF = 5,
    GIF_STATUS_INVALID_FORMAT = 6,
    GIF_STATUS_UNSUPPORTED_FEATURE = 7,
    GIF_STATUS_BUFFER_TOO_SMALL = 8,
    GIF_STATUS_INTERNAL_ERROR = 9
} GifStatus;

typedef enum GifReadStatus {
    GIF_READ_OK = 0,
    GIF_READ_EOF = 1,
    GIF_READ_IO_ERROR = 2
} GifReadStatus;

/*
 * Read up to requested_bytes into destination and store the number of bytes
 * produced in actual_bytes.
 *
 * GIF_READ_OK may legally return fewer bytes than requested, but must return
 * at least one byte. GIF_READ_EOF and GIF_READ_IO_ERROR may return final valid
 * bytes before making the condition terminal. actual_bytes must never exceed
 * requested_bytes.
 */
typedef GifReadStatus (*GifReadCallback)(void *io_context,
                                         uint8_t *destination,
                                         size_t requested_bytes,
                                         size_t *actual_bytes);

typedef struct GifDecoderConfig {
    GifReadCallback read;
    void *io_context;
} GifDecoderConfig;

typedef struct GifStreamInfo {
    uint32_t canvas_width;
    uint32_t canvas_height;
    uint8_t background_color_index;
    uint8_t color_resolution;
    uint8_t has_global_color_table;
} GifStreamInfo;

GifStatus gif_decoder_open(const GifDecoderConfig *config,
                           GifDecoder **out_decoder,
                           GifStreamInfo *out_stream);

void gif_decoder_close(GifDecoder *decoder);

const char *gif_status_string(GifStatus status);

#ifdef __cplusplus
}
#endif

#endif /* GIF_DECODER_H */
