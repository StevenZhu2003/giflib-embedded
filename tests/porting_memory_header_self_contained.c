/**
 * @file porting_memory_header_self_contained.c
 * @brief Compile-time check that the port-only memory bridge is self-contained.
 *
 * Copyright (c) 2026 Steven Zhu
 * SPDX-License-Identifier: MIT
 */

#include "gif_porting_memory.h"

/**
 * @brief Exercise the port-only bridge declarations in one translation unit.
 *
 * @param[in] size Requested port-handle size.
 * @return Allocation returned by the selected memory backend.
 */
void *gif_porting_memory_header_allocate(size_t size) {
    return gif_porting_mem_alloc(size);
}
