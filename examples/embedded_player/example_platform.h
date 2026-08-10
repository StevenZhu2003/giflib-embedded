/**
 * @file example_platform.h
 * @brief Display and time boundary used by the embedded player application.
 *
 * Copyright (c) 2026 Steven Zhu
 * SPDX-License-Identifier: MIT
 */

#ifndef GIF_EXAMPLE_PLATFORM_H
#define GIF_EXAMPLE_PLATFORM_H

#include <stddef.h>
#include <stdint.h>

/**
 * @brief Prepare the application display for an RGB888 canvas.
 *
 * @param[in] width  Displayed canvas width in pixels.
 * @param[in] height Displayed canvas height in pixels.
 * @return Zero on success, otherwise a platform-defined failure value.
 */
int example_platform_display_open(uint32_t width, uint32_t height);

/**
 * @brief Present one completed RGB888 framebuffer to the display layer.
 *
 * The call may return after copying the pixels or after the physical transfer
 * completes, according to the platform implementation. The application does
 * not modify the framebuffer until this function returns.
 *
 * @param[in] pixels      First byte of the caller-owned framebuffer.
 * @param[in] width       Visible width in pixels.
 * @param[in] height      Visible height in pixels.
 * @param[in] stride      Byte distance between consecutive framebuffer rows.
 * @param[in] frame_index Zero-based frame number for diagnostics or capture.
 * @return Zero on success, otherwise a platform-defined failure value.
 */
int example_platform_display_present_rgb888(const uint8_t *pixels,
                                            uint32_t width,
                                            uint32_t height,
                                            size_t stride,
                                            uint32_t frame_index);

/**
 * @brief Wait according to the application's playback timing policy.
 *
 * @param[in] delay_ms Delay reported for the completed GIF frame.
 */
void example_platform_delay_ms(uint32_t delay_ms);

/** @brief Release resources owned by the application display layer. */
void example_platform_display_close(void);

/**
 * @brief Report an application diagnostic through the platform's log channel.
 *
 * A target without a log channel may provide an empty implementation.
 *
 * @param[in] message Null-terminated diagnostic text.
 */
void example_platform_log(const char *message);

#endif /* GIF_EXAMPLE_PLATFORM_H */
