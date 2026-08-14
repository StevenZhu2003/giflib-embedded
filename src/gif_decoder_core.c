/**
 * @file gif_decoder_core.c
 * @brief Hidden streaming implementation of the portable GIF decoder.
 *
 * Copyright (c) 2026 Steven Zhu
 * SPDX-License-Identifier: MIT
 */

#include "gif_decoder_core.h"

#include "gif_config.h"
#include "gif_lib.h"
#include "gif_mem.h"

#include <stdint.h>
#include <string.h>

/** @brief Terminal state remembered by the byte-source bridge. */
typedef enum GifSourceTerminal {
    GIF_SOURCE_ACTIVE = 0, /**< The source may provide more bytes. */
    GIF_SOURCE_EOF,        /**< The source reported its final byte. */
    GIF_SOURCE_IO_ERROR    /**< The source reported an unrecoverable error. */
} GifSourceTerminal;

/** @brief Graphic Control Extension state applying to the next image only. */
typedef struct GifFrameControl {
    int transparent_color;  /**< Palette index skipped during composition. */
    uint32_t delay_ms;      /**< Frame delay converted from centiseconds. */
    uint8_t disposal_mode;  /**< Disposal instruction for the next image. */
} GifFrameControl;

/** @brief Valid canvas rectangle remembered for composition bookkeeping. */
typedef struct GifCanvasRectangle {
    uint32_t left;   /**< Zero-based canvas column of the rectangle. */
    uint32_t top;    /**< Zero-based canvas row of the rectangle. */
    uint32_t width;  /**< Rectangle width in pixels. */
    uint32_t height; /**< Rectangle height in pixels. */
} GifCanvasRectangle;

/** @brief Deferred disposal action applied immediately before a later image. */
typedef enum GifPendingDisposal {
    GIF_PENDING_DISPOSAL_NONE = 0,
    GIF_PENDING_DISPOSAL_BACKGROUND,
    GIF_PENDING_DISPOSAL_PREVIOUS
} GifPendingDisposal;

/** @brief Internal state hidden behind the public opaque decoder handle. */
struct GifDecoder {
    GifFileType *gif;                  /**< Private giflib decoder instance. */
    GifPortingHandle source_handle;    /**< Open handle supplied by the port. */
    GifSourceTerminal source_terminal; /**< Remembered source terminal state. */
    GifOutputSurface output;           /**< Copied caller output descriptor. */
    GifStatus terminal_status;         /**< Sticky public decode failure. */
    GifFrameControl pending_control;   /**< Control state for the next image. */
    GifCanvasRectangle pending_disposal_rect; /**< Prior area to restore. */
#if GIF_ENABLE_DISPOSAL_METHOD_3
    uint8_t *pending_previous_pixels; /**< Packed snapshot for prior method 3. */
#endif
    uint32_t frame_index;              /**< Next zero-based frame index. */
    GifPendingDisposal pending_disposal; /**< Deferred action from prior frame. */
    uint8_t output_bound;              /**< Non-zero after output binding. */
    uint8_t stream_ended;              /**< Non-zero after the GIF trailer. */
};

/**
 * @brief Reset pending control state to the GIF defaults for an image.
 *
 * Graphic Control Extensions apply only to the next graphic rendering block.
 * Resetting after every decoded image prevents delay and transparency from
 * leaking into a later image that has no GCE.
 *
 * @param[out] control Frame control state to reset.
 */
static inline void gif_decoder_reset_frame_control(GifFrameControl *control) {
    control->transparent_color = NO_TRANSPARENT_COLOR;
    control->delay_ms = 0;
    control->disposal_mode = DISPOSAL_UNSPECIFIED;
}

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
 * @brief Return the number of bytes occupied by one validated output pixel.
 *
 * @param[in] pixel_format Validated destination layout.
 * @return Number of bytes in one destination pixel.
 */
