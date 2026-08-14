/**
 * @file fuzz_decoder.c
 * @brief Host-only libFuzzer entry point for the public decoder lifecycle.
 *
 * Every fuzz input is presented through the normal memory-backed port and is
 * decoded only through the installed public facade.  Besides the full input,
 * each invocation exercises deterministic prefixes and legal short-read
 * schedules so a single corpus mutation explores parser, extension, LZW, and
 * truncated-stream behaviour without a large configuration cross product.
 *
 * Copyright (c) 2026 Steven Zhu
 * SPDX-License-Identifier: MIT
 */

#include "gif_decoder.h"

#include "test_porting.h"

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

/** @brief Largest application framebuffer allocated for one non-normal input. */
#define GIF_FUZZ_MAX_FRAMEBUFFER_BYTES (4U * 1024U * 1024U)
/** @brief Bound on frames decoded from one input before yielding to libFuzzer. */
#define GIF_FUZZ_MAX_FRAME_STEPS 2048U
/** @brief Number of source/read variants exercised per generated input. */
#define GIF_FUZZ_VARIANT_COUNT 4U

/** @brief Legal port read chunk limits selected by the input-derived schedule. */
static const size_t gif_fuzz_chunk_limits[] = {0U, 1U, 2U, 7U, 31U, 255U};

/**
 * @brief Safely derive a contiguous RGB888 framebuffer size from stream data.
 *
 * The framebuffer remains application-owned, just as it would in an embedded
 * integration.  Rejecting unreasonable logical screens avoids turning a
 * non-normal header into an unbounded host allocation during a fuzz run.
 *
 * @param[in] stream Logical-screen descriptor returned by a successful open.
 * @param[out] out_stride Receives the packed RGB888 row stride.
 * @param[out] out_capacity Receives the required framebuffer capacity.
 * @return Non-zero when the framebuffer fits the harness resource boundary.
 */
static int gif_fuzz_framebuffer_requirements(const GifStreamInfo *stream,
                                             size_t *out_stride,
                                             size_t *out_capacity) {
    size_t width;
    size_t height;
    size_t stride;

    if (stream == NULL || out_stride == NULL || out_capacity == NULL ||
        stream->canvas_width == 0U || stream->canvas_height == 0U) {
        return 0;
    }

    width = (size_t)stream->canvas_width;
    height = (size_t)stream->canvas_height;
    if (width > SIZE_MAX / 3U) {
        return 0;
    }
    stride = width * 3U;
    if (height > SIZE_MAX / stride) {
        return 0;
    }
    if (height * stride > GIF_FUZZ_MAX_FRAMEBUFFER_BYTES) {
        return 0;
    }

    *out_stride = stride;
    *out_capacity = height * stride;
    return 1;
}

/**
 * @brief Run one complete public decoder lifecycle against one source variant.
 *
 * Arbitrary status values are valid fuzz outcomes.  The harness only treats a
 * ownership invariant as a stopping condition: every successful open must close
 * exactly its one port handle before this function returns.
 *
 * @param[in] data Input bytes supplied by libFuzzer.
 * @param[in] size Number of bytes visible through this source variant.
 * @param[in] variant Input-derived read schedule selector.
 */
static void gif_fuzz_decode_variant(const uint8_t *data,
                                    size_t size,
                                    unsigned int variant) {
    GifDecoderConfig config;
    GifDecoder *decoder = NULL;
    GifFrameInfo frame;
    GifOutputSurface surface;
    GifStatus status;
    GifStreamInfo stream;
    MemorySource source;
    uint8_t *framebuffer = NULL;
    size_t framebuffer_size;
    size_t stride;
    unsigned int frame_steps;

    source.data = data;
    source.size = size;
    source.offset = 0U;
    source.max_chunk =
        gif_fuzz_chunk_limits[variant %
                              (sizeof(gif_fuzz_chunk_limits) /
                               sizeof(gif_fuzz_chunk_limits[0]))];
    source.error_offset = 0U;
    source.read_calls = 0U;
    source.close_calls = 0U;
    source.inject_open_error = false;
    source.inject_error = false;
    source.eof_with_final_bytes = (variant == GIF_FUZZ_VARIANT_COUNT - 1U);

    config.source_identifier = &source;
    status = gif_decoder_open(&config, &decoder, &stream);
    if (status != GIF_STATUS_OK || decoder == NULL) {
        if (decoder != NULL) {
            gif_decoder_close(decoder);
        }
        if (source.close_calls > 1U) {
            abort();
        }
        return;
    }

    if (gif_fuzz_framebuffer_requirements(&stream, &stride,
                                          &framebuffer_size)) {
        framebuffer = (uint8_t *)malloc(framebuffer_size);
        if (framebuffer != NULL) {
            surface.pixels = framebuffer;
            surface.capacity_bytes = framebuffer_size;
            surface.stride_bytes = stride;
            surface.pixel_format = GIF_PIXEL_RGB888;
            status = gif_decoder_bind_output(decoder, &surface);
            if (status == GIF_STATUS_OK) {
                for (frame_steps = 0U;
                     frame_steps < GIF_FUZZ_MAX_FRAME_STEPS;
                     ++frame_steps) {
                    status = gif_decoder_next_frame(decoder, &frame);
                    if (status != GIF_STATUS_OK) {
                        break;
                    }
                }
            }
        }
    }

    gif_decoder_close(decoder);
    free(framebuffer);
    if (source.close_calls != 1U) {
        abort();
    }
}

/**
 * @brief libFuzzer callback receiving arbitrary byte sequences.
 *
 * @param[in] data Immutable fuzz input bytes.
 * @param[in] size Number of input bytes.
 * @return Always zero; instrumentation output and ownership aborts stop a run.
 */
int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    size_t prefix_size;
    unsigned int variant;

    if (data == NULL) {
        return 0;
    }

    for (variant = 0U; variant < GIF_FUZZ_VARIANT_COUNT; ++variant) {
        if (variant == 0U) {
            prefix_size = size;
        } else if (variant == 1U) {
            prefix_size = (size == 0U) ? 0U : size - 1U;
        } else if (variant == 2U) {
            prefix_size = (size == 0U) ? 0U : (size_t)data[0] % (size + 1U);
        } else {
            prefix_size = (size == 0U) ? 0U :
                          (size_t)data[size - 1U] % (size + 1U);
        }
        gif_fuzz_decode_variant(data, prefix_size, variant);
    }
    return 0;
}
