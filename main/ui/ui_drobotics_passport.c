// main/ui/ui_drobotics_passport.c
// D-Robotics 2026 Developer Conference AI Passport Badge UI Implementation.
// Features a 1:1 square 192x192 3D Halftone viewport (1.25x logo scale),
// streamlined top/bottom key text, 100% integer fixed-point rendering,
// byte-aligned 4x4 halftone cell blitting, and partial dirty-rect invalidation
// for smooth 50 FPS animation on ESP32-C3.
#include "ui_drobotics_passport.h"
#include "bsp_battery.h"
#include "bsp_display.h"
#include "bsp_pins.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "lvgl.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "drobotics_ui";

#define SCREEN_W                BSP_LCD_W
#define SCREEN_H                BSP_LCD_H
#define FRAME_PERIOD_MS         20u   // 50 FPS target matching LVGL 20ms refresh period
#define BATTERY_POLL_MS         5000u

// 1:1 Square Viewport coordinates (192 x 192 px)
#define VIEWPORT_X              24
#define VIEWPORT_Y              35

// Full-screen 4-bit indexed (I4) static draw buffer: 240 * 320 / 2 = 38,400 bytes
LV_DRAW_BUF_DEFINE_STATIC(s_passport_buf, SCREEN_W, SCREEN_H, LV_COLOR_FORMAT_I4);

static lv_obj_t                   *s_scr;
static lv_obj_t                   *s_canvas;
static lv_timer_t                 *s_timer;
static drobotics_passport_state_t  s_state;
static drobotics_render_style_t    s_applied_style = DROBOTICS_STYLE_COUNT;
static drobotics_badge_mode_t      s_drawn_badge_mode = DROBOTICS_BADGE_MODE_COUNT;
static bool                        s_drawn_cursor;
static bool                        s_drawn_overclock;
static bool                        s_full_hud_dirty = true;
static uint8_t                     s_logo_grid[DROBOTICS_LOGO_GRID_H][DROBOTICS_LOGO_GRID_W];
static uint64_t                    s_last_frame_us;
static uint32_t                    s_bat_elapsed_ms;
static int                         s_battery_soc = -1;
static uint8_t                    *s_px_base;
static uint32_t                    s_px_stride;

// ---------------------------------------------------------------------------
// 1. 16-Color CRT Phosphor Palettes (0 = BG, 3 = DIM, 8 = AMBER, 11 = BRIGHT, 15 = HOT)
// ---------------------------------------------------------------------------
static const uint32_t PALETTE_AMBER_HALFTONE[16] = {
    0x050300u, 0x160900u, 0x2A1200u, 0x4A2200u,
    0x683000u, 0x8A4200u, 0xB05600u, 0xD86C00u,
    0xFF8800u, 0xFF9A10u, 0xFFAE24u, 0xFFCC00u,
    0xFFD844u, 0xFFE378u, 0xFFEDA8u, 0xFFF5CCu
};

static const uint32_t PALETTE_GREEN_PHOSPHOR[16] = {
    0x010501u, 0x051408u, 0x0A240Fu, 0x123C1Au,
    0x1A5626u, 0x247433u, 0x2E9441u, 0x3AB850u,
    0x48DC60u, 0x5EEA74u, 0x78F48Cu, 0x94FAA4u,
    0xB0FCBEu, 0xC8FFD2u, 0xE0FFE6u, 0xF2FFF5u
};

static const uint32_t PALETTE_SQUARE_LED[16] = {
    0x050300u, 0x120600u, 0x240E00u, 0x4A2200u,
    0x783400u, 0xA84800u, 0xD66000u, 0xF27400u,
    0xFF8800u, 0xFFA200u, 0xFFBC00u, 0xFFD000u,
    0xFFE038u, 0xFFEB74u, 0xFFF2AAu, 0xFFF5CCu
};

static void apply_canvas_palette(drobotics_render_style_t style) {
    if (!s_canvas || style == s_applied_style) return;
    const uint32_t *pal = PALETTE_AMBER_HALFTONE;
    if (style == DROBOTICS_STYLE_GREEN_PHOSPHOR) {
        pal = PALETTE_GREEN_PHOSPHOR;
    } else if (style == DROBOTICS_STYLE_SQUARE_LED) {
        pal = PALETTE_SQUARE_LED;
    }
    for (uint8_t i = 0; i < 16u; i++) {
        lv_canvas_set_palette(s_canvas, i, lv_color_to_32(lv_color_hex(pal[i]), LV_OPA_COVER));
    }
    s_applied_style = style;
    s_full_hud_dirty = true;
}

// ---------------------------------------------------------------------------
// 2. Fast Direct I4 Buffer Drawing Primitives
// ---------------------------------------------------------------------------
static inline void put_px(int x, int y, uint8_t color) {
    if ((unsigned)x >= (unsigned)SCREEN_W || (unsigned)y >= (unsigned)SCREEN_H) return;
    uint8_t *byte_ptr = s_px_base + (uint32_t)y * s_px_stride + ((uint32_t)x >> 1);
    if ((x & 1) == 0) {
        *byte_ptr = (uint8_t)((*byte_ptr & 0x0Fu) | ((color & 0x0Fu) << 4));
    } else {
        *byte_ptr = (uint8_t)((*byte_ptr & 0xF0u) | (color & 0x0Fu));
    }
}