static inline size_t gif_decoder_pixel_bytes(GifPixelFormat pixel_format) {
    return pixel_format == GIF_PIXEL_RGB565 ? 2U : 3U;
}

/**
 * @brief Return the address of one visible output pixel.
 *
 * This short calculation is shared by background fill, image composition,
 * and enabled restore-to-previous row copies. It remains inline because all
 * of those paths execute it in their row-processing loops.
 */
static inline uint8_t *gif_decoder_output_pixel(GifDecoder *decoder,
                                                 uint32_t row,
                                                 uint32_t column) {
    return (uint8_t *)decoder->output.pixels +
           (size_t)row * decoder->output.stride_bytes +
           (size_t)column *
               gif_decoder_pixel_bytes(decoder->output.pixel_format);
}

/**
 * @brief Store one RGB color in the selected packed output layout.
 *
 * This is shared by background restoration and image composition. It is
 * deliberately inline because both callers execute it once per output pixel.
 *
 * @param[in] pixel_format Destination layout.
 * @param[out] destination  Writable destination pixel bytes.
 * @param[in] red           Red component to store.
 * @param[in] green         Green component to store.
 * @param[in] blue          Blue component to store.
 */
static inline void gif_decoder_store_pixel(GifPixelFormat pixel_format,
                                           uint8_t *destination,
                                           uint8_t red,
                                           uint8_t green,
                                           uint8_t blue) {
    if (pixel_format == GIF_PIXEL_RGB888) {
        destination[0] = red;
        destination[1] = green;
        destination[2] = blue;
    } else if (pixel_format == GIF_PIXEL_BGR888) {
        destination[0] = blue;
        destination[1] = green;
        destination[2] = red;
    } else {
        uint16_t packed = (uint16_t)(((uint16_t)(red >> 3U) << 11U) |
                                     ((uint16_t)(green >> 2U) << 5U) |
                                     (uint16_t)(blue >> 3U));

        /* memcpy preserves native word byte order without alignment demands. */
        memcpy(destination, &packed, sizeof(packed));
    }
}

/**
 * @brief Fill one visible canvas rectangle with the logical background color.
 *
 * Row padding is intentionally left untouched.
 *
 * @param[in,out] decoder Decoder with a validated bound output surface.
 */
static void gif_decoder_fill_background_rectangle(
    GifDecoder *decoder, const GifCanvasRectangle *rectangle) {
    const ColorMapObject *color_map = decoder->gif->SColorMap;
    int background_index = decoder->gif->SBackGroundColor;
    uint8_t red = 0;
    uint8_t green = 0;
    uint8_t blue = 0;
    size_t pixel_bytes = gif_decoder_pixel_bytes(decoder->output.pixel_format);
    uint32_t row;
    uint32_t column;

    if (color_map != NULL && background_index >= 0 &&
        background_index < color_map->ColorCount) {
        red = color_map->Colors[background_index].Red;
        green = color_map->Colors[background_index].Green;
        blue = color_map->Colors[background_index].Blue;
    }
    for (row = 0; row < rectangle->height; row++) {
        uint8_t *destination = gif_decoder_output_pixel(
            decoder, rectangle->top + row, rectangle->left);

        for (column = 0; column < rectangle->width; column++) {
            gif_decoder_store_pixel(decoder->output.pixel_format,
                                    destination, red, green, blue);
            destination += pixel_bytes;
        }
    }
}

