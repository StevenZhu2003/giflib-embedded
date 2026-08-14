/**
 * @file test_compatibility.c
 * @brief Opt-in structural compatibility test for the local GIF corpus.
 *
 * This executable never places corpus data in the repository. It preloads one
 * locally acquired file into application-owned host memory, then uses the
 * public decoder facade and the normal forward-only test port.
 *
 * Copyright (c) 2026 Steven Zhu
 * SPDX-License-Identifier: MIT
 */

#if defined(_MSC_VER)
#define _CRT_SECURE_NO_WARNINGS
#endif

#include "gif_decoder.h"

#include "test_porting.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/** @brief Maximum frames accepted from a corpus case before treating it as stuck. */
#define COMPATIBILITY_MAX_FRAMES 512U
/** @brief Number of files in the pinned initial local compatibility corpus. */
#define COMPATIBILITY_CASE_COUNT 39U

/** @brief Count of structural checks that failed in this executable. */
static int failures;

/** @brief Corpus root received from CTest rather than compiled into this binary. */
static const char *compatibility_corpus_dir;

#ifdef GIFLIB_TEST_ALLOC_TRACKING
/** @brief Return the number of PRIVATE-test allocations still live. */
size_t giflib_test_outstanding_allocations(void);
#endif

/** @brief Return tracked allocation count where this backend exposes it. */
static size_t outstanding_allocations(void) {
#ifdef GIFLIB_TEST_ALLOC_TRACKING
    return giflib_test_outstanding_allocations();
#else
    return 0U;
#endif
}

/** @brief Immutable expected outcome for one locally retained corpus file. */
typedef struct CompatibilityCase {
    const char *relative_path; /**< Path relative to the configured corpus root. */
    const char *classification; /**< Project classification frozen for Stage 8. */
    GifStatus open_status; /**< Required result from gif_decoder_open(). */
    GifStatus bind_status; /**< Required result from bind after a successful open. */
    GifStatus terminal_status; /**< Required terminal decode result after binding. */
    uint32_t frame_count; /**< Required number of successfully emitted frames. */
    uint32_t canvas_width; /**< Required logical-screen width. */
    uint32_t canvas_height; /**< Required logical-screen height. */
} CompatibilityCase;

/** @brief Independently reviewed RGB888 canvas hash for one emitted frame. */
typedef struct CompatibilityPixelOracle {
    const char *relative_path; /**< Path relative to the configured corpus root. */
    uint32_t frame_index; /**< Zero-based emitted frame index. */
    uint64_t fnv1a64; /**< FNV-1a hash of the contiguous RGB888 canvas. */
} CompatibilityPixelOracle;

