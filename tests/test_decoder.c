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

    if (failures != 0) {
        fprintf(stderr, "%d facade check(s) failed\n", failures);
        return 1;
    }

    puts("gif decoder facade tests passed");
    return 0;
}
