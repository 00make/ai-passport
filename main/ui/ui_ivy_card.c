#include "ui_ivy_card.h"
#include "ivy_card_logic.h"
#include "ivy_sprites_data.h"
#include "bsp_battery.h"
#include "bsp_display.h"
#include "lvgl.h"
#include "esp_log.h"
#include "esp_timer.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *TAG = "ui_ivy_card";

#define RGB565(r, g, b) ((uint16_t)((((r) >> 3) << 11) | (((g) >> 2) << 5) | ((b) >> 3)))

#define STAGE_CX        (IVY_STAGE_W / 2)
#define DOCK_X          16
#define DOCK_Y          262
#define DOCK_W          208
#define DOCK_H          54

#define C_SCREEN_BG     RGB565(7, 8, 11)
#define C_BG_LEFT       RGB565(17, 15, 11)
#define C_BG_RIGHT      RGB565(10, 12, 18)
#define C_GRID_LEFT     RGB565(46, 37, 18)
#define C_GRID_RIGHT    RGB565(24, 34, 42)
#define C_SPLIT_LINE    RGB565(82, 70, 36)
#define C_FLOOR_FILL    RGB565(21, 24, 32)
#define C_AURA_DARK     RGB565(34, 30, 20)

#define C_ROSE          RGB565(255, 107, 157)
#define C_CREAM_PINK    RGB565(255, 214, 231)
#define C_GOLD          RGB565(255, 209, 102)
#define C_LIME          RGB565(168, 255, 53)
#define C_AMBER         RGB565(255, 178, 88)
#define C_CYAN          RGB565(116, 214, 255)
#define C_WHITE         RGB565(250, 248, 255)
#define C_MUTED         RGB565(184, 172, 198)
#define C_PILL_BG       RGB565(26, 18, 30)
#define C_DOCK_BG       RGB565(22, 16, 26)
#define C_DOCK_DIV      RGB565(72, 48, 76)
#define C_DOCK_TRACK    RGB565(44, 32, 50)
#define C_HEART_DIM     RGB565(78, 56, 82)

#define SCREEN_BG_HEX   0x07080Bu

LV_DRAW_BUF_DEFINE_STATIC(s_stage_buf, IVY_STAGE_W, IVY_STAGE_H, LV_COLOR_FORMAT_RGB565);

typedef struct {
    uint16_t *px;
    int w;
    int h;
    uint32_t stride_px;
} ivy_fb_t;

static ivy_card_state_t s_state;
static lv_obj_t *s_scr;
static lv_obj_t *s_canvas;
static lv_obj_t *s_dock;
static lv_draw_buf_t s_dock_buf;
static uint8_t *s_dock_raw;

static int s_last_bat = -2;
static int s_last_tab = -1;
static int s_last_mood = -1;
static int s_last_poster = -1;
static uint32_t s_bat_tick;

/* Compact 5x7 ASCII bitmap glyphs (columns 0..4, LSB = top row). */
typedef struct {
    char ch;
    uint8_t cols[5];
} font5x7_t;