/** @brief Frozen classifications and structural outcomes for all 39 corpus files. */
static const CompatibilityCase compatibility_cases[] = {
    {"valid/1x1.gif", "supported-valid", GIF_STATUS_OK, GIF_STATUS_OK, GIF_STATUS_END_OF_STREAM, 1U, 1U, 1U},
    {"valid/2color.gif", "supported-valid", GIF_STATUS_OK, GIF_STATUS_OK, GIF_STATUS_END_OF_STREAM, 1U, 4U, 4U},
    {"valid/anim_10frame.gif", "supported-valid", GIF_STATUS_OK, GIF_STATUS_OK, GIF_STATUS_END_OF_STREAM, 10U, 8U, 8U},
    {"valid/anim_2frame.gif", "supported-valid", GIF_STATUS_OK, GIF_STATUS_OK, GIF_STATUS_END_OF_STREAM, 2U, 8U, 8U},
    {"valid/anim_3frame_rgb.gif", "supported-valid", GIF_STATUS_OK, GIF_STATUS_OK, GIF_STATUS_END_OF_STREAM, 3U, 8U, 8U},
    {"valid/delay_0.gif", "supported-valid", GIF_STATUS_OK, GIF_STATUS_OK, GIF_STATUS_END_OF_STREAM, 2U, 4U, 4U},
    {"valid/delay_10ms.gif", "supported-valid", GIF_STATUS_OK, GIF_STATUS_OK, GIF_STATUS_END_OF_STREAM, 2U, 4U, 4U},
    {"valid/delay_1s.gif", "supported-valid", GIF_STATUS_OK, GIF_STATUS_OK, GIF_STATUS_END_OF_STREAM, 2U, 4U, 4U},
    {"valid/dispose_background.gif", "supported-valid", GIF_STATUS_OK, GIF_STATUS_OK, GIF_STATUS_END_OF_STREAM, 2U, 8U, 8U},
    {"valid/dispose_none.gif", "supported-valid", GIF_STATUS_OK, GIF_STATUS_OK, GIF_STATUS_END_OF_STREAM, 2U, 8U, 8U},
    {"valid/dispose_previous.gif", "deliberately-unsupported", GIF_STATUS_OK, GIF_STATUS_OK, GIF_STATUS_UNSUPPORTED_FEATURE, 0U, 8U, 8U},
    {"valid/dispose_unspecified.gif", "supported-valid", GIF_STATUS_OK, GIF_STATUS_OK, GIF_STATUS_END_OF_STREAM, 2U, 8U, 8U},
    {"valid/global_ct_only.gif", "supported-valid", GIF_STATUS_OK, GIF_STATUS_OK, GIF_STATUS_END_OF_STREAM, 2U, 4U, 4U},
    {"valid/local_ct.gif", "supported-valid", GIF_STATUS_OK, GIF_STATUS_OK, GIF_STATUS_END_OF_STREAM, 2U, 4U, 4U},
    {"valid/loop_3.gif", "supported-valid", GIF_STATUS_OK, GIF_STATUS_OK, GIF_STATUS_END_OF_STREAM, 2U, 4U, 4U},
    {"valid/loop_infinite.gif", "supported-valid", GIF_STATUS_OK, GIF_STATUS_OK, GIF_STATUS_END_OF_STREAM, 2U, 4U, 4U},
    {"valid/loop_once.gif", "supported-valid", GIF_STATUS_OK, GIF_STATUS_OK, GIF_STATUS_END_OF_STREAM, 2U, 4U, 4U},
    {"valid/mixed_ct.gif", "supported-valid", GIF_STATUS_OK, GIF_STATUS_OK, GIF_STATUS_END_OF_STREAM, 3U, 4U, 4U},
    {"valid/no_loop_ext.gif", "supported-valid", GIF_STATUS_OK, GIF_STATUS_OK, GIF_STATUS_END_OF_STREAM, 2U, 4U, 4U},
    {"valid/overlapping_frames.gif", "supported-valid", GIF_STATUS_OK, GIF_STATUS_OK, GIF_STATUS_END_OF_STREAM, 2U, 16U, 16U},
    {"valid/small_frame_big_canvas.gif", "supported-valid", GIF_STATUS_OK, GIF_STATUS_OK, GIF_STATUS_END_OF_STREAM, 1U, 16U, 16U},
    {"valid/static_256colors.gif", "supported-valid", GIF_STATUS_OK, GIF_STATUS_OK, GIF_STATUS_END_OF_STREAM, 1U, 16U, 16U},
    {"valid/static_4x4_red.gif", "supported-valid", GIF_STATUS_OK, GIF_STATUS_OK, GIF_STATUS_END_OF_STREAM, 1U, 4U, 4U},
    {"valid/static_8x8_palette.gif", "supported-valid", GIF_STATUS_OK, GIF_STATUS_OK, GIF_STATUS_END_OF_STREAM, 1U, 8U, 8U},
    {"valid/static_interlaced.gif", "supported-valid", GIF_STATUS_OK, GIF_STATUS_OK, GIF_STATUS_END_OF_STREAM, 1U, 8U, 8U},
    {"valid/transparent_bg.gif", "supported-valid", GIF_STATUS_OK, GIF_STATUS_OK, GIF_STATUS_END_OF_STREAM, 1U, 8U, 8U},
    {"valid/transparent_frame.gif", "supported-valid", GIF_STATUS_OK, GIF_STATUS_OK, GIF_STATUS_END_OF_STREAM, 2U, 8U, 8U},
    {"valid/variable_delay.gif", "supported-valid", GIF_STATUS_OK, GIF_STATUS_OK, GIF_STATUS_END_OF_STREAM, 4U, 4U, 4U},
    {"invalid/bad_lzw_code.gif", "malformed", GIF_STATUS_OK, GIF_STATUS_OK, GIF_STATUS_INVALID_FORMAT, 0U, 4U, 4U},
    {"invalid/bad_magic.gif", "forward-version-compatibility", GIF_STATUS_OK, GIF_STATUS_OK, GIF_STATUS_END_OF_STREAM, 1U, 4U, 4U},
    {"invalid/empty.gif", "malformed", GIF_STATUS_UNEXPECTED_EOF, GIF_STATUS_INVALID_STATE, GIF_STATUS_INVALID_STATE, 0U, 0U, 0U},
    {"invalid/no_trailer.gif", "malformed", GIF_STATUS_OK, GIF_STATUS_OK, GIF_STATUS_UNEXPECTED_EOF, 1U, 4U, 4U},
    {"invalid/truncated_header.gif", "malformed", GIF_STATUS_UNEXPECTED_EOF, GIF_STATUS_INVALID_STATE, GIF_STATUS_INVALID_STATE, 0U, 0U, 0U},
    {"invalid/truncated_lzw.gif", "malformed", GIF_STATUS_OK, GIF_STATUS_OK, GIF_STATUS_UNEXPECTED_EOF, 0U, 8U, 8U},
    {"invalid/zero_dimensions.gif", "malformed", GIF_STATUS_OK, GIF_STATUS_INVALID_FORMAT, GIF_STATUS_INVALID_STATE, 0U, 0U, 0U},
    {"edge-cases/comment_ext.gif", "supported-valid", GIF_STATUS_OK, GIF_STATUS_OK, GIF_STATUS_END_OF_STREAM, 1U, 4U, 4U},
    {"edge-cases/gif87a.gif", "supported-valid", GIF_STATUS_OK, GIF_STATUS_OK, GIF_STATUS_END_OF_STREAM, 1U, 4U, 4U},
    {"edge-cases/large_palette_small_image.gif", "supported-valid", GIF_STATUS_OK, GIF_STATUS_OK, GIF_STATUS_END_OF_STREAM, 1U, 4U, 4U},
    {"edge-cases/plain_text_ext.gif", "deliberately-unsupported", GIF_STATUS_OK, GIF_STATUS_OK, GIF_STATUS_UNSUPPORTED_FEATURE, 1U, 64U, 16U},
};

