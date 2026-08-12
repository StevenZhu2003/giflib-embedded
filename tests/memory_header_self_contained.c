/**
 * @file memory_header_self_contained.c
 * @brief Compile private memory boundaries without accidental prior includes.
 *
 * Copyright (c) 2026 Steven Zhu
 * SPDX-License-Identifier: MIT
 */

#include "gif_mem.h"
#include "gif_mem_builtin.h"
#include "gif_mem_private.h"
#include "gif_tlsf.h"

int gif_memory_header_self_contained(void) {
    return 0;
}
