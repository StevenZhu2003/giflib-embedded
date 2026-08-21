/**
 * @file test_decoder.c
 * @brief Host-side tests for the platform-neutral GIF decoder facade.
 *
 * Copyright (c) 2026 Steven Zhu
 * SPDX-License-Identifier: MIT
 */

#include "gif_decoder.h"
#include "gif_config.h"

#include "test_porting.h"

#include <stdio.h>
#include <string.h>

/** @brief Number of failed checks observed by this test executable. */
static int failures;

#ifdef GIFLIB_TEST_ALLOC_TRACKING
/** @brief Return the number of allocations not yet released. */
size_t giflib_test_outstanding_allocations(void);

/** @brief Fail allocation calls after the requested successful-call count. */
void giflib_test_fail_allocation_after(size_t successful_allocations);

/** @brief Disable allocator fault injection. */
void giflib_test_disable_allocation_failure(void);
#endif

/** @brief Record a failed test condition without aborting the test process. */
#define CHECK(condition)                                                       \
    do {                                                                       \
        if (!(condition)) {                                                    \
            fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__,           \
                    __LINE__, #condition);                                     \
            failures++;                                                        \
        }                                                                      \
    } while (0)

/** @brief Minimal screen descriptor and global palette used by open tests. */
static const uint8_t gif_header_with_palette[] = {
    'G', 'I', 'F', '8', '9', 'a',
    0x02, 0x00, 0x03, 0x00,
    0x80, 0x01, 0x00,
    0x00, 0x00, 0x00,
    0xff, 0xff, 0xff,
};

/** @brief Complete two-pixel GIF using its global color table. */
static const uint8_t gif_two_pixel_global[] = {
    'G', 'I', 'F', '8', '9', 'a',
    0x02, 0x00, 0x01, 0x00, 0x80, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x11, 0x22, 0x33,
    0x2c,
    0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x01, 0x00, 0x00,
    0x02, 0x02, 0x44, 0x0a, 0x00,
    0x3b,
};

/** @brief Complete two-row GIF used to verify destination stride handling. */
static const uint8_t gif_two_rows_global[] = {
    'G', 'I', 'F', '8', '9', 'a',
    0x01, 0x00, 0x02, 0x00, 0x80, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x11, 0x22, 0x33,
    0x2c,
    0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x02, 0x00, 0x00,
    0x02, 0x02, 0x44, 0x0a, 0x00,
    0x3b,
};

/** @brief Complete two-pixel GIF using an image-local color table. */
static const uint8_t gif_two_pixel_local[] = {
    'G', 'I', 'F', '8', '9', 'a',
    0x02, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00,
    0x2c,
    0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x01, 0x00, 0x80,
    0x44, 0x55, 0x66, 0xaa, 0xbb, 0xcc,
    0x02, 0x02, 0x0c, 0x0a, 0x00,
    0x3b,
};

/** @brief GIF whose image rectangle covers only part of the logical canvas. */
static const uint8_t gif_partial_frame[] = {
    'G', 'I', 'F', '8', '9', 'a',
    0x03, 0x00, 0x01, 0x00, 0x80, 0x01, 0x00,
    0x00, 0x00, 0x00, 0x10, 0x20, 0x30,
    0x2c,
    0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x01, 0x00, 0x00,
    0x02, 0x02, 0x44, 0x01, 0x00,
    0x3b,
};

/** @brief Two-frame GIF using the supported keep-in-place disposal method. */
static const uint8_t gif_two_frames_disposal_one[] = {
    'G', 'I', 'F', '8', '9', 'a',
    0x02, 0x00, 0x01, 0x00, 0x80, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x11, 0x22, 0x33,
    0x21, 0xf9, 0x04, 0x04, 0x00, 0x00, 0x00, 0x00,
    0x2c,
    0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x01, 0x00, 0x00,
    0x02, 0x02, 0x4c, 0x01, 0x00,
    0x2c,
    0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x01, 0x00, 0x00,
    0x02, 0x02, 0x4c, 0x01, 0x00,
    0x3b,
};

/**
 * @brief Eight-row interlaced GIF covering every prescribed GIF pass.
 *
 * Source rows are stored in pass order 0, 4, 2, 6, 1, 3, 5, 7. Each two-pixel
 * row has a distinct palette-index pattern, allowing the test to verify that
 * the decoder writes it to the corresponding logical-screen row.
 */
static const uint8_t gif_interlaced[] = {
    'G', 'I', 'F', '8', '9', 'a',
    0x02, 0x00, 0x08, 0x00, 0x81, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x11, 0x00, 0x00,
    0x00, 0x22, 0x00, 0x00, 0x00, 0x33,
    0x2c,
    0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x08, 0x00, 0x40,
    0x02, 0x0d,
    0x0c, 0xc3, 0x50, 0x0c, 0x47, 0x31, 0x14,
    0x45, 0x71, 0x1c, 0xc3, 0x51, 0x05,
    0x00,
    0x3b,
};

/**
 * @brief Three-frame GIF combining interlace with composition features.
 *
 * The second image is a two-column, eight-row, interlaced local-palette
 * rectangle. Its transparent pixels retain the first frame's global-palette
 * background, and its disposal method 2 is applied when the third image
 * begins. This checks pass ordering independently of canvas composition.
 */
static const uint8_t gif_interlaced_composition[] = {
    'G', 'I', 'F', '8', '9', 'a',
    0x03, 0x00, 0x08, 0x00, 0x81, 0x01, 0x00,
    0x00, 0x00, 0x00, 0x11, 0x00, 0x00,
    0x00, 0x22, 0x00, 0x00, 0x00, 0x33,
    0x2c,
    0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x08, 0x00, 0x00,
    0x02, 0x13,
    0x0c, 0xc3, 0x30, 0x0c, 0xc3, 0x30, 0x0c, 0xc3, 0x30,
    0x0c, 0xc3, 0x30, 0x0c, 0xc3, 0x30, 0x0c, 0xc3, 0x30,
    0x05,
    0x00,
    0x21, 0xf9, 0x04, 0x09, 0x00, 0x00, 0x00, 0x00,
    0x2c,
    0x01, 0x00, 0x00, 0x00, 0x02, 0x00, 0x08, 0x00, 0xc1,
    0x00, 0x00, 0x00, 0x44, 0x55, 0x66,
    0x77, 0x88, 0x99, 0xaa, 0xbb, 0xcc,
    0x02, 0x0d,
    0x0c, 0x41, 0x71, 0x1c, 0xc1, 0x50, 0x04, 0x43,
    0x11, 0x1c, 0x43, 0x50, 0x05,
    0x00,
    0x2c,
    0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x01, 0x00, 0x00,
    0x02, 0x02, 0x54, 0x01, 0x00,
    0x3b,
};

/** @brief Three-frame GIF exercising delay, transparency, and GCE scope. */
static const uint8_t gif_three_frames_with_gce[] = {
    'G', 'I', 'F', '8', '9', 'a',
    0x02, 0x00, 0x01, 0x00, 0x81, 0x02, 0x00,
    0x00, 0x00, 0x00, 0x11, 0x22, 0x33,
    0x44, 0x55, 0x66, 0xff, 0xff, 0xff,
    0x21, 0xf9, 0x04, 0x05, 0x02, 0x00, 0x00, 0x00,
    0x2c,
    0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x01, 0x00, 0x00,
    0x02, 0x02, 0x44, 0x0a, 0x00,
    0x21, 0xf9, 0x04, 0x04, 0x03, 0x00, 0x00, 0x00,
    0x2c,
    0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x01, 0x00, 0x00,
    0x02, 0x02, 0x44, 0x01, 0x00,
    0x2c,
    0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x01, 0x00, 0x00,
    0x02, 0x02, 0x44, 0x01, 0x00,
    0x3b,
};

/** @brief GCE followed by a non-rendering comment before its target image. */
static const uint8_t gif_gce_before_comment[] = {
    'G', 'I', 'F', '8', '9', 'a',
    0x01, 0x00, 0x01, 0x00, 0x80, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x11, 0x22, 0x33,
    0x21, 0xf9, 0x04, 0x04, 0xff, 0xff, 0x00, 0x00,
    0x21, 0xfe, 0x03, 'g', 'i', 'f', 0x00,
    0x2c,
    0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x01, 0x00, 0x00,
    0x02, 0x02, 0x4c, 0x01, 0x00,
    0x3b,
};

/** @brief GIF whose GCE requests unsupported user-input interaction. */
static const uint8_t gif_with_user_input[] = {
    'G', 'I', 'F', '8', '9', 'a',
    0x01, 0x00, 0x01, 0x00, 0x80, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x11, 0x22, 0x33,
    0x21, 0xf9, 0x04, 0x02, 0x00, 0x00, 0x00, 0x00,
    0x2c,
    0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x01, 0x00, 0x00,
    0x02, 0x02, 0x44, 0x01, 0x00,
    0x3b,
};

/**
 * @brief Four-frame composition sequence covering GIF disposal modes 0/1/2.
 *
 * The sequence leaves the first global-palette pixel in place, restores two
 * successive partial rectangles to the global background, uses a local palette
 * for the second restored image, and ends with a transparent global-palette
 * image. It therefore also exercises conservative updated-rectangle reporting.
 */
static const uint8_t gif_disposal_composition[] = {
    'G', 'I', 'F', '8', '9', 'a',
    0x03, 0x00, 0x01, 0x00, 0x81, 0x00, 0x00,
    0x10, 0x20, 0x30, 0x11, 0x22, 0x33,
    0x44, 0x55, 0x66, 0xaa, 0xbb, 0xcc,
    0x21, 0xf9, 0x04, 0x04, 0x00, 0x00, 0x00, 0x00,
    0x2c,
    0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x01, 0x00, 0x00,
    0x02, 0x02, 0x4c, 0x01, 0x00,
    0x21, 0xf9, 0x04, 0x08, 0x00, 0x00, 0x00, 0x00,
    0x2c,
    0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x01, 0x00, 0x00,
    0x02, 0x02, 0x54, 0x01, 0x00,
    0x21, 0xf9, 0x04, 0x08, 0x00, 0x00, 0x00, 0x00,
    0x2c,
    0x02, 0x00, 0x00, 0x00, 0x01, 0x00, 0x01, 0x00, 0x80,
    0x00, 0x00, 0x00, 0x77, 0x88, 0x99,
    0x02, 0x02, 0x4c, 0x01, 0x00,
    0x21, 0xf9, 0x04, 0x01, 0x00, 0x00, 0x00, 0x00,
    0x2c,
    0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x01, 0x00, 0x00,
    0x02, 0x02, 0x44, 0x01, 0x00,
    0x3b,
};