static const font5x7_t s_font5x7[] = {
    { ' ', { 0x00, 0x00, 0x00, 0x00, 0x00 } },
    { '!', { 0x00, 0x00, 0x5F, 0x00, 0x00 } },
    { '"', { 0x00, 0x07, 0x00, 0x07, 0x00 } },
    { '#', { 0x14, 0x7F, 0x14, 0x7F, 0x14 } },
    { '%', { 0x23, 0x13, 0x08, 0x64, 0x62 } },
    { '\'',{ 0x00, 0x05, 0x03, 0x00, 0x00 } },
    { '(', { 0x00, 0x1C, 0x22, 0x41, 0x00 } },
    { ')', { 0x00, 0x41, 0x22, 0x1C, 0x00 } },
    { '*', { 0x14, 0x08, 0x3E, 0x08, 0x14 } },
    { '+', { 0x08, 0x08, 0x3E, 0x08, 0x08 } },
    { ',', { 0x00, 0x50, 0x30, 0x00, 0x00 } },
    { '-', { 0x08, 0x08, 0x08, 0x08, 0x08 } },
    { '.', { 0x00, 0x60, 0x60, 0x00, 0x00 } },
    { '/', { 0x20, 0x10, 0x08, 0x04, 0x02 } },
    { '0', { 0x3E, 0x51, 0x49, 0x45, 0x3E } },
    { '1', { 0x00, 0x42, 0x7F, 0x40, 0x00 } },
    { '2', { 0x42, 0x61, 0x51, 0x49, 0x46 } },
    { '3', { 0x21, 0x41, 0x45, 0x4B, 0x31 } },
    { '4', { 0x18, 0x14, 0x12, 0x7F, 0x10 } },
    { '5', { 0x27, 0x45, 0x45, 0x45, 0x39 } },
    { '6', { 0x3C, 0x4A, 0x49, 0x49, 0x30 } },
    { '7', { 0x01, 0x71, 0x09, 0x05, 0x03 } },
    { '8', { 0x36, 0x49, 0x49, 0x49, 0x36 } },
    { '9', { 0x06, 0x49, 0x49, 0x29, 0x1E } },
    { ':', { 0x00, 0x36, 0x36, 0x00, 0x00 } },
    { ';', { 0x00, 0x56, 0x36, 0x00, 0x00 } },
    { '<', { 0x08, 0x14, 0x22, 0x41, 0x00 } },
    { '>', { 0x00, 0x41, 0x22, 0x14, 0x08 } },
    { '?', { 0x02, 0x01, 0x51, 0x09, 0x06 } },
    { '@', { 0x32, 0x49, 0x79, 0x41, 0x3E } },
    { 'A', { 0x7E, 0x11, 0x11, 0x11, 0x7E } },
    { 'B', { 0x7F, 0x49, 0x49, 0x49, 0x36 } },
    { 'C', { 0x3E, 0x41, 0x41, 0x41, 0x22 } },
    { 'D', { 0x7F, 0x41, 0x41, 0x22, 0x1C } },
    { 'E', { 0x7F, 0x49, 0x49, 0x49, 0x41 } },
    { 'F', { 0x7F, 0x09, 0x09, 0x09, 0x01 } },
    { 'G', { 0x3E, 0x41, 0x49, 0x49, 0x7A } },
    { 'H', { 0x7F, 0x08, 0x08, 0x08, 0x7F } },
    { 'I', { 0x00, 0x41, 0x7F, 0x41, 0x00 } },
    { 'J', { 0x20, 0x40, 0x41, 0x3F, 0x01 } },
    { 'K', { 0x7F, 0x08, 0x14, 0x22, 0x41 } },
    { 'L', { 0x7F, 0x40, 0x40, 0x40, 0x40 } },
    { 'M', { 0x7F, 0x02, 0x0C, 0x02, 0x7F } },
    { 'N', { 0x7F, 0x04, 0x08, 0x10, 0x7F } },
    { 'O', { 0x3E, 0x41, 0x41, 0x41, 0x3E } },
    { 'P', { 0x7F, 0x09, 0x09, 0x09, 0x06 } },
    { 'Q', { 0x3E, 0x41, 0x51, 0x21, 0x5E } },
    { 'R', { 0x7F, 0x09, 0x19, 0x29, 0x46 } },
    { 'S', { 0x46, 0x49, 0x49, 0x49, 0x31 } },
    { 'T', { 0x01, 0x01, 0x7F, 0x01, 0x01 } },
    { 'U', { 0x3F, 0x40, 0x40, 0x40, 0x3F } },
    { 'V', { 0x1F, 0x20, 0x40, 0x20, 0x1F } },
    { 'W', { 0x3F, 0x40, 0x38, 0x40, 0x3F } },
    { 'X', { 0x63, 0x14, 0x08, 0x14, 0x63 } },
    { 'Y', { 0x07, 0x08, 0x70, 0x08, 0x07 } },
    { 'Z', { 0x61, 0x51, 0x49, 0x45, 0x43 } },
    { '[', { 0x00, 0x7F, 0x41, 0x41, 0x00 } },
    { ']', { 0x00, 0x41, 0x41, 0x7F, 0x00 } },
    { '^', { 0x04, 0x02, 0x01, 0x02, 0x04 } },
    { '_', { 0x40, 0x40, 0x40, 0x40, 0x40 } },
    { 'o', { 0x38, 0x44, 0x44, 0x44, 0x38 } },
    { 'v', { 0x1C, 0x20, 0x40, 0x20, 0x1C } },
};

static ivy_fb_t get_stage_fb(void) {
    uint32_t stride = s_stage_buf.header.stride / sizeof(uint16_t);
    return (ivy_fb_t){
        .px = (uint16_t *)s_stage_buf.data,
        .w = IVY_STAGE_W,
        .h = IVY_STAGE_H,
        .stride_px = stride >= IVY_STAGE_W ? stride : IVY_STAGE_W,
    };
}

