#include "gif_decoder.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

typedef struct MemorySource {
    const uint8_t *data;
    size_t size;
    size_t offset;
    size_t max_chunk;
    size_t error_offset;
    size_t read_calls;
    bool inject_error;
    bool eof_with_final_bytes;
} MemorySource;

static int failures;

#ifdef GIFLIB_TEST_ALLOC_TRACKING
size_t giflib_test_outstanding_allocations(void);
void giflib_test_fail_allocation_after(size_t successful_allocations);
void giflib_test_disable_allocation_failure(void);
#endif

#define CHECK(condition)                                                       \
    do {                                                                       \
        if (!(condition)) {                                                    \
            fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__,           \
                    __LINE__, #condition);                                     \
            failures++;                                                        \
        }                                                                      \
    } while (0)

static const uint8_t gif_header_with_palette[] = {
    'G', 'I', 'F', '8', '9', 'a',
    0x02, 0x00, 0x03, 0x00,
    0x80, 0x01, 0x00,
    0x00, 0x00, 0x00,
    0xff, 0xff, 0xff,
};

static const uint8_t gif_two_pixel_global[] = {
    'G', 'I', 'F', '8', '9', 'a',
    0x02, 0x00, 0x01, 0x00, 0x80, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x11, 0x22, 0x33,
    0x2c,
    0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x01, 0x00, 0x00,
    0x02, 0x02, 0x44, 0x0a, 0x00,
    0x3b,
};

static const uint8_t gif_two_rows_global[] = {
    'G', 'I', 'F', '8', '9', 'a',
    0x01, 0x00, 0x02, 0x00, 0x80, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x11, 0x22, 0x33,
    0x2c,
    0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x02, 0x00, 0x00,
    0x02, 0x02, 0x44, 0x0a, 0x00,
    0x3b,
};

static const uint8_t gif_two_pixel_local[] = {
    'G', 'I', 'F', '8', '9', 'a',
    0x02, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00,
    0x2c,
    0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x01, 0x00, 0x80,
    0x44, 0x55, 0x66, 0xaa, 0xbb, 0xcc,
    0x02, 0x02, 0x0c, 0x0a, 0x00,
    0x3b,
};

static const uint8_t gif_partial_frame[] = {
    'G', 'I', 'F', '8', '9', 'a',
    0x03, 0x00, 0x01, 0x00, 0x80, 0x01, 0x00,
    0x00, 0x00, 0x00, 0x10, 0x20, 0x30,
    0x2c,
    0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x01, 0x00, 0x00,
    0x02, 0x02, 0x44, 0x01, 0x00,
    0x3b,
};

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

static const uint8_t gif_interlaced[] = {
    'G', 'I', 'F', '8', '9', 'a',
    0x02, 0x00, 0x01, 0x00, 0x80, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x11, 0x22, 0x33,
    0x2c,
    0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x01, 0x00, 0x40,
    0x02, 0x02, 0x44, 0x0a, 0x00,
    0x3b,
};

static const uint8_t gif_with_transparency[] = {
    'G', 'I', 'F', '8', '9', 'a',
    0x01, 0x00, 0x01, 0x00, 0x80, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x11, 0x22, 0x33,
    0x21, 0xf9, 0x04, 0x01, 0x00, 0x00, 0x00, 0x00,
    0x2c,
    0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x01, 0x00, 0x00,
    0x02, 0x02, 0x44, 0x01, 0x00,
    0x3b,
};

static const uint8_t gif_with_disposal_two[] = {
    'G', 'I', 'F', '8', '9', 'a',
    0x01, 0x00, 0x01, 0x00, 0x80, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x11, 0x22, 0x33,
    0x21, 0xf9, 0x04, 0x08, 0x00, 0x00, 0x00, 0x00,
    0x2c,
    0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x01, 0x00, 0x00,
    0x02, 0x02, 0x44, 0x01, 0x00,
    0x3b,
};

static void memory_source_init(MemorySource *source,
                               const uint8_t *data,
                               size_t size) {
    memset(source, 0, sizeof(*source));
    source->data = data;
    source->size = size;
    source->max_chunk = size;
}

static GifReadStatus memory_source_read(void *io_context,
                                        uint8_t *destination,
                                        size_t requested_bytes,
                                        size_t *actual_bytes) {
    MemorySource *source = (MemorySource *)io_context;
    size_t available;
    size_t amount;

    if (actual_bytes == NULL) {
        return GIF_READ_IO_ERROR;
    }
    *actual_bytes = 0;

    if (source == NULL || destination == NULL || requested_bytes == 0) {
        return GIF_READ_IO_ERROR;
    }

    source->read_calls++;
    if (source->inject_error && source->offset >= source->error_offset) {
        return GIF_READ_IO_ERROR;
    }
    if (source->offset >= source->size) {
        return GIF_READ_EOF;
    }

    available = source->size - source->offset;
    amount = requested_bytes;
    if (amount > available) {
        amount = available;
    }
    if (source->max_chunk != 0 && amount > source->max_chunk) {
        amount = source->max_chunk;
    }
    if (source->inject_error &&
        source->offset + amount > source->error_offset) {
        amount = source->error_offset - source->offset;
    }

    if (amount != 0) {
        memcpy(destination, source->data + source->offset, amount);
        source->offset += amount;
        *actual_bytes = amount;
    }

    if (source->inject_error && source->offset >= source->error_offset) {
        return GIF_READ_IO_ERROR;
    }
    if (source->eof_with_final_bytes && source->offset == source->size) {
        return GIF_READ_EOF;
    }
    return GIF_READ_OK;
}

static GifStatus open_source(MemorySource *source,
                             GifDecoder **decoder,
                             GifStreamInfo *stream) {
    GifDecoderConfig config;

    config.read = memory_source_read;
    config.io_context = source;
    return gif_decoder_open(&config, decoder, stream);
}

static GifStatus bind_output(GifDecoder *decoder,
                             void *pixels,
                             size_t capacity_bytes,
                             size_t stride_bytes,
                             GifPixelFormat pixel_format) {
    GifOutputSurface surface;

    surface.pixels = pixels;
    surface.capacity_bytes = capacity_bytes;
    surface.stride_bytes = stride_bytes;
    surface.pixel_format = pixel_format;
    return gif_decoder_bind_output(decoder, &surface);
}

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
}

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

static void test_unexpected_eof(void) {
    MemorySource source;
    GifDecoder *decoder = NULL;
    GifStreamInfo stream;

    memory_source_init(&source, gif_header_with_palette, 7);

    CHECK(open_source(&source, &decoder, &stream) ==
          GIF_STATUS_UNEXPECTED_EOF);
    CHECK(decoder == NULL);
}

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
}

static void test_malformed_header(void) {
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

static void test_oom_mapping(void) {
#ifdef GIFLIB_TEST_ALLOC_TRACKING
    MemorySource source;
    GifDecoder *decoder = NULL;
    GifStreamInfo stream;
    size_t allocations_before = giflib_test_outstanding_allocations();
    GifStatus status;

    memory_source_init(&source, gif_header_with_palette,
                       sizeof(gif_header_with_palette));

    /* Allow the facade and GifFile allocations, then fail giflib private
     * state allocation so the mapping covers an underlying giflib OOM. */
    giflib_test_fail_allocation_after(2);
    status = open_source(&source, &decoder, &stream);
    giflib_test_disable_allocation_failure();

    CHECK(status == GIF_STATUS_OUT_OF_MEMORY);
    CHECK(decoder == NULL);
    CHECK(giflib_test_outstanding_allocations() == allocations_before);
#endif
}

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
    gif_decoder_close(decoder);
}

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
    CHECK(frame.image_left == 0 && frame.image_top == 0);
    CHECK(frame.image_width == 2 && frame.image_height == 1);
    CHECK(frame.updated_left == 0 && frame.updated_top == 0);
    CHECK(frame.updated_width == 2 && frame.updated_height == 1);
    CHECK(pixels[0] == 0x00 && pixels[1] == 0x00 && pixels[2] == 0x00);
    CHECK(pixels[3] == 0x11 && pixels[4] == 0x22 && pixels[5] == 0x33);
    CHECK(pixels[6] == 0xee && pixels[7] == 0xee);
    CHECK(gif_decoder_bind_output(decoder, &(GifOutputSurface){
              pixels, sizeof(pixels), sizeof(pixels), GIF_PIXEL_RGB888}) ==
          GIF_STATUS_INVALID_STATE);
    CHECK(gif_decoder_next_frame(decoder, &frame) ==
          GIF_STATUS_END_OF_STREAM);
    CHECK(gif_decoder_next_frame(decoder, &frame) ==
          GIF_STATUS_END_OF_STREAM);
    gif_decoder_close(decoder);
}

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

static void check_unsupported_gif(const uint8_t *data, size_t size) {
    MemorySource source;
    GifDecoder *decoder = NULL;
    GifStreamInfo stream;
    GifFrameInfo frame;
    uint8_t pixels[6];

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

static void test_unsupported_features(void) {
    check_unsupported_gif(gif_interlaced, sizeof(gif_interlaced));
    check_unsupported_gif(gif_with_transparency,
                          sizeof(gif_with_transparency));
    check_unsupported_gif(gif_with_disposal_two,
                          sizeof(gif_with_disposal_two));
}

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

int main(void) {
    test_open_memory_source();
    test_legal_short_reads();
    test_final_bytes_with_eof();
    test_unexpected_eof();
    test_injected_io_error();
    test_malformed_header();
    test_invalid_arguments();
    test_oom_mapping();
    test_repeated_open_close();
    test_output_surface_validation();
    test_rgb888_and_stride();
    test_bgr888_and_local_palette();
    test_stride_row_advance();
    test_partial_frame_background();
    test_two_streaming_frames();
    test_unsupported_features();
    test_frame_read_failures();
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