#ifdef GIFLIB_TEST_ALLOC_TRACKING
/** @brief Disposal-2 stream that ends during the following image payload. */
static const uint8_t gif_disposal_two_truncated_next_image[] = {
    'G', 'I', 'F', '8', '9', 'a',
    0x02, 0x00, 0x01, 0x00, 0x80, 0x00, 0x00,
    0x10, 0x20, 0x30, 0x11, 0x22, 0x33,
    0x21, 0xf9, 0x04, 0x08, 0x00, 0x00, 0x00, 0x00,
    0x2c,
    0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x01, 0x00, 0x00,
    0x02, 0x02, 0x4c, 0x01, 0x00,
    0x2c,
    0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x01, 0x00, 0x00,
    0x02,
};
#endif

/** @brief Change one fixture GCE's packed field without duplicating its bytes. */
static int set_gce_disposal(uint8_t *data,
                            size_t size,
                            size_t gce_index,
                            uint8_t packed_field) {
    size_t index;

    for (index = 0U; index + 3U < size; index++) {
        if (data[index] == 0x21U && data[index + 1U] == 0xf9U &&
            data[index + 2U] == 0x04U) {
            if (gce_index == 0U) {
                data[index + 3U] = packed_field;
                return 1;
            }
            gce_index--;
        }
    }
    return 0;
}

/**
 * @brief Initialize an in-memory input source with default read behavior.
 *
 * @param[out] source Source object to initialize.
 * @param[in] data    Byte array exposed by the source.
 * @param[in] size    Number of valid bytes in @p data.
 */
static void memory_source_init(MemorySource *source,
                               const uint8_t *data,
                               size_t size) {
    memset(source, 0, sizeof(*source));
    source->data = data;
    source->size = size;
    source->max_chunk = size;
}

/**
 * @brief Open a decoder whose input is supplied by a MemorySource object.
 *
 * @param[in,out] source Source object selected through the public config.
 * @param[out] decoder   Receives the opened decoder on success.
 * @param[out] stream    Receives logical-screen metadata on success.
 * @return Decoder status returned by gif_decoder_open().
 */
static GifStatus open_source(MemorySource *source,
                             GifDecoder **decoder,
                             GifStreamInfo *stream) {
    GifDecoderConfig config;

    config.source_identifier = source;
    return gif_decoder_open(&config, decoder, stream);
}

/**
 * @brief Construct and bind an output surface for a facade test.
 *
 * @param[in,out] decoder Decoder instance to configure.
 * @param[out] pixels     Caller-owned destination storage.
 * @param[in] capacity_bytes Size of @p pixels in bytes.
 * @param[in] stride_bytes   Distance between destination rows in bytes.
 * @param[in] pixel_format   Requested destination byte order.
 * @return Decoder status returned by gif_decoder_bind_output().
 */
static GifStatus bind_output(GifDecoder *decoder,
                             void *pixels,
                             size_t capacity_bytes,
                             size_t stride_bytes,
                             GifPixelFormat pixel_format) {
    GifOutputSurface surface = {0};

    surface.pixels = pixels;
    surface.capacity_bytes = capacity_bytes;
    surface.stride_bytes = stride_bytes;
    surface.pixel_format = pixel_format;
    return gif_decoder_bind_output(decoder, &surface);
}

#if GIF_ENABLE_DISPOSAL_METHOD_3
/** @brief Bind output with one caller-owned Restore-to-Previous snapshot. */
static GifStatus bind_output_with_disposal3_snapshot(
    GifDecoder *decoder,
    void *pixels,
    size_t capacity_bytes,
    size_t stride_bytes,
    GifPixelFormat pixel_format,
    void *snapshot,
    size_t snapshot_capacity_bytes) {
    GifOutputSurface surface = {0};

    surface.pixels = pixels;
    surface.capacity_bytes = capacity_bytes;
    surface.stride_bytes = stride_bytes;
    surface.pixel_format = pixel_format;
    surface.disposal3_snapshot = snapshot;
    surface.disposal3_snapshot_capacity_bytes = snapshot_capacity_bytes;
    return gif_decoder_bind_output(decoder, &surface);
}
#endif

/** @brief Compare one RGB565 output pixel without assuming host byte order. */
static void check_rgb565_pixel(const uint8_t *pixels,
                               size_t offset,
                               uint8_t red,
                               uint8_t green,
                               uint8_t blue) {
    uint16_t actual;
    uint16_t expected =
        (uint16_t)(((uint16_t)(red >> 3U) << 11U) |
                   ((uint16_t)(green >> 2U) << 5U) |
                   (uint16_t)(blue >> 3U));

    memcpy(&actual, pixels + offset, sizeof(actual));
    CHECK(actual == expected);
}

/** @brief Verify open-time metadata obtained from a memory-backed source. */
static void test_open_memory_source(void) {
    MemorySource source;
    GifDecoder *decoder = NULL;
    GifStreamInfo stream;

    memory_source_init(&source, gif_header_with_palette,
                       sizeof(gif_header_with_palette));

    CHECK(open_source(&source, &decoder, &stream) == GIF_STATUS_OK);
    CHECK(decoder != NULL);
    CHECK(stream.canvas_width == 2);
    CHECK(stream.canvas_height == 3);
    CHECK(stream.background_color_index == 1);
    CHECK(stream.color_resolution == 1);
    CHECK(stream.has_global_color_table == 1);
    gif_decoder_close(decoder);
    CHECK(source.close_calls == 1);
}

/** @brief Verify that successful short reads are accumulated correctly. */
static void test_legal_short_reads(void) {
    MemorySource source;
    GifDecoder *decoder = NULL;
    GifStreamInfo stream;

    memory_source_init(&source, gif_header_with_palette,
                       sizeof(gif_header_with_palette));
    source.max_chunk = 1;

    CHECK(open_source(&source, &decoder, &stream) == GIF_STATUS_OK);
    CHECK(decoder != NULL);
    CHECK(stream.canvas_width == 2);
    CHECK(stream.canvas_height == 3);
    CHECK(source.read_calls == sizeof(gif_header_with_palette));
    gif_decoder_close(decoder);
}

/** @brief Accept a port read that returns final bytes together with EOF. */
static void test_final_bytes_with_eof(void) {
    MemorySource source;
    GifDecoder *decoder = NULL;
    GifStreamInfo stream;

    memory_source_init(&source, gif_header_with_palette,
                       sizeof(gif_header_with_palette));
    source.max_chunk = 1;
    source.eof_with_final_bytes = true;

    CHECK(open_source(&source, &decoder, &stream) == GIF_STATUS_OK);
    CHECK(decoder != NULL);
    gif_decoder_close(decoder);
}

/**
 * @brief Freeze source-terminal outcomes needed by the later burst adapter.
 *
 * A final trailer byte remains meaningful when it accompanies either EOF or
 * an I/O status: giflib has already consumed a complete GIF at that point.
 * In contrast, a zero-byte successful port result cannot make progress and
 * becomes the existing sticky public I/O result.
 */
static void test_terminal_read_contract_baseline(void) {
    MemorySource source;
    GifDecoder *decoder = NULL;
    GifStreamInfo stream;
    GifFrameInfo frame;
    uint8_t pixels[6];

    memory_source_init(&source, gif_two_pixel_global,
                       sizeof(gif_two_pixel_global));
    source.max_chunk = 1U;
    source.eof_with_final_bytes = true;
    CHECK(open_source(&source, &decoder, &stream) == GIF_STATUS_OK);
    if (decoder != NULL) {
        CHECK(bind_output(decoder, pixels, sizeof(pixels), sizeof(pixels),
                          GIF_PIXEL_RGB888) == GIF_STATUS_OK);
        CHECK(gif_decoder_next_frame(decoder, &frame) == GIF_STATUS_OK);
        CHECK(gif_decoder_next_frame(decoder, &frame) ==
              GIF_STATUS_END_OF_STREAM);
        CHECK(gif_decoder_next_frame(decoder, &frame) ==
              GIF_STATUS_END_OF_STREAM);
        gif_decoder_close(decoder);
        CHECK(source.close_calls == 1U);
    }

    decoder = NULL;
    memory_source_init(&source, gif_two_pixel_global,
                       sizeof(gif_two_pixel_global));
    source.max_chunk = 1U;
    source.inject_error = true;
    source.error_offset = sizeof(gif_two_pixel_global);
    CHECK(open_source(&source, &decoder, &stream) == GIF_STATUS_OK);
    if (decoder != NULL) {
        CHECK(bind_output(decoder, pixels, sizeof(pixels), sizeof(pixels),
                          GIF_PIXEL_RGB888) == GIF_STATUS_OK);
        CHECK(gif_decoder_next_frame(decoder, &frame) == GIF_STATUS_OK);
        CHECK(gif_decoder_next_frame(decoder, &frame) ==
              GIF_STATUS_END_OF_STREAM);
        CHECK(gif_decoder_next_frame(decoder, &frame) ==
              GIF_STATUS_END_OF_STREAM);
        gif_decoder_close(decoder);
        CHECK(source.close_calls == 1U);
    }

    decoder = NULL;
    memory_source_init(&source, gif_two_pixel_global,
                       sizeof(gif_two_pixel_global));
    source.max_chunk = 1U;
    source.inject_zero_ok = true;
    source.zero_ok_offset = sizeof(gif_header_with_palette);
    CHECK(open_source(&source, &decoder, &stream) == GIF_STATUS_OK);
    if (decoder != NULL) {
        CHECK(bind_output(decoder, pixels, sizeof(pixels), sizeof(pixels),
                          GIF_PIXEL_RGB888) == GIF_STATUS_OK);
        CHECK(gif_decoder_next_frame(decoder, &frame) == GIF_STATUS_IO_ERROR);
        CHECK(gif_decoder_next_frame(decoder, &frame) == GIF_STATUS_IO_ERROR);
        gif_decoder_close(decoder);
        CHECK(source.close_calls == 1U);
    }
}

/** @brief Map a platform source-open failure without issuing a close. */
static void test_port_open_error(void) {
    MemorySource source;
    GifDecoder *decoder = NULL;
    GifStreamInfo stream;

    memory_source_init(&source, gif_header_with_palette,
                       sizeof(gif_header_with_palette));
    source.inject_open_error = true;

    CHECK(open_source(&source, &decoder, &stream) == GIF_STATUS_IO_ERROR);
    CHECK(decoder == NULL);
    CHECK(source.read_calls == 0);
    CHECK(source.close_calls == 0);
}