/**
 * @brief Exact-canvas oracle subset generated independently with Pillow 12.3.0.
 *
 * The disposable generator is `_TEMP/compatibility_oracle/generate_oracles.py`.
 * It is not part of this target; only these reviewed constants are retained.
 */
static const CompatibilityPixelOracle compatibility_pixel_oracles[] = {
    {"valid/static_interlaced.gif", 0U, UINT64_C(0x623db8499df41915)},
    {"valid/small_frame_big_canvas.gif", 0U, UINT64_C(0xe83a574741348ed5)},
    {"valid/overlapping_frames.gif", 0U, UINT64_C(0x246dd1e0d7eb05e5)},
    {"valid/overlapping_frames.gif", 1U, UINT64_C(0xd137fffd9614a075)},
    {"valid/transparent_frame.gif", 0U, UINT64_C(0x0b6a23e02628ad0e5)},
    {"valid/transparent_frame.gif", 1U, UINT64_C(0x0b6a23e02628ad0e5)},
    {"valid/dispose_background.gif", 0U, UINT64_C(0x3eb9df3741da0225)},
    {"valid/dispose_background.gif", 1U, UINT64_C(0x208fde6708d79525)},
};

/** @brief Report one failed structural check with its corpus case and test phase. */
static void check_case(int condition,
                       const CompatibilityCase *test_case,
                       const char *phase,
                       const char *expression) {
    if (!condition) {
        fprintf(stderr, "%s [%s, %s]: %s\n", test_case->relative_path,
                test_case->classification, phase, expression);
        failures++;
    }
}

/** @brief Return the manifest entry having @p relative_path, or NULL. */
static const CompatibilityCase *find_case(const char *relative_path) {
    size_t index;

    for (index = 0U; index < sizeof(compatibility_cases) /
                                      sizeof(compatibility_cases[0]);
         index++) {
        if (strcmp(compatibility_cases[index].relative_path, relative_path) ==
            0) {
            return &compatibility_cases[index];
        }
    }
    return NULL;
}