static ivy_fb_t get_dock_fb(void) {
    uint32_t stride = s_dock_buf.header.stride / sizeof(uint16_t);
    return (ivy_fb_t){
        .px = (uint16_t *)s_dock_buf.data,
        .w = DOCK_W,
        .h = DOCK_H,
        .stride_px = stride >= DOCK_W ? stride : DOCK_W,
    };
}

static inline void fb_put_px(const ivy_fb_t *fb, int x, int y, uint16_t col) {
    if ((unsigned)x < (unsigned)fb->w && (unsigned)y < (unsigned)fb->h) {
        fb->px[(uint32_t)y * fb->stride_px + (uint32_t)x] = col;
    }
}

static void fb_fill_rect(const ivy_fb_t *fb, int x, int y, int w, int h, uint16_t col) {
    int x0 = x < 0 ? 0 : x;
    int y0 = y < 0 ? 0 : y;
    int x1 = (x + w) > fb->w ? fb->w : (x + w);
    int y1 = (y + h) > fb->h ? fb->h : (y + h);
    for (int yy = y0; yy < y1; ++yy) {
        uint16_t *row = fb->px + (uint32_t)yy * fb->stride_px;
        for (int xx = x0; xx < x1; ++xx) {
            row[xx] = col;
        }
    }
}

static bool round_rect_inside(int lx, int ly, int w, int h, int r) {
    if (lx < 0 || lx >= w || ly < 0 || ly >= h) return false;
    if (r <= 0) return true;
    int cx = (lx < r) ? (r - 1 - lx) : ((lx >= w - r) ? (lx - (w - r)) : -1);
    int cy = (ly < r) ? (r - 1 - ly) : ((ly >= h - r) ? (ly - (h - r)) : -1);
    if (cx < 0 || cy < 0) return true;
    int dx2 = 2 * cx + 1;
    int dy2 = 2 * cy + 1;
    return (dx2 * dx2 + dy2 * dy2) <= (4 * r * r);
}

static void fb_draw_round_rect(const ivy_fb_t *fb, int x, int y, int w, int h, int r,
                               bool fill, uint16_t fill_col,
                               bool border, uint16_t border_col) {
    for (int ly = 0; ly < h; ++ly) {
        for (int lx = 0; lx < w; ++lx) {
            if (!round_rect_inside(lx, ly, w, h, r)) continue;
            bool is_edge = !round_rect_inside(lx - 1, ly, w, h, r)
                        || !round_rect_inside(lx + 1, ly, w, h, r)
                        || !round_rect_inside(lx, ly - 1, w, h, r)
                        || !round_rect_inside(lx, ly + 1, w, h, r);
            if (border && is_edge) {
                fb_put_px(fb, x + lx, y + ly, border_col);
            } else if (fill) {
                fb_put_px(fb, x + lx, y + ly, fill_col);
            }
        }
    }
}

static void draw_mini_heart(const ivy_fb_t *fb, int cx, int cy, uint16_t col) {
    static const uint8_t heart_rows[6] = { 0x36, 0x7F, 0x7F, 0x3E, 0x1C, 0x08 };
    for (int r = 0; r < 6; ++r) {
        uint8_t row = heart_rows[r];
        for (int c = 0; c < 7; ++c) {
            if ((row >> (6 - c)) & 1u) {
                fb_put_px(fb, cx + c - 3, cy + r - 3, col);
            }
        }
    }
}

