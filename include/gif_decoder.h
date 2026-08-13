/**
 * @file gif_decoder.h
 * @brief Public platform-independent API for the embedded GIF decoder.
 *
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

/** @brief Opaque decoder instance owned by the library. */
typedef struct GifDecoder GifDecoder;

/** @brief Status values returned by the public decoder API. */
typedef enum GifStatus {
    GIF_STATUS_OK = 0,                  /**< Operation completed successfully. */
    GIF_STATUS_END_OF_STREAM = 1,       /**< GIF trailer was reached. */
    GIF_STATUS_INVALID_ARGUMENT = 2,    /**< A required argument was invalid. */
    GIF_STATUS_OUT_OF_MEMORY = 3,       /**< A dynamic allocation failed. */
    GIF_STATUS_IO_ERROR = 4,            /**< The byte source reported an error. */
    GIF_STATUS_UNEXPECTED_EOF = 5,      /**< Input ended before data was complete. */
    GIF_STATUS_INVALID_FORMAT = 6,      /**< Input is not a valid supported GIF. */
    GIF_STATUS_UNSUPPORTED_FEATURE = 7, /**< GIF feature is not implemented yet. */
    GIF_STATUS_BUFFER_TOO_SMALL = 8,    /**< Output capacity or stride is too small. */
    GIF_STATUS_INTERNAL_ERROR = 9,      /**< Unexpected internal decoder failure. */
    GIF_STATUS_INVALID_STATE = 10       /**< Operation is invalid in current state. */
} GifStatus;

/**
 * @brief Application configuration required to select one GIF source.
 *
 * The application selects a resource but does not perform platform I/O. The
 * target's `gif_porting.c` implementation defines how the opaque identifier is
 * interpreted. For example, a FatFs port may treat it as a path string.
 */
typedef struct GifDecoderConfig {
    const void *source_identifier; /**< Resource interpreted by the port. */
} GifDecoderConfig;

/** @brief Logical-screen information available immediately after open. */
typedef struct GifStreamInfo {
    uint32_t canvas_width;          /**< Logical-screen width in pixels. */
    uint32_t canvas_height;         /**< Logical-screen height in pixels. */
    uint8_t background_color_index; /**< Global background palette index. */
    uint8_t color_resolution;       /**< GIF color resolution in bits. */
    uint8_t has_global_color_table; /**< Non-zero when a global table exists. */
} GifStreamInfo;

/** @brief Packed 24-bit pixel layouts supported by the compositor. */
typedef enum GifPixelFormat {
    GIF_PIXEL_RGB888 = 0, /**< Byte order: red, green, blue. */
    GIF_PIXEL_BGR888 = 1  /**< Byte order: blue, green, red. */
} GifPixelFormat;

/** @brief Caller-owned destination for fully composited canvas pixels. */
typedef struct GifOutputSurface {
    void *pixels; /**< Pixel storage that remains owned by the caller. */
    size_t capacity_bytes; /**< Accessible bytes beginning at `pixels`. */
    size_t stride_bytes;   /**< Byte distance between consecutive rows. */
    GifPixelFormat pixel_format; /**< Packed pixel layout used by the surface. */
} GifOutputSurface;

/**
 * @brief Metadata describing a frame and the canvas rectangle it updates.
 *
 * Frame indices are zero-based. Delay is the exact GIF centisecond value
 * converted to milliseconds; zero remains zero and timing policy belongs to
 * the application. The updated rectangle conservatively covers the image
 * rectangle even when transparent pixels remain unchanged and, when disposal
 * method 2 restores the preceding frame, the restored rectangle as well.
 */
typedef struct GifFrameInfo {
    uint32_t frame_index;    /**< Zero-based decoded frame number. */
    uint32_t delay_ms;       /**< GIF frame delay in milliseconds. */
    uint32_t image_left;     /**< Image rectangle left edge. */
    uint32_t image_top;      /**< Image rectangle top edge. */
    uint32_t image_width;    /**< Image rectangle width. */
    uint32_t image_height;   /**< Image rectangle height. */
    uint32_t updated_left;   /**< Updated canvas rectangle left edge. */
    uint32_t updated_top;    /**< Updated canvas rectangle top edge. */
    uint32_t updated_width;  /**< Updated canvas rectangle width. */
    uint32_t updated_height; /**< Updated canvas rectangle height. */
} GifFrameInfo;

/**
 * @brief Open a GIF stream and read its logical-screen descriptor.
 *
 * On failure, `*out_decoder` is set to `NULL` and `*out_stream` is cleared.
 *
 * @param[in] config          Platform-neutral source selection.
 * @param[out] out_decoder    Receives a newly allocated decoder handle.
 * @param[out] out_stream     Receives logical-screen information.
 * @return `GIF_STATUS_OK` on success, otherwise an argument, source, format,
 *         or allocation status.
 */
GifStatus gif_decoder_open(const GifDecoderConfig *config,
                           GifDecoder **out_decoder,
                           GifStreamInfo *out_stream);

/**
 * @brief Bind caller-owned pixel storage and initialize the canvas background.
 *
 * The descriptor is copied, but its pixel storage must remain valid until the
 * decoder is closed. Capacity must cover at least
 * `(canvas_height - 1) * stride_bytes + packed_canvas_row_bytes`.
 *
 * @param[in,out] decoder Decoder returned by `gif_decoder_open()`.
 * @param[in] surface     Output descriptor to validate and copy.
 * @return `GIF_STATUS_OK` on success or a validation/state status on failure.
 */
GifStatus gif_decoder_bind_output(GifDecoder *decoder,
                                  const GifOutputSurface *surface);

/**
 * @brief Decode the next image into the bound fully composited canvas.
 *
 * @param[in,out] decoder Decoder with a valid bound output surface.
 * @param[out] out_frame  Receives metadata for the decoded frame.
 * @return `GIF_STATUS_OK`, `GIF_STATUS_END_OF_STREAM`, or a sticky decode
 *         failure status.
 */
GifStatus gif_decoder_next_frame(GifDecoder *decoder,
                                 GifFrameInfo *out_frame);

/**
 * @brief Release a decoder and all library-owned resources.
 *
 * Passing `NULL` is allowed. Caller-owned input and output storage is never
 * released by this function.
 *
 * @param[in,out] decoder Decoder to release, or `NULL`.
 */
void gif_decoder_close(GifDecoder *decoder);

/**
 * @brief Return a static human-readable description of a public status.
 *
 * @param[in] status Status value to describe.
 * @return Pointer to a null-terminated static string.
 */
const char *gif_status_string(GifStatus status);

#ifdef __cplusplus
}
#endif

#endif /* GIF_DECODER_H */