/** @brief Calculate a compact test-only FNV-1a fingerprint over RGB888 bytes. */
static uint64_t fnv1a64_bytes(const uint8_t *data, size_t size) {
    uint64_t hash = UINT64_C(0xcbf29ce484222325);
    size_t index;

    for (index = 0U; index < size; index++) {
        hash ^= data[index];
        hash *= UINT64_C(0x100000001b3);
    }
    return hash;
}

/** @brief Check a canvas only when the current frame has a reviewed oracle. */
static void check_pixel_oracle(const CompatibilityCase *test_case,
                               uint32_t frame_index,
                               const GifOutputSurface *surface,
                               const char *phase) {
    size_t index;

    for (index = 0U;
         index < sizeof(compatibility_pixel_oracles) /
                     sizeof(compatibility_pixel_oracles[0]);
         index++) {
        const CompatibilityPixelOracle *oracle =
            &compatibility_pixel_oracles[index];

        if (oracle->frame_index == frame_index &&
            strcmp(oracle->relative_path, test_case->relative_path) == 0) {
            check_case(fnv1a64_bytes((const uint8_t *)surface->pixels,
                                     surface->capacity_bytes) == oracle->fnv1a64,
                       test_case, phase,
                       "RGB888 canvas hash must match independent oracle");
            return;
        }
    }
}

/** @brief Read one local corpus file into application-owned host memory. */
static int load_case_bytes(const CompatibilityCase *test_case,
                           uint8_t **out_data,
                           size_t *out_size) {
    char path[1024];
    FILE *file;
    long length;
    uint8_t *data;

    *out_data = NULL;
    *out_size = 0U;
    if (snprintf(path, sizeof(path), "%s/%s", compatibility_corpus_dir,
                 test_case->relative_path) < 0) {
        return 0;
    }
    file = fopen(path, "rb");
    if (file == NULL || fseek(file, 0L, SEEK_END) != 0 ||
        (length = ftell(file)) < 0 || fseek(file, 0L, SEEK_SET) != 0) {
        if (file != NULL) {
            fclose(file);
        }
        return 0;
    }

    data = (uint8_t *)malloc(length == 0L ? 1U : (size_t)length);
    if (data == NULL) {
        fclose(file);
        return 0;
    }
    if (length != 0L &&
        fread(data, 1U, (size_t)length, file) != (size_t)length) {
        free(data);
        fclose(file);
        return 0;
    }
    fclose(file);
    *out_data = data;
    *out_size = (size_t)length;
    return 1;
}

/** @brief Configure the shared test port with one immutable memory stream. */
static void initialise_source(MemorySource *source,
                              const uint8_t *data,
                              size_t size,
                              size_t max_chunk) {
    memset(source, 0, sizeof(*source));
    source->data = data;
    source->size = size;
    source->max_chunk = max_chunk;
}

/** @brief Allocate caller-owned RGB888 storage for one observed stream. */
static uint8_t *allocate_surface(const GifStreamInfo *stream,
                                 GifOutputSurface *surface) {
    size_t row_bytes;
    size_t capacity;
    uint8_t *pixels;

    memset(surface, 0, sizeof(*surface));
    surface->pixel_format = GIF_PIXEL_RGB888;
    if (stream->canvas_width == 0U || stream->canvas_height == 0U) {
        pixels = (uint8_t *)malloc(1U);
        if (pixels != NULL) {
            surface->pixels = pixels;
            surface->capacity_bytes = 1U;
        }
        return pixels;
    }
    if ((size_t)stream->canvas_width > SIZE_MAX / 3U) {
        return NULL;
    }
    row_bytes = (size_t)stream->canvas_width * 3U;
    if ((size_t)stream->canvas_height > SIZE_MAX / row_bytes) {
        return NULL;
    }
    capacity = row_bytes * (size_t)stream->canvas_height;
    pixels = (uint8_t *)malloc(capacity);
    if (pixels != NULL) {
        memset(pixels, 0xa5, capacity);
        surface->pixels = pixels;
        surface->capacity_bytes = capacity;
        surface->stride_bytes = row_bytes;
    }
    return pixels;
}