static inline void put_px_clipped(int x, int y, int clip_y1, int clip_y2, uint8_t color) {
    if (y < clip_y1 || y > clip_y2) return;
    put_px(x, y, color);
}

static void draw_hline(int x1, int x2, int y, uint8_t color) {
    if (y < 0 || y >= SCREEN_H) return;
    if (x1 > x2) { int t = x1; x1 = x2; x2 = t; }
    if (x1 < 0) x1 = 0;
    if (x2 >= SCREEN_W) x2 = SCREEN_W - 1;
    for (int x = x1; x <= x2; x++) {
        put_px(x, y, color);
    }
}

static void draw_vline(int x, int y1, int y2, uint8_t color) {
    if (x < 0 || x >= SCREEN_W) return;
    if (y1 > y2) { int t = y1; y1 = y2; y2 = t; }
    if (y1 < 0) y1 = 0;
    if (y2 >= SCREEN_H) y2 = SCREEN_H - 1;
    for (int y = y1; y <= y2; y++) {
        put_px(x, y, color);
    }
}

static void draw_line(int x0, int y0, int x1, int y1, uint8_t color) {
    int dx = (x1 > x0) ? (x1 - x0) : (x0 - x1);
    int sx = (x0 < x1) ? 1 : -1;
    int dy = (y1 > y0) ? -(y1 - y0) : -(y0 - y1);
    int sy = (y0 < y1) ? 1 : -1;
    int err = dx + dy;
    for (;;) {
        put_px(x0, y0, color);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

static void fill_rect(int x, int y, int w, int h, uint8_t color) {
    int x2 = x + w - 1;
    int y2 = y + h - 1;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x2 >= SCREEN_W) x2 = SCREEN_W - 1;
    if (y2 >= SCREEN_H) y2 = SCREEN_H - 1;
    uint8_t packed = (uint8_t)(((color & 0x0Fu) << 4) | (color & 0x0Fu));
    for (int py = y; py <= y2; py++) {
        int px = x;
        if ((px & 1) && px <= x2) {
            put_px(px, py, color);
            px++;
        }
        uint8_t *row_ptr = s_px_base + (uint32_t)py * s_px_stride + ((uint32_t)px >> 1);
        int pairs = (x2 - px + 1) >> 1;
        if (pairs > 0) {
            memset(row_ptr, packed, (size_t)pairs);
            px += pairs * 2;
        }
        if (px <= x2) {
            put_px(px, py, color);
        }
    }
}

static void draw_rect(int x, int y, int w, int h, uint8_t color) {
    draw_hline(x, x + w - 1, y, color);
    draw_hline(x, x + w - 1, y + h - 1, color);
    draw_vline(x, y, y + h - 1, color);
    draw_vline(x + w - 1, y, y + h - 1, color);
}

// ---------------------------------------------------------------------------
// 3. 5x7 Retro Terminal Monospace Bitmap Font (ASCII 0x20..0x5F)
// ---------------------------------------------------------------------------
static const uint8_t FONT_5X7[64][5] = {
    {0x00,0x00,0x00,0x00,0x00}, // 0x20 ' '
    {0x00,0x00,0x5F,0x00,0x00}, // 0x21 '!'
    {0x00,0x07,0x00,0x07,0x00}, // 0x22 '"'
    {0x14,0x7F,0x14,0x7F,0x14}, // 0x23 '#'
    {0x24,0x2A,0x7F,0x2A,0x12}, // 0x24 '$'
    {0x23,0x13,0x08,0x64,0x62}, // 0x25 '%'
    {0x36,0x49,0x55,0x22,0x50}, // 0x26 '&'
    {0x00,0x05,0x03,0x00,0x00}, // 0x27 '''
    {0x00,0x1C,0x22,0x41,0x00}, // 0x28 '('
    {0x00,0x41,0x22,0x1C,0x00}, // 0x29 ')'
    {0x14,0x08,0x3E,0x08,0x14}, // 0x2A '*'
    {0x08,0x08,0x3E,0x08,0x08}, // 0x2B '+'
    {0x00,0x50,0x30,0x00,0x00}, // 0x2C ','
    {0x08,0x08,0x08,0x08,0x08}, // 0x2D '-'
    {0x00,0x60,0x60,0x00,0x00}, // 0x2E '.'
    {0x20,0x10,0x08,0x04,0x02}, // 0x2F '/'
    {0x3E,0x51,0x49,0x45,0x3E}, // 0x30 '0'
    {0x00,0x42,0x7F,0x40,0x00}, // 0x31 '1'
    {0x42,0x61,0x51,0x49,0x46}, // 0x32 '2'
    {0x21,0x41,0x45,0x4B,0x31}, // 0x33 '3'
    {0x18,0x14,0x12,0x7F,0x10}, // 0x34 '4'
    {0x27,0x45,0x45,0x45,0x39}, // 0x35 '5'
    {0x3C,0x4A,0x49,0x49,0x30}, // 0x36 '6'
    {0x01,0x71,0x09,0x05,0x03}, // 0x37 '7'
    {0x36,0x49,0x49,0x49,0x36}, // 0x38 '8'
    {0x06,0x49,0x49,0x29,0x1E}, // 0x39 '9'
    {0x00,0x36,0x36,0x00,0x00}, // 0x3A ':'
    {0x00,0x56,0x36,0x00,0x00}, // 0x3B ';'
    {0x08,0x14,0x22,0x41,0x00}, // 0x3C '<'
    {0x14,0x14,0x14,0x14,0x14}, // 0x3D '='
    {0x00,0x41,0x22,0x14,0x08}, // 0x3E '>'
    {0x02,0x01,0x51,0x09,0x06}, // 0x3F '?'
    {0x32,0x49,0x79,0x41,0x3E}, // 0x40 '@'
    {0x7E,0x11,0x11,0x11,0x7E}, // 0x41 'A'
    {0x7F,0x49,0x49,0x49,0x36}, // 0x42 'B'
    {0x3E,0x41,0x41,0x41,0x22}, // 0x43 'C'
    {0x7F,0x41,0x41,0x22,0x1C}, // 0x44 'D'
    {0x7F,0x49,0x49,0x49,0x41}, // 0x45 'E'
    {0x7F,0x09,0x09,0x09,0x01}, // 0x46 'F'
    {0x3E,0x41,0x49,0x49,0x7A}, // 0x47 'G'
    {0x7F,0x08,0x08,0x08,0x7F}, // 0x48 'H'
    {0x00,0x41,0x7F,0x41,0x00}, // 0x49 'I'
    {0x20,0x40,0x41,0x3F,0x01}, // 0x4A 'J'
    {0x7F,0x08,0x14,0x22,0x41}, // 0x4B 'K'
    {0x7F,0x40,0x40,0x40,0x40}, // 0x4C 'L'
    {0x7F,0x02,0x0C,0x02,0x7F}, // 0x4D 'M'
    {0x7F,0x04,0x08,0x10,0x7F}, // 0x4E 'N'
    {0x3E,0x41,0x41,0x41,0x3E}, // 0x4F 'O'
    {0x7F,0x09,0x09,0x09,0x06}, // 0x50 'P'
    {0x3E,0x41,0x51,0x21,0x5E}, // 0x51 'Q'
    {0x7F,0x09,0x19,0x29,0x46}, // 0x52 'R'
    {0x46,0x49,0x49,0x49,0x31}, // 0x53 'S'
    {0x01,0x01,0x7F,0x01,0x01}, // 0x54 'T'
    {0x3F,0x40,0x40,0x40,0x3F}, // 0x55 'U'
    {0x1F,0x20,0x40,0x20,0x1F}, // 0x56 'V'
    {0x3F,0x40,0x38,0x40,0x3F}, // 0x57 'W'
    {0x63,0x14,0x08,0x14,0x63}, // 0x58 'X'
    {0x07,0x08,0x70,0x08,0x07}, // 0x59 'Y'
    {0x61,0x51,0x49,0x45,0x43}, // 0x5A 'Z'
    {0x00,0x7F,0x41,0x41,0x00}, // 0x5B '['
    {0x02,0x04,0x08,0x10,0x20}, // 0x5C '\'
    {0x00,0x41,0x41,0x7F,0x00}, // 0x5D ']'
    {0x04,0x02,0x01,0x02,0x04}, // 0x5E '^'
    {0x40,0x40,0x40,0x40,0x40}, // 0x5F '_'
};

static const uint8_t *glyph_5x7(char c) {
    if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
    if (c == '|') {
        static const uint8_t BAR_GLYPH[5] = {0x00, 0x00, 0x7F, 0x00, 0x00};
        return BAR_GLYPH;
    }
    if (c < 0x20 || c > 0x5F) return FONT_5X7[0];
    return FONT_5X7[(uint8_t)(c - 0x20)];
}

static void draw_char_5x7(int x, int y, char c, uint8_t color, uint8_t glow_color) {
    const uint8_t *cols = glyph_5x7(c);
    for (int cx = 0; cx < 5; cx++) {
        uint8_t bits = cols[cx];
        if (!bits) continue;
        for (int cy = 0; cy < 7; cy++) {
            if (bits & (1u << cy)) {
                put_px(x + cx, y + cy, color);
                if (glow_color > 0u && cx < 4 && !(cols[cx + 1] & (1u << cy))) {
                    put_px(x + cx + 1, y + cy, glow_color);
                }
            }
        }
    }
}

static void draw_text_5x7(int x, int y, const char *str, int pitch_x,
                          uint8_t color, uint8_t glow_color) {
    if (!str) return;
    int cx = x;
    while (*str) {
        draw_char_5x7(cx, y, *str, color, glow_color);
        cx += pitch_x;
        str++;
    }
}

static void draw_text_5x7_centered(int center_x, int y, const char *str, int pitch_x,
                                   uint8_t color, uint8_t glow_color) {
    if (!str) return;
    int len = (int)strlen(str);
    if (len <= 0) return;
    int total_w = (len - 1) * pitch_x + 5;
    draw_text_5x7(center_x - total_w / 2, y, str, pitch_x, color, glow_color);
}

static void draw_char_odometer_clipped(int x, int y, char c,
                                       int clip_y1, int clip_y2,
                                       uint8_t base_color, bool tall_drum) {
    const uint8_t *cols = glyph_5x7(c);
    int h = tall_drum ? 11 : 7;
    for (int cy = 0; cy < h; cy++) {
        int py = y + cy;
        if (py < clip_y1 || py > clip_y2) continue;

        uint8_t row_col = base_color;
        if (py == clip_y1 || py == clip_y2) {
            row_col = (base_color > 5u) ? (uint8_t)(base_color - 5u) : 2u;
        } else if (py == clip_y1 + 1 || py == clip_y2 - 1) {
            row_col = (base_color > 2u) ? (uint8_t)(base_color - 2u) : 3u;
        }

        int src_y = tall_drum ? ((cy * 7) / 11) : cy;
        for (int cx = 0; cx < 5; cx++) {
            if (cols[cx] & (1u << src_y)) {
                put_px_clipped(x + cx, py, clip_y1, clip_y2, row_col);
                if (tall_drum) {
                    put_px_clipped(x + cx + 1, py, clip_y1, clip_y2,
                                   (row_col > 2u) ? (uint8_t)(row_col - 1u) : row_col);
                }
            }
        }
    }
}

static void draw_dot_matrix_ascii_centered(int center_x, int y, const char *text,
                                           uint8_t core_col, uint8_t glow_col) {
    if (!text) return;
    int len = (int)strlen(text);
    if (len <= 0) return;

    int pitch = (len > 11) ? 14 : 17;
    int cell_step = (len > 11) ? 2 : 3;
    int char_w = 5 * cell_step;
    int total_w = (len - 1) * pitch + char_w;
    int start_x = center_x - total_w / 2;

    for (int i = 0; i < len; i++) {
        const uint8_t *cols = glyph_5x7(text[i]);
        int bx = start_x + i * pitch;
        for (int cx = 0; cx < 5; cx++) {
            uint8_t bits = cols[cx];
            for (int cy = 0; cy < 7; cy++) {
                int px = bx + cx * cell_step;
                int py = y + cy * 3;
                if (bits & (1u << cy)) {
                    put_px(px,     py,     core_col);
                    put_px(px + 1, py,     core_col);
                    put_px(px,     py + 1, glow_col);
                    put_px(px + 1, py + 1, core_col);
                } else {
                    put_px(px, py, 1u);
                }
            }
        }
    }
}

static void draw_dot_matrix_chinese_brand(int center_x, int y,
                                          uint8_t core_col, uint8_t glow_col) {
    const int char_count = 5;
    const int pitch = 36;
    const int total_w = char_count * pitch - 4;
    int start_x = center_x - total_w / 2;

    for (int c = 0; c < char_count; c++) {
        const uint16_t *rows = drobotics_chinese_glyph_16x16((uint8_t)c);
        int bx = start_x + c * pitch;
        for (int ry = 0; ry < 15; ry++) {
            uint16_t row_bits = rows[ry];
            int py = y + (ry * 3) / 2;
            for (int rx = 0; rx < 16; rx++) {
                int px = bx + rx * 2;
                if (row_bits & (1u << (15 - rx))) {
                    put_px(px,     py,     core_col);
                    put_px(px + 1, py,     glow_col);
                    put_px(px,     py + 1, glow_col);
                } else if (((rx + ry) & 1) == 0) {
                    put_px(px, py, 1u);
                }
            }
        }
    }
}

// ---------------------------------------------------------------------------
// 4. Ultra-Fast Byte-Aligned 4x4 Halftone Cell Blitter (192 x 192 Square)
//    Each 4-pixel row is 2 bytes: { (p0 << 4) | p1, (p2 << 4) | p3 }
// ---------------------------------------------------------------------------
#define PACK4(p0, p1, p2, p3) \
    { (uint8_t)(((p0) << 4) | (p1)), (uint8_t)(((p2) << 4) | (p3)) }

// Concentric 4x4 circular halftone dots (with 2x2 center core at (1..2, 1..2))
static const uint8_t HALFTONE_4X4_DOTS[16][4][2] = {
    // 0: off
    { PACK4(0,0,0,0), PACK4(0,0,0,0), PACK4(0,0,0,0), PACK4(0,0,0,0) },
    // 1: faint 1x1 speck
    { PACK4(0,0,0,0), PACK4(0,3,0,0), PACK4(0,0,0,0), PACK4(0,0,0,0) },
    // 2: dim 2x1 speck
    { PACK4(0,0,0,0), PACK4(0,4,3,0), PACK4(0,3,0,0), PACK4(0,0,0,0) },
    // 3: dim 2x2 dot
    { PACK4(0,0,0,0), PACK4(0,5,5,0), PACK4(0,5,4,0), PACK4(0,0,0,0) },
    // 4: medium 2x2 dot
    { PACK4(0,1,1,0), PACK4(1,7,7,1), PACK4(1,7,6,1), PACK4(0,1,1,0) },
    // 5: bright 2x2 dot + soft halo
    { PACK4(0,2,2,0), PACK4(2,8,8,2), PACK4(2,8,8,2), PACK4(0,2,2,0) },
    // 6: warm circular dot
    { PACK4(0,3,3,0), PACK4(3,9,9,3), PACK4(3,9,9,3), PACK4(0,3,3,0) },
    // 7: medium-large circular dot
    { PACK4(0,4,4,0), PACK4(4,10,10,4), PACK4(4,10,10,4), PACK4(0,4,4,0) },
    // 8: classic amber circular dot
    { PACK4(1,5,5,1), PACK4(5,11,11,5), PACK4(5,11,11,5), PACK4(1,5,5,1) },
    // 9: rich amber-gold dot
    { PACK4(1,6,6,1), PACK4(6,12,12,6), PACK4(6,12,12,6), PACK4(1,6,6,1) },
    // 10: bright gold circular dot
    { PACK4(2,7,7,2), PACK4(7,13,13,7), PACK4(7,13,13,7), PACK4(2,7,7,2) },
    // 11: large glowing dot
    { PACK4(3,8,8,3), PACK4(8,13,13,8), PACK4(8,13,13,8), PACK4(3,8,8,3) },
    // 12: dense golden dot
    { PACK4(4,9,9,4), PACK4(9,14,14,9), PACK4(9,14,14,9), PACK4(4,9,9,4) },
    // 13: high-intensity specular dot
    { PACK4(5,11,11,5), PACK4(11,14,14,11), PACK4(11,14,14,11), PACK4(5,11,11,5) },
    // 14: hot-core dot
    { PACK4(7,12,12,7), PACK4(12,15,15,12), PACK4(12,15,15,12), PACK4(7,12,12,7) },
    // 15: incandescent 3D edge crease highlight
    { PACK4(10,14,14,10), PACK4(14,15,15,14), PACK4(14,15,15,14), PACK4(10,14,14,10) },
};

static inline void blit_cell_4x4(int px, int py, const uint8_t pat[4][2]) {
    uint8_t *dst = s_px_base + (uint32_t)py * s_px_stride + ((uint32_t)px >> 1);
    dst[0] = pat[0][0]; dst[1] = pat[0][1];
    dst += s_px_stride;
    dst[0] = pat[1][0]; dst[1] = pat[1][1];
    dst += s_px_stride;
    dst[0] = pat[2][0]; dst[1] = pat[2][1];
    dst += s_px_stride;
    dst[0] = pat[3][0]; dst[1] = pat[3][1];
}

static void render_3d_viewport(int vx, int vy) {
    drobotics_render_logo_grid(&s_state, s_logo_grid);
    drobotics_render_style_t style = s_state.render_style;

    static const uint8_t BG_EMPTY[4][2] = {
        PACK4(0,0,0,0), PACK4(0,0,0,0), PACK4(0,0,0,0), PACK4(0,0,0,0)
    };
    static const uint8_t BG_GRID_DOT[4][2] = {
        PACK4(0,0,0,0), PACK4(0,1,0,0), PACK4(0,0,0,0), PACK4(0,0,0,0)
    };

    for (int gy = 0; gy < DROBOTICS_LOGO_GRID_H; gy++) {
        int py = vy + (gy << 2);
        for (int gx = 0; gx < DROBOTICS_LOGO_GRID_W; gx++) {
            int px = vx + (gx << 2);
            uint8_t raw = s_logo_grid[gy][gx];
            uint8_t level = raw & 0x0Fu;
            bool is_crease = (raw & 0x80u) != 0u;

            if (level == 0u) {
                blit_cell_4x4(px, py, ((gx & 1) == 0 && (gy & 1) == 0) ? BG_GRID_DOT : BG_EMPTY);
                continue;
            }

            if (style == DROBOTICS_STYLE_AMBER_HALFTONE) {
                uint8_t idx = is_crease ? 15u : level;
                blit_cell_4x4(px, py, HALFTONE_4X4_DOTS[idx]);
            } else if (style == DROBOTICS_STYLE_SQUARE_LED) {
                // 3x3 square LED die with 1px dark bezel on right and bottom
                uint8_t c = is_crease ? 15u : (uint8_t)(level < 14u ? level + 1u : 15u);
                uint8_t r = (c > 3u) ? (uint8_t)(c - 2u) : c;
                uint8_t b0 = (uint8_t)((c << 4) | c);
                uint8_t b1 = (uint8_t)((r << 4) | 1u);
                uint8_t pat[4][2] = {
                    { b0, b1 },
                    { b0, b1 },
                    { (uint8_t)((r << 4) | r), (uint8_t)((r << 4) | 1u) },
                    { 0x11u, 0x10u }
                };
                blit_cell_4x4(px, py, pat);
            } else {
                // Green CRT Phosphor / ASCII Matrix style
                uint8_t fg = is_crease ? 15u : (uint8_t)(level < 14u ? level + 1u : 15u);
                uint8_t dm = (fg > 4u) ? (uint8_t)(fg - 4u) : 2u;
                uint8_t pat[4][2];
                if (is_crease || level >= 11u) {
                    pat[0][0] = (uint8_t)((dm << 4) | fg); pat[0][1] = (uint8_t)((fg << 4) | dm);
                    pat[1][0] = (uint8_t)((fg << 4) | 15u); pat[1][1] = (uint8_t)((15u << 4) | fg);
                    pat[2][0] = (uint8_t)((fg << 4) | 15u); pat[2][1] = (uint8_t)((15u << 4) | fg);
                    pat[3][0] = (uint8_t)((dm << 4) | fg); pat[3][1] = (uint8_t)((fg << 4) | dm);
                } else if (level >= 7u) {
                    pat[0][0] = (uint8_t)fg;                pat[0][1] = (uint8_t)(fg << 4);
                    pat[1][0] = (uint8_t)((fg << 4) | fg);  pat[1][1] = (uint8_t)((fg << 4) | fg);
                    pat[2][0] = (uint8_t)fg;                pat[2][1] = (uint8_t)(fg << 4);
                    pat[3][0] = 0u;                         pat[3][1] = 0u;
                } else {
                    pat[0][0] = 0u; pat[0][1] = 0u;
                    pat[1][0] = (uint8_t)fg; pat[1][1] = (uint8_t)(dm << 4);
                    pat[2][0] = (uint8_t)dm; pat[2][1] = 0u;
                    pat[3][0] = 0u; pat[3][1] = 0u;
                }
                blit_cell_4x4(px, py, pat);
            }
        }
    }
}

// ---------------------------------------------------------------------------
// 5. Mechanical Odometer Drum Box Renderer
// ---------------------------------------------------------------------------
static void draw_odometer_drums(const drobotics_odometer_t *odo,
                                int x, int y, int slot_w, int slot_h,
                                bool tall_drum) {
    if (!odo || odo->slot_count == 0u) return;
    int total_w = (int)odo->slot_count * slot_w + 2;

    fill_rect(x, y, total_w, slot_h, 0u);
    draw_rect(x, y, total_w, slot_h, 8u);

    int clip_y1 = y + 1;
    int clip_y2 = y + slot_h - 2;
    int char_h  = tall_drum ? 11 : 7;
    int pad_x   = (slot_w - (tall_drum ? 6 : 5)) / 2;
    int pad_y   = (slot_h - char_h) / 2;

    for (uint8_t i = 0; i < odo->slot_count; i++) {
        int sx = x + 1 + (int)i * slot_w;
        const drobotics_odometer_slot_t *s = &odo->slots[i];

        draw_hline(sx, sx + slot_w - 2, clip_y1, 1u);
        draw_hline(sx, sx + slot_w - 2, clip_y2, 1u);

        if (i > 0u) {
            draw_vline(sx, y + 1, y + slot_h - 2, 3u);
        }

        int base_x = sx + pad_x;
        int base_y = y + pad_y;

        if (!s->rolling || s->offset_q8 == 0u) {
            uint8_t col = (s->current_char == '#' || s->current_char == '-') ? 9u : 13u;
            draw_char_odometer_clipped(base_x, base_y, s->current_char,
                                       clip_y1, clip_y2, col, tall_drum);
        } else {
            int pitch_y = slot_h - 1;
            int shift = ((int)s->offset_q8 * pitch_y) >> 8;
            int cur_y = (s->direction >= 0) ? (base_y - shift) : (base_y + shift);
            int nxt_y = (s->direction >= 0) ? (base_y + pitch_y - shift)
                                            : (base_y - pitch_y + shift);
            draw_char_odometer_clipped(base_x, cur_y, s->current_char,
                                       clip_y1, clip_y2, 11u, tall_drum);
            draw_char_odometer_clipped(base_x, nxt_y, s->next_char,
                                       clip_y1, clip_y2, 15u, tall_drum);
        }
    }
}

// ---------------------------------------------------------------------------
// 6. Streamlined HUD Layout (Key Text Only + 192x192 Square 3D Viewport)
// ---------------------------------------------------------------------------
static void draw_top_header(void) {
    fill_rect(18, 10, 204, 19, 0u);
    fill_rect(22, 15, 7, 7, 11u);
    draw_text_5x7(33, 15, "> D-ROBOTICS", 6, 13u, 0u);
    if (s_state.cursor_visible) {
        fill_rect(107, 15, 5, 7, 15u);
    }

    // Top-right 7-slot rolling counter ("503.300" / "999.800")
    draw_odometer_drums(&s_state.top_odometer, 136, 12, 11, 13, false);
    draw_hline(16, 223, 29, 4u);
}

static void draw_square_viewport_brackets(void) {
    // 1:1 Square Frame around the 192x192 3D viewport (x: 20..219, y: 32..229)
    uint8_t brk_col = s_state.overclock_active ? 15u : 10u;
    draw_rect(20, 32, 200, 198, 2u);

    // Corner Brackets ┌ ┐ └ ┘
    draw_hline(19, 32, 32, brk_col);
    draw_vline(19, 32, 45, brk_col);

    draw_hline(207, 220, 32, brk_col);
    draw_vline(220, 32, 45, brk_col);

    draw_hline(19, 32, 229, brk_col);
    draw_vline(19, 216, 229, brk_col);

    draw_hline(207, 220, 229, brk_col);
    draw_vline(220, 216, 229, brk_col);
}

static void draw_bottom_badge(void) {
    fill_rect(12, 232, 216, 78, 0u);
    draw_hline(18, 221, 233, 6u);

    // 1. Main Brand Dot-Matrix Title (y: 237..258)
    if (s_state.badge_mode == DROBOTICS_BADGE_MODE_VIP_PASS) {
        draw_dot_matrix_ascii_centered(120, 237, "D-ROBOTICS", 14u, 9u);
    } else if (s_state.badge_mode == DROBOTICS_BADGE_MODE_BRAND_CN) {
        draw_dot_matrix_chinese_brand(120, 236, 14u, 9u);
    } else {
        draw_dot_matrix_ascii_centered(120, 237, "RDK DEVELOPER", 14u, 9u);
    }

    // 2. Key Holder Identity Lines (y: 263..283, centered & clean)
    draw_text_5x7_centered(120, 263, s_state.holder_name, 7, 14u, 8u);
    draw_text_5x7_centered(120, 275, s_state.role_tag, 7, 10u, 0u);

    // 3. Rolling Pass ID Bar (#2026-0001) + Compact Hazard Stripe Accent (y: 287..305)
    draw_text_5x7(22, 293, "PASS", 6, 11u, 0u);
    draw_odometer_drums(&s_state.pass_odometer, 52, 287, 13, 18, true);

    // Compact right hazard stripe block (x: 188..216, y: 288..304)
    draw_rect(188, 288, 28, 16, 7u);
    for (int py = 289; py < 303; py++) {
        for (int px = 189; px < 215; px++) {
            int phase = (px + py) & 7;
            put_px(px, py, (phase < 3) ? 9u : 1u);
        }
    }
}

static void draw_full_hud_static(void) {
    fill_rect(0, 0, SCREEN_W, SCREEN_H, 0u);

    // Outer 45-degree chamfered border inside R=30 rounded corners
    const uint8_t border_col = 5u;
    draw_hline(26, 213, 4, border_col);
    draw_hline(26, 213, 315, border_col);
    draw_vline(4, 26, 293, border_col);
    draw_vline(235, 26, 293, border_col);
    draw_line(26, 4, 4, 26, border_col);
    draw_line(213, 4, 235, 26, border_col);
    draw_line(4, 293, 26, 315, border_col);
    draw_line(213, 315, 235, 293, border_col);

    draw_top_header();
    draw_square_viewport_brackets();
    render_3d_viewport(VIEWPORT_X, VIEWPORT_Y);
    draw_bottom_badge();

    s_drawn_badge_mode = s_state.badge_mode;
    s_drawn_cursor = s_state.cursor_visible;
    s_drawn_overclock = s_state.overclock_active;
    s_full_hud_dirty = false;
}

// ---------------------------------------------------------------------------
// 7. LVGL Timer & Public Lifecycle API (Partial Dirty-Rect Invalidation)
// ---------------------------------------------------------------------------
static void passport_timer_cb(lv_timer_t *timer) {
    (void)timer;
    if (!s_scr || !s_canvas) return;

    uint64_t now_us = (uint64_t)esp_timer_get_time();
    uint32_t dt_ms = FRAME_PERIOD_MS;
    if (s_last_frame_us != 0u && now_us > s_last_frame_us) {
        uint64_t diff_us = now_us - s_last_frame_us;
        dt_ms = (uint32_t)(diff_us / 1000u);
        if (dt_ms < 5u) dt_ms = 5u;
        if (dt_ms > 80u) dt_ms = 80u;
    }
    s_last_frame_us = now_us;

    s_bat_elapsed_ms += dt_ms;
    if (s_bat_elapsed_ms >= BATTERY_POLL_MS) {
        s_bat_elapsed_ms = 0u;
        s_battery_soc = bsp_battery_soc();
    }

    bool top_was_rolling  = drobotics_odometer_is_rolling(&s_state.top_odometer);
    bool pass_was_rolling = drobotics_odometer_is_rolling(&s_state.pass_odometer);

    drobotics_passport_step(&s_state, (uint16_t)dt_ms);
    apply_canvas_palette(s_state.render_style);

    if (s_full_hud_dirty || s_state.badge_mode != s_drawn_badge_mode) {
        draw_full_hud_static();
        lv_obj_invalidate(s_canvas);
        return;
    }

    // 1. Always update the 192x192 square 3D viewport and invalidate ONLY its rect
    if (s_state.overclock_active != s_drawn_overclock) {
        draw_square_viewport_brackets();
        s_drawn_overclock = s_state.overclock_active;
    }
    render_3d_viewport(VIEWPORT_X, VIEWPORT_Y);
    lv_area_t vp_area = {
        .x1 = VIEWPORT_X,
        .y1 = VIEWPORT_Y,
        .x2 = VIEWPORT_X + DROBOTICS_LOGO_VIEW_W - 1,
        .y2 = VIEWPORT_Y + DROBOTICS_LOGO_VIEW_H - 1,
    };
    lv_obj_invalidate_area(s_canvas, &vp_area);

    // 2. Only redraw & invalidate top header when top odometer rolls or cursor toggles
    bool top_rolling = drobotics_odometer_is_rolling(&s_state.top_odometer);
    if (top_rolling || top_was_rolling || s_state.cursor_visible != s_drawn_cursor) {
        draw_top_header();
        s_drawn_cursor = s_state.cursor_visible;
        lv_area_t top_area = { .x1 = 18, .y1 = 10, .x2 = 222, .y2 = 28 };
        lv_obj_invalidate_area(s_canvas, &top_area);
    }

    // 3. Only redraw & invalidate bottom Pass ID odometer when its reels are rolling
    bool pass_rolling = drobotics_odometer_is_rolling(&s_state.pass_odometer);
    if (pass_rolling || pass_was_rolling) {
        draw_odometer_drums(&s_state.pass_odometer, 52, 287, 13, 18, true);
        lv_area_t pass_area = { .x1 = 52, .y1 = 287, .x2 = 186, .y2 = 305 };
        lv_obj_invalidate_area(s_canvas, &pass_area);
    }
}

void ui_drobotics_passport_configure(const char *holder_name,
                                     const char *role_tag,
                                     const char *pass_id) {
    if (!bsp_lvgl_lock(250)) return;
    drobotics_passport_set_identity(&s_state, holder_name, role_tag, pass_id);
    s_full_hud_dirty = true;
    bsp_lvgl_unlock();
}

void ui_drobotics_passport_enter(void) {
    if (s_scr) return;

    drobotics_passport_init(&s_state);
    s_applied_style = DROBOTICS_STYLE_COUNT;
    s_drawn_badge_mode = DROBOTICS_BADGE_MODE_COUNT;
    s_full_hud_dirty = true;
    s_last_frame_us = (uint64_t)esp_timer_get_time();
    s_bat_elapsed_ms = 0u;
    s_battery_soc = bsp_battery_soc();

    s_scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_scr, lv_color_hex(DROBOTICS_COLOR_BG), 0);
    lv_obj_set_style_bg_opa(s_scr, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_scr, 0, 0);
    lv_obj_set_style_pad_all(s_scr, 0, 0);
    lv_obj_remove_flag(s_scr, LV_OBJ_FLAG_SCROLLABLE);

    LV_DRAW_BUF_INIT_STATIC(s_passport_buf);
    s_canvas = lv_canvas_create(s_scr);
    lv_canvas_set_draw_buf(s_canvas, &s_passport_buf);
    lv_obj_set_pos(s_canvas, 0, 0);
    lv_obj_set_size(s_canvas, SCREEN_W, SCREEN_H);

    lv_draw_buf_t *db = lv_canvas_get_draw_buf(s_canvas);
    s_px_base = lv_draw_buf_goto_xy(db, 0, 0);
    s_px_stride = db->header.stride;

    apply_canvas_palette(s_state.render_style);
    draw_full_hud_static();
    lv_obj_invalidate(s_canvas);

    s_timer = lv_timer_create(passport_timer_cb, FRAME_PERIOD_MS, NULL);
    lv_screen_load(s_scr);

    ESP_LOGI(TAG, "D-Robotics 2026 Passport UI ready (192x192 square, 1.25x): %s | %s | %s",
             s_state.holder_name, s_state.role_tag, s_state.pass_id);
}