static void draw_stage_backdrop(const ivy_fb_t *fb, uint32_t tick, ivy_hero_mode_t mode) {
    /* Blonde-left / obsidian-right split backdrop honoring IVY's signature hair */
    for (int y = 0; y < fb->h; ++y) {
        uint16_t *row = fb->px + (uint32_t)y * fb->stride_px;
        for (int x = 0; x < STAGE_CX; ++x) {
            row[x] = C_BG_LEFT;
        }
        for (int x = STAGE_CX; x < fb->w; ++x) {
            row[x] = C_BG_RIGHT;
        }
        if ((y & 3) == 0) {
            row[STAGE_CX - 1] = C_SPLIT_LINE;
            row[STAGE_CX] = C_SPLIT_LINE;
        }
    }

    /* Subtle scrolling dot matrix */
    int grid_shift = (int)((tick / 4u) & 15u);
    for (int y = grid_shift; y < fb->h; y += 16) {
        uint16_t *row = fb->px + (uint32_t)y * fb->stride_px;
        for (int x = 8; x < fb->w; x += 16) {
            row[x] = (x < STAGE_CX) ? C_GRID_LEFT : C_GRID_RIGHT;
        }
    }

    if (mode == IVY_MODE_EXPRESSIONS) {
        const int cx = STAGE_CX;
        const int cy = 126;
        for (int dy = -94; dy <= 94; ++dy) {
            int y = cy + dy;
            if ((unsigned)y >= (unsigned)fb->h) continue;
            uint16_t *row = fb->px + (uint32_t)y * fb->stride_px;
            int dy2 = dy * dy;
            for (int dx = -94; dx <= 94; ++dx) {
                int x = cx + dx;
                if ((unsigned)x >= (unsigned)fb->w) continue;
                int d2 = dx * dx + dy2;
                if (d2 >= 7744 && d2 <= 8836) {
                    if (d2 >= 8464) {
                        row[x] = (x < STAGE_CX) ? C_GOLD : C_ROSE;
                    } else {
                        row[x] = C_AURA_DARK;
                    }
                }
            }
        }
    } else {
        /* Glowing pedestal ring under IVY's feet */
        const int cx = STAGE_CX;
        const int cy = 246;
        const int rx = 84;
        const int ry = 10;
        for (int dy = -ry; dy <= ry; ++dy) {
            int y = cy + dy;
            if ((unsigned)y >= (unsigned)fb->h) continue;
            uint16_t *row = fb->px + (uint32_t)y * fb->stride_px;
            for (int dx = -rx; dx <= rx; ++dx) {
                int x = cx + dx;
                if ((unsigned)x >= (unsigned)fb->w) continue;
                int32_t ex = (dx * 32) / rx;
                int32_t ey = (dy * 32) / ry;
                int32_t norm = ex * ex + ey * ey;
                if (norm <= 1024) {
                    if (norm >= 840) {
                        row[x] = (x < STAGE_CX) ? C_GOLD : C_ROSE;
                    } else if (norm >= 420 && norm <= 490) {
                        row[x] = C_SPLIT_LINE;
                    } else {
                        row[x] = C_FLOOR_FILL;
                    }
                }
            }
        }
    }
}

static void blit_sprite(const ivy_fb_t *fb, ivy_sprite_id_t id, int dst_x, int dst_y) {
    const ivy_sprite_t *sp = ivy_sprite_get(id);
    if (!sp || !sp->pixels || !sp->palette) return;

    int x_start = 0;
    int y_start = 0;
    int x_end = (int)sp->w;
    int y_end = (int)sp->h;

    if (dst_x < 0) x_start = -dst_x;
    if (dst_y < 0) y_start = -dst_y;
    if (dst_x + x_end > fb->w) x_end = fb->w - dst_x;
    if (dst_y + y_end > fb->h) y_end = fb->h - dst_y;
    if (x_start >= x_end || y_start >= y_end) return;

    const uint16_t *pal = sp->palette;
    for (int sy = y_start; sy < y_end; ++sy) {
        const uint8_t *src = sp->pixels + (uint32_t)sy * sp->w;
        uint16_t *dst = fb->px + (uint32_t)(dst_y + sy) * fb->stride_px + dst_x;
        for (int sx = x_start; sx < x_end; ++sx) {
            uint8_t idx = src[sx];
            if (idx != 0u) {
                dst[sx] = pal[idx];
            }
        }
    }
}

static const uint8_t *find_char5x7(char c) {
    if (c >= 'a' && c <= 'z' && c != 'v' && c != 'o') {
        c = (char)(c - ('a' - 'A'));
    }
    size_t count = sizeof(s_font5x7) / sizeof(s_font5x7[0]);
    for (size_t i = 0; i < count; ++i) {
        if (s_font5x7[i].ch == c) {
            return s_font5x7[i].cols;
        }
    }
    return s_font5x7[0].cols;
}

static void fb_draw_ascii_5x7(const ivy_fb_t *fb, int x, int y, const char *txt, uint16_t col) {
    if (!txt) return;
    int cx = x;
    while (*txt) {
        if (*txt == ' ') {
            cx += 4;
            txt++;
            continue;
        }
        const uint8_t *cols = find_char5x7(*txt);
        for (int i = 0; i < 5; ++i) {
            uint8_t bits = cols[i];
            for (int r = 0; r < 7; ++r) {
                if ((bits >> r) & 1u) {
                    fb_put_px(fb, cx + i, y + r, col);
                }
            }
        }
        cx += 6;
        txt++;
    }
}

