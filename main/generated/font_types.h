#pragma once

#include <stdint.h>

typedef struct {
    uint16_t cp;
    uint8_t w;
    uint8_t h;
    uint8_t advance;
    uint8_t rb;
    const uint8_t *data;
} glyph_t;