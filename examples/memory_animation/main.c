/**
 * @file main.c
 * @brief Complete hosted-C example for memory-backed GIF animation decoding.
 *
 * Copyright (c) 2026 Steven Zhu
 * SPDX-License-Identifier: MIT
 */

#include "gif_decoder.h"

#include "memory_source.h"

#include <stdint.h>
#include <stdio.h>
#include <time.h>

/** @brief Width of the project-original demonstration animation. */
#define EXAMPLE_CANVAS_WIDTH 2U

/** @brief Height of the project-original demonstration animation. */
#define EXAMPLE_CANVAS_HEIGHT 1U

/** @brief RGB888 byte stride of the example framebuffer. */
#define EXAMPLE_FRAMEBUFFER_STRIDE (EXAMPLE_CANVAS_WIDTH * 3U)

/**
 * @brief Project-original three-frame GIF fixture used by the example.
 *
 * Frame zero has a transparent pixel and a 20 ms delay. Frame one has a
 * 30 ms delay, and frame two has no GCE so its delay returns to zero.
 */
static const uint8_t example_animation[] = {
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

/** @brief Caller-owned RGB888 canvas used for every decoded frame. */
static uint8_t framebuffer[EXAMPLE_CANVAS_HEIGHT *
                           EXAMPLE_FRAMEBUFFER_STRIDE];

/**
 * @brief Print one fully composited frame using application-owned output code.
 *
 * @param[in] frame Metadata returned with the completed frame.
 */
static void application_present_frame(const GifFrameInfo *frame) {
    unsigned int column;

    printf("frame %u, delay %u ms:",
           (unsigned int)frame->frame_index,
           (unsigned int)frame->delay_ms);
    for (column = 0; column < EXAMPLE_CANVAS_WIDTH; column++) {
        const uint8_t *pixel = framebuffer + (size_t)column * 3U;

        printf(" #%02x%02x%02x", (unsigned int)pixel[0],
               (unsigned int)pixel[1], (unsigned int)pixel[2]);
    }
    putchar('\n');
}

/**
 * @brief Wait for the decoded frame delay using a hosted-C application policy.
 *
 * This busy wait is intentionally outside the decoder and is suitable only
 * for a small portable demonstration. A target application should replace it
 * with its own timer, scheduler, event loop, or deliberate no-delay policy.
 *
 * @param[in] delay_ms Delay requested by the current frame in milliseconds.
 */
static void application_wait_ms(uint32_t delay_ms) {
    clock_t current;
    clock_t start;
    double wait_ticks;

    if (delay_ms == 0) {
        return;
    }

    start = clock();
    if (start == (clock_t)-1) {
        return;
    }
    wait_ticks = ((double)delay_ms * (double)CLOCKS_PER_SEC) / 1000.0;

    for (;;) {
        current = clock();
        if (current == (clock_t)-1 ||
            (double)(current - start) >= wait_ticks) {
            break;
        }
        /* The application owns its wait policy; the decoder remains idle. */
    }
}

/**
 * @brief Decode the built-in animation through the public API.
 *
 * @return Zero on complete success, otherwise one.
 */
int main(void) {
    const GifMemorySource source = {
        example_animation,
        sizeof(example_animation),
    };
    const GifDecoderConfig config = {
        &source,
    };
    GifDecoder *decoder = NULL;
    GifStreamInfo stream;
    GifOutputSurface surface;
    GifFrameInfo frame;
    GifStatus status;

    status = gif_decoder_open(&config, &decoder, &stream);
    if (status != GIF_STATUS_OK) {
        fprintf(stderr, "open failed: %s\n", gif_status_string(status));
        return 1;
    }
    if (stream.canvas_width != EXAMPLE_CANVAS_WIDTH ||
        stream.canvas_height != EXAMPLE_CANVAS_HEIGHT) {
        fputs("unexpected example canvas dimensions\n", stderr);
        gif_decoder_close(decoder);
        return 1;
    }

    surface.pixels = framebuffer;
    surface.capacity_bytes = sizeof(framebuffer);
    surface.stride_bytes = EXAMPLE_FRAMEBUFFER_STRIDE;
    surface.pixel_format = GIF_PIXEL_RGB888;

    status = gif_decoder_bind_output(decoder, &surface);
    if (status != GIF_STATUS_OK) {
        fprintf(stderr, "output bind failed: %s\n",
                gif_status_string(status));
        gif_decoder_close(decoder);
        return 1;
    }

    while ((status = gif_decoder_next_frame(decoder, &frame)) ==
           GIF_STATUS_OK) {
        application_present_frame(&frame);
        application_wait_ms(frame.delay_ms);
    }

    gif_decoder_close(decoder);
    if (status != GIF_STATUS_END_OF_STREAM) {
        fprintf(stderr, "decode failed: %s\n", gif_status_string(status));
        return 1;
    }

    puts("animation complete");
    return 0;
}