/** @brief Map a truncated logical-screen descriptor to unexpected EOF. */
static void test_unexpected_eof(void) {
    MemorySource source;
    GifDecoder *decoder = NULL;
    GifStreamInfo stream;

    memory_source_init(&source, gif_header_with_palette, 7);

    CHECK(open_source(&source, &decoder, &stream) ==
          GIF_STATUS_UNEXPECTED_EOF);
    CHECK(decoder == NULL);
    CHECK(source.close_calls == 1);
}

/** @brief Preserve a port read I/O error during decoder open. */
static void test_injected_io_error(void) {
    MemorySource source;
    GifDecoder *decoder = NULL;
    GifStreamInfo stream;

    memory_source_init(&source, gif_header_with_palette,
                       sizeof(gif_header_with_palette));
    source.inject_error = true;
    source.error_offset = 8;

    CHECK(open_source(&source, &decoder, &stream) == GIF_STATUS_IO_ERROR);
    CHECK(decoder == NULL);
    CHECK(source.close_calls == 1);
}

/** @brief Reject an input whose GIF signature is malformed. */
static void test_malformed_header(void) {
    /** @brief Non-GIF header with otherwise plausible dimensions. */
    static const uint8_t malformed[] = {
        'N', 'O', 'T', '8', '9', 'a',
        0x01, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00,
    };
    MemorySource source;
    GifDecoder *decoder = NULL;
    GifStreamInfo stream;

    memory_source_init(&source, malformed, sizeof(malformed));

    CHECK(open_source(&source, &decoder, &stream) ==
          GIF_STATUS_INVALID_FORMAT);
    CHECK(decoder == NULL);
}

/** @brief Reject invalid facade arguments and tolerate closing NULL. */
static void test_invalid_arguments(void) {
    GifDecoderConfig config;
    GifDecoder *decoder = NULL;
    GifStreamInfo stream;

    memset(&config, 0, sizeof(config));
    CHECK(gif_decoder_open(NULL, &decoder, &stream) ==
          GIF_STATUS_INVALID_ARGUMENT);
    CHECK(gif_decoder_open(&config, &decoder, &stream) ==
          GIF_STATUS_INVALID_ARGUMENT);
    gif_decoder_close(NULL);
    CHECK(strcmp(gif_status_string(GIF_STATUS_IO_ERROR),
                 "input/output error") == 0);
}

/** @brief Exercise every open-path allocation failure and verify cleanup. */
static void test_oom_mapping(void) {
#ifdef GIFLIB_TEST_ALLOC_TRACKING
    MemorySource source;
    GifDecoder *decoder = NULL;
    GifStreamInfo stream;
    size_t allocations_before = giflib_test_outstanding_allocations();
    size_t failure_index;
    GifStatus status;

    for (failure_index = 0U; failure_index < 6U; failure_index++) {
        memory_source_init(&source, gif_header_with_palette,
                           sizeof(gif_header_with_palette));
        decoder = NULL;

        giflib_test_fail_allocation_after(failure_index);
        status = open_source(&source, &decoder, &stream);
        giflib_test_disable_allocation_failure();

        CHECK(status == GIF_STATUS_OUT_OF_MEMORY);
        CHECK(decoder == NULL);
        CHECK(source.close_calls == (failure_index == 0U ? 0U : 1U));
        CHECK(giflib_test_outstanding_allocations() == allocations_before);
    }
#endif
}

/** @brief Repeatedly open and close a stream without leaking allocations. */
static void test_repeated_open_close(void) {
    int iteration;
#ifdef GIFLIB_TEST_ALLOC_TRACKING
    size_t allocations_before = giflib_test_outstanding_allocations();
#endif

    for (iteration = 0; iteration < 1000; iteration++) {
        MemorySource source;
        GifDecoder *decoder = NULL;
        GifStreamInfo stream;

        memory_source_init(&source, gif_header_with_palette,
                           sizeof(gif_header_with_palette));
        CHECK(open_source(&source, &decoder, &stream) == GIF_STATUS_OK);
        gif_decoder_close(decoder);
        if (failures != 0) {
            break;
        }
    }

#ifdef GIFLIB_TEST_ALLOC_TRACKING
    CHECK(giflib_test_outstanding_allocations() == allocations_before);
#endif
}

/** @brief Validate output pointers, formats, strides, sizes, and state. */
static void test_output_surface_validation(void) {
    MemorySource source;
    GifDecoder *decoder = NULL;
    GifStreamInfo stream;
    GifFrameInfo frame;
    GifOutputSurface surface;
    uint8_t pixels[18];

    memory_source_init(&source, gif_header_with_palette,
                       sizeof(gif_header_with_palette));
    CHECK(open_source(&source, &decoder, &stream) == GIF_STATUS_OK);
    if (decoder == NULL) {
        return;
    }

    memset(&surface, 0, sizeof(surface));
    CHECK(gif_decoder_bind_output(decoder, NULL) ==
          GIF_STATUS_INVALID_ARGUMENT);
    CHECK(gif_decoder_bind_output(decoder, &surface) ==
          GIF_STATUS_INVALID_ARGUMENT);

    surface.pixels = pixels;
    surface.capacity_bytes = sizeof(pixels);
    surface.stride_bytes = 6;
    surface.pixel_format = (GifPixelFormat)99;
    CHECK(gif_decoder_bind_output(decoder, &surface) ==
          GIF_STATUS_INVALID_ARGUMENT);

    surface.pixel_format = GIF_PIXEL_RGB888;
    surface.stride_bytes = 5;
    CHECK(gif_decoder_bind_output(decoder, &surface) ==
          GIF_STATUS_BUFFER_TOO_SMALL);

    surface.stride_bytes = 6;
    surface.capacity_bytes = sizeof(pixels) - 1;
    CHECK(gif_decoder_bind_output(decoder, &surface) ==
          GIF_STATUS_BUFFER_TOO_SMALL);

    surface.capacity_bytes = (size_t)-1;
    surface.stride_bytes = (size_t)-1;
    CHECK(gif_decoder_bind_output(decoder, &surface) ==
          GIF_STATUS_BUFFER_TOO_SMALL);

    CHECK(gif_decoder_next_frame(decoder, &frame) ==
          GIF_STATUS_INVALID_STATE);

    surface.pixel_format = GIF_PIXEL_RGB565;
    surface.stride_bytes = 3;
    surface.capacity_bytes = sizeof(pixels);
    CHECK(gif_decoder_bind_output(decoder, &surface) ==
          GIF_STATUS_BUFFER_TOO_SMALL);

    surface.stride_bytes = 4;
    surface.capacity_bytes = 11;
    CHECK(gif_decoder_bind_output(decoder, &surface) ==
          GIF_STATUS_BUFFER_TOO_SMALL);

    surface.capacity_bytes = 12;
    CHECK(gif_decoder_bind_output(decoder, &surface) == GIF_STATUS_OK);
    gif_decoder_close(decoder);
}

/** @brief Decode RGB888 pixels while preserving destination padding. */
static void test_rgb888_and_stride(void) {
    MemorySource source;
    GifDecoder *decoder = NULL;
    GifStreamInfo stream;
    GifFrameInfo frame;
    uint8_t pixels[8];

    memset(pixels, 0xee, sizeof(pixels));
    memory_source_init(&source, gif_two_pixel_global,
                       sizeof(gif_two_pixel_global));
    CHECK(open_source(&source, &decoder, &stream) == GIF_STATUS_OK);
    if (decoder == NULL) {
        return;
    }

    CHECK(bind_output(decoder, pixels, sizeof(pixels), sizeof(pixels),
                      GIF_PIXEL_RGB888) == GIF_STATUS_OK);
    CHECK(pixels[6] == 0xee && pixels[7] == 0xee);
    CHECK(gif_decoder_next_frame(decoder, &frame) == GIF_STATUS_OK);
    CHECK(frame.frame_index == 0);
    CHECK(frame.delay_ms == 0);
    CHECK(frame.image_left == 0 && frame.image_top == 0);
    CHECK(frame.image_width == 2 && frame.image_height == 1);
    CHECK(frame.updated_left == 0 && frame.updated_top == 0);
    CHECK(frame.updated_width == 2 && frame.updated_height == 1);
    CHECK(pixels[0] == 0x00 && pixels[1] == 0x00 && pixels[2] == 0x00);
    CHECK(pixels[3] == 0x11 && pixels[4] == 0x22 && pixels[5] == 0x33);
    CHECK(pixels[6] == 0xee && pixels[7] == 0xee);
    CHECK(gif_decoder_bind_output(decoder, &(GifOutputSurface){
              pixels, sizeof(pixels), sizeof(pixels), GIF_PIXEL_RGB888,
              NULL, 0U}) ==
          GIF_STATUS_INVALID_STATE);
    CHECK(gif_decoder_next_frame(decoder, &frame) ==
          GIF_STATUS_END_OF_STREAM);
    CHECK(gif_decoder_next_frame(decoder, &frame) ==
          GIF_STATUS_END_OF_STREAM);
    gif_decoder_close(decoder);
}

/** @brief Decode a local color table into BGR888 byte order. */
static void test_bgr888_and_local_palette(void) {
    MemorySource source;
    GifDecoder *decoder = NULL;
    GifStreamInfo stream;
    GifFrameInfo frame;
    uint8_t pixels[6];

    memory_source_init(&source, gif_two_pixel_local,
                       sizeof(gif_two_pixel_local));
    CHECK(open_source(&source, &decoder, &stream) == GIF_STATUS_OK);
    if (decoder == NULL) {
        return;
    }

    CHECK(bind_output(decoder, pixels, sizeof(pixels), sizeof(pixels),
                      GIF_PIXEL_BGR888) == GIF_STATUS_OK);
    CHECK(gif_decoder_next_frame(decoder, &frame) == GIF_STATUS_OK);
    CHECK(pixels[0] == 0xcc && pixels[1] == 0xbb && pixels[2] == 0xaa);
    CHECK(pixels[3] == 0x66 && pixels[4] == 0x55 && pixels[5] == 0x44);
    gif_decoder_close(decoder);
}

