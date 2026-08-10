/**
 * @file hosted_platform.c
 * @brief Standard-C display and time backend for the embedded player example.
 *
 * The hosted display writes each completed RGB888 framebuffer as a portable
 * pixmap (PPM). An embedded integration replaces this file with its display
 * transfer and timer or scheduler functions; the player remains unchanged.
 *
 * Copyright (c) 2026 Steven Zhu
 * SPDX-License-Identifier: MIT
 */

#include "example_platform.h"

#include <stdio.h>
#include <time.h>

/** @brief Maximum length of a generated frame-capture filename. */
#define EXAMPLE_FRAME_FILENAME_CAPACITY 64U

/** @copydoc example_platform_display_open */
int example_platform_display_open(uint32_t width, uint32_t height) {
    printf("display ready: %u x %u RGB888\n",
           (unsigned int)width, (unsigned int)height);
    return 0;
}

/** @copydoc example_platform_display_present_rgb888 */
int example_platform_display_present_rgb888(const uint8_t *pixels,
                                            uint32_t width,
                                            uint32_t height,
                                            size_t stride,
                                            uint32_t frame_index) {
    char filename[EXAMPLE_FRAME_FILENAME_CAPACITY];
    FILE *output;
    uint32_t row;
    size_t visible_row_bytes;
    int filename_length;
    int result = 0;

    if (pixels == NULL || width == 0 || height == 0) {
        return -1;
    }

    visible_row_bytes = (size_t)width * 3U;
    if (stride < visible_row_bytes) {
        return -1;
    }

    filename_length = snprintf(filename, sizeof(filename),
                               "gif_frame_%03u.ppm",
                               (unsigned int)frame_index);
    if (filename_length < 0 ||
        (size_t)filename_length >= sizeof(filename)) {
        return -1;
    }

    output = fopen(filename, "wb");
    if (output == NULL) {
        return -1;
    }

    if (fprintf(output, "P6\n%u %u\n255\n",
                (unsigned int)width, (unsigned int)height) < 0) {
        result = -1;
    }

    for (row = 0; result == 0 && row < height; row++) {
        const uint8_t *source_row = pixels + (size_t)row * stride;

        if (fwrite(source_row, 1, visible_row_bytes, output) !=
            visible_row_bytes) {
            result = -1;
        }
    }

    if (fclose(output) != 0) {
        result = -1;
    }
    if (result == 0) {
        printf("presented frame %u -> %s\n",
               (unsigned int)frame_index, filename);
    }
    return result;
}

/** @copydoc example_platform_delay_ms */
void example_platform_delay_ms(uint32_t delay_ms) {
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
    }
}

/** @copydoc example_platform_display_close */
void example_platform_display_close(void) {
    /* The standard-C capture backend owns no persistent display resource. */
}

/** @copydoc example_platform_log */
void example_platform_log(const char *message) {
    if (message != NULL) {
        puts(message);
    }
}