/** @brief Check generic public frame geometry without defining pixel oracles. */
static void check_frame_geometry(const CompatibilityCase *test_case,
                                 const GifFrameInfo *frame,
                                 uint32_t expected_index,
                                 const char *phase) {
    check_case(frame->frame_index == expected_index, test_case, phase,
               "frame index must be consecutive");
    check_case(frame->image_width != 0U && frame->image_height != 0U,
               test_case, phase, "image rectangle must be non-empty");
    check_case(frame->image_left <= test_case->canvas_width &&
                   frame->image_width <=
                       test_case->canvas_width - frame->image_left &&
                   frame->image_top <= test_case->canvas_height &&
                   frame->image_height <=
                       test_case->canvas_height - frame->image_top,
               test_case, phase, "image rectangle must fit the canvas");
    check_case(frame->updated_width != 0U && frame->updated_height != 0U,
               test_case, phase, "updated rectangle must be non-empty");
    check_case(frame->updated_left <= test_case->canvas_width &&
                   frame->updated_width <=
                       test_case->canvas_width - frame->updated_left &&
                   frame->updated_top <= test_case->canvas_height &&
                   frame->updated_height <=
                       test_case->canvas_height - frame->updated_top,
               test_case, phase, "updated rectangle must fit the canvas");
}

/** @brief Run one manifest case under a selected legal port read schedule. */
static void run_case(const CompatibilityCase *test_case,
                     const char *phase,
                     size_t max_chunk,
                     int eof_with_final_bytes) {
    uint8_t *data;
    size_t data_size;
    MemorySource source;
    GifDecoderConfig config;
    GifDecoder *decoder = NULL;
    GifStreamInfo stream;
    GifOutputSurface surface;
    GifFrameInfo frame;
    GifStatus status;
    uint8_t *pixels = NULL;
    uint32_t frame_count = 0U;
    size_t allocations_before = outstanding_allocations();

    if (!load_case_bytes(test_case, &data, &data_size)) {
        check_case(0, test_case, phase, "must load local corpus file");
        return;
    }
    initialise_source(&source, data, data_size, max_chunk);
    source.eof_with_final_bytes = eof_with_final_bytes != 0;
    config.source_identifier = &source;
    memset(&stream, 0, sizeof(stream));

    status = gif_decoder_open(&config, &decoder, &stream);
    check_case(status == test_case->open_status, test_case, phase,
               "open status must match manifest");
    if (status == GIF_STATUS_OK) {
        check_case(decoder != NULL, test_case, phase,
                   "successful open must return a decoder");
        check_case(stream.canvas_width == test_case->canvas_width &&
                       stream.canvas_height == test_case->canvas_height,
                   test_case, phase, "stream canvas must match manifest");

        pixels = allocate_surface(&stream, &surface);
        check_case(pixels != NULL, test_case, phase,
                   "test must allocate caller-owned surface");
        if (pixels != NULL) {
            status = gif_decoder_bind_output(decoder, &surface);
            check_case(status == test_case->bind_status, test_case, phase,
                       "bind status must match manifest");
            if (status == GIF_STATUS_OK) {
                do {
                    status = gif_decoder_next_frame(decoder, &frame);
                    if (status == GIF_STATUS_OK) {
                        check_frame_geometry(test_case, &frame, frame_count,
                                             phase);
                        check_pixel_oracle(test_case, frame_count, &surface,
                                           phase);
                        frame_count++;
                    }
                } while (status == GIF_STATUS_OK &&
                         frame_count < COMPATIBILITY_MAX_FRAMES);
                check_case(frame_count < COMPATIBILITY_MAX_FRAMES, test_case,
                           phase, "decoder must reach a terminal status");
                check_case(frame_count == test_case->frame_count, test_case,
                           phase, "frame count must match manifest");
                check_case(status == test_case->terminal_status, test_case,
                           phase, "terminal status must match manifest");
            }
        }
        gif_decoder_close(decoder);
    } else {
        check_case(decoder == NULL, test_case, phase,
                   "failed open must not return a decoder");
    }
    free(pixels);
    check_case(source.close_calls == 1U, test_case, phase,
               "every port-opened source must close exactly once");
    check_case(outstanding_allocations() == allocations_before,
               test_case, phase, "decoder and port allocations must balance");
    free(data);
}