/** @brief Decode RGB565 words while preserving byte-addressed row padding. */
static void test_rgb565_and_stride(void) {
    MemorySource source;
    GifDecoder *decoder = NULL;
    GifStreamInfo stream;
    GifFrameInfo frame;
    uint8_t pixels[6];

    memset(pixels, 0xee, sizeof(pixels));
    memory_source_init(&source, gif_two_pixel_global,
                       sizeof(gif_two_pixel_global));
    CHECK(open_source(&source, &decoder, &stream) == GIF_STATUS_OK);
    if (decoder == NULL) {
        return;
    }

    CHECK(bind_output(decoder, pixels, sizeof(pixels), sizeof(pixels),
                      GIF_PIXEL_RGB565) == GIF_STATUS_OK);
    check_rgb565_pixel(pixels, 0U, 0x00, 0x00, 0x00);
    check_rgb565_pixel(pixels, 2U, 0x00, 0x00, 0x00);
    CHECK(pixels[4] == 0xee && pixels[5] == 0xee);

    CHECK(gif_decoder_next_frame(decoder, &frame) == GIF_STATUS_OK);
    check_rgb565_pixel(pixels, 0U, 0x00, 0x00, 0x00);
    check_rgb565_pixel(pixels, 2U, 0x11, 0x22, 0x33);
    CHECK(pixels[4] == 0xee && pixels[5] == 0xee);
    gif_decoder_close(decoder);
}

/** @brief Advance rows using the caller-provided output stride. */
static void test_stride_row_advance(void) {
    MemorySource source;
    GifDecoder *decoder = NULL;
    GifStreamInfo stream;
    GifFrameInfo frame;
    uint8_t pixels[8];

    memset(pixels, 0xee, sizeof(pixels));
    memory_source_init(&source, gif_two_rows_global,
                       sizeof(gif_two_rows_global));
    CHECK(open_source(&source, &decoder, &stream) == GIF_STATUS_OK);
    if (decoder == NULL) {
        return;
    }

    CHECK(bind_output(decoder, pixels, sizeof(pixels), 5,
                      GIF_PIXEL_RGB888) == GIF_STATUS_OK);
    CHECK(gif_decoder_next_frame(decoder, &frame) == GIF_STATUS_OK);
    CHECK(pixels[0] == 0x00 && pixels[1] == 0x00 && pixels[2] == 0x00);
    CHECK(pixels[3] == 0xee && pixels[4] == 0xee);
    CHECK(pixels[5] == 0x11 && pixels[6] == 0x22 && pixels[7] == 0x33);
    gif_decoder_close(decoder);
}

/** @brief Decode all four GIF interlace passes into their logical row order. */
static void test_interlaced_row_order(void) {
    static const uint8_t expected_indices[8][2] = {
        {1, 1}, {2, 2}, {1, 3}, {2, 3},
        {1, 2}, {3, 1}, {2, 1}, {3, 2},
    };
    static const uint8_t palette[4][3] = {
        {0x00, 0x00, 0x00}, {0x11, 0x00, 0x00},
        {0x00, 0x22, 0x00}, {0x00, 0x00, 0x33},
    };
    MemorySource source;
    GifDecoder *decoder = NULL;
    GifStreamInfo stream;
    GifFrameInfo frame;
    uint8_t pixels[2 * 8 * 3];
    int row;
    int column;

    memory_source_init(&source, gif_interlaced, sizeof(gif_interlaced));
    CHECK(open_source(&source, &decoder, &stream) == GIF_STATUS_OK);
    if (decoder == NULL) {
        return;
    }

    CHECK(bind_output(decoder, pixels, sizeof(pixels), 2 * 3,
                      GIF_PIXEL_RGB888) == GIF_STATUS_OK);
    CHECK(gif_decoder_next_frame(decoder, &frame) == GIF_STATUS_OK);
    CHECK(frame.image_left == 0 && frame.image_top == 0);
    CHECK(frame.image_width == 2 && frame.image_height == 8);
    CHECK(frame.updated_left == 0 && frame.updated_top == 0);
    CHECK(frame.updated_width == 2 && frame.updated_height == 8);

    for (row = 0; row < 8; row++) {
        for (column = 0; column < 2; column++) {
            size_t pixel_offset = (size_t)(row * 2 + column) * 3U;
            uint8_t palette_index = expected_indices[row][column];

            CHECK(pixels[pixel_offset] == palette[palette_index][0]);
            CHECK(pixels[pixel_offset + 1U] == palette[palette_index][1]);
            CHECK(pixels[pixel_offset + 2U] == palette[palette_index][2]);
        }
    }
    CHECK(gif_decoder_next_frame(decoder, &frame) ==
          GIF_STATUS_END_OF_STREAM);
    gif_decoder_close(decoder);
}

/**
 * @brief Compose an interlaced local-palette rectangle into a persistent canvas.
 *
 * This verifies that pass row order, transparency, local palette selection,
 * pending disposal 2, and the reported updated rectangle remain consistent.
 */
static void test_interlaced_composition(void) {
    static const uint8_t expected_right[8][2][3] = {
        {{0x44, 0x55, 0x66}, {0x11, 0x00, 0x00}},
        {{0x11, 0x00, 0x00}, {0x44, 0x55, 0x66}},
        {{0xaa, 0xbb, 0xcc}, {0x11, 0x00, 0x00}},
        {{0x77, 0x88, 0x99}, {0x11, 0x00, 0x00}},
        {{0x77, 0x88, 0x99}, {0xaa, 0xbb, 0xcc}},
        {{0xaa, 0xbb, 0xcc}, {0x44, 0x55, 0x66}},
        {{0x44, 0x55, 0x66}, {0x77, 0x88, 0x99}},
        {{0x11, 0x00, 0x00}, {0x77, 0x88, 0x99}},
    };
    MemorySource source;
    GifDecoder *decoder = NULL;
    GifStreamInfo stream;
    GifFrameInfo frame;
    uint8_t pixels[3 * 8 * 3];
    int row;
    int column;

    memory_source_init(&source, gif_interlaced_composition,
                       sizeof(gif_interlaced_composition));
    CHECK(open_source(&source, &decoder, &stream) == GIF_STATUS_OK);
    if (decoder == NULL) {
        return;
    }

    CHECK(bind_output(decoder, pixels, sizeof(pixels), 3 * 3,
                      GIF_PIXEL_RGB888) == GIF_STATUS_OK);
    CHECK(gif_decoder_next_frame(decoder, &frame) == GIF_STATUS_OK);
    CHECK(frame.frame_index == 0 && frame.image_left == 0 &&
          frame.image_width == 3 && frame.image_height == 8);

    CHECK(gif_decoder_next_frame(decoder, &frame) == GIF_STATUS_OK);
    CHECK(frame.frame_index == 1 && frame.image_left == 1 &&
          frame.image_top == 0 && frame.image_width == 2 &&
          frame.image_height == 8);
    CHECK(frame.updated_left == 1 && frame.updated_top == 0 &&
          frame.updated_width == 2 && frame.updated_height == 8);
    for (row = 0; row < 8; row++) {
        size_t row_offset = (size_t)row * 3U * 3U;

        CHECK(pixels[row_offset] == 0x11 &&
              pixels[row_offset + 1U] == 0x00 &&
              pixels[row_offset + 2U] == 0x00);
        for (column = 0; column < 2; column++) {
            size_t pixel_offset = row_offset + (size_t)(column + 1) * 3U;

            CHECK(pixels[pixel_offset] == expected_right[row][column][0]);
            CHECK(pixels[pixel_offset + 1U] ==
                  expected_right[row][column][1]);
            CHECK(pixels[pixel_offset + 2U] ==
                  expected_right[row][column][2]);
        }
    }

    CHECK(gif_decoder_next_frame(decoder, &frame) == GIF_STATUS_OK);
    CHECK(frame.frame_index == 2 && frame.image_left == 0 &&
          frame.image_top == 0 && frame.image_width == 1 &&
          frame.image_height == 1);
    CHECK(frame.updated_left == 0 && frame.updated_top == 0 &&
          frame.updated_width == 3 && frame.updated_height == 8);
    for (row = 0; row < 8; row++) {
        for (column = 0; column < 3; column++) {
            size_t pixel_offset = (size_t)(row * 3 + column) * 3U;
            uint8_t expected_red = 0x11;
            uint8_t expected_green = 0x00;

            if (row == 0 && column == 0) {
                expected_red = 0x00;
                expected_green = 0x22;
            }
            CHECK(pixels[pixel_offset] == expected_red);
            CHECK(pixels[pixel_offset + 1U] == expected_green);
            CHECK(pixels[pixel_offset + 2U] == 0x00);
        }
    }

    CHECK(gif_decoder_next_frame(decoder, &frame) ==
          GIF_STATUS_END_OF_STREAM);
    gif_decoder_close(decoder);
}

/** @brief Initialize uncovered canvas pixels from the GIF background color. */
static void test_partial_frame_background(void) {
    MemorySource source;
    GifDecoder *decoder = NULL;
    GifStreamInfo stream;
    GifFrameInfo frame;
    uint8_t pixels[9];

    memory_source_init(&source, gif_partial_frame,
                       sizeof(gif_partial_frame));
    CHECK(open_source(&source, &decoder, &stream) == GIF_STATUS_OK);
    if (decoder == NULL) {
        return;
    }

    CHECK(bind_output(decoder, pixels, sizeof(pixels), sizeof(pixels),
                      GIF_PIXEL_RGB888) == GIF_STATUS_OK);
    CHECK(gif_decoder_next_frame(decoder, &frame) == GIF_STATUS_OK);
    CHECK(frame.image_left == 1 && frame.image_width == 1);
    CHECK(pixels[0] == 0x10 && pixels[1] == 0x20 && pixels[2] == 0x30);
    CHECK(pixels[3] == 0x00 && pixels[4] == 0x00 && pixels[5] == 0x00);
    CHECK(pixels[6] == 0x10 && pixels[7] == 0x20 && pixels[8] == 0x30);
    gif_decoder_close(decoder);
}

/** @brief Decode successive frames into one persistent output surface. */
static void test_two_streaming_frames(void) {
    MemorySource source;
    GifDecoder *decoder = NULL;
    GifStreamInfo stream;
    GifFrameInfo frame;
    uint8_t pixels[6];

    memory_source_init(&source, gif_two_frames_disposal_one,
                       sizeof(gif_two_frames_disposal_one));
    CHECK(open_source(&source, &decoder, &stream) == GIF_STATUS_OK);
    if (decoder == NULL) {
        return;
    }

    CHECK(bind_output(decoder, pixels, sizeof(pixels), sizeof(pixels),
                      GIF_PIXEL_RGB888) == GIF_STATUS_OK);
    CHECK(gif_decoder_next_frame(decoder, &frame) == GIF_STATUS_OK);
    CHECK(frame.frame_index == 0 && frame.image_left == 0);
    CHECK(pixels[0] == 0x11 && pixels[1] == 0x22 && pixels[2] == 0x33);
    CHECK(pixels[3] == 0x00 && pixels[4] == 0x00 && pixels[5] == 0x00);

    CHECK(gif_decoder_next_frame(decoder, &frame) == GIF_STATUS_OK);
    CHECK(frame.frame_index == 1 && frame.image_left == 1);
    CHECK(pixels[0] == 0x11 && pixels[1] == 0x22 && pixels[2] == 0x33);
    CHECK(pixels[3] == 0x11 && pixels[4] == 0x22 && pixels[5] == 0x33);
    CHECK(gif_decoder_next_frame(decoder, &frame) ==
          GIF_STATUS_END_OF_STREAM);
    gif_decoder_close(decoder);
}

/**
 * @brief Compose disposal 0/1/2 frames into one persistent canvas.
 *
 * Method 2 is deferred until the next image starts. Each affected frame
 * therefore reports a rectangle covering both the restored prior area and its
 * own image rectangle, including when the current image is transparent.
 */
static void test_disposal_composition(void) {
    MemorySource source;
    GifDecoder *decoder = NULL;
    GifStreamInfo stream;
    GifFrameInfo frame;
    uint8_t pixels[9];

    memory_source_init(&source, gif_disposal_composition,
                       sizeof(gif_disposal_composition));
    CHECK(open_source(&source, &decoder, &stream) == GIF_STATUS_OK);
    if (decoder == NULL) {
        return;
    }

    CHECK(bind_output(decoder, pixels, sizeof(pixels), sizeof(pixels),
                      GIF_PIXEL_RGB888) == GIF_STATUS_OK);
    CHECK(pixels[0] == 0x10 && pixels[1] == 0x20 && pixels[2] == 0x30);
    CHECK(pixels[3] == 0x10 && pixels[4] == 0x20 && pixels[5] == 0x30);
    CHECK(pixels[6] == 0x10 && pixels[7] == 0x20 && pixels[8] == 0x30);

    CHECK(gif_decoder_next_frame(decoder, &frame) == GIF_STATUS_OK);
    CHECK(frame.frame_index == 0 && frame.image_left == 0);
    CHECK(frame.updated_left == 0 && frame.updated_width == 1);
    CHECK(pixels[0] == 0x11 && pixels[1] == 0x22 && pixels[2] == 0x33);
    CHECK(pixels[3] == 0x10 && pixels[4] == 0x20 && pixels[5] == 0x30);
    CHECK(pixels[6] == 0x10 && pixels[7] == 0x20 && pixels[8] == 0x30);

    CHECK(gif_decoder_next_frame(decoder, &frame) == GIF_STATUS_OK);
    CHECK(frame.frame_index == 1 && frame.image_left == 1);
    CHECK(frame.updated_left == 1 && frame.updated_width == 1);
    CHECK(pixels[0] == 0x11 && pixels[1] == 0x22 && pixels[2] == 0x33);
    CHECK(pixels[3] == 0x44 && pixels[4] == 0x55 && pixels[5] == 0x66);
    CHECK(pixels[6] == 0x10 && pixels[7] == 0x20 && pixels[8] == 0x30);

    CHECK(gif_decoder_next_frame(decoder, &frame) == GIF_STATUS_OK);
    CHECK(frame.frame_index == 2 && frame.image_left == 2);
    CHECK(frame.updated_left == 1 && frame.updated_top == 0);
    CHECK(frame.updated_width == 2 && frame.updated_height == 1);
    CHECK(pixels[0] == 0x11 && pixels[1] == 0x22 && pixels[2] == 0x33);
    CHECK(pixels[3] == 0x10 && pixels[4] == 0x20 && pixels[5] == 0x30);
    CHECK(pixels[6] == 0x77 && pixels[7] == 0x88 && pixels[8] == 0x99);

    CHECK(gif_decoder_next_frame(decoder, &frame) == GIF_STATUS_OK);
    CHECK(frame.frame_index == 3 && frame.image_left == 0);
    CHECK(frame.updated_left == 0 && frame.updated_top == 0);
    CHECK(frame.updated_width == 3 && frame.updated_height == 1);
    CHECK(pixels[0] == 0x11 && pixels[1] == 0x22 && pixels[2] == 0x33);
    CHECK(pixels[3] == 0x10 && pixels[4] == 0x20 && pixels[5] == 0x30);
    CHECK(pixels[6] == 0x10 && pixels[7] == 0x20 && pixels[8] == 0x30);
    CHECK(gif_decoder_next_frame(decoder, &frame) ==
          GIF_STATUS_END_OF_STREAM);
    gif_decoder_close(decoder);
}

/**
 * @brief Compose global/local palettes, transparency, and disposal 0/1/2 in RGB565.
 *
 * This mirrors the RGB888 composition fixture so direct 16-bit composition
 * cannot regress background initialization, partial rectangles, or deferred
 * disposal handling.
 */
static void test_rgb565_disposal_composition(void) {
    MemorySource source;
    GifDecoder *decoder = NULL;
    GifStreamInfo stream;
    GifFrameInfo frame;
    uint8_t pixels[8];

    memset(pixels, 0xee, sizeof(pixels));
    memory_source_init(&source, gif_disposal_composition,
                       sizeof(gif_disposal_composition));
    CHECK(open_source(&source, &decoder, &stream) == GIF_STATUS_OK);
    if (decoder == NULL) {
        return;
    }

    CHECK(bind_output(decoder, pixels, sizeof(pixels), sizeof(pixels),
                      GIF_PIXEL_RGB565) == GIF_STATUS_OK);
    check_rgb565_pixel(pixels, 0U, 0x10, 0x20, 0x30);
    check_rgb565_pixel(pixels, 2U, 0x10, 0x20, 0x30);
    check_rgb565_pixel(pixels, 4U, 0x10, 0x20, 0x30);
    CHECK(pixels[6] == 0xee && pixels[7] == 0xee);

    CHECK(gif_decoder_next_frame(decoder, &frame) == GIF_STATUS_OK);
    CHECK(frame.updated_left == 0 && frame.updated_width == 1);
    check_rgb565_pixel(pixels, 0U, 0x11, 0x22, 0x33);
    check_rgb565_pixel(pixels, 2U, 0x10, 0x20, 0x30);
    check_rgb565_pixel(pixels, 4U, 0x10, 0x20, 0x30);

    CHECK(gif_decoder_next_frame(decoder, &frame) == GIF_STATUS_OK);
    CHECK(frame.updated_left == 1 && frame.updated_width == 1);
    check_rgb565_pixel(pixels, 0U, 0x11, 0x22, 0x33);
    check_rgb565_pixel(pixels, 2U, 0x44, 0x55, 0x66);
    check_rgb565_pixel(pixels, 4U, 0x10, 0x20, 0x30);

    CHECK(gif_decoder_next_frame(decoder, &frame) == GIF_STATUS_OK);
    CHECK(frame.updated_left == 1 && frame.updated_width == 2);
    check_rgb565_pixel(pixels, 0U, 0x11, 0x22, 0x33);
    check_rgb565_pixel(pixels, 2U, 0x10, 0x20, 0x30);
    check_rgb565_pixel(pixels, 4U, 0x77, 0x88, 0x99);

    CHECK(gif_decoder_next_frame(decoder, &frame) == GIF_STATUS_OK);
    CHECK(frame.updated_left == 0 && frame.updated_width == 3);
    check_rgb565_pixel(pixels, 0U, 0x11, 0x22, 0x33);
    check_rgb565_pixel(pixels, 2U, 0x10, 0x20, 0x30);
    check_rgb565_pixel(pixels, 4U, 0x10, 0x20, 0x30);
    CHECK(pixels[6] == 0xee && pixels[7] == 0xee);
    CHECK(gif_decoder_next_frame(decoder, &frame) ==
          GIF_STATUS_END_OF_STREAM);
    gif_decoder_close(decoder);
}

#if GIF_ENABLE_DISPOSAL_METHOD_3
/**
 * @brief Restore saved RGB888 rectangles across initial and consecutive mode-3 frames.
 */