void ui_drobotics_passport_exit(void) {
    if (s_timer) {
        lv_timer_delete(s_timer);
        s_timer = NULL;
    }
    if (s_scr) {
        lv_obj_delete(s_scr);
        s_scr = NULL;
        s_canvas = NULL;
    }
    s_px_base = NULL;
}

void ui_drobotics_passport_handle_key(bsp_btn_t btn, bsp_btn_ev_t ev) {
    drobotics_key_ev_t logic_ev;
    switch (ev) {
        case BSP_BTN_PRESS:   logic_ev = DROBOTICS_KEY_EVENT_PRESS;   break;
        case BSP_BTN_CLICK:   logic_ev = DROBOTICS_KEY_EVENT_CLICK;   break;
        case BSP_BTN_DOUBLE:  logic_ev = DROBOTICS_KEY_EVENT_DOUBLE;  break;
        case BSP_BTN_LONG:    logic_ev = DROBOTICS_KEY_EVENT_LONG;    break;
        case BSP_BTN_RELEASE: logic_ev = DROBOTICS_KEY_EVENT_RELEASE; break;
        default: return;
    }

    if (!bsp_lvgl_lock(250)) return;
    drobotics_passport_handle_key(&s_state, (int)btn, logic_ev);
    bsp_lvgl_unlock();
}
