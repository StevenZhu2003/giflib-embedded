/**
 * @file test_decoder_builtin_oom.c
 * @brief Verify decoder cleanup when its intentionally undersized pool exhausts.
 *
 * Copyright (c) 2026 Steven Zhu
 * SPDX-License-Identifier: MIT
 */

#include "gif_decoder.h"
#include "gif_mem_builtin.h"
#include "test_porting.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int failures;

#define CHECK(condition)                                                       \
    do {                                                                       \
        if (!(condition)) {                                                    \
            fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__,          \
                    __LINE__, #condition);                                    \
            failures++;                                                        \
        }                                                                      \
    } while (0)

/** @brief Valid GIF header whose global palette makes decoder construction OOM. */
static const uint8_t gif_header_with_palette[] = {
    'G', 'I', 'F', '8', '9', 'a',
    1, 0, 1, 0, 0x80, 0, 0,
    0, 0, 0, 255, 255, 255,
    ';'
};

int main(void) {
    MemorySource source;
    GifDecoderConfig config;
    GifDecoder *decoder = NULL;
    GifStreamInfo stream;

    memset(&source, 0, sizeof(source));
    source.data = gif_header_with_palette;
    source.size = sizeof(gif_header_with_palette);
    source.max_chunk = source.size;
    config.source_identifier = &source;
    CHECK(gif_decoder_open(&config, &decoder, &stream) ==
          GIF_STATUS_OUT_OF_MEMORY);
    CHECK(decoder == NULL);
    CHECK(source.close_calls == 1U);
    CHECK(gif_mem_builtin_check_integrity() == 0);

    if (failures != 0) {
        fprintf(stderr, "%d builtin decoder OOM check(s) failed\n", failures);
        return 1;
    }
    return 0;
}