static uint32_t utf8_next_cp(const char **p) {
    const uint8_t *s = (const uint8_t *)(*p);
    if (!s || !*s) return 0;
    uint32_t cp;
    if (s[0] < 0x80u) {
        cp = s[0];
        *p += 1;
    } else if ((s[0] & 0xE0u) == 0xC0u && s[1]) {
        cp = ((uint32_t)(s[0] & 0x1Fu) << 6) | (uint32_t)(s[1] & 0x3Fu);
        *p += 2;
    } else if ((s[0] & 0xF0u) == 0xE0u && s[1] && s[2]) {
        cp = ((uint32_t)(s[0] & 0x0Fu) << 12)
           | ((uint32_t)(s[1] & 0x3Fu) << 6)
           | (uint32_t)(s[2] & 0x3Fu);
        *p += 3;
    } else {
        cp = '?';
        *p += 1;
    }
    return cp;
}

static int fb_measure_utf8_12(const char *txt) {
    if (!txt) return 0;
    const char *p = txt;
    int w = 0;
    int last_gap = 0;
    while (*p) {
        uint32_t cp = utf8_next_cp(&p);
        if (cp == ' ') {
            w += 4;
            last_gap = 4;
        } else if (cp < 0x80u) {
            w += 6;
            last_gap = 1;
        } else {
            w += 13;
            last_gap = 1;
        }
    }
    return w > last_gap ? (w - last_gap) : w;
}

static uint16_t glyph_tint(uint32_t cp, uint16_t default_col) {
    switch (cp) {
        case 0x2665u: /* ♥ */
        case 0x273Fu: /* ✿ */
            return C_ROSE;
        case 0x2605u: /* ★ */
        case 0x2726u: /* ✦ */
        case 0x00B7u: /* · */
            return C_GOLD;
        case 0x266Au: /* ♪ */
            return C_CYAN;
        default:
            return default_col;
    }
}

static void fb_draw_utf8_12(const ivy_fb_t *fb, int x, int y, const char *utf8_txt, uint16_t text_col) {
    if (!utf8_txt) return;
    int cx = x;
    const char *p = utf8_txt;
    while (*p) {
        uint32_t cp = utf8_next_cp(&p);
        if (cp == ' ') {
            cx += 4;
        } else if (cp < 0x80u) {
            char tmp[2] = { (char)cp, '\0' };
            fb_draw_ascii_5x7(fb, cx, y + 3, tmp, text_col);
            cx += 6;
        } else {
            const uint16_t *rows = ivy_glyph12_find(cp);
            uint16_t col = glyph_tint(cp, text_col);
            if (rows) {
                for (int r = 0; r < 12; ++r) {
                    uint16_t bits = rows[r];
                    for (int c = 0; c < 12; ++c) {
                        if ((bits >> (11 - c)) & 1u) {
                            fb_put_px(fb, cx + c, y + r, col);
                        }
                    }
                }
            }
            cx += 13;
        }
    }
}

static void draw_cute_pill(const ivy_fb_t *fb, int x, int y, const char *utf8_txt,
                           uint16_t border_col, uint16_t text_col) {
    if (!utf8_txt || !*utf8_txt) return;
    int tw = fb_measure_utf8_12(utf8_txt);
    int pw = tw + 10;
    int ph = 18;
    fb_draw_round_rect(fb, x, y, pw, ph, 4, true, C_PILL_BG, true, border_col);
    fb_draw_utf8_12(fb, x + 5, y + 3, utf8_txt, text_col);
}

static void draw_particles(const ivy_fb_t *fb, const ivy_card_state_t *st) {
    for (int i = 0; i < IVY_PARTICLE_MAX; ++i) {
        const ivy_particle_t *p = &st->particles[i];
        if (p->kind == IVY_PARTICLE_NONE || p->ttl == 0) continue;
        int px = p->x_q4 >> 4;
        int py = p->y_q4 >> 4;
        if (p->kind == IVY_PARTICLE_SWEAT) {
            fb_put_px(fb, px + 1, py, p->color565);
            fb_put_px(fb, px + 1, py + 1, p->color565);
            fb_fill_rect(fb, px, py + 2, 3, 2, p->color565);
            fb_put_px(fb, px + 1, py + 4, C_WHITE);
            fb_put_px(fb, px + 1, py + 2, C_WHITE);
        } else if (p->kind == IVY_PARTICLE_STAR) {
            fb_fill_rect(fb, px - 2, py, 5, 1, p->color565);
            fb_fill_rect(fb, px, py - 2, 1, 5, p->color565);
            fb_put_px(fb, px, py, C_WHITE);
        } else if (p->kind == IVY_PARTICLE_HEART) {
            draw_mini_heart(fb, px, py, p->color565);
        } else {
            bool flip = ((st->tick >> 1) & 1u) != 0u;
            fb_fill_rect(fb, px - 1, py - 1, flip ? 4 : 2, flip ? 2 : 4, p->color565);
        }
    }
}