static void test_disposal_previous_composition(void) {
    uint8_t previous[sizeof(gif_disposal_composition)];
    uint8_t snapshot[9];
    MemorySource source;
    GifDecoder *decoder = NULL;
    GifStreamInfo stream;
    GifFrameInfo frame;
    uint8_t pixels[9];

    memcpy(previous, gif_disposal_composition, sizeof(previous));
    CHECK(set_gce_disposal(previous, sizeof(previous), 1U, 0x0cU));
    CHECK(set_gce_disposal(previous, sizeof(previous), 2U, 0x0cU));
    memory_source_init(&source, previous, sizeof(previous));
    CHECK(open_source(&source, &decoder, &stream) == GIF_STATUS_OK);
    if (decoder == NULL) {
        return;
    }

    CHECK(bind_output_with_disposal3_snapshot(
              decoder, pixels, sizeof(pixels), sizeof(pixels),
              GIF_PIXEL_RGB888, snapshot, sizeof(snapshot)) == GIF_STATUS_OK);
    CHECK(gif_decoder_next_frame(decoder, &frame) == GIF_STATUS_OK);
    CHECK(pixels[0] == 0x11 && pixels[1] == 0x22 && pixels[2] == 0x33);
    CHECK(gif_decoder_next_frame(decoder, &frame) == GIF_STATUS_OK);
    CHECK(frame.updated_left == 1 && frame.updated_width == 1);
    CHECK(pixels[0] == 0x11 && pixels[1] == 0x22 && pixels[2] == 0x33);
    CHECK(pixels[3] == 0x44 && pixels[4] == 0x55 && pixels[5] == 0x66);
    CHECK(gif_decoder_next_frame(decoder, &frame) == GIF_STATUS_OK);
    CHECK(frame.updated_left == 1 && frame.updated_width == 2);
    CHECK(pixels[3] == 0x10 && pixels[4] == 0x20 && pixels[5] == 0x30);
    CHECK(pixels[6] == 0x77 && pixels[7] == 0x88 && pixels[8] == 0x99);
    CHECK(gif_decoder_next_frame(decoder, &frame) == GIF_STATUS_OK);
    CHECK(frame.updated_left == 0 && frame.updated_width == 3);
    CHECK(pixels[0] == 0x11 && pixels[1] == 0x22 && pixels[2] == 0x33);
    CHECK(pixels[3] == 0x10 && pixels[4] == 0x20 && pixels[5] == 0x30);
    CHECK(pixels[6] == 0x10 && pixels[7] == 0x20 && pixels[8] == 0x30);
    CHECK(gif_decoder_next_frame(decoder, &frame) ==
          GIF_STATUS_END_OF_STREAM);
    gif_decoder_close(decoder);

    memcpy(previous, gif_disposal_composition, sizeof(previous));
    CHECK(set_gce_disposal(previous, sizeof(previous), 0U, 0x0cU));
    decoder = NULL;
    memory_source_init(&source, previous, sizeof(previous));
    CHECK(open_source(&source, &decoder, &stream) == GIF_STATUS_OK);
    if (decoder == NULL) {
        return;
    }

    CHECK(bind_output_with_disposal3_snapshot(
              decoder, pixels, sizeof(pixels), sizeof(pixels),
              GIF_PIXEL_RGB888, snapshot, sizeof(snapshot)) == GIF_STATUS_OK);
    CHECK(gif_decoder_next_frame(decoder, &frame) == GIF_STATUS_OK);
    CHECK(pixels[0] == 0x11 && pixels[1] == 0x22 && pixels[2] == 0x33);
    CHECK(gif_decoder_next_frame(decoder, &frame) == GIF_STATUS_OK);
    CHECK(frame.updated_left == 0 && frame.updated_width == 2);
    CHECK(pixels[0] == 0x10 && pixels[1] == 0x20 && pixels[2] == 0x30);
    CHECK(pixels[3] == 0x44 && pixels[4] == 0x55 && pixels[5] == 0x66);
    CHECK(pixels[6] == 0x10 && pixels[7] == 0x20 && pixels[8] == 0x30);
    gif_decoder_close(decoder);

    /* Method 2 applies before the method-3 image captures its snapshot. */
    memcpy(previous, gif_disposal_composition, sizeof(previous));
    CHECK(set_gce_disposal(previous, sizeof(previous), 2U, 0x0cU));
    decoder = NULL;
    memory_source_init(&source, previous, sizeof(previous));
    CHECK(open_source(&source, &decoder, &stream) == GIF_STATUS_OK);
    if (decoder == NULL) {
        return;
    }

    CHECK(bind_output_with_disposal3_snapshot(
              decoder, pixels, sizeof(pixels), sizeof(pixels),
              GIF_PIXEL_RGB888, snapshot, sizeof(snapshot)) == GIF_STATUS_OK);
    CHECK(gif_decoder_next_frame(decoder, &frame) == GIF_STATUS_OK);
    CHECK(gif_decoder_next_frame(decoder, &frame) == GIF_STATUS_OK);
    CHECK(gif_decoder_next_frame(decoder, &frame) == GIF_STATUS_OK);
    CHECK(frame.updated_left == 1 && frame.updated_width == 2);
    CHECK(pixels[3] == 0x10 && pixels[4] == 0x20 && pixels[5] == 0x30);
    CHECK(pixels[6] == 0x77 && pixels[7] == 0x88 && pixels[8] == 0x99);
    CHECK(gif_decoder_next_frame(decoder, &frame) == GIF_STATUS_OK);
    CHECK(frame.updated_left == 0 && frame.updated_width == 3);
    CHECK(pixels[3] == 0x10 && pixels[4] == 0x20 && pixels[5] == 0x30);
    CHECK(pixels[6] == 0x10 && pixels[7] == 0x20 && pixels[8] == 0x30);
    gif_decoder_close(decoder);
}

/** @brief Restore saved rectangles directly into native-word RGB565 output. */
static void test_rgb565_disposal_previous_composition(void) {
    uint8_t previous[sizeof(gif_disposal_composition)];
    uint8_t snapshot[6];
    MemorySource source;
    GifDecoder *decoder = NULL;
    GifStreamInfo stream;
    GifFrameInfo frame;
    uint8_t pixels[8];

    memset(pixels, 0xee, sizeof(pixels));
    memcpy(previous, gif_disposal_composition, sizeof(previous));
    CHECK(set_gce_disposal(previous, sizeof(previous), 1U, 0x0cU));
    CHECK(set_gce_disposal(previous, sizeof(previous), 2U, 0x0cU));
    memory_source_init(&source, previous, sizeof(previous));
    CHECK(open_source(&source, &decoder, &stream) == GIF_STATUS_OK);
    if (decoder == NULL) {
        return;
    }

    CHECK(bind_output_with_disposal3_snapshot(
              decoder, pixels, sizeof(pixels), sizeof(pixels),
              GIF_PIXEL_RGB565, snapshot, sizeof(snapshot)) == GIF_STATUS_OK);
    CHECK(gif_decoder_next_frame(decoder, &frame) == GIF_STATUS_OK);
    CHECK(gif_decoder_next_frame(decoder, &frame) == GIF_STATUS_OK);
    check_rgb565_pixel(pixels, 0U, 0x11, 0x22, 0x33);
    check_rgb565_pixel(pixels, 2U, 0x44, 0x55, 0x66);
    check_rgb565_pixel(pixels, 4U, 0x10, 0x20, 0x30);
    CHECK(gif_decoder_next_frame(decoder, &frame) == GIF_STATUS_OK);
    CHECK(frame.updated_left == 1 && frame.updated_width == 2);
    check_rgb565_pixel(pixels, 0U, 0x11, 0x22, 0x33);
    check_rgb565_pixel(pixels, 2U, 0x10, 0x20, 0x30);
    check_rgb565_pixel(pixels, 4U, 0x77, 0x88, 0x99);
    CHECK(gif_decoder_next_frame(decoder, &frame) == GIF_STATUS_OK);
    CHECK(frame.updated_left == 0 && frame.updated_width == 3);
    check_rgb565_pixel(pixels, 0U, 0x11, 0x22, 0x33);
    check_rgb565_pixel(pixels, 2U, 0x10, 0x20, 0x30);
    check_rgb565_pixel(pixels, 4U, 0x10, 0x20, 0x30);
    CHECK(pixels[6] == 0xee && pixels[7] == 0xee);
    gif_decoder_close(decoder);
}

#ifdef GIFLIB_TEST_ALLOC_TRACKING
/** @brief Release a pending mode-3 snapshot when the following image fails. */
static void test_disposal_previous_failure_cleanup(void) {
    uint8_t truncated[sizeof(gif_disposal_two_truncated_next_image)];
    uint8_t interrupted[sizeof(gif_disposal_composition)];
    uint8_t snapshot[9];
    MemorySource source;
    GifDecoder *decoder = NULL;
    GifStreamInfo stream;
    GifFrameInfo frame;
    uint8_t pixels[9];
    size_t allocations_before = giflib_test_outstanding_allocations();

    memcpy(truncated, gif_disposal_two_truncated_next_image, sizeof(truncated));
    CHECK(set_gce_disposal(truncated, sizeof(truncated), 0U, 0x0cU));
    memory_source_init(&source, truncated, sizeof(truncated));
    CHECK(open_source(&source, &decoder, &stream) == GIF_STATUS_OK);
    if (decoder == NULL) {
        return;
    }

    CHECK(bind_output_with_disposal3_snapshot(
              decoder, pixels, sizeof(pixels), sizeof(pixels),
              GIF_PIXEL_RGB888, snapshot, sizeof(snapshot)) == GIF_STATUS_OK);
    CHECK(gif_decoder_next_frame(decoder, &frame) == GIF_STATUS_OK);
    CHECK(gif_decoder_next_frame(decoder, &frame) ==
          GIF_STATUS_UNEXPECTED_EOF);
    CHECK(gif_decoder_next_frame(decoder, &frame) ==
          GIF_STATUS_UNEXPECTED_EOF);
    gif_decoder_close(decoder);
    CHECK(source.close_calls == 1U);
    CHECK(giflib_test_outstanding_allocations() == allocations_before);

    memcpy(interrupted, gif_disposal_composition, sizeof(interrupted));
    CHECK(set_gce_disposal(interrupted, sizeof(interrupted), 0U, 0x0cU));
    decoder = NULL;
    memory_source_init(&source, interrupted, sizeof(interrupted));
    source.max_chunk = 1U;
    source.inject_error = true;
    source.error_offset = 48U;
    CHECK(open_source(&source, &decoder, &stream) == GIF_STATUS_OK);
    if (decoder == NULL) {
        return;
    }

    CHECK(bind_output_with_disposal3_snapshot(
              decoder, pixels, sizeof(pixels), sizeof(pixels),
              GIF_PIXEL_RGB888, snapshot, sizeof(snapshot)) == GIF_STATUS_OK);
    CHECK(gif_decoder_next_frame(decoder, &frame) == GIF_STATUS_OK);
    CHECK(gif_decoder_next_frame(decoder, &frame) == GIF_STATUS_IO_ERROR);
    CHECK(gif_decoder_next_frame(decoder, &frame) == GIF_STATUS_IO_ERROR);
    gif_decoder_close(decoder);
    CHECK(source.close_calls == 1U);
    CHECK(giflib_test_outstanding_allocations() == allocations_before);
}

/** @brief Repeat end-of-stream cleanup while a method-3 snapshot is pending. */
static void test_disposal_previous_lifecycle_cleanup(void) {
    uint8_t previous[sizeof(gif_disposal_composition)];
    size_t allocations_before = giflib_test_outstanding_allocations();
    unsigned int iteration;

    for (iteration = 0U; iteration < 4U; iteration++) {
        MemorySource source;
        GifDecoder *decoder = NULL;
        GifStreamInfo stream;
        GifFrameInfo frame;
        GifStatus status;
        uint8_t pixels[9];
        uint8_t snapshot[9];

        memcpy(previous, gif_disposal_composition, sizeof(previous));
        CHECK(set_gce_disposal(previous, sizeof(previous), 2U, 0x0cU));
        memory_source_init(&source, previous, sizeof(previous));
        CHECK(open_source(&source, &decoder, &stream) == GIF_STATUS_OK);
        if (decoder == NULL) {
            return;
        }

        CHECK(bind_output_with_disposal3_snapshot(
                  decoder, pixels, sizeof(pixels), sizeof(pixels),
                  GIF_PIXEL_RGB888, snapshot, sizeof(snapshot)) ==
              GIF_STATUS_OK);
        do {
            status = gif_decoder_next_frame(decoder, &frame);
        } while (status == GIF_STATUS_OK);
        CHECK(status == GIF_STATUS_END_OF_STREAM);
        gif_decoder_close(decoder);
        CHECK(source.close_calls == 1U);
        CHECK(giflib_test_outstanding_allocations() == allocations_before);
    }
}

