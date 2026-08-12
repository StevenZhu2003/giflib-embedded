/**
 * @file gif_mem_private.c
 * @brief Template translation unit for the PRIVATE memory backend.
 *
 * Copyright (c) 2026 Steven Zhu
 * SPDX-License-Identifier: MIT
 *
 * This file deliberately provides no fallback implementation. When PRIVATE is
 * selected, the application must define the three functions declared in
 * gif_mem_private.h and link them in the same allocator domain.
 */

#include "gif_mem_private.h"