static void render_stage_canvas(void) {
    ivy_fb_t fb = get_stage_fb();
    if (!fb.px) return;

    ivy_render_snapshot_t snap;
    ivy_card_logic_get_snapshot(&s_state, &snap);

    draw_stage_backdrop(&fb, s_state.tick, s_state.hero_mode);

    const ivy_sprite_t *main_sp = ivy_sprite_get(snap.main_sprite);
    if (main_sp) {
        int sx = (IVY_STAGE_W - (int)main_sp->w) / 2 + snap.sprite_dx;
        int sy;
        if (s_state.hero_mode == IVY_MODE_EXPRESSIONS) {
            sy = 14 + snap.sprite_dy;
        } else if (s_state.hero_mode == IVY_MODE_TURNAROUND) {
            sy = 252 - (int)main_sp->h + snap.sprite_dy;
        } else if (snap.show_offwork_banner) {
            sy = 228 - (int)main_sp->h + snap.sprite_dy;
        } else {
            sy = 244 - (int)main_sp->h + snap.sprite_dy;
        }
        blit_sprite(&fb, snap.main_sprite, sx, sy);
    }

    if (snap.show_offwork_banner) {
        const ivy_sprite_t *ban = ivy_sprite_get(IVY_SPRITE_OFFWORK_BANNER);
        if (ban) {
            int bx = (IVY_STAGE_W - (int)ban->w) / 2;
            int by = 220 + snap.banner_dy;
            blit_sprite(&fb, IVY_SPRITE_OFFWORK_BANNER, bx, by);
        }
    }

    draw_particles(&fb, &s_state);

    /* Cute rounded Chinese corner badges safely inside 30px top screen radius */
    bool is_hot = (s_state.hero_mode != IVY_MODE_STORY || s_state.story_phase != IVY_STORY_WORKING);
    draw_cute_pill(&fb, 16, 6, snap.stage_badge_utf8,
                   is_hot ? C_ROSE : C_GOLD, C_WHITE);

    if (snap.mode_pill && *snap.mode_pill) {
        int mw = fb_measure_utf8_12(snap.mode_pill) + 10;
        draw_cute_pill(&fb, IVY_STAGE_W - mw - 16, 6,
                       snap.mode_pill, is_hot ? C_GOLD : C_AMBER,
                       is_hot ? C_CREAM_PINK : C_GOLD);
    }

    if (!snap.show_offwork_banner && snap.stage_hint_ascii && *snap.stage_hint_ascii) {
        int hw = fb_measure_utf8_12(snap.stage_hint_ascii) + 10;
        draw_cute_pill(&fb, (IVY_STAGE_W - hw) / 2, IVY_STAGE_H - 19,
                       snap.stage_hint_ascii,
                       is_hot ? C_ROSE : C_GOLD,
                       C_WHITE);
    }

    lv_obj_invalidate(s_canvas);
}

