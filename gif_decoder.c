/*
 * Copyright (c) 2026 Steven Zhu
 * SPDX-License-Identifier: MIT
 */

#include "gif_decoder.h"

#include "gif_lib.h"

#include <stdlib.h>
#include <string.h>

typedef enum GifSourceTerminal {
    GIF_SOURCE_ACTIVE = 0,
    GIF_SOURCE_EOF,
    GIF_SOURCE_IO_ERROR
} GifSourceTerminal;

struct GifDecoder {
    GifFileType *gif;
    GifDecoderConfig source;
    GifSourceTerminal source_terminal;
};

static int gif_decoder_read_bridge(GifFileType *gif,
                                   GifByteType *destination,
                                   int length) {
    GifDecoder *decoder;
    size_t requested;
    size_t total = 0;

    if (gif == NULL || destination == NULL || length <= 0) {
        return 0;
    }

    decoder = (GifDecoder *)gif->UserData;
    if (decoder == NULL || decoder->source.read == NULL) {
        return 0;
    }

    requested = (size_t)length;
    while (total < requested &&
           decoder->source_terminal == GIF_SOURCE_ACTIVE) {
        GifReadStatus read_status;
        size_t actual = 0;
        size_t remaining = requested - total;

        read_status = decoder->source.read(decoder->source.io_context,
                                           destination + total,
                                           remaining,
                                           &actual);

        if (actual > remaining) {
            decoder->source_terminal = GIF_SOURCE_IO_ERROR;
            break;
        }

        total += actual;

        switch (read_status) {
        case GIF_READ_OK:
            if (actual == 0) {
                /* A zero-length successful read cannot make progress. */
                decoder->source_terminal = GIF_SOURCE_IO_ERROR;
            }
            break;
        case GIF_READ_EOF:
            decoder->source_terminal = GIF_SOURCE_EOF;
            break;
        case GIF_READ_IO_ERROR:
            decoder->source_terminal = GIF_SOURCE_IO_ERROR;
            break;
        default:
            decoder->source_terminal = GIF_SOURCE_IO_ERROR;
            break;
        }
    }

    return (int)total;
}

static GifStatus gif_decoder_map_open_error(const GifDecoder *decoder,
                                            int giflib_error) {
    switch (giflib_error) {
    case D_GIF_ERR_NOT_ENOUGH_MEM:
        return GIF_STATUS_OUT_OF_MEMORY;
    case D_GIF_ERR_READ_FAILED:
        if (decoder->source_terminal == GIF_SOURCE_EOF) {
            return GIF_STATUS_UNEXPECTED_EOF;
        }
        return GIF_STATUS_IO_ERROR;
    case D_GIF_ERR_NOT_GIF_FILE:
    case D_GIF_ERR_NO_SCRN_DSCR:
    case D_GIF_ERR_NO_IMAG_DSCR:
    case D_GIF_ERR_NO_COLOR_MAP:
    case D_GIF_ERR_WRONG_RECORD:
    case D_GIF_ERR_DATA_TOO_BIG:
    case D_GIF_ERR_IMAGE_DEFECT:
    case D_GIF_ERR_EOF_TOO_SOON:
        return GIF_STATUS_INVALID_FORMAT;
    default:
        return GIF_STATUS_INTERNAL_ERROR;
    }
}

GifStatus gif_decoder_open(const GifDecoderConfig *config,
                           GifDecoder **out_decoder,
                           GifStreamInfo *out_stream) {
    GifDecoder *decoder;
    GifFileType *gif;
    GifStatus status;
    int giflib_error = D_GIF_SUCCEEDED;

    if (out_decoder != NULL) {
        *out_decoder = NULL;
    }
    if (out_stream != NULL) {
        memset(out_stream, 0, sizeof(*out_stream));
    }

    if (config == NULL || config->read == NULL || out_decoder == NULL ||
        out_stream == NULL) {
        return GIF_STATUS_INVALID_ARGUMENT;
    }

    decoder = (GifDecoder *)calloc(1, sizeof(*decoder));
    if (decoder == NULL) {
        return GIF_STATUS_OUT_OF_MEMORY;
    }

    decoder->source = *config;
    decoder->source_terminal = GIF_SOURCE_ACTIVE;

    gif = DGifOpen(decoder, gif_decoder_read_bridge, &giflib_error);
    if (gif == NULL) {
        status = gif_decoder_map_open_error(decoder, giflib_error);
        free(decoder);
        return status;
    }

    if (decoder->source_terminal == GIF_SOURCE_IO_ERROR) {
        (void)DGifCloseFile(gif, NULL);
        free(decoder);
        return GIF_STATUS_IO_ERROR;
    }

    decoder->gif = gif;
    out_stream->canvas_width = (uint32_t)gif->SWidth;
    out_stream->canvas_height = (uint32_t)gif->SHeight;
    out_stream->background_color_index =
        (uint8_t)gif->SBackGroundColor;
    out_stream->color_resolution = (uint8_t)gif->SColorResolution;
    out_stream->has_global_color_table =
        gif->SColorMap != NULL ? (uint8_t)1 : (uint8_t)0;
    *out_decoder = decoder;

    return GIF_STATUS_OK;
}

void gif_decoder_close(GifDecoder *decoder) {
    if (decoder == NULL) {
        return;
    }

    if (decoder->gif != NULL) {
        (void)DGifCloseFile(decoder->gif, NULL);
    }
    free(decoder);
}

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
    default:
        return "unknown decoder status";
    }
}