/** @brief Discard any decoder-owned state retained for a deferred disposal. */
static void gif_decoder_clear_pending_disposal(GifDecoder *decoder) {
#if GIF_ENABLE_DISPOSAL_METHOD_3
    gif_mem_free(decoder->pending_previous_pixels);
    decoder->pending_previous_pixels = NULL;
#endif
    decoder->pending_disposal = GIF_PENDING_DISPOSAL_NONE;
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

    decoder = (GifDecoder *)gif_mem_calloc(1, sizeof(*decoder));
    if (decoder == NULL) {
        return GIF_STATUS_OUT_OF_MEMORY;
    }

    decoder->source_handle = source_handle;
    decoder->source_terminal = GIF_SOURCE_ACTIVE;
    gif_decoder_reset_frame_control(&decoder->pending_control);

    gif = DGifOpen(decoder, gif_decoder_read_bridge, &giflib_error);
    if (gif == NULL) {
        status = gif_decoder_map_error(decoder, giflib_error);
        gif_mem_free(decoder);
        return status;
    }

    if (decoder->source_terminal == GIF_SOURCE_IO_ERROR) {
        (void)DGifCloseFile(gif, NULL);
        gif_mem_free(decoder);
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
    GifCanvasRectangle canvas;
    size_t row_bytes;
    size_t required_bytes;
    size_t rows_before_last;
    size_t pixel_bytes;

    if (decoder == NULL) {
        return GIF_STATUS_INVALID_ARGUMENT;
    }
    if (decoder->terminal_status != GIF_STATUS_OK ||
        decoder->stream_ended || decoder->frame_index != 0) {
        return GIF_STATUS_INVALID_STATE;
    }
    if (surface == NULL || surface->pixels == NULL ||
        (surface->pixel_format != GIF_PIXEL_RGB888 &&
         surface->pixel_format != GIF_PIXEL_BGR888 &&
         surface->pixel_format != GIF_PIXEL_RGB565)) {
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
    pixel_bytes = gif_decoder_pixel_bytes(surface->pixel_format);
    if ((size_t)decoder->gif->SWidth > SIZE_MAX / pixel_bytes) {
        return GIF_STATUS_BUFFER_TOO_SMALL;
    }

    row_bytes = (size_t)decoder->gif->SWidth * pixel_bytes;
    if (surface->stride_bytes < row_bytes) {
        return GIF_STATUS_BUFFER_TOO_SMALL;
    }
    rows_before_last = (size_t)decoder->gif->SHeight - 1U;
    if (rows_before_last != 0U &&
        surface->stride_bytes > (SIZE_MAX - row_bytes) / rows_before_last) {
        return GIF_STATUS_BUFFER_TOO_SMALL;
    }
    required_bytes = rows_before_last * surface->stride_bytes + row_bytes;
    if (surface->capacity_bytes < required_bytes) {
        return GIF_STATUS_BUFFER_TOO_SMALL;
    }

    decoder->output = *surface;
    decoder->output_bound = 1;
    canvas.left = 0;
    canvas.top = 0;
    canvas.width = (uint32_t)decoder->gif->SWidth;
    canvas.height = (uint32_t)decoder->gif->SHeight;
    gif_decoder_fill_background_rectangle(decoder, &canvas);
    return GIF_STATUS_OK;
}

/** @copydoc gif_decoder_core_next_frame */
GifStatus gif_decoder_core_next_frame(GifDecoder *decoder,
                                      GifFrameInfo *out_frame) {
    GifRecordType record_type;
    GifPixelType *row_buffer = NULL;
#if GIF_ENABLE_DISPOSAL_METHOD_3
    uint8_t *saved_previous_pixels = NULL;
#endif
    GifStatus status;

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
        if (DGifGetRecordType(decoder->gif, &record_type) == GIF_ERROR) {
            status = gif_decoder_map_error(decoder, decoder->gif->Error);
            goto fail;
        }

        switch (record_type) {
        case IMAGE_DESC_RECORD_TYPE: {
            const ColorMapObject *color_map;
            GifCanvasRectangle image_rect;
            GifCanvasRectangle restored_rect = {0};
            GifCanvasRectangle updated_rect;
            GifImageDesc *image = &decoder->gif->Image;
            uint8_t restored_previous = 0;
            int interlace_pass = 0;
            int interlace_row = 0;
            int row;
            size_t pixel_bytes =
                gif_decoder_pixel_bytes(decoder->output.pixel_format);

            if (DGifGetImageHeader(decoder->gif) == GIF_ERROR) {
                status = gif_decoder_map_error(decoder, decoder->gif->Error);
                goto fail;
            }
            if (image->Left < 0 || image->Top < 0 || image->Width <= 0 ||
                image->Height <= 0 || image->Left > decoder->gif->SWidth ||
                image->Top > decoder->gif->SHeight ||
                image->Width > decoder->gif->SWidth - image->Left ||
                image->Height > decoder->gif->SHeight - image->Top) {
                status = GIF_STATUS_INVALID_FORMAT;
                goto fail;
            }
            color_map = image->ColorMap != NULL ? image->ColorMap
                                                : decoder->gif->SColorMap;
            if (color_map == NULL || color_map->Colors == NULL ||
                color_map->ColorCount <= 0) {
                status = GIF_STATUS_INVALID_FORMAT;
                goto fail;
            }
            if (decoder->pending_control.transparent_color !=
                    NO_TRANSPARENT_COLOR &&
                decoder->pending_control.transparent_color >=
                    color_map->ColorCount) {
                status = GIF_STATUS_INVALID_FORMAT;
                goto fail;
            }

            image_rect.left = (uint32_t)image->Left;
            image_rect.top = (uint32_t)image->Top;
            image_rect.width = (uint32_t)image->Width;
            image_rect.height = (uint32_t)image->Height;
            row_buffer =
                (GifPixelType *)gif_mem_malloc((size_t)image->Width);
            if (row_buffer == NULL) {
                status = GIF_STATUS_OUT_OF_MEMORY;
                goto fail;
            }

            /* Apply the prior frame's deferred disposal before this image. */
            if (decoder->pending_disposal == GIF_PENDING_DISPOSAL_BACKGROUND) {
                gif_decoder_fill_background_rectangle(
                    decoder, &decoder->pending_disposal_rect);
                restored_rect = decoder->pending_disposal_rect;
                restored_previous = 1;
#if GIF_ENABLE_DISPOSAL_METHOD_3
            } else if (decoder->pending_disposal ==
                           GIF_PENDING_DISPOSAL_PREVIOUS &&
                       decoder->pending_previous_pixels != NULL) {
                size_t restored_row_bytes =
                    (size_t)decoder->pending_disposal_rect.width *
                    pixel_bytes;
                uint32_t restored_row;

                for (restored_row = 0;
                     restored_row < decoder->pending_disposal_rect.height;
                     restored_row++) {
                    memcpy(gif_decoder_output_pixel(
                               decoder,
                               decoder->pending_disposal_rect.top +
                                   restored_row,
                               decoder->pending_disposal_rect.left),
                           decoder->pending_previous_pixels +
                               (size_t)restored_row * restored_row_bytes,
                           restored_row_bytes);
                }
                restored_rect = decoder->pending_disposal_rect;
                restored_previous = 1;
#endif
            }
            gif_decoder_clear_pending_disposal(decoder);

#if GIF_ENABLE_DISPOSAL_METHOD_3
            if (decoder->pending_control.disposal_mode == DISPOSE_PREVIOUS) {
                size_t saved_row_bytes;
                size_t saved_total_bytes;
                uint32_t saved_row;

                if ((size_t)image_rect.width > SIZE_MAX / pixel_bytes) {
                    status = GIF_STATUS_OUT_OF_MEMORY;
                    goto fail;
                }
                saved_row_bytes = (size_t)image_rect.width * pixel_bytes;
                if ((size_t)image_rect.height > SIZE_MAX / saved_row_bytes) {
                    status = GIF_STATUS_OUT_OF_MEMORY;
                    goto fail;
                }
                saved_total_bytes = saved_row_bytes * (size_t)image_rect.height;
                saved_previous_pixels =
                    (uint8_t *)gif_mem_malloc(saved_total_bytes);
                if (saved_previous_pixels == NULL) {
                    status = GIF_STATUS_OUT_OF_MEMORY;
                    goto fail;
                }
                for (saved_row = 0; saved_row < image_rect.height;
                     saved_row++) {
                    memcpy(saved_previous_pixels +
                               (size_t)saved_row * saved_row_bytes,
                           gif_decoder_output_pixel(
                               decoder, image_rect.top + saved_row,
                               image_rect.left),
                           saved_row_bytes);
                }
            }
#endif

            for (row = 0; row < image->Height; row++) {
                uint8_t *destination;
                int column;
                int destination_row = row;

                if (DGifGetLine(decoder->gif, row_buffer, image->Width) ==
                    GIF_ERROR) {
                    status =
                        gif_decoder_map_error(decoder, decoder->gif->Error);
                    goto fail;
                }

                if (image->Interlace) {
                    destination_row = interlace_row;
                }
                destination = gif_decoder_output_pixel(
                    decoder, (uint32_t)(image->Top + destination_row),
                    (uint32_t)image->Left);
                for (column = 0; column < image->Width; column++) {
                    int palette_index = row_buffer[column];
                    const GifColorType *color;

                    if (palette_index >= color_map->ColorCount) {
                        status = GIF_STATUS_INVALID_FORMAT;
                        goto fail;
                    }
                    if (palette_index ==
                        decoder->pending_control.transparent_color) {
                        destination += pixel_bytes;
                        continue;
                    }
                    color = &color_map->Colors[palette_index];
                    gif_decoder_store_pixel(decoder->output.pixel_format,
                                            destination, color->Red,
                                            color->Green, color->Blue);
                    destination += pixel_bytes;
                }

                if (image->Interlace) {
                    /* GIF stores rows as passes: 0/8, 4/8, 2/4, then 1/2. */
                    static const int interlace_starts[] = {0, 4, 2, 1};
                    static const int interlace_steps[] = {8, 8, 4, 2};

                    interlace_row += interlace_steps[interlace_pass];
                    while (interlace_row >= image->Height &&
                           ++interlace_pass < 4) {
                        interlace_row = interlace_starts[interlace_pass];
                    }
                }
            }

            gif_mem_free(row_buffer);
            row_buffer = NULL;

            if (restored_previous != 0) {
                uint32_t image_right = image_rect.left + image_rect.width;
                uint32_t image_bottom = image_rect.top + image_rect.height;
                uint32_t restored_right =
                    restored_rect.left + restored_rect.width;
                uint32_t restored_bottom =
                    restored_rect.top + restored_rect.height;
                uint32_t right = image_right > restored_right ? image_right
                                                               : restored_right;
                uint32_t bottom = image_bottom > restored_bottom
                                      ? image_bottom
                                      : restored_bottom;

                updated_rect.left = image_rect.left < restored_rect.left
                                        ? image_rect.left
                                        : restored_rect.left;
                updated_rect.top = image_rect.top < restored_rect.top
                                       ? image_rect.top
                                       : restored_rect.top;
                updated_rect.width = right - updated_rect.left;
                updated_rect.height = bottom - updated_rect.top;
            } else {
                updated_rect = image_rect;
            }

            out_frame->frame_index = decoder->frame_index;
            out_frame->delay_ms = decoder->pending_control.delay_ms;
            out_frame->image_left = image_rect.left;
            out_frame->image_top = image_rect.top;
            out_frame->image_width = image_rect.width;
            out_frame->image_height = image_rect.height;
            out_frame->updated_left = updated_rect.left;
            out_frame->updated_top = updated_rect.top;
            out_frame->updated_width = updated_rect.width;
            out_frame->updated_height = updated_rect.height;
            decoder->pending_disposal = GIF_PENDING_DISPOSAL_NONE;
            if (decoder->pending_control.disposal_mode == DISPOSE_BACKGROUND) {
                decoder->pending_disposal = GIF_PENDING_DISPOSAL_BACKGROUND;
                decoder->pending_disposal_rect = image_rect;
#if GIF_ENABLE_DISPOSAL_METHOD_3
            } else if (decoder->pending_control.disposal_mode ==
                       DISPOSE_PREVIOUS) {
                decoder->pending_disposal = GIF_PENDING_DISPOSAL_PREVIOUS;
                decoder->pending_disposal_rect = image_rect;
                decoder->pending_previous_pixels = saved_previous_pixels;
                saved_previous_pixels = NULL;
#endif
            }
            decoder->frame_index++;
            gif_decoder_reset_frame_control(&decoder->pending_control);
            return GIF_STATUS_OK;
        }
        case EXTENSION_RECORD_TYPE: {
            GifByteType *extension = NULL;
            int extension_code = 0;

            if (DGifGetExtension(decoder->gif, &extension_code, &extension) ==
                GIF_ERROR) {
                status = gif_decoder_map_error(decoder, decoder->gif->Error);
                goto fail;
            }

            if (extension_code == GRAPHICS_EXT_FUNC_CODE) {
                GraphicsControlBlock control;

                if (extension == NULL || extension[0] != 4 ||
                    (extension[1] & 0xe0U) != 0 ||
                    DGifExtensionToGCB((size_t)extension[0], extension + 1,
                                       &control) == GIF_ERROR) {
                    status = GIF_STATUS_INVALID_FORMAT;
                    goto fail;
                }
                if (control.UserInputFlag ||
#if GIF_ENABLE_DISPOSAL_METHOD_3
                    control.DisposalMode > DISPOSE_PREVIOUS) {
#else
                    control.DisposalMode > DISPOSE_BACKGROUND) {
#endif
                    status = GIF_STATUS_UNSUPPORTED_FEATURE;
                    goto fail;
                }

                decoder->pending_control.delay_ms =
                    (uint32_t)control.DelayTime * 10U;
                decoder->pending_control.transparent_color =
                    control.TransparentColor;
                decoder->pending_control.disposal_mode =
                    (uint8_t)control.DisposalMode;

                if (DGifGetExtensionNext(decoder->gif, &extension) ==
                    GIF_ERROR) {
                    status =
                        gif_decoder_map_error(decoder, decoder->gif->Error);
                    goto fail;
                }
                if (extension != NULL) {
                    status = GIF_STATUS_INVALID_FORMAT;
                    goto fail;
                }
            } else if (extension_code == PLAINTEXT_EXT_FUNC_CODE) {
                status = GIF_STATUS_UNSUPPORTED_FEATURE;
                goto fail;
            } else {
                do {
                    if (DGifGetExtensionNext(decoder->gif, &extension) ==
                        GIF_ERROR) {
                        status = gif_decoder_map_error(decoder,
                                                       decoder->gif->Error);
                        goto fail;
                    }
                } while (extension != NULL);
            }
            break;
        }
        case TERMINATE_RECORD_TYPE:
            decoder->stream_ended = 1;
            gif_decoder_clear_pending_disposal(decoder);
            return GIF_STATUS_END_OF_STREAM;
        default:
            status = GIF_STATUS_INVALID_FORMAT;
            goto fail;
        }
    }

fail:
    gif_mem_free(row_buffer);
#if GIF_ENABLE_DISPOSAL_METHOD_3
    gif_mem_free(saved_previous_pixels);
#endif
    gif_decoder_clear_pending_disposal(decoder);
    decoder->terminal_status = status;
    return status;
}

/** @copydoc gif_decoder_core_close */
void gif_decoder_core_close(GifDecoder *decoder) {
    if (decoder == NULL) {
        return;
    }

    gif_decoder_clear_pending_disposal(decoder);
    if (decoder->gif != NULL) {
        (void)DGifCloseFile(decoder->gif, NULL);
    }
    if (decoder->source_handle != NULL) {
        gif_porting_close(decoder->source_handle);
    }
    gif_mem_free(decoder);
}
