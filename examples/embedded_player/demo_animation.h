/**
 * @file demo_animation.h
 * @brief Embedded byte representation of the example boot animation.
 *
 * Copyright (c) 2026 Steven Zhu
 * SPDX-License-Identifier: MIT
 */

#ifndef GIF_EXAMPLE_DEMO_ANIMATION_H
#define GIF_EXAMPLE_DEMO_ANIMATION_H

#include <stddef.h>
#include <stdint.h>

/** @brief Encoded bytes from assets/device_boot.gif. */
extern const uint8_t gif_example_demo_animation[];

/** @brief Size of gif_example_demo_animation in bytes. */
extern const size_t gif_example_demo_animation_size;

#endif /* GIF_EXAMPLE_DEMO_ANIMATION_H */