/** @brief Run a selected source-I/O fault at a named parser progress boundary. */
static void run_injected_io_case(const CompatibilityCase *test_case,
                                 const char *phase,
                                 size_t error_offset,
                                 GifStatus expected_open,
                                 GifStatus expected_terminal) {
    uint8_t *data;
    size_t data_size;
    MemorySource source;
    GifDecoderConfig config;
    GifDecoder *decoder = NULL;
    GifStreamInfo stream;
    GifOutputSurface surface;
    GifFrameInfo frame;
    GifStatus status;
    uint8_t *pixels = NULL;
    size_t allocations_before = outstanding_allocations();

    if (!load_case_bytes(test_case, &data, &data_size)) {
        check_case(0, test_case, phase, "must load local corpus file");
        return;
    }
    initialise_source(&source, data, data_size, 0U);
    source.inject_error = true;
    source.error_offset = error_offset;
    config.source_identifier = &source;
    memset(&stream, 0, sizeof(stream));

    status = gif_decoder_open(&config, &decoder, &stream);
    check_case(status == expected_open, test_case, phase,
               "injected-I/O open status must match expectation");
    if (status == GIF_STATUS_OK) {
        pixels = allocate_surface(&stream, &surface);
        check_case(pixels != NULL, test_case, phase,
                   "test must allocate caller-owned surface");
        if (pixels != NULL) {
            status = gif_decoder_bind_output(decoder, &surface);
            check_case(status == GIF_STATUS_OK, test_case, phase,
                       "injected-I/O case must bind output");
            if (status == GIF_STATUS_OK) {
                do {
                    status = gif_decoder_next_frame(decoder, &frame);
                } while (status == GIF_STATUS_OK);
                check_case(status == expected_terminal, test_case, phase,
                           "injected-I/O terminal status must match expectation");
            }
        }
        gif_decoder_close(decoder);
    } else {
        check_case(decoder == NULL, test_case, phase,
                   "failed open must not return a decoder");
    }
    free(pixels);
    check_case(source.close_calls == 1U, test_case, phase,
               "injected-I/O source must close exactly once");
    check_case(outstanding_allocations() == allocations_before,
               test_case, phase, "injected-I/O allocations must balance");
    free(data);
}

/** @brief Exercise the complete 39-file normal-read structural baseline. */
static void run_normal_baseline(void) {
    size_t index;

    for (index = 0U; index < sizeof(compatibility_cases) /
                                      sizeof(compatibility_cases[0]);
         index++) {
        run_case(&compatibility_cases[index], "normal", 0U, 0);
    }
}

/** @brief Exercise all error cases plus one representative per supported family. */
static void run_one_byte_matrix(void) {
    static const char *const selected[] = {
        "valid/1x1.gif", "valid/mixed_ct.gif", "valid/static_interlaced.gif",
        "valid/dispose_background.gif", "valid/transparent_frame.gif",
        "valid/anim_10frame.gif", "edge-cases/comment_ext.gif",
        "edge-cases/gif87a.gif", "invalid/bad_magic.gif",
        "valid/dispose_previous.gif", "edge-cases/plain_text_ext.gif",
        "invalid/bad_lzw_code.gif", "invalid/empty.gif",
        "invalid/no_trailer.gif", "invalid/truncated_header.gif",
        "invalid/truncated_lzw.gif", "invalid/zero_dimensions.gif",
    };
    size_t index;

    for (index = 0U; index < sizeof(selected) / sizeof(selected[0]); index++) {
        const CompatibilityCase *test_case = find_case(selected[index]);

        if (test_case == NULL) {
            fprintf(stderr, "internal manifest selection missing: %s\n",
                    selected[index]);
            failures++;
        } else {
            run_case(test_case, "one-byte-read", 1U, 0);
        }
    }
}

/** @brief Exercise one additional practical short-read size for core feature families. */
static void run_practical_short_reads(void) {
    static const char *const selected[] = {
        "valid/static_256colors.gif", "valid/anim_10frame.gif",
        "valid/static_interlaced.gif", "valid/dispose_background.gif",
    };
    size_t index;

    for (index = 0U; index < sizeof(selected) / sizeof(selected[0]); index++) {
        run_case(find_case(selected[index]), "seven-byte-read", 7U, 0);
    }
}