static void render_dock_canvas(const ivy_render_snapshot_t *snap, int bat) {
    ivy_fb_t fb = get_dock_fb();
    if (!fb.px) return;

    /* Clear exterior pixels for clean rounded corners */
    fb_fill_rect(&fb, 0, 0, DOCK_W, DOCK_H, C_SCREEN_BG);

    uint16_t border_col = (s_state.card_tab == IVY_TAB_PROFILE)
                        ? (snap->mood_pct >= 90 ? C_ROSE : C_GOLD)
                        : ((s_state.card_tab == IVY_TAB_STATS) ? C_ROSE : C_GOLD);
    fb_draw_round_rect(&fb, 0, 0, DOCK_W, DOCK_H, 6, true, C_DOCK_BG, true, border_col);

    /* Soft header divider */
    fb_fill_rect(&fb, 8, 17, DOCK_W - 16, 1, C_DOCK_DIV);

    /* Battery % right-aligned before the 3 mini heart tab indicators */
    char bat_buf[12];
    if (bat >= 0) {
        snprintf(bat_buf, sizeof(bat_buf), "%d%%", bat);
    } else {
        snprintf(bat_buf, sizeof(bat_buf), "--%%");
    }
    int bat_w = fb_measure_utf8_12(bat_buf);
    fb_draw_ascii_5x7(&fb, 166 - bat_w, 6, bat_buf, C_MUTED);

    for (int i = 0; i < IVY_TAB_COUNT; ++i) {
        bool active = (i == (int)s_state.card_tab);
        draw_mini_heart(&fb, 175 + i * 10, 9, active ? C_ROSE : C_HEART_DIM);
    }

    const char *hdr = NULL;
    const char *l1 = NULL;
    const char *l2 = NULL;
    uint16_t l1_col = C_WHITE;
    uint16_t l2_col = C_CREAM_PINK;
    uint16_t bar_col = C_ROSE;
    int bar_pct = snap->mood_pct;

    if (s_state.card_tab == IVY_TAB_PROFILE) {
        hdr = "♥ " IVY_CARD_NAME " 专属甜心卡";
        if (snap->mood_pct < 50) {
            l1 = "状态: 努力搬砖 · 等你接下班 ♥";
            l1_col = C_AMBER;
            bar_col = C_AMBER;
        } else if (snap->mood_pct < 90) {
            l1 = "状态: 准备下班 · 开心加倍 ✦";
            l1_col = C_GOLD;
            bar_col = C_GOLD;
        } else {
            l1 = "身份: 甜酷小仙女 · 快乐满分 ♥";
            l1_col = C_CREAM_PINK;
            bar_col = C_ROSE;
        }
        l2 = "OK互动 · 上换模式 · 下翻页";
        l2_col = C_MUTED;
    } else if (s_state.card_tab == IVY_TAB_STATS) {
        hdr = "✦ 我家宝贝属性卡";
        l1 = "可爱值: 999+  甜酷值: 满分 ♥";
        l1_col = C_GOLD;
        l2 = "心动指数: ♥♥♥♥♥ 超喜欢你";
        l2_col = C_CREAM_PINK;
        bar_col = C_ROSE;
        bar_pct = 100;
    } else {
        hdr = "✿ 送给宝贝的小礼物";
        l1 = "愿宝贝每天准点下班 超开心 ♥";
        l1_col = C_CREAM_PINK;
        l2 = "专属小礼物 · 永远偏爱你 ♥";
        l2_col = C_GOLD;
        bar_col = C_CYAN;
        bar_pct = 100;
    }

    fb_draw_utf8_12(&fb, 8, 3, hdr, C_GOLD);
    fb_draw_utf8_12(&fb, 8, 20, l1, l1_col);
    fb_draw_utf8_12(&fb, 8, 34, l2, l2_col);

    /* Bottom rounded love/mood progress bar */
    const int bar_w = DOCK_W - 16;
    int fill_w = (bar_w * bar_pct) / 100;
    fb_fill_rect(&fb, 8, 48, bar_w, 3, C_DOCK_TRACK);
    if (fill_w > 0) {
        fb_fill_rect(&fb, 8, 48, fill_w, 3, bar_col);
    }

    lv_obj_invalidate(s_dock);
}

static void refresh_hud_and_dock(void) {
    ivy_render_snapshot_t snap;
    ivy_card_logic_get_snapshot(&s_state, &snap);

    /* Toggle bottom dock in poster_mode */
    if ((int)s_state.poster_mode != s_last_poster) {
        s_last_poster = (int)s_state.poster_mode;
        if (s_state.poster_mode) {
            if (s_dock) lv_obj_add_flag(s_dock, LV_OBJ_FLAG_HIDDEN);
            lv_obj_set_pos(s_canvas, 0, 30);
        } else {
            if (s_dock) lv_obj_clear_flag(s_dock, LV_OBJ_FLAG_HIDDEN);
            lv_obj_set_pos(s_canvas, 0, 0);
        }
    }

    bool need_dock_redraw = false;
    if ((s_bat_tick % 30u) == 0u || s_last_bat == -2) {
        int bat = bsp_battery_soc();
        if (bat != s_last_bat) {
            s_last_bat = bat;
            need_dock_redraw = true;
        }
    }
    s_bat_tick++;

    if ((int)s_state.card_tab != s_last_tab || (int)snap.mood_pct != s_last_mood) {
        s_last_tab = (int)s_state.card_tab;
        s_last_mood = (int)snap.mood_pct;
        need_dock_redraw = true;
    }

    if (need_dock_redraw && s_dock) {
        render_dock_canvas(&snap, s_last_bat);
    }
}

