/**
 * @file gif_decoder.c
 * @brief Fixed application-facing facade for the portable GIF decoder.
 *
 * Ordinary users call only the functions in this file through
 * `gif_decoder.h`. Platform integration belongs exclusively in
 * `gif_porting.c`; decoder behavior belongs in the hidden core.
 *
 * Copyright (c) 2026 Steven Zhu
 * SPDX-License-Identifier: MIT
 */

#include "gif_decoder.h"

#include "gif_decoder_core.h"
#include "gif_porting.h"

#include <string.h>

/** @copydoc gif_decoder_open */
GifStatus gif_decoder_open(const GifDecoderConfig *config,
                           GifDecoder **out_decoder,
                           GifStreamInfo *out_stream) {
    GifPortingHandle porting_handle = NULL;
    GifPortingStatus porting_status;
    GifStatus status;

    if (out_decoder != NULL) {
        *out_decoder = NULL;
    }
    if (out_stream != NULL) {
        memset(out_stream, 0, sizeof(*out_stream));
    }

    if (config == NULL || config->source_identifier == NULL ||
        out_decoder == NULL || out_stream == NULL) {
        return GIF_STATUS_INVALID_ARGUMENT;
    }

    porting_status =
        gif_porting_open(config->source_identifier, &porting_handle);
    if (porting_status != GIF_PORTING_OK || porting_handle == NULL) {
        if (porting_handle != NULL) {
            gif_porting_close(porting_handle);
        }
        return GIF_STATUS_IO_ERROR;
    }

    status = gif_decoder_core_open(porting_handle, out_decoder, out_stream);
    if (status != GIF_STATUS_OK) {
        gif_porting_close(porting_handle);
    }
    return status;
}

/** @copydoc gif_decoder_bind_output */
GifStatus gif_decoder_bind_output(GifDecoder *decoder,
                                  const GifOutputSurface *surface) {
    return gif_decoder_core_bind_output(decoder, surface);
}

/** @copydoc gif_decoder_next_frame */
GifStatus gif_decoder_next_frame(GifDecoder *decoder,
                                 GifFrameInfo *out_frame) {
    return gif_decoder_core_next_frame(decoder, out_frame);
}

/** @copydoc gif_decoder_close */
void gif_decoder_close(GifDecoder *decoder) {
    gif_decoder_core_close(decoder);
}

/** @copydoc gif_status_string */
const char *gif_status_string(GifStatus status) {
    switch (status) {
    case GIF_STATUS_OK:
        return "success";
    case GIF_STATUS_END_OF_STREAM:
        return "end of stream";
    case GIF_STATUS_INVALID_ARGUMENT:
        return "invalid argument";
    case GIF_STATUS_OUT_OF_MEMORY:
        return "out of memory";
    case GIF_STATUS_IO_ERROR:
        return "input/output error";
    case GIF_STATUS_UNEXPECTED_EOF:
        return "unexpected end of input";
    case GIF_STATUS_INVALID_FORMAT:
        return "invalid GIF data";
    case GIF_STATUS_UNSUPPORTED_FEATURE:
        return "unsupported GIF feature";
    case GIF_STATUS_BUFFER_TOO_SMALL:
        return "buffer too small";
    case GIF_STATUS_INTERNAL_ERROR:
        return "internal decoder error";
    case GIF_STATUS_INVALID_STATE:
        return "invalid decoder state";
    default:
        return "unknown decoder status";
    }
}