/** @brief Check legal final-byte EOF reporting for selected complete streams. */
static void run_final_byte_eof_cases(void) {
    static const char *const selected[] = {
        "valid/1x1.gif", "valid/anim_10frame.gif",
        "valid/static_interlaced.gif", "valid/dispose_background.gif",
    };
    size_t index;

    for (index = 0U; index < sizeof(selected) / sizeof(selected[0]); index++) {
        run_case(find_case(selected[index]), "final-byte-eof", 1U, 1);
    }
}

/** @brief Repeat representative success, malformed, and unsupported lifecycles. */
static void run_lifecycle_repeats(void) {
    static const char *const selected[] = {
        "valid/anim_10frame.gif", "valid/static_interlaced.gif",
        "valid/dispose_background.gif", "invalid/bad_lzw_code.gif",
        "invalid/no_trailer.gif", "invalid/truncated_lzw.gif",
        "invalid/zero_dimensions.gif", "valid/dispose_previous.gif",
        "edge-cases/plain_text_ext.gif",
    };
    size_t index;
    unsigned int repeat;

    for (index = 0U; index < sizeof(selected) / sizeof(selected[0]); index++) {
        const CompatibilityCase *test_case = find_case(selected[index]);

        for (repeat = 0U; repeat < 4U; repeat++) {
            run_case(test_case, "repeated-lifecycle", 0U, 0);
        }
    }
}

/** @brief Exercise four named parser-progress I/O fault locations without a cross product. */
static void run_injected_io_cases(void) {
    run_injected_io_case(find_case("valid/1x1.gif"), "I/O-header", 3U,
                         GIF_STATUS_IO_ERROR, GIF_STATUS_INVALID_STATE);
    run_injected_io_case(find_case("valid/anim_10frame.gif"), "I/O-record", 70U,
                         GIF_STATUS_OK, GIF_STATUS_IO_ERROR);
    run_injected_io_case(find_case("valid/static_interlaced.gif"),
                         "I/O-image-data", 42U, GIF_STATUS_OK,
                         GIF_STATUS_IO_ERROR);
    run_injected_io_case(find_case("valid/dispose_background.gif"),
                         "I/O-later-frame", 65U, GIF_STATUS_OK,
                          GIF_STATUS_IO_ERROR);
}

/** @brief Exercise a small cross-backend smoke subset without matrix expansion. */
static void run_backend_smoke(void) {
    static const char *const selected[] = {
        "valid/anim_10frame.gif", "valid/static_interlaced.gif",
        "valid/dispose_background.gif", "invalid/bad_lzw_code.gif",
        "invalid/truncated_lzw.gif", "invalid/zero_dimensions.gif",
    };
    size_t index;

    for (index = 0U; index < sizeof(selected) / sizeof(selected[0]); index++) {
        run_case(find_case(selected[index]), "backend-smoke", 0U, 0);
    }
    run_case(find_case("valid/static_interlaced.gif"),
             "backend-smoke-one-byte", 1U, 0);
    run_case(find_case("invalid/truncated_lzw.gif"),
             "backend-smoke-one-byte", 1U, 0);
}

/** @brief Run the bounded Stage 8 local compatibility matrix. */
int main(int argc, char *argv[]) {
    int backend_smoke = 0;

    if (argc != 2 && (argc != 3 || strcmp(argv[2], "--backend-smoke") != 0)) {
        fputs("expected corpus directory and optional --backend-smoke\n", stderr);
        return 2;
    }
    compatibility_corpus_dir = argv[1];
    backend_smoke = argc == 3;
    if (sizeof(compatibility_cases) / sizeof(compatibility_cases[0]) !=
        COMPATIBILITY_CASE_COUNT) {
        fputs("compatibility manifest must contain exactly 39 cases\n", stderr);
        return 2;
    }
    if (backend_smoke) {
        run_backend_smoke();
        if (failures != 0) {
            fprintf(stderr, "%d compatibility check(s) failed\n", failures);
            return 1;
        }
        puts("local GIF compatibility backend smoke tests passed");
        return 0;
    }
    run_normal_baseline();
    run_one_byte_matrix();
    run_practical_short_reads();
    run_final_byte_eof_cases();
    run_lifecycle_repeats();
    run_injected_io_cases();

    if (failures != 0) {
        fprintf(stderr, "%d compatibility check(s) failed\n", failures);
        return 1;
    }
    puts("local GIF compatibility tests passed");
    return 0;
}