static void timer_cb(lv_timer_t *timer) {
    (void)timer;
    ivy_card_logic_tick(&s_state);
    render_stage_canvas();
    refresh_hud_and_dock();
}

void ui_ivy_card_init(void) {
    if (s_scr) return;

    ivy_card_logic_init(&s_state);
    s_last_bat = -2;
    s_last_tab = -1;
    s_last_mood = -1;
    s_last_poster = -1;
    s_bat_tick = 0;

    s_scr = lv_obj_create(NULL);
    lv_obj_remove_flag(s_scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(s_scr, lv_color_hex(SCREEN_BG_HEX), 0);
    lv_obj_set_style_border_width(s_scr, 0, 0);
    lv_obj_set_style_pad_all(s_scr, 0, 0);

    /* Full-width Hero Cartoon Stage Canvas (240 x 260 = 81.25% vertical screen coverage) */
    LV_DRAW_BUF_INIT_STATIC(s_stage_buf);
    s_canvas = lv_canvas_create(s_scr);
    lv_canvas_set_draw_buf(s_canvas, &s_stage_buf);
    lv_obj_set_pos(s_canvas, 0, 0);
    lv_obj_set_size(s_canvas, IVY_STAGE_W, IVY_STAGE_H);

    /* Cute Chinese Gift Card Dock Canvas (208 x 54 at x=16, y=262, heap-backed) */
    uint32_t dock_sz = LV_DRAW_BUF_SIZE(DOCK_W, DOCK_H, LV_COLOR_FORMAT_RGB565);
    s_dock_raw = (uint8_t *)malloc(dock_sz);
    if (s_dock_raw) {
        uint8_t *aligned = (uint8_t *)lv_draw_buf_align(s_dock_raw, LV_COLOR_FORMAT_RGB565);
        uint32_t offset = (uint32_t)(aligned - s_dock_raw);
        if (lv_draw_buf_init(&s_dock_buf, DOCK_W, DOCK_H, LV_COLOR_FORMAT_RGB565, 0,
                            aligned, dock_sz - offset) == LV_RESULT_OK) {
            lv_draw_buf_set_flag(&s_dock_buf, LV_IMAGE_FLAGS_MODIFIABLE);
            s_dock = lv_canvas_create(s_scr);
            lv_canvas_set_draw_buf(s_dock, &s_dock_buf);
            lv_obj_set_pos(s_dock, DOCK_X, DOCK_Y);
            lv_obj_set_size(s_dock, DOCK_W, DOCK_H);
        } else {
            ESP_LOGW(TAG, "Failed to init dock draw buffer");
        }
    } else {
        ESP_LOGW(TAG, "Failed to allocate %lu-byte dock buffer", (unsigned long)dock_sz);
    }

    int64_t t0 = esp_timer_get_time();
    render_stage_canvas();
    refresh_hud_and_dock();
    int64_t t1 = esp_timer_get_time();

    lv_screen_load(s_scr);
    if (!lv_timer_create(timer_cb, 30, NULL)) {
        ESP_LOGW(TAG, "Failed to create animation timer");
    }

    ESP_LOGI(TAG, "IVY card initialized (initial frame %lld ms)", (long long)((t1 - t0) / 1000));
}

void ui_ivy_card_on_button(bsp_btn_t btn, bsp_btn_ev_t ev) {
    if (!s_scr) return;

    ivy_btn_t logic_btn;
    if (btn == BSP_BTN_UP) {
        logic_btn = IVY_BTN_UP;
    } else if (btn == BSP_BTN_DOWN) {
        logic_btn = IVY_BTN_DOWN;
    } else if (btn == BSP_BTN_OK) {
        logic_btn = IVY_BTN_OK;
    } else {
        return;
    }

    ivy_btn_ev_t logic_ev;
    if (ev == BSP_BTN_CLICK) {
        logic_ev = IVY_EV_CLICK;
    } else if (ev == BSP_BTN_DOUBLE) {
        logic_ev = IVY_EV_DOUBLE;
    } else if (ev == BSP_BTN_LONG) {
        logic_ev = IVY_EV_LONG;
    } else {
        return;
    }

    ivy_card_logic_on_button(&s_state, logic_btn, logic_ev);
    render_stage_canvas();
    refresh_hud_and_dock();
}
