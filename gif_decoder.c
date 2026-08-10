/*
 * Copyright (c) 2026 Steven Zhu
 * SPDX-License-Identifier: MIT
 */

#include "gif_decoder.h"

#include "gif_lib.h"

#include <stdint.h>
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
    GifOutputSurface output;
    GifStatus terminal_status;
    uint32_t frame_index;
    uint8_t output_bound;
    uint8_t stream_ended;
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

static GifStatus gif_decoder_map_error(const GifDecoder *decoder,
                                       int giflib_error) {
    switch (giflib_error) {
    case D_GIF_ERR_NOT_ENOUGH_MEM:
        return GIF_STATUS_OUT_OF_MEMORY;
    case D_GIF_ERR_READ_FAILED:
        if (decoder->source_terminal == GIF_SOURCE_EOF) {
            return GIF_STATUS_UNEXPECTED_EOF;
        }
        if (decoder->source_terminal == GIF_SOURCE_IO_ERROR) {
            return GIF_STATUS_IO_ERROR;
        }
        return GIF_STATUS_INVALID_FORMAT;
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

static GifStatus gif_decoder_fail(GifDecoder *decoder, GifStatus status) {
    decoder->terminal_status = status;
    return status;
}

static size_t gif_decoder_bytes_per_pixel(GifPixelFormat pixel_format) {
    switch (pixel_format) {
    case GIF_PIXEL_RGB888:
    case GIF_PIXEL_BGR888:
        return 3;
    default:
        return 0;
    }
}

static GifStatus gif_decoder_validate_surface(const GifDecoder *decoder,
                                              const GifOutputSurface *surface) {
    size_t bytes_per_pixel;
    size_t row_bytes;
    size_t required_bytes;
    size_t rows_before_last;

    if (surface == NULL || surface->pixels == NULL) {
        return GIF_STATUS_INVALID_ARGUMENT;
    }

    bytes_per_pixel = gif_decoder_bytes_per_pixel(surface->pixel_format);
    if (bytes_per_pixel == 0) {
        return GIF_STATUS_INVALID_ARGUMENT;
    }
    if (decoder->gif->SWidth <= 0 || decoder->gif->SHeight <= 0) {
        return GIF_STATUS_INVALID_FORMAT;
    }
    if (decoder->gif->SColorMap != NULL &&
        (decoder->gif->SColorMap->Colors == NULL ||
         decoder->gif->SColorMap->ColorCount <= 0 ||
         decoder->gif->SBackGroundColor < 0 ||
         decoder->gif->SBackGroundColor >=
             decoder->gif->SColorMap->ColorCount)) {
        return GIF_STATUS_INVALID_FORMAT;
    }
    if ((size_t)decoder->gif->SWidth > SIZE_MAX / bytes_per_pixel) {
        return GIF_STATUS_BUFFER_TOO_SMALL;
    }

    row_bytes = (size_t)decoder->gif->SWidth * bytes_per_pixel;
    if (surface->stride_bytes < row_bytes) {
        return GIF_STATUS_BUFFER_TOO_SMALL;
    }

    rows_before_last = (size_t)decoder->gif->SHeight - 1;
    if (rows_before_last != 0 &&
        surface->stride_bytes > (SIZE_MAX - row_bytes) / rows_before_last) {
        return GIF_STATUS_BUFFER_TOO_SMALL;
    }
    required_bytes = rows_before_last * surface->stride_bytes + row_bytes;
    if (surface->capacity_bytes < required_bytes) {
        return GIF_STATUS_BUFFER_TOO_SMALL;
    }

    return GIF_STATUS_OK;
}

static void gif_decoder_background_color(const GifDecoder *decoder,
                                         uint8_t *red,
                                         uint8_t *green,
                                         uint8_t *blue) {
    const ColorMapObject *color_map = decoder->gif->SColorMap;
    int background_index = decoder->gif->SBackGroundColor;

    *red = 0;
    *green = 0;
    *blue = 0;

    if (color_map != NULL && background_index >= 0 &&
        background_index < color_map->ColorCount) {
        *red = color_map->Colors[background_index].Red;
        *green = color_map->Colors[background_index].Green;
        *blue = color_map->Colors[background_index].Blue;
    }
}

static void gif_decoder_initialize_output(GifDecoder *decoder) {
    uint8_t *pixels = (uint8_t *)decoder->output.pixels;
    uint8_t red;
    uint8_t green;
    uint8_t blue;
    int row;
    int column;

    gif_decoder_background_color(decoder, &red, &green, &blue);
    for (row = 0; row < decoder->gif->SHeight; row++) {
        uint8_t *destination =
            pixels + (size_t)row * decoder->output.stride_bytes;

        for (column = 0; column < decoder->gif->SWidth; column++) {
            if (decoder->output.pixel_format == GIF_PIXEL_RGB888) {
                destination[0] = red;
                destination[1] = green;
                destination[2] = blue;
            } else {
                destination[0] = blue;
                destination[1] = green;
                destination[2] = red;
            }
            destination += 3;
        }
    }
}

static GifStatus gif_decoder_process_extension(GifDecoder *decoder) {
    GifByteType *extension = NULL;
    int extension_code = 0;

    if (DGifGetExtension(decoder->gif, &extension_code, &extension) ==
        GIF_ERROR) {
        return gif_decoder_map_error(decoder, decoder->gif->Error);
    }

    if (extension_code == GRAPHICS_EXT_FUNC_CODE) {
        uint8_t packed;
        uint8_t disposal_mode;

        if (extension == NULL || extension[0] != 4) {
            return GIF_STATUS_INVALID_FORMAT;
        }

        packed = extension[1];
        disposal_mode = (uint8_t)((packed >> 2) & 0x07);
        if (disposal_mode > DISPOSE_DO_NOT || (packed & 0x03) != 0 ||
            extension[2] != 0 || extension[3] != 0) {
            return GIF_STATUS_UNSUPPORTED_FEATURE;
        }
    } else if (extension_code == PLAINTEXT_EXT_FUNC_CODE) {
        return GIF_STATUS_UNSUPPORTED_FEATURE;
    }

    do {
        if (DGifGetExtensionNext(decoder->gif, &extension) == GIF_ERROR) {
            return gif_decoder_map_error(decoder, decoder->gif->Error);
        }
    } while (extension != NULL);

    return GIF_STATUS_OK;
}

static int gif_decoder_image_rectangle_is_valid(const GifDecoder *decoder) {
    const GifImageDesc *image = &decoder->gif->Image;

    if (image->Left < 0 || image->Top < 0 || image->Width <= 0 ||
        image->Height <= 0 || image->Left > decoder->gif->SWidth ||
        image->Top > decoder->gif->SHeight) {
        return 0;
    }
    if (image->Width > decoder->gif->SWidth - image->Left ||
        image->Height > decoder->gif->SHeight - image->Top) {
        return 0;
    }
    return 1;
}

static GifStatus gif_decoder_decode_image(GifDecoder *decoder,
                                          GifFrameInfo *out_frame) {
    const ColorMapObject *color_map;
    GifPixelType *row_buffer;
    GifImageDesc *image = &decoder->gif->Image;
    int row;

    if (DGifGetImageHeader(decoder->gif) == GIF_ERROR) {
        return gif_decoder_map_error(decoder, decoder->gif->Error);
    }
    if (!gif_decoder_image_rectangle_is_valid(decoder)) {
        return GIF_STATUS_INVALID_FORMAT;
    }
    if (image->Interlace) {
        return GIF_STATUS_UNSUPPORTED_FEATURE;
    }

    color_map = image->ColorMap != NULL ? image->ColorMap
                                        : decoder->gif->SColorMap;
    if (color_map == NULL || color_map->Colors == NULL ||
        color_map->ColorCount <= 0) {
        return GIF_STATUS_INVALID_FORMAT;
    }

    row_buffer = (GifPixelType *)malloc((size_t)image->Width);
    if (row_buffer == NULL) {
        return GIF_STATUS_OUT_OF_MEMORY;
    }

    for (row = 0; row < image->Height; row++) {
        uint8_t *destination;
        int column;

        if (DGifGetLine(decoder->gif, row_buffer, image->Width) ==
            GIF_ERROR) {
            GifStatus status =
                gif_decoder_map_error(decoder, decoder->gif->Error);
            free(row_buffer);
            return status;
        }

        destination = (uint8_t *)decoder->output.pixels +
                      (size_t)(image->Top + row) *
                          decoder->output.stride_bytes +
                      (size_t)image->Left * 3;
        for (column = 0; column < image->Width; column++) {
            int palette_index = row_buffer[column];
            const GifColorType *color;

            if (palette_index >= color_map->ColorCount) {
                free(row_buffer);
                return GIF_STATUS_INVALID_FORMAT;
            }
            color = &color_map->Colors[palette_index];
            if (decoder->output.pixel_format == GIF_PIXEL_RGB888) {
                destination[0] = color->Red;
                destination[1] = color->Green;
                destination[2] = color->Blue;
            } else {
                destination[0] = color->Blue;
                destination[1] = color->Green;
                destination[2] = color->Red;
            }
            destination += 3;
        }
    }

    free(row_buffer);

    out_frame->frame_index = decoder->frame_index;
    out_frame->image_left = (uint32_t)image->Left;
    out_frame->image_top = (uint32_t)image->Top;
    out_frame->image_width = (uint32_t)image->Width;
    out_frame->image_height = (uint32_t)image->Height;
    out_frame->updated_left = out_frame->image_left;
    out_frame->updated_top = out_frame->image_top;
    out_frame->updated_width = out_frame->image_width;
    out_frame->updated_height = out_frame->image_height;
    decoder->frame_index++;

    return GIF_STATUS_OK;
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
        status = gif_decoder_map_error(decoder, giflib_error);
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

GifStatus gif_decoder_bind_output(GifDecoder *decoder,
                                  const GifOutputSurface *surface) {
    GifStatus status;

    if (decoder == NULL) {
        return GIF_STATUS_INVALID_ARGUMENT;
    }
    if (decoder->terminal_status != GIF_STATUS_OK ||
        decoder->stream_ended || decoder->frame_index != 0) {
        return GIF_STATUS_INVALID_STATE;
    }

    status = gif_decoder_validate_surface(decoder, surface);
    if (status != GIF_STATUS_OK) {
        return status;
    }

    decoder->output = *surface;
    decoder->output_bound = 1;
    gif_decoder_initialize_output(decoder);
    return GIF_STATUS_OK;
}

GifStatus gif_decoder_next_frame(GifDecoder *decoder,
                                 GifFrameInfo *out_frame) {
    GifRecordType record_type;

    if (decoder == NULL || out_frame == NULL) {
        return GIF_STATUS_INVALID_ARGUMENT;
    }
    memset(out_frame, 0, sizeof(*out_frame));

    if (decoder->terminal_status != GIF_STATUS_OK) {
        return decoder->terminal_status;
    }
    if (decoder->stream_ended) {
        return GIF_STATUS_END_OF_STREAM;
    }
    if (!decoder->output_bound) {
        return GIF_STATUS_INVALID_STATE;
    }

    for (;;) {
        GifStatus status;

        if (DGifGetRecordType(decoder->gif, &record_type) == GIF_ERROR) {
            return gif_decoder_fail(
                decoder,
                gif_decoder_map_error(decoder, decoder->gif->Error));
        }

        switch (record_type) {
        case IMAGE_DESC_RECORD_TYPE:
            status = gif_decoder_decode_image(decoder, out_frame);
            if (status != GIF_STATUS_OK) {
                return gif_decoder_fail(decoder, status);
            }
            return GIF_STATUS_OK;
        case EXTENSION_RECORD_TYPE:
            status = gif_decoder_process_extension(decoder);
            if (status != GIF_STATUS_OK) {
                return gif_decoder_fail(decoder, status);
            }
            break;
        case TERMINATE_RECORD_TYPE:
            decoder->stream_ended = 1;
            return GIF_STATUS_END_OF_STREAM;
        default:
            return gif_decoder_fail(decoder, GIF_STATUS_INVALID_FORMAT);
        }
    }
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
    case GIF_STATUS_INVALID_STATE:
        return "invalid decoder state";
    default:
        return "unknown decoder status";
    }
}