/** @brief Reject missing or too-small caller snapshot storage for method 3. */
static void test_disposal_previous_snapshot_capacity(void) {
    uint8_t previous[sizeof(gif_disposal_composition)];
    uint8_t too_small_snapshot[2];
    uint8_t snapshot[9];
    MemorySource source;
    GifDecoder *decoder = NULL;
    GifStreamInfo stream;
    GifFrameInfo frame;
    uint8_t pixels[9];
    size_t allocations_before = giflib_test_outstanding_allocations();

    memcpy(previous, gif_disposal_composition, sizeof(previous));
    CHECK(set_gce_disposal(previous, sizeof(previous), 1U, 0x0cU));
    memory_source_init(&source, previous, sizeof(previous));
    CHECK(open_source(&source, &decoder, &stream) == GIF_STATUS_OK);
    if (decoder == NULL) {
        return;
    }

    CHECK(bind_output(decoder, pixels, sizeof(pixels), sizeof(pixels),
                      GIF_PIXEL_RGB888) == GIF_STATUS_OK);
    CHECK(gif_decoder_next_frame(decoder, &frame) == GIF_STATUS_OK);
    CHECK(gif_decoder_next_frame(decoder, &frame) ==
          GIF_STATUS_BUFFER_TOO_SMALL);
    CHECK(gif_decoder_next_frame(decoder, &frame) ==
          GIF_STATUS_BUFFER_TOO_SMALL);
    gif_decoder_close(decoder);
    CHECK(source.close_calls == 1U);
    CHECK(giflib_test_outstanding_allocations() == allocations_before);

    decoder = NULL;
    memory_source_init(&source, previous, sizeof(previous));
    CHECK(open_source(&source, &decoder, &stream) == GIF_STATUS_OK);
    if (decoder == NULL) {
        return;
    }
    CHECK(bind_output_with_disposal3_snapshot(
              decoder, pixels, sizeof(pixels), sizeof(pixels),
              GIF_PIXEL_RGB888, too_small_snapshot,
              sizeof(too_small_snapshot)) == GIF_STATUS_OK);
    CHECK(gif_decoder_next_frame(decoder, &frame) == GIF_STATUS_OK);
    CHECK(gif_decoder_next_frame(decoder, &frame) ==
          GIF_STATUS_BUFFER_TOO_SMALL);
    gif_decoder_close(decoder);
    CHECK(source.close_calls == 1U);
    CHECK(giflib_test_outstanding_allocations() == allocations_before);

    decoder = NULL;
    memory_source_init(&source, previous, sizeof(previous));
    CHECK(open_source(&source, &decoder, &stream) == GIF_STATUS_OK);
    if (decoder == NULL) {
        return;
    }
    CHECK(bind_output_with_disposal3_snapshot(
              decoder, pixels, sizeof(pixels), sizeof(pixels),
              GIF_PIXEL_RGB888, snapshot, sizeof(snapshot)) == GIF_STATUS_OK);
    CHECK(gif_decoder_next_frame(decoder, &frame) == GIF_STATUS_OK);
    /* Allow the row allocation only; method-3 capture must not allocate. */
    giflib_test_fail_allocation_after(1U);
    CHECK(gif_decoder_next_frame(decoder, &frame) == GIF_STATUS_OK);
    giflib_test_disable_allocation_failure();
    gif_decoder_close(decoder);
    CHECK(source.close_calls == 1U);
    CHECK(giflib_test_outstanding_allocations() == allocations_before);
}
#endif
#endif

/** @brief Release all allocations after disposal-2 decode failure paths. */
static void test_disposal_two_failure_cleanup(void) {
#ifdef GIFLIB_TEST_ALLOC_TRACKING
    MemorySource source;
    GifDecoder *decoder = NULL;
    GifStreamInfo stream;
    GifFrameInfo frame;
    uint8_t pixels[6];
    size_t allocations_before = giflib_test_outstanding_allocations();

    memory_source_init(&source, gif_disposal_two_truncated_next_image,
                       sizeof(gif_disposal_two_truncated_next_image));
    CHECK(open_source(&source, &decoder, &stream) == GIF_STATUS_OK);
    if (decoder == NULL) {
        return;
    }
    CHECK(bind_output(decoder, pixels, sizeof(pixels), sizeof(pixels),
                      GIF_PIXEL_RGB888) == GIF_STATUS_OK);
    CHECK(gif_decoder_next_frame(decoder, &frame) == GIF_STATUS_OK);
    CHECK(gif_decoder_next_frame(decoder, &frame) ==
          GIF_STATUS_UNEXPECTED_EOF);
    CHECK(gif_decoder_next_frame(decoder, &frame) ==
          GIF_STATUS_UNEXPECTED_EOF);
    gif_decoder_close(decoder);
    CHECK(source.close_calls == 1);
    CHECK(giflib_test_outstanding_allocations() == allocations_before);
#endif
}

/** @brief Apply GCE delay and transparency only to their associated frames. */
static void test_graphics_control_scope(void) {
    MemorySource source;
    GifDecoder *decoder = NULL;
    GifStreamInfo stream;
    GifFrameInfo frame;
    uint8_t pixels[6];

    memory_source_init(&source, gif_three_frames_with_gce,
                       sizeof(gif_three_frames_with_gce));
    CHECK(open_source(&source, &decoder, &stream) == GIF_STATUS_OK);
    if (decoder == NULL) {
        return;
    }

    CHECK(bind_output(decoder, pixels, sizeof(pixels), sizeof(pixels),
                      GIF_PIXEL_RGB888) == GIF_STATUS_OK);

    CHECK(gif_decoder_next_frame(decoder, &frame) == GIF_STATUS_OK);
    CHECK(frame.frame_index == 0 && frame.delay_ms == 20);
    CHECK(pixels[0] == 0x44 && pixels[1] == 0x55 && pixels[2] == 0x66);
    CHECK(pixels[3] == 0x11 && pixels[4] == 0x22 && pixels[5] == 0x33);

    CHECK(gif_decoder_next_frame(decoder, &frame) == GIF_STATUS_OK);
    CHECK(frame.frame_index == 1 && frame.delay_ms == 30);
    CHECK(pixels[0] == 0x00 && pixels[1] == 0x00 && pixels[2] == 0x00);
    CHECK(pixels[3] == 0x11 && pixels[4] == 0x22 && pixels[5] == 0x33);

    CHECK(gif_decoder_next_frame(decoder, &frame) == GIF_STATUS_OK);
    CHECK(frame.frame_index == 2 && frame.delay_ms == 0);
    CHECK(pixels[0] == 0x00 && pixels[1] == 0x00 && pixels[2] == 0x00);
    CHECK(pixels[3] == 0x00 && pixels[4] == 0x00 && pixels[5] == 0x00);
    CHECK(gif_decoder_next_frame(decoder, &frame) ==
          GIF_STATUS_END_OF_STREAM);

    gif_decoder_close(decoder);
}

/** @brief Preserve a maximum-delay GCE across a non-rendering extension. */
static void test_graphics_control_before_comment(void) {
    MemorySource source;
    GifDecoder *decoder = NULL;
    GifStreamInfo stream;
    GifFrameInfo frame;
    uint8_t pixels[3];

    memory_source_init(&source, gif_gce_before_comment,
                       sizeof(gif_gce_before_comment));
    CHECK(open_source(&source, &decoder, &stream) == GIF_STATUS_OK);
    if (decoder == NULL) {
        return;
    }

    CHECK(bind_output(decoder, pixels, sizeof(pixels), sizeof(pixels),
                      GIF_PIXEL_RGB888) == GIF_STATUS_OK);
    CHECK(gif_decoder_next_frame(decoder, &frame) == GIF_STATUS_OK);
    CHECK(frame.delay_ms == 655350U);
    CHECK(pixels[0] == 0x11 && pixels[1] == 0x22 && pixels[2] == 0x33);
    gif_decoder_close(decoder);
}

/** @brief Reject malformed GCE reserved bits and transparent indices. */
static void test_invalid_graphics_control(void) {
    uint8_t invalid[sizeof(gif_three_frames_with_gce)];
    MemorySource source;
    GifDecoder *decoder = NULL;
    GifStreamInfo stream;
    GifFrameInfo frame;
    uint8_t pixels[6];

    memcpy(invalid, gif_three_frames_with_gce, sizeof(invalid));
    invalid[28] |= 0x20; /* Set one reserved GCE packed-field bit. */
    memory_source_init(&source, invalid, sizeof(invalid));
    CHECK(open_source(&source, &decoder, &stream) == GIF_STATUS_OK);
    if (decoder != NULL) {
        CHECK(bind_output(decoder, pixels, sizeof(pixels), sizeof(pixels),
                          GIF_PIXEL_RGB888) == GIF_STATUS_OK);
        CHECK(gif_decoder_next_frame(decoder, &frame) ==
              GIF_STATUS_INVALID_FORMAT);
        gif_decoder_close(decoder);
    }

    decoder = NULL;
    memcpy(invalid, gif_three_frames_with_gce, sizeof(invalid));
    invalid[31] = 4; /* The four-entry palette has valid indices 0..3. */
    memory_source_init(&source, invalid, sizeof(invalid));
    CHECK(open_source(&source, &decoder, &stream) == GIF_STATUS_OK);
    if (decoder != NULL) {
        CHECK(bind_output(decoder, pixels, sizeof(pixels), sizeof(pixels),
                          GIF_PIXEL_RGB888) == GIF_STATUS_OK);
        CHECK(gif_decoder_next_frame(decoder, &frame) ==
              GIF_STATUS_INVALID_FORMAT);
        gif_decoder_close(decoder);
    }
}

/**
 * @brief Verify stable rejection of one syntactically valid unsupported GIF.
 *
 * @param[in] data Complete encoded GIF byte array.
 * @param[in] size Number of valid bytes in @p data.
 */
