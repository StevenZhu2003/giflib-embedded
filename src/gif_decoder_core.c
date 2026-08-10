/**
 * @file gif_decoder_core.c
 * @brief Hidden streaming implementation of the portable GIF decoder.
 *
 * Copyright (c) 2026 Steven Zhu
 * SPDX-License-Identifier: MIT
 */

#include "gif_decoder_core.h"

#include "gif_lib.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/** @brief Terminal state remembered by the byte-source bridge. */
typedef enum GifSourceTerminal {
    GIF_SOURCE_ACTIVE = 0, /**< The source may provide more bytes. */
    GIF_SOURCE_EOF,        /**< The source reported its final byte. */
    GIF_SOURCE_IO_ERROR    /**< The source reported an unrecoverable error. */
} GifSourceTerminal;

/** @brief Internal state hidden behind the public opaque decoder handle. */
struct GifDecoder {
    GifFileType *gif;                  /**< Private giflib decoder instance. */
    GifPortingHandle source_handle;    /**< Open handle supplied by the port. */
    GifSourceTerminal source_terminal; /**< Remembered source terminal state. */
    GifOutputSurface output;           /**< Copied caller output descriptor. */
    GifStatus terminal_status;         /**< Sticky public decode failure. */
    uint32_t frame_index;              /**< Next zero-based frame index. */
    uint8_t output_bound;              /**< Non-zero after output binding. */
    uint8_t stream_ended;              /**< Non-zero after the GIF trailer. */
};

/**
 * @brief Adapt the hidden short-read source to giflib's exact-read contract.
 *
 * The bridge repeatedly calls the facade-provided source while it returns
 * progress, so a legal short read is not mistaken for EOF. Terminal source
 * state is retained for public error mapping.
 *
 * @param[in] gif          giflib decoder that owns the hidden source context.
 * @param[out] destination Buffer supplied by giflib.
 * @param[in] length       Exact byte count requested by giflib.
 * @return Number of bytes copied, which is less than `length` on failure.
 */
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
    if (decoder == NULL || decoder->source_handle == NULL) {
        return 0;
    }

    requested = (size_t)length;
    while (total < requested &&
           decoder->source_terminal == GIF_SOURCE_ACTIVE) {
        GifPortingStatus read_status;
        size_t actual = 0;
        size_t remaining = requested - total;

        read_status = gif_porting_read(decoder->source_handle,
                                       destination + total,
                                       remaining,
                                       &actual);

        if (actual > remaining) {
            decoder->source_terminal = GIF_SOURCE_IO_ERROR;
            break;
        }

        total += actual;

        switch (read_status) {
        case GIF_PORTING_OK:
            if (actual == 0) {
                /* A zero-length successful read cannot make progress. */
                decoder->source_terminal = GIF_SOURCE_IO_ERROR;
            }
            break;
        case GIF_PORTING_EOF:
            decoder->source_terminal = GIF_SOURCE_EOF;
            break;
        case GIF_PORTING_IO_ERROR:
            decoder->source_terminal = GIF_SOURCE_IO_ERROR;
            break;
        default:
            decoder->source_terminal = GIF_SOURCE_IO_ERROR;
            break;
        }
    }

    return (int)total;
}

/**
 * @brief Translate a giflib error and source state into the public status model.
 *
 * @param[in] decoder      Decoder containing the remembered source state.
 * @param[in] giflib_error Error code produced by giflib.
 * @return Corresponding public decoder status.
 */
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

/**
 * @brief Store and return a sticky decoder failure.
 *
 * @param[in,out] decoder Decoder that entered a terminal failure state.
 * @param[in] status      Public failure to retain.
 * @return The supplied status value.
 */
static GifStatus gif_decoder_fail(GifDecoder *decoder, GifStatus status) {
    decoder->terminal_status = status;
    return status;
}

/**
 * @brief Return the packed byte width of a supported pixel format.
 *
 * @param[in] pixel_format Pixel layout to query.
 * @return Bytes per pixel, or zero for an unknown format.
 */
static size_t gif_decoder_bytes_per_pixel(GifPixelFormat pixel_format) {
    switch (pixel_format) {
    case GIF_PIXEL_RGB888:
    case GIF_PIXEL_BGR888:
        return 3;
    default:
        return 0;
    }
}

/**
 * @brief Validate output format, stride, palette background, and capacity.
 *
 * All size calculations are checked before multiplication or addition so the
 * same validation is safe on 32-bit embedded targets.
 *
 * @param[in] decoder Decoder containing logical-screen information.
 * @param[in] surface Caller-provided output descriptor.
 * @return `GIF_STATUS_OK` or a precise validation status.
 */
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

/**
 * @brief Resolve the initial logical-screen background color.
 *
 * A stream without a global color table uses deterministic black. A present
 * global table has already been validated by the surface validator.
 *
 * @param[in] decoder Decoder containing the global color table.
 * @param[out] red    Resolved red component.
 * @param[out] green  Resolved green component.
 * @param[out] blue   Resolved blue component.
 */
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

/**
 * @brief Fill the visible canvas with its initial background color.
 *
 * Row padding is intentionally left untouched.
 *
 * @param[in,out] decoder Decoder with a validated bound output surface.
 */
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

/**
 * @brief Consume one extension record without accumulating extension storage.
 *
 * Stage 3 accepts only Graphic Control Extensions whose behavior is equivalent
 * to disposal 0/1 without delay, transparency, or user input. Features that
 * would change visible output are rejected explicitly.
 *
 * @param[in,out] decoder Decoder positioned after an extension introducer.
 * @return Public status for the consumed or rejected extension.
 */
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

/**
 * @brief Check that the current image rectangle lies inside the canvas.
 *
 * @param[in] decoder Decoder containing the current giflib image descriptor.
 * @return Non-zero when the rectangle is valid, otherwise zero.
 */
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

/**
 * @brief Stream one non-interlaced image into the composited output surface.
 *
 * One palette-index row is allocated at a time. The function selects the local
 * color table when present and otherwise uses the global table.
 *
 * @param[in,out] decoder Decoder positioned at an image descriptor.
 * @param[out] out_frame  Receives frame and updated-rectangle metadata.
 * @return Public decode, format, feature, source, or allocation status.
 */
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

/** @copydoc gif_decoder_core_open */
GifStatus gif_decoder_core_open(GifPortingHandle source_handle,
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

    if (source_handle == NULL || out_decoder == NULL ||
        out_stream == NULL) {
        return GIF_STATUS_INVALID_ARGUMENT;
    }

    decoder = (GifDecoder *)calloc(1, sizeof(*decoder));
    if (decoder == NULL) {
        return GIF_STATUS_OUT_OF_MEMORY;
    }

    decoder->source_handle = source_handle;
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

/** @copydoc gif_decoder_core_bind_output */
GifStatus gif_decoder_core_bind_output(GifDecoder *decoder,
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

/** @copydoc gif_decoder_core_next_frame */
GifStatus gif_decoder_core_next_frame(GifDecoder *decoder,
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

/** @copydoc gif_decoder_core_close */
void gif_decoder_core_close(GifDecoder *decoder) {
    if (decoder == NULL) {
        return;
    }

    if (decoder->gif != NULL) {
        (void)DGifCloseFile(decoder->gif, NULL);
    }
    if (decoder->source_handle != NULL) {
        gif_porting_close(decoder->source_handle);
    }
    free(decoder);
}
