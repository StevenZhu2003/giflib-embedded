/**
 * @file main.c
 * @brief Platform-neutral embedded GIF player reference application.
 *
 * Copyright (c) 2026 Steven Zhu
 * SPDX-License-Identifier: MIT
 */

#include "gif_decoder.h"

#include "demo_animation.h"
#include "example_platform.h"
#include "memory_source.h"

#include <stdint.h>

/** @brief Maximum canvas width supported by the example application. */
#define EXAMPLE_MAX_CANVAS_WIDTH 128U

/** @brief Maximum canvas height supported by the example application. */
#define EXAMPLE_MAX_CANVAS_HEIGHT 64U

/** @brief Bytes occupied by one RGB888 row in the application framebuffer. */
#define EXAMPLE_FRAMEBUFFER_STRIDE (EXAMPLE_MAX_CANVAS_WIDTH * 3U)

/** @brief Caller-owned RGB888 canvas reused for every decoded frame. */
static uint8_t example_framebuffer[EXAMPLE_MAX_CANVAS_HEIGHT *
                                   EXAMPLE_FRAMEBUFFER_STRIDE];

/**
 * @brief Play one complete GIF resource through the public decoder API.
 *
 * @param[in] source Encoded GIF resource selected by the application.
 * @return Zero after normal end of stream, otherwise one.
 */
static int application_play_gif(const GifMemorySource *source) {
    const GifDecoderConfig config = {source};
    GifDecoder *decoder = NULL;
    GifStreamInfo stream;
    GifOutputSurface surface;
    GifFrameInfo frame;
    GifStatus status;
    int display_open = 0;
    int result = 1;

    status = gif_decoder_open(&config, &decoder, &stream);
    if (status != GIF_STATUS_OK) {
        example_platform_log("GIF open failed");
        example_platform_log(gif_status_string(status));
        goto cleanup;
    }

    if (stream.canvas_width == 0 || stream.canvas_height == 0 ||
        stream.canvas_width > EXAMPLE_MAX_CANVAS_WIDTH ||
        stream.canvas_height > EXAMPLE_MAX_CANVAS_HEIGHT) {
        example_platform_log("GIF canvas exceeds the application framebuffer");
        goto cleanup;
    }

    if (example_platform_display_open(stream.canvas_width,
                                      stream.canvas_height) != 0) {
        example_platform_log("display initialization failed");
        goto cleanup;
    }
    display_open = 1;

    surface.pixels = example_framebuffer;
    surface.capacity_bytes = sizeof(example_framebuffer);
    surface.stride_bytes = EXAMPLE_FRAMEBUFFER_STRIDE;
    surface.pixel_format = GIF_PIXEL_RGB888;

    status = gif_decoder_bind_output(decoder, &surface);
    if (status != GIF_STATUS_OK) {
        example_platform_log("framebuffer bind failed");
        example_platform_log(gif_status_string(status));
        goto cleanup;
    }

    while ((status = gif_decoder_next_frame(decoder, &frame)) ==
           GIF_STATUS_OK) {
        if (example_platform_display_present_rgb888(
                example_framebuffer,
                stream.canvas_width,
                stream.canvas_height,
                EXAMPLE_FRAMEBUFFER_STRIDE,
                frame.frame_index) != 0) {
            example_platform_log("frame presentation failed");
            goto cleanup;
        }

        example_platform_delay_ms(frame.delay_ms);
    }

    if (status != GIF_STATUS_END_OF_STREAM) {
        example_platform_log("GIF decode failed");
        example_platform_log(gif_status_string(status));
        goto cleanup;
    }

    result = 0;

cleanup:
    if (display_open) {
        example_platform_display_close();
    }
    gif_decoder_close(decoder);
    return result;
}

/**
 * @brief Select the built-in resource and run one application playback pass.
 *
 * @return Process success or failure for the hosted reference build.
 */
int main(void) {
    const GifMemorySource boot_animation = {
        gif_example_demo_animation,
        gif_example_demo_animation_size,
    };
    int result;

    result = application_play_gif(&boot_animation);
    if (result == 0) {
        example_platform_log("animation playback complete");
    }
    return result;
}