static void check_unsupported_gif(const uint8_t *data, size_t size) {
    MemorySource source;
    GifDecoder *decoder = NULL;
    GifStreamInfo stream;
    GifFrameInfo frame;
    uint8_t pixels[9];

    memory_source_init(&source, data, size);
    CHECK(open_source(&source, &decoder, &stream) == GIF_STATUS_OK);
    if (decoder == NULL) {
        return;
    }
    CHECK(bind_output(decoder, pixels, sizeof(pixels), sizeof(pixels),
                      GIF_PIXEL_RGB888) == GIF_STATUS_OK);
    CHECK(gif_decoder_next_frame(decoder, &frame) ==
          GIF_STATUS_UNSUPPORTED_FEATURE);
    CHECK(gif_decoder_next_frame(decoder, &frame) ==
          GIF_STATUS_UNSUPPORTED_FEATURE);
    gif_decoder_close(decoder);
}

/** @brief Reject user input and disabled optional disposal requests. */
static void test_unsupported_features(void) {
    check_unsupported_gif(gif_with_user_input,
                          sizeof(gif_with_user_input));
#if !GIF_ENABLE_DISPOSAL_METHOD_3
    {
        uint8_t unsupported[sizeof(gif_disposal_composition)];

        memcpy(unsupported, gif_disposal_composition, sizeof(unsupported));
        CHECK(set_gce_disposal(unsupported, sizeof(unsupported), 0U, 0x0cU));
        check_unsupported_gif(unsupported, sizeof(unsupported));
    }
#endif
}

/** @brief Distinguish port read I/O failure from EOF during frame decoding. */
static void test_frame_read_failures(void) {
    MemorySource source;
    GifDecoder *decoder = NULL;
    GifStreamInfo stream;
    GifFrameInfo frame;
    uint8_t pixels[6];

    memory_source_init(&source, gif_two_pixel_global,
                       sizeof(gif_two_pixel_global));
    source.inject_error = true;
    source.error_offset = 30;
    CHECK(open_source(&source, &decoder, &stream) == GIF_STATUS_OK);
    if (decoder != NULL) {
        CHECK(bind_output(decoder, pixels, sizeof(pixels), sizeof(pixels),
                          GIF_PIXEL_RGB888) == GIF_STATUS_OK);
        CHECK(gif_decoder_next_frame(decoder, &frame) == GIF_STATUS_IO_ERROR);
        gif_decoder_close(decoder);
    }

    decoder = NULL;
    memory_source_init(&source, gif_two_pixel_global, 30);
    CHECK(open_source(&source, &decoder, &stream) == GIF_STATUS_OK);
    if (decoder != NULL) {
        CHECK(bind_output(decoder, pixels, sizeof(pixels), sizeof(pixels),
                          GIF_PIXEL_RGB888) == GIF_STATUS_OK);
        CHECK(gif_decoder_next_frame(decoder, &frame) ==
              GIF_STATUS_UNEXPECTED_EOF);
        gif_decoder_close(decoder);
    }
}

/** @brief Preserve failure mapping and cleanup when an interlaced image fails. */
static void test_interlaced_failure_paths(void) {
    uint8_t malformed[sizeof(gif_interlaced)];
    MemorySource source;
    GifDecoder *decoder = NULL;
    GifStreamInfo stream;
    GifFrameInfo frame;
    uint8_t pixels[2 * 8 * 3];
#ifdef GIFLIB_TEST_ALLOC_TRACKING
    size_t allocations_before = giflib_test_outstanding_allocations();
#endif

    /* Retain the image data block length but remove its final compressed byte. */
    memory_source_init(&source, gif_interlaced, sizeof(gif_interlaced) - 4U);
    CHECK(open_source(&source, &decoder, &stream) == GIF_STATUS_OK);
    if (decoder != NULL) {
        CHECK(bind_output(decoder, pixels, sizeof(pixels), 2 * 3,
                          GIF_PIXEL_RGB888) == GIF_STATUS_OK);
        CHECK(gif_decoder_next_frame(decoder, &frame) ==
              GIF_STATUS_UNEXPECTED_EOF);
        CHECK(gif_decoder_next_frame(decoder, &frame) ==
              GIF_STATUS_UNEXPECTED_EOF);
        gif_decoder_close(decoder);
        CHECK(source.close_calls == 1);
    }

    memcpy(malformed, gif_interlaced, sizeof(malformed));
    malformed[35] = 9; /* GIF LZW minimum code size cannot exceed 8. */
    decoder = NULL;
    memory_source_init(&source, malformed, sizeof(malformed));
    CHECK(open_source(&source, &decoder, &stream) == GIF_STATUS_OK);
    if (decoder != NULL) {
        CHECK(bind_output(decoder, pixels, sizeof(pixels), 2 * 3,
                          GIF_PIXEL_RGB888) == GIF_STATUS_OK);
        CHECK(gif_decoder_next_frame(decoder, &frame) ==
              GIF_STATUS_INVALID_FORMAT);
        CHECK(gif_decoder_next_frame(decoder, &frame) ==
              GIF_STATUS_INVALID_FORMAT);
        gif_decoder_close(decoder);
        CHECK(source.close_calls == 1);
    }

#ifdef GIFLIB_TEST_ALLOC_TRACKING
    CHECK(giflib_test_outstanding_allocations() == allocations_before);
#endif
}

/** @brief Map invalid LZW parameters to an invalid-format status. */
static void test_malformed_image_data(void) {
    uint8_t malformed[sizeof(gif_two_pixel_global)];
    MemorySource source;
    GifDecoder *decoder = NULL;
    GifStreamInfo stream;
    GifFrameInfo frame;
    uint8_t pixels[6];

    memcpy(malformed, gif_two_pixel_global, sizeof(malformed));
    malformed[29] = 9; /* GIF LZW minimum code size cannot exceed 8. */
    memory_source_init(&source, malformed, sizeof(malformed));
    CHECK(open_source(&source, &decoder, &stream) == GIF_STATUS_OK);
    if (decoder == NULL) {
        return;
    }
    CHECK(bind_output(decoder, pixels, sizeof(pixels), sizeof(pixels),
                      GIF_PIXEL_RGB888) == GIF_STATUS_OK);
    CHECK(gif_decoder_next_frame(decoder, &frame) ==
          GIF_STATUS_INVALID_FORMAT);
    gif_decoder_close(decoder);
}

/** @brief Map and clean up an allocation failure during frame decoding. */
static void test_frame_oom_mapping(void) {
#ifdef GIFLIB_TEST_ALLOC_TRACKING
    MemorySource source;
    GifDecoder *decoder = NULL;
    GifStreamInfo stream;
    GifFrameInfo frame;
    uint8_t pixels[6];
    size_t allocations_before = giflib_test_outstanding_allocations();

    memory_source_init(&source, gif_two_pixel_global,
                       sizeof(gif_two_pixel_global));
    CHECK(open_source(&source, &decoder, &stream) == GIF_STATUS_OK);
    if (decoder == NULL) {
        return;
    }
    CHECK(bind_output(decoder, pixels, sizeof(pixels), sizeof(pixels),
                      GIF_PIXEL_RGB888) == GIF_STATUS_OK);

    giflib_test_fail_allocation_after(0);
    CHECK(gif_decoder_next_frame(decoder, &frame) ==
          GIF_STATUS_OUT_OF_MEMORY);
    giflib_test_disable_allocation_failure();
    gif_decoder_close(decoder);
    CHECK(giflib_test_outstanding_allocations() == allocations_before);
#endif
}

/** @brief Repeatedly decode a complete stream without leaking allocations. */
static void test_repeated_streaming_decode(void) {
    int iteration;
#ifdef GIFLIB_TEST_ALLOC_TRACKING
    size_t allocations_before = giflib_test_outstanding_allocations();
#endif

    for (iteration = 0; iteration < 1000; iteration++) {
        MemorySource source;
        GifDecoder *decoder = NULL;
        GifStreamInfo stream;
        GifFrameInfo frame;
        uint8_t pixels[6];

        memory_source_init(&source, gif_two_pixel_global,
                           sizeof(gif_two_pixel_global));
        CHECK(open_source(&source, &decoder, &stream) == GIF_STATUS_OK);
        if (decoder == NULL) {
            break;
        }
        CHECK(bind_output(decoder, pixels, sizeof(pixels), sizeof(pixels),
                          GIF_PIXEL_RGB888) == GIF_STATUS_OK);
        CHECK(gif_decoder_next_frame(decoder, &frame) == GIF_STATUS_OK);
        CHECK(gif_decoder_next_frame(decoder, &frame) ==
              GIF_STATUS_END_OF_STREAM);
        gif_decoder_close(decoder);
        if (failures != 0) {
            break;
        }
    }

#ifdef GIFLIB_TEST_ALLOC_TRACKING
    CHECK(giflib_test_outstanding_allocations() == allocations_before);
#endif
}

/**
 * @brief Run all facade tests and report their aggregate result.
 *
 * @return Zero when every check passes, otherwise one.
 */
int main(void) {
    test_open_memory_source();
    test_legal_short_reads();
    test_final_bytes_with_eof();
    test_terminal_read_contract_baseline();
    test_port_open_error();
    test_unexpected_eof();
    test_injected_io_error();
    test_malformed_header();
    test_invalid_arguments();
    test_oom_mapping();
    test_repeated_open_close();
    test_output_surface_validation();
    test_rgb888_and_stride();
    test_bgr888_and_local_palette();
    test_rgb565_and_stride();
    test_stride_row_advance();
    test_interlaced_row_order();
    test_interlaced_composition();
    test_partial_frame_background();
    test_two_streaming_frames();
    test_disposal_composition();
    test_rgb565_disposal_composition();
#if GIF_ENABLE_DISPOSAL_METHOD_3
    test_disposal_previous_composition();
    test_rgb565_disposal_previous_composition();
#ifdef GIFLIB_TEST_ALLOC_TRACKING
    test_disposal_previous_failure_cleanup();
    test_disposal_previous_lifecycle_cleanup();
    test_disposal_previous_snapshot_capacity();
#endif
#endif
    test_disposal_two_failure_cleanup();
    test_graphics_control_scope();
    test_graphics_control_before_comment();
    test_invalid_graphics_control();
    test_unsupported_features();
    test_frame_read_failures();
    test_interlaced_failure_paths();
    test_malformed_image_data();
    test_frame_oom_mapping();
    test_repeated_streaming_decode();

    if (failures != 0) {
        fprintf(stderr, "%d facade check(s) failed\n", failures);
        return 1;
    }

    puts("gif decoder facade tests passed");
    return 0;
}
