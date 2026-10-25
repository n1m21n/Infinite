/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception
 * Copyright (c) 2026 M-VAVE. */
#pragma once
#include <stdint.h>

/* Serialized controls use integers from zero through max, inclusive. */
typedef struct {
    const char *name;
    uint8_t max;
    uint8_t def;
    const char *const *names;
} fm1_param_t;
