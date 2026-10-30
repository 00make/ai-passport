// main/ui/drobotics_passport_logic.c
// Pure C11 implementation of the D-Robotics 2026 Developer Conference
// AI Passport 3D Halftone Logo renderer (100% integer fixed-point Q10 for
// FPU-less ESP32-C3), Odometer vertical rolling digits, and Matrix digital roll.
#include "drobotics_passport_logic.h"
#include <string.h>

// ---------------------------------------------------------------------------
// 1. PRNG, Integer Square Root & Fixed-Point Trig LUT
// ---------------------------------------------------------------------------
static uint32_t xorshift32(uint32_t *state) {
    uint32_t x = *state ? *state : 0x9E3779B9u;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *state = x;
    return x;
}

static inline int32_t iabs32(int32_t v) {
    return v < 0 ? -v : v;
}

// Fast exact integer square root for 32-bit unsigned values (max input ~2.5e7)
static uint32_t isqrt_u32(uint32_t n) {
    if (n == 0u) return 0u;
    uint32_t root = 0u;
    uint32_t bit = 1u << 24; // Since max disc_q20 is < 2^25
    while (bit > n) {
        bit >>= 2;
    }
    while (bit != 0u) {
        if (n >= root + bit) {
            n -= root + bit;
            root = (root >> 1) + bit;
        } else {
            root >>= 1;
        }
        bit >>= 2;
    }
    return root;
}

static void copy_trunc(char *dst, size_t cap, const char *src) {
    if (!dst || cap == 0) return;
    if (!src) {
        dst[0] = '\0';
        return;
    }
    size_t len = strlen(src);
    if (len >= cap) len = cap - 1;
    memcpy(dst, src, len);
    dst[len] = '\0';
}

// Quarter-wave 64-entry sine table in Q12 (4096 = 1.0) for 256-entry full circle
static const int16_t SIN_Q12_QUARTER[65] = {
       0,  100,  201,  301,  401,  501,  601,  700,
     799,  897,  995, 1092, 1189, 1285, 1380, 1474,
    1567, 1660, 1751, 1842, 1931, 2019, 2106, 2191,
    2276, 2359, 2440, 2520, 2598, 2675, 2751, 2824,
    2896, 2967, 3035, 3102, 3166, 3229, 3290, 3349,
    3406, 3461, 3513, 3564, 3612, 3659, 3703, 3745,
    3784, 3822, 3857, 3889, 3920, 3948, 3973, 3996,
    4017, 4036, 4052, 4065, 4076, 4085, 4091, 4095,
    4096
};

// Interpolated sine in Q12 (4096 = 1.0) across all 4096 steps of a full revolution
static int32_t sin_q12(int32_t angle_q12) {
    uint32_t u = (uint32_t)angle_q12 & 0xFFFu;
    uint32_t idx = u >> 4;        // 0..255
    uint32_t frac = u & 0x0Fu;    // 0..15 sub-step interpolation for ultra-smooth rotation
    uint32_t idx_next = (idx + 1u) & 0xFFu;

    uint32_t q0 = idx >> 6, off0 = idx & 0x3Fu;
    int32_t v0 = (q0 == 0u) ? SIN_Q12_QUARTER[off0]
               : (q0 == 1u) ? SIN_Q12_QUARTER[64u - off0]
               : (q0 == 2u) ? -SIN_Q12_QUARTER[off0]
                            : -SIN_Q12_QUARTER[64u - off0];

    uint32_t q1 = idx_next >> 6, off1 = idx_next & 0x3Fu;
    int32_t v1 = (q1 == 0u) ? SIN_Q12_QUARTER[off1]
               : (q1 == 1u) ? SIN_Q12_QUARTER[64u - off1]
               : (q1 == 2u) ? -SIN_Q12_QUARTER[off1]
                            : -SIN_Q12_QUARTER[64u - off1];

    return v0 + (((v1 - v0) * (int32_t)frac) >> 4);
}

static int32_t cos_q12(int32_t angle_q12) {
    return sin_q12(angle_q12 + 1024);
}

int32_t drobotics_angle_q12_to_deg10(int32_t angle_q12) {
    int32_t wrapped = angle_q12 % 4096;
    if (wrapped < 0) wrapped += 4096;
    return (wrapped * 3600) / 4096;
}

// ---------------------------------------------------------------------------
// 2. 16x16 Chinese Dot-Matrix Glyphs for "地瓜机器人"
// ---------------------------------------------------------------------------
static const uint16_t CHINESE_GLYPHS_16X16[5][16] = {
    // [0] 地 (di)
    {
        0x0020, 0x1024, 0x1024, 0x1024,
        0x7C7E, 0x10A4, 0x1124, 0x1224,
        0x1024, 0x1024, 0x1428, 0x1820,
        0x6102, 0x0102, 0x00FE, 0x0000
    },
    // [1] 瓜 (gua)
    {
        0x007C, 0x0780, 0x1800, 0x1110,
        0x1110, 0x1110, 0x1110, 0x1128,
        0x1128, 0x1148, 0x1384, 0x2104,
        0x2002, 0x4002, 0x8000, 0x0000
    },
    // [2] 机 (ji)
    {
        0x1000, 0x11F8, 0x1108, 0x1108,
        0xFD08, 0x1108, 0x3108, 0x3908,
        0x5508, 0x5108, 0x9108, 0x1208,
        0x120A, 0x140A, 0x1806, 0x0000
    },
    // [3] 器 (qi)
    {
        0x7E7E, 0x4242, 0x4242, 0x7E7E,
        0x0180, 0x0180, 0xFFFE, 0x0340,
        0x0C30, 0x300C, 0xC003, 0x7E7E,
        0x4242, 0x4242, 0x7E7E, 0x0000
    },
    // [4] 人 (ren)
    {
        0x0180, 0x0180, 0x0180, 0x0180,
        0x0180, 0x0240, 0x0240, 0x0420,
        0x0420, 0x0810, 0x0810, 0x1008,
        0x2004, 0x4002, 0x8001, 0x0000
    }
};

const uint16_t *drobotics_chinese_glyph_16x16(uint8_t index) {
    if (index >= 5u) return CHINESE_GLYPHS_16X16[0];
    return CHINESE_GLYPHS_16X16[index];
}

// ---------------------------------------------------------------------------
// 3. Odometer Vertical Rolling Digits (Mode A)
// ---------------------------------------------------------------------------
static uint8_t ease_out_q8(uint8_t t_q8) {
    uint32_t inv = 255u - (uint32_t)t_q8;
    uint32_t inv3 = (inv * inv * inv) / (255u * 255u);
    return (uint8_t)(255u - inv3);
}

static char next_reel_symbol(char current, char target, uint8_t spins_left) {
    if (spins_left <= 1u) {
        return target;
    }
    if (current >= '0' && current <= '9') {
        return (char)('0' + ((current - '0' + 1) % 10));
    }
    if (target >= '0' && target <= '9') {
        return (char)('0' + (spins_left % 10u));
    }
    if (current >= 'A' && current <= 'Z') {
        return (char)('A' + ((current - 'A' + 1) % 26));
    }
    return target;
}

void drobotics_odometer_init(drobotics_odometer_t *odo, const char *initial_text, uint32_t seed) {
    if (!odo) return;
    memset(odo, 0, sizeof(*odo));
    odo->rng_state = seed ? seed : 0x20260001u;
    size_t len = initial_text ? strlen(initial_text) : 0u;
    if (len > DROBOTICS_ODOMETER_MAX_SLOTS) len = DROBOTICS_ODOMETER_MAX_SLOTS;
    odo->slot_count = (uint8_t)len;
    for (size_t i = 0; i < len; i++) {
        odo->slots[i].current_char = initial_text[i];
        odo->slots[i].next_char = initial_text[i];
        odo->slots[i].target_char = initial_text[i];
        odo->slots[i].direction = 1;
        odo->slots[i].rolling = false;
    }
}

void drobotics_odometer_set_target(drobotics_odometer_t *odo,
                                   const char *target_text,
                                   uint8_t base_spins,
                                   uint16_t stagger_ms) {
    if (!odo || !target_text) return;
    size_t len = strlen(target_text);
    if (len > DROBOTICS_ODOMETER_MAX_SLOTS) len = DROBOTICS_ODOMETER_MAX_SLOTS;
    if (len > odo->slot_count) odo->slot_count = (uint8_t)len;

    for (size_t i = 0; i < odo->slot_count; i++) {
        char tgt = (i < len) ? target_text[i] : ' ';
        drobotics_odometer_slot_t *s = &odo->slots[i];
        s->target_char = tgt;

        bool is_punct = (tgt == '#' || tgt == '-' || tgt == '.' || tgt == ' ');
        if (is_punct && s->current_char == tgt && base_spins > 0u) {
            s->spins_remaining = 0u;
            s->rolling = false;
            s->offset_q8 = 0u;
            s->next_char = tgt;
            continue;
        }

        if (base_spins == 0u && s->current_char == tgt && !s->rolling) {
            continue;
        }

        uint8_t spins = (uint8_t)(base_spins + (uint8_t)(i % 4u));
        if (spins == 0u) spins = 1u;
        s->spins_remaining = spins;
        s->delay_ms = (uint16_t)(i * stagger_ms);
        s->elapsed_ms = 0u;
        s->step_duration_ms = (spins > 2u) ? 55u : 130u;
        s->offset_q8 = 0u;
        s->direction = (i & 1u) ? -1 : 1;
        s->next_char = next_reel_symbol(s->current_char, tgt, spins);
        s->rolling = true;
    }
}

void drobotics_odometer_step(drobotics_odometer_t *odo, uint16_t dt_ms) {
    if (!odo) return;
    for (uint8_t i = 0; i < odo->slot_count; i++) {
        drobotics_odometer_slot_t *s = &odo->slots[i];
        if (!s->rolling) continue;

        if (s->delay_ms > 0u) {
            if (dt_ms < s->delay_ms) {
                s->delay_ms = (uint16_t)(s->delay_ms - dt_ms);
                continue;
            }
            uint16_t rem = (uint16_t)(dt_ms - s->delay_ms);
            s->delay_ms = 0u;
            s->elapsed_ms = (uint16_t)(s->elapsed_ms + rem);
        } else {
            s->elapsed_ms = (uint16_t)(s->elapsed_ms + dt_ms);
        }

        while (s->rolling && s->elapsed_ms >= s->step_duration_ms) {
            s->elapsed_ms = (uint16_t)(s->elapsed_ms - s->step_duration_ms);
            s->current_char = s->next_char;
            if (s->spins_remaining > 0u) {
                s->spins_remaining--;
            }
            if (s->spins_remaining == 0u) {
                s->current_char = s->target_char;
                s->next_char = s->target_char;
                s->offset_q8 = 0u;
                s->elapsed_ms = 0u;
                s->rolling = false;
                break;
            }
            s->next_char = next_reel_symbol(s->current_char, s->target_char, s->spins_remaining);
            if (s->spins_remaining == 1u) {
                s->step_duration_ms = 165u;
            } else if (s->spins_remaining == 2u) {
                s->step_duration_ms = 95u;
            } else {
                s->step_duration_ms = 52u;
            }
        }

        if (s->rolling && s->step_duration_ms > 0u) {
            uint32_t raw_q8 = ((uint32_t)s->elapsed_ms * 255u) / (uint32_t)s->step_duration_ms;
            if (raw_q8 > 255u) raw_q8 = 255u;
            if (s->spins_remaining <= 1u) {
                s->offset_q8 = ease_out_q8((uint8_t)raw_q8);
            } else {
                s->offset_q8 = (uint8_t)raw_q8;
            }
        }
    }
}

bool drobotics_odometer_is_rolling(const drobotics_odometer_t *odo) {
    if (!odo) return false;
    for (uint8_t i = 0; i < odo->slot_count; i++) {
        if (odo->slots[i].rolling) return true;
    }
    return false;
}

void drobotics_odometer_get_settled_string(const drobotics_odometer_t *odo,
                                           char *out_buf,
                                           size_t buf_size) {
    if (!out_buf || buf_size == 0u) return;
    if (!odo) {
        out_buf[0] = '\0';
        return;
    }
    size_t n = odo->slot_count;
    if (n >= buf_size) n = buf_size - 1u;
    for (size_t i = 0; i < n; i++) {
        out_buf[i] = odo->slots[i].current_char;
    }
    out_buf[n] = '\0';
}

// ---------------------------------------------------------------------------
// 4. Matrix Digital Roll / Hex Waterfall State Machine (Mode B)
// ---------------------------------------------------------------------------
static const char MATRIX_HEX_CHARS[] = "0123456789ABCDEF#$%&*+<>?";

void drobotics_matrix_roll_init(drobotics_matrix_roll_t *mr, const char *initial_text, uint32_t seed) {
    if (!mr) return;
    memset(mr, 0, sizeof(*mr));
    mr->rng_state = seed ? seed : 0xD20B2026u;
    copy_trunc(mr->target, sizeof(mr->target), initial_text);
    copy_trunc(mr->display, sizeof(mr->display), initial_text);
    mr->length = (uint8_t)strlen(mr->target);
    mr->total_duration_ms = 300u;
    mr->active = false;
}

void drobotics_matrix_roll_trigger(drobotics_matrix_roll_t *mr,
                                   const char *target_text,
                                   uint16_t duration_ms) {
    if (!mr || !target_text) return;
    copy_trunc(mr->target, sizeof(mr->target), target_text);
    mr->length = (uint8_t)strlen(mr->target);
    mr->elapsed_ms = 0u;
    mr->total_duration_ms = (duration_ms > 0u) ? duration_ms : 300u;
    mr->active = true;

    for (uint8_t i = 0; i < mr->length; i++) {
        char c = mr->target[i];
        if (c == ' ' || c == '|') {
            mr->display[i] = c;
        } else {
            uint32_t r = xorshift32(&mr->rng_state);
            mr->display[i] = MATRIX_HEX_CHARS[r % 16u];
        }
    }
    mr->display[mr->length] = '\0';
}

void drobotics_matrix_roll_step(drobotics_matrix_roll_t *mr, uint16_t dt_ms) {
    if (!mr || !mr->active) return;
    mr->elapsed_ms = (uint16_t)(mr->elapsed_ms + dt_ms);
    if (mr->elapsed_ms >= mr->total_duration_ms) {
        copy_trunc(mr->display, sizeof(mr->display), mr->target);
        mr->active = false;
        return;
    }

    uint32_t lock_count = 0u;
    uint32_t scramble_phase = (uint32_t)mr->total_duration_ms * 45u / 100u;
    if (mr->elapsed_ms > scramble_phase && mr->total_duration_ms > scramble_phase) {
        uint32_t lock_elapsed = (uint32_t)mr->elapsed_ms - scramble_phase;
        uint32_t lock_window = (uint32_t)mr->total_duration_ms - scramble_phase;
        lock_count = (lock_elapsed * (uint32_t)mr->length) / lock_window;
    }

    for (uint8_t i = 0; i < mr->length; i++) {
        char c = mr->target[i];
        if (i < lock_count || c == ' ' || c == '|') {
            mr->display[i] = c;
        } else {
            uint32_t r = xorshift32(&mr->rng_state);
            mr->display[i] = MATRIX_HEX_CHARS[r % (sizeof(MATRIX_HEX_CHARS) - 1u)];
        }
    }
    mr->display[mr->length] = '\0';
}

// ---------------------------------------------------------------------------
// 5. 100% Pure 32-Bit Fixed-Point (Q10) 3D Halftone Logo Renderer
//    Zero floating-point emulation -> runs in < 0.8 ms per frame on ESP32-C3!
// ---------------------------------------------------------------------------
static const uint8_t BAYER_4X4[4][4] = {
    {  0,  8,  2, 10 },
    { 12,  4, 14,  6 },
    {  3, 11,  1,  9 },
    { 15,  7, 13,  5 }
};

static uint8_t cell_hash_u8(int gx, int gy) {
    uint32_t h = (uint32_t)(gx * 374761393u + gy * 668265263u);
    h = (h ^ (h >> 13)) * 1274126177u;
    return (uint8_t)((h >> 24) & 0xFFu);
}

// Q10 fixed-point geometry constants (1024 = 1.0 world unit):
// Composite logo shifted by +0.15 so X in [-1.81, +2.05] is centered in the 1:1 square
#define Q10_ONE        1024
#define D_HALF_Z_Q10    538   // 0.525 * 1024
#define LEFT_X_Q10    (-952)  // (-1.08 + 0.15) * 1024
#define ARCH_CX_Q10     379   // (0.22 + 0.15) * 1024
#define OUTER_R_Q10    1720   // 1.68 * 1024
#define INNER_R_Q10     881   // 0.86 * 1024
#define CYL_CX_Q10   (-1055)  // (-1.18 + 0.15) * 1024
#define CYL_CY_Q10     (-20)  // -0.02 * 1024
#define CYL_CZ_Q10      143   // +0.14 * 1024
#define CYL_R_Q10       799   // 0.78 * 1024
#define CREASE_W_Q10    118   // 0.115 * 1024

// Exact 2D Signed Distance (inside > 0) in Q10 for the D-Robotics 'D' U-Arch
static bool eval_d_arch_2d_q10(int32_t x_q10, int32_t y_q10, int32_t *out_edge_q10) {
    int32_t ay = iabs32(y_q10);
    if (x_q10 < LEFT_X_Q10 || ay > OUTER_R_Q10) return false;

    if (x_q10 <= ARCH_CX_Q10) {
        if (ay < INNER_R_Q10) return false;
        int32_t d1 = x_q10 - LEFT_X_Q10;
        int32_t d2 = OUTER_R_Q10 - ay;
        int32_t d3 = ay - INNER_R_Q10;
        int32_t d = (d1 < d2) ? d1 : d2;
        if (d3 < d) d = d3;
        *out_edge_q10 = d;
        return true;
    }

    int32_t dx = x_q10 - ARCH_CX_Q10;
    uint32_t r2 = (uint32_t)(dx * dx + y_q10 * y_q10);
    if (r2 > (uint32_t)(OUTER_R_Q10 * OUTER_R_Q10) ||
        r2 < (uint32_t)(INNER_R_Q10 * INNER_R_Q10)) {
        return false;
    }
    int32_t r = (int32_t)isqrt_u32(r2);
    int32_t d_out = OUTER_R_Q10 - r;
    int32_t d_in  = r - INNER_R_Q10;
    *out_edge_q10 = (d_out < d_in) ? d_out : d_in;
    return true;
}

typedef struct {
    bool    hit;
    int32_t t_q10;
    int32_t nx_q10;
    int32_t ny_q10;
    int32_t nz_q10;
    int32_t local_y_q10;
    bool    is_cylinder;
    bool    is_crease;
} ray_hit_q10_t;

static inline void try_hit_q10(ray_hit_q10_t *best, int32_t t_q10,
                               int32_t nx_q10, int32_t ny_q10, int32_t nz_q10,
                               int32_t local_y_q10, bool is_cyl, bool is_cr) {
    if (t_q10 < best->t_q10) {
        best->hit = true;
        best->t_q10 = t_q10;
        best->nx_q10 = nx_q10;
        best->ny_q10 = ny_q10;
        best->nz_q10 = nz_q10;
        best->local_y_q10 = local_y_q10;
        best->is_cylinder = is_cyl;
        best->is_crease = is_cr;
    }
}

void drobotics_render_logo_grid(const drobotics_passport_state_t *state,
                                uint8_t grid_out[DROBOTICS_LOGO_GRID_H][DROBOTICS_LOGO_GRID_W]) {
    if (!state || !grid_out) return;

    // 1. Build 3D rotation matrix in Q10 (1024 = 1.0) from Q12 angles
    int32_t sx = sin_q12(state->rot_x_q12) >> 2;
    int32_t cx = cos_q12(state->rot_x_q12) >> 2;
    int32_t sy = sin_q12(state->rot_y_q12) >> 2;
    int32_t cy = cos_q12(state->rot_y_q12) >> 2;
    int32_t sz = sin_q12(state->rot_z_q12) >> 2;
    int32_t cz = cos_q12(state->rot_z_q12) >> 2;

    int32_t sy_cz = (sy * cz) >> 10;
    int32_t sy_sz = (sy * sz) >> 10;

    int32_t r00 = (cy * cz) >> 10;
    int32_t r01 = -((cy * sz) >> 10);
    int32_t r02 = sy;
    int32_t r10 = ((sx * sy_cz) >> 10) + ((cx * sz) >> 10);
    int32_t r11 = -((sx * sy_sz) >> 10) + ((cx * cz) >> 10);
    int32_t r12 = -((sx * cy) >> 10);
    int32_t r20 = -((cx * sy_cz) >> 10) + ((sx * sz) >> 10);
    int32_t r21 = ((cx * sy_sz) >> 10) + ((sx * cz) >> 10);
    int32_t r22 = (cx * cy) >> 10;

    // Camera viewing direction in local space: D = (-r20, -r21, -r22) in Q10
    int32_t dx = -r20;
    int32_t dy = -r21;
    int32_t dz = -r22;

    // 1.25x enlarged scale for 48x48 square viewport (192x192 px):
    // Each half-cell step (gx*2 - 47) is 40/1024 world units -> 1 cell = 80/1024 = 0.078125 units
    // Total 48-cell square viewport spans 3.75 world units (1.25x larger logo on screen!)
    const int32_t half_step_q10 = 40;

    // Precompute reciprocals in Q10 (so t_q10 = (delta_q10 * inv_d_q10) >> 10)
    int32_t a_xy_q10 = (dx * dx + dy * dy) >> 10;
    int32_t inv_a_xy_q10 = (a_xy_q10 > 4) ? ((1024 * 1024) / a_xy_q10) : 0;
    int32_t inv_dx_q10 = (iabs32(dx) > 4) ? ((1024 * 1024) / dx) : 0;
    int32_t inv_dy_q10 = (iabs32(dy) > 4) ? ((1024 * 1024) / dy) : 0;
    int32_t inv_dz_q10 = (iabs32(dz) > 4) ? ((1024 * 1024) / dz) : 0;

    // Camera plane origin offset (world z = +4.0 -> 4 * r2_col in Q10)
    int32_t cam_ox = 4 * r20;
    int32_t cam_oy = 4 * r21;
    int32_t cam_oz = 4 * r22;

    const int32_t cyl_r2_q10   = (CYL_R_Q10 * CYL_R_Q10) >> 10;
    const int32_t outer_r2_q10 = (OUTER_R_Q10 * OUTER_R_Q10) >> 10;
    const int32_t inner_r2_q10 = (INNER_R_Q10 * INNER_R_Q10) >> 10;

    for (int gy = 0; gy < DROBOTICS_LOGO_GRID_H; gy++) {
        // sy2 = 2 * gy - 47 (from -47 to +47 in steps of 2)
        int32_t sy2 = gy * 2 - (DROBOTICS_LOGO_GRID_H - 1);
        int32_t vy_world_q10 = -(sy2 * half_step_q10);

        int32_t base_ox = cam_ox + ((vy_world_q10 * r10) >> 10);
        int32_t base_oy = cam_oy + ((vy_world_q10 * r11) >> 10);
        int32_t base_oz = cam_oz + ((vy_world_q10 * r12) >> 10);

        for (int gx = 0; gx < DROBOTICS_LOGO_GRID_W; gx++) {
            int32_t sx2 = gx * 2 - (DROBOTICS_LOGO_GRID_W - 1);

            // Fast 2D bounding circle early-out (max logo radius ~2.08 world units)
            if (sx2 * sx2 + sy2 * sy2 > 2950) {
                grid_out[gy][gx] = 0u;
                continue;
            }

            int32_t ux_world_q10 = sx2 * half_step_q10;
            int32_t ox = base_ox + ((ux_world_q10 * r00) >> 10);
            int32_t oy = base_oy + ((ux_world_q10 * r01) >> 10);
            int32_t oz = base_oz + ((ux_world_q10 * r02) >> 10);

            ray_hit_q10_t best;
            best.hit = false;
            best.t_q10 = 1 << 28;
            best.nx_q10 = 0;
            best.ny_q10 = 0;
            best.nz_q10 = Q10_ONE;
            best.local_y_q10 = 0;
            best.is_cylinder = false;
            best.is_crease = false;

            // ---------------------------------------------------------------
            // A. Left Nested Cylinder (center=(-1.03, -0.02, 0.14), R=0.78)
            // ---------------------------------------------------------------
            int32_t ocx = ox - CYL_CX_Q10;
            int32_t ocy = oy - CYL_CY_Q10;
            int32_t c_cyl_q10 = ((ocx * ocx + ocy * ocy) >> 10) - cyl_r2_q10;
            int32_t hb_cyl_q10 = (ocx * dx + ocy * dy) >> 10;
            int32_t disc_cyl_q20 = hb_cyl_q10 * hb_cyl_q10 - a_xy_q10 * c_cyl_q10;

            if (disc_cyl_q20 >= 0 || c_cyl_q10 <= 0) {
                if (inv_dz_q10 != 0) {
                    int32_t z_cap  = (dz < 0) ? (CYL_CZ_Q10 + D_HALF_Z_Q10) : (CYL_CZ_Q10 - D_HALF_Z_Q10);
                    int32_t nz_cap = (dz < 0) ? Q10_ONE : -Q10_ONE;
                    int32_t t_cap  = ((z_cap - oz) * inv_dz_q10) >> 10;
                    if (t_cap > 0 && t_cap < best.t_q10) {
                        int32_t hx = ocx + ((t_cap * dx) >> 10);
                        int32_t hy = ocy + ((t_cap * dy) >> 10);
                        uint32_t r2 = (uint32_t)(hx * hx + hy * hy);
                        if (r2 <= (uint32_t)(CYL_R_Q10 * CYL_R_Q10)) {
                            int32_t r = (int32_t)isqrt_u32(r2);
                            try_hit_q10(&best, t_cap, 0, 0, nz_cap, hy, true,
                                        (CYL_R_Q10 - r) < CREASE_W_Q10);
                        }
                    }
                }
                if (disc_cyl_q20 >= 0 && inv_a_xy_q10 > 0) {
                    int32_t s_q10 = (int32_t)isqrt_u32((uint32_t)disc_cyl_q20);
                    int32_t t_side = ((-hb_cyl_q10 - s_q10) * inv_a_xy_q10) >> 10;
                    if (t_side > 0 && t_side < best.t_q10) {
                        int32_t hz = oz + ((t_side * dz) >> 10);
                        int32_t dz_abs = iabs32(hz - CYL_CZ_Q10);
                        if (dz_abs <= D_HALF_Z_Q10) {
                            int32_t hx = ocx + ((t_side * dx) >> 10);
                            int32_t hy = ocy + ((t_side * dy) >> 10);
                            int32_t nx = (hx * 1312) >> 10; // 1024 / 0.78 = 1312
                            int32_t ny = (hy * 1312) >> 10;
                            try_hit_q10(&best, t_side, nx, ny, 0, hy, true,
                                        (D_HALF_Z_Q10 - dz_abs) < CREASE_W_Q10);
                        }
                    }
                }
            }

            // ---------------------------------------------------------------
            // B. Right 'D' U-Arch (z in [-0.525, +0.525])
            // ---------------------------------------------------------------
            // 1. Front/Back Z-caps of 'D'
            if (inv_dz_q10 != 0) {
                int32_t z_cap  = (dz < 0) ? D_HALF_Z_Q10 : -D_HALF_Z_Q10;
                int32_t nz_cap = (dz < 0) ? Q10_ONE : -Q10_ONE;
                int32_t t_cap  = ((z_cap - oz) * inv_dz_q10) >> 10;
                if (t_cap > 0 && t_cap < best.t_q10) {
                    int32_t hx = ox + ((t_cap * dx) >> 10);
                    int32_t hy = oy + ((t_cap * dy) >> 10);
                    int32_t edge_q10 = 0;
                    if (eval_d_arch_2d_q10(hx, hy, &edge_q10)) {
                        try_hit_q10(&best, t_cap, 0, 0, nz_cap, hy, false,
                                    edge_q10 < CREASE_W_Q10);
                    }
                }
            }

            // 2. Left vertical end planes of 'D' (x = left_x)
            if (dx > 4 && inv_dx_q10 != 0) {
                int32_t t_left = ((LEFT_X_Q10 - ox) * inv_dx_q10) >> 10;
                if (t_left > 0 && t_left < best.t_q10) {
                    int32_t hy = oy + ((t_left * dy) >> 10);
                    int32_t hz = oz + ((t_left * dz) >> 10);
                    int32_t ahy = iabs32(hy);
                    int32_t ahz = iabs32(hz);
                    if (ahz <= D_HALF_Z_Q10 && ahy >= INNER_R_Q10 && ahy <= OUTER_R_Q10) {
                        bool cr = ((D_HALF_Z_Q10 - ahz) < CREASE_W_Q10) ||
                                  ((OUTER_R_Q10 - ahy) < CREASE_W_Q10) ||
                                  ((ahy - INNER_R_Q10) < CREASE_W_Q10);
                        try_hit_q10(&best, t_left, -Q10_ONE, 0, 0, hy, false, cr);
                    }
                }
            }

            // 3. Horizontal flat bar walls of 'D'
            if (inv_dy_q10 != 0) {
                static const int32_t Y_PLANES[4] = {
                    OUTER_R_Q10, -OUTER_R_Q10, INNER_R_Q10, -INNER_R_Q10
                };
                static const int32_t Y_NORMS[4] = {
                    Q10_ONE, -Q10_ONE, -Q10_ONE, Q10_ONE
                };
                for (int p = 0; p < 4; p++) {
                    if (dy * Y_NORMS[p] >= 0) continue;
                    int32_t tp = ((Y_PLANES[p] - oy) * inv_dy_q10) >> 10;
                    if (tp > 0 && tp < best.t_q10) {
                        int32_t hx = ox + ((tp * dx) >> 10);
                        int32_t hz = oz + ((tp * dz) >> 10);
                        int32_t ahz = iabs32(hz);
                        if (ahz <= D_HALF_Z_Q10 && hx >= LEFT_X_Q10 && hx <= ARCH_CX_Q10) {
                            bool cr = ((D_HALF_Z_Q10 - ahz) < CREASE_W_Q10) ||
                                      ((hx - LEFT_X_Q10) < CREASE_W_Q10);
                            try_hit_q10(&best, tp, 0, Y_NORMS[p], 0, Y_PLANES[p], false, cr);
                        }
                    }
                }
            }

            // 4. Semicircular Outer & Inner Arch Cylinders
            if (inv_a_xy_q10 > 0) {
                int32_t oax = ox - ARCH_CX_Q10;
                int32_t oay = oy;
                int32_t hb_a_q10 = (oax * dx + oay * dy) >> 10;
                int32_t c_base_q10 = (oax * oax + oay * oay) >> 10;

                int32_t disc_out_q20 = hb_a_q10 * hb_a_q10 - a_xy_q10 * (c_base_q10 - outer_r2_q10);
                if (disc_out_q20 >= 0) {
                    int32_t s_out = (int32_t)isqrt_u32((uint32_t)disc_out_q20);
                    int32_t t_out = ((-hb_a_q10 - s_out) * inv_a_xy_q10) >> 10;
                    if (t_out > 0 && t_out < best.t_q10) {
                        int32_t hx = ox + ((t_out * dx) >> 10);
                        int32_t hz = oz + ((t_out * dz) >> 10);
                        int32_t ahz = iabs32(hz);
                        if (hx >= ARCH_CX_Q10 && ahz <= D_HALF_Z_Q10) {
                            int32_t hy = oy + ((t_out * dy) >> 10);
                            int32_t nx = ((hx - ARCH_CX_Q10) * 609) >> 10; // 1024 / 1.68 = 609
                            int32_t ny = (hy * 609) >> 10;
                            try_hit_q10(&best, t_out, nx, ny, 0, hy, false,
                                        (D_HALF_Z_Q10 - ahz) < CREASE_W_Q10);
                        }
                    }
                }

                int32_t disc_in_q20 = hb_a_q10 * hb_a_q10 - a_xy_q10 * (c_base_q10 - inner_r2_q10);
                if (disc_in_q20 >= 0) {
                    int32_t s_in = (int32_t)isqrt_u32((uint32_t)disc_in_q20);
                    int32_t t_in = ((-hb_a_q10 + s_in) * inv_a_xy_q10) >> 10;
                    if (t_in > 0 && t_in < best.t_q10) {
                        int32_t hx = ox + ((t_in * dx) >> 10);
                        int32_t hz = oz + ((t_in * dz) >> 10);
                        int32_t ahz = iabs32(hz);
                        if (hx >= ARCH_CX_Q10 && ahz <= D_HALF_Z_Q10) {
                            int32_t hy = oy + ((t_in * dy) >> 10);
                            int32_t nx = -(((hx - ARCH_CX_Q10) * 1190) >> 10); // 1024 / 0.86 = 1190
                            int32_t ny = -((hy * 1190) >> 10);
                            try_hit_q10(&best, t_in, nx, ny, 0, hy, false,
                                        (D_HALF_Z_Q10 - ahz) < CREASE_W_Q10);
                        }
                    }
                }
            }

            if (!best.hit) {
                grid_out[gy][gx] = 0u;
                continue;
            }

            // Transform local normal to world space (in Q10)
            int32_t wnx = (r00 * best.nx_q10 + r01 * best.ny_q10 + r02 * best.nz_q10) >> 10;
            int32_t wny = (r10 * best.nx_q10 + r11 * best.ny_q10 + r12 * best.nz_q10) >> 10;
            int32_t wnz = (r20 * best.nx_q10 + r21 * best.ny_q10 + r22 * best.nz_q10) >> 10;

            // Directional lights in Q10:
            // Key=(-571, 395, 748), Fill=(912, -268, 376), Top=(0, 990, 260), Half=(-308, 213, 953)
            int32_t key_diff  = (wnx * (-571) + wny * 395 + wnz * 748) >> 10;
            if (key_diff < 0) key_diff = 0;
            int32_t fill_diff = (wnx * 912 + wny * (-268) + wnz * 376) >> 10;
            if (fill_diff < 0) fill_diff = 0;
            int32_t top_diff  = (wny * 990 + wnz * 260) >> 10;
            if (top_diff < 0) top_diff = 0;

            // Specular highlight: (N . H)^16 in Q10
            int32_t ndoth = (wnx * (-308) + wny * 213 + wnz * 953) >> 10;
            int32_t spec_q10 = 0;
            if (ndoth > 0) {
                if (ndoth > Q10_ONE) ndoth = Q10_ONE;
                int32_t s2 = (ndoth * ndoth) >> 10;
                int32_t s4 = (s2 * s2) >> 10;
                int32_t s8 = (s4 * s4) >> 10;
                spec_q10 = (s8 * s8) >> 10;
            }

            // Grazing Fresnel rim light: (1 - |wnz|)^2 in Q10
            int32_t abs_nz = iabs32(wnz);
            if (abs_nz > Q10_ONE) abs_nz = Q10_ONE;
            int32_t one_m_nz = Q10_ONE - abs_nz;
            int32_t fresnel_q10 = (one_m_nz * one_m_nz) >> 10;

            int32_t lum_q10 = 102 + ((key_diff * 655 + fill_diff * 328 +
                                      top_diff * 225 + spec_q10 * 390 +
                                      fresnel_q10 * 410) >> 10);

            if (best.is_cylinder) {
                lum_q10 = (lum_q10 * 1146) >> 10; // 1.12x cylinder front pop
            }

            // Ordered Bayer 4x4 dithering + side-wall particle dithering
            int32_t bayer_centered = ((int32_t)BAYER_4X4[gy & 3][gx & 3] - 8) * 16; // [-128..+112]
            if (lum_q10 > 30 && lum_q10 < 590 && !best.is_crease) {
                int32_t threshold = ((lum_q10 + (bayer_centered >> 1)) * 470) >> 10;
                if ((int32_t)cell_hash_u8(gx, gy) > threshold + 92) {
                    lum_q10 >>= 2;
                }
            } else {
                lum_q10 += (bayer_centered >> 1);
            }

            if (best.is_crease) {
                int32_t crease_q10 = state->overclock_active ? 1500 : 1280;
                if (crease_q10 > lum_q10) lum_q10 = crease_q10;
            }

            int32_t level = (lum_q10 * 14) >> 10;
            if (level < 1) level = 1;
            if (level > 15) level = 15;

            uint8_t cell_val = (uint8_t)level;
            if (best.is_crease) {
                cell_val |= 0x80u;
            }
            grid_out[gy][gx] = cell_val;
        }
    }
}

// ---------------------------------------------------------------------------
// 6. Passport Application State & Interaction Reducer
// ---------------------------------------------------------------------------
static const char *const TELEMETRY_STRINGS[3][2] = {
    { "POS:28.4X|RDK-S100|BPU:98.4%", "PASS:#2026-0001|AUTH:VERIFIED" },
    { "CONF:2026|D-ROBOTICS|VIP:001", "ARCH:EMBODIED-AI|00MAKE:LOCK" },
    { "RDK-S100|128TOPS|LAT:01.2MS",  "BPU0:99.1%|MEM:8MB|TEMP:41C"  }
};

static const char *const TOP_COUNTER_TARGETS[3] = {
    "503.300",
    "2026.01",
    "128.000"
};

void drobotics_passport_init(drobotics_passport_state_t *state) {
    if (!state) return;
    memset(state, 0, sizeof(*state));

    copy_trunc(state->holder_name, sizeof(state->holder_name), DROBOTICS_HOLDER_NAME_DEFAULT);
    copy_trunc(state->role_tag, sizeof(state->role_tag), DROBOTICS_ROLE_TAG_DEFAULT);
    copy_trunc(state->pass_id, sizeof(state->pass_id), DROBOTICS_PASS_ID_DEFAULT);

    state->badge_mode = DROBOTICS_BADGE_MODE_VIP_PASS;
    state->render_style = DROBOTICS_STYLE_AMBER_HALFTONE;

    state->rot_x_q12 = 143;
    state->rot_y_q12 = 3796;
    state->rot_z_q12 = 91;

    state->speed_scale_q8 = 256u; // 1.0x normal speed
    state->fps_x10 = 500u;        // 50.0 Hz
    state->cursor_visible = true;

    drobotics_odometer_init(&state->top_odometer, "000.000", 0x503300u);
    drobotics_odometer_set_target(&state->top_odometer, TOP_COUNTER_TARGETS[0], 4u, 25u);

    drobotics_odometer_init(&state->pass_odometer, "#0000-0000", 0x20260001u);
    drobotics_odometer_set_target(&state->pass_odometer, state->pass_id, 6u, 32u);

    drobotics_matrix_roll_init(&state->matrix_roll, TELEMETRY_STRINGS[0][0], 0xD20B0001u);
    drobotics_matrix_roll_trigger(&state->matrix_roll, TELEMETRY_STRINGS[0][0], 300u);
}

void drobotics_passport_set_identity(drobotics_passport_state_t *state,
                                     const char *holder_name,
                                     const char *role_tag,
                                     const char *pass_id) {
    if (!state) return;
    if (holder_name) copy_trunc(state->holder_name, sizeof(state->holder_name), holder_name);
    if (role_tag) copy_trunc(state->role_tag, sizeof(state->role_tag), role_tag);
    if (pass_id) {
        copy_trunc(state->pass_id, sizeof(state->pass_id), pass_id);
        drobotics_odometer_set_target(&state->pass_odometer, state->pass_id, 6u, 30u);
    }
}

void drobotics_passport_handle_key(drobotics_passport_state_t *state,
                                   int btn_id,
                                   drobotics_key_ev_t ev) {
    if (!state) return;

    if (ev == DROBOTICS_KEY_EVENT_CLICK) {
        if (btn_id == 1) {
            state->badge_mode = (drobotics_badge_mode_t)(
                (state->badge_mode + DROBOTICS_BADGE_MODE_COUNT - 1) % DROBOTICS_BADGE_MODE_COUNT);
        } else {
            state->badge_mode = (drobotics_badge_mode_t)(
                (state->badge_mode + 1) % DROBOTICS_BADGE_MODE_COUNT);
        }

        drobotics_odometer_set_target(&state->pass_odometer, state->pass_id, 7u, 30u);
        drobotics_odometer_set_target(&state->top_odometer,
                                      TOP_COUNTER_TARGETS[state->badge_mode], 5u, 22u);

        state->telemetry_page = 0u;
        drobotics_matrix_roll_trigger(&state->matrix_roll,
                                      TELEMETRY_STRINGS[state->badge_mode][0], 300u);
        state->periodic_roll_ms = 0u;
        return;
    }

    if (ev == DROBOTICS_KEY_EVENT_DOUBLE) {
        state->render_style = (drobotics_render_style_t)(
            (state->render_style + 1) % DROBOTICS_STYLE_COUNT);
        drobotics_matrix_roll_trigger(&state->matrix_roll,
                                      TELEMETRY_STRINGS[state->badge_mode][state->telemetry_page & 1u],
                                      300u);
        return;
    }

    if (ev == DROBOTICS_KEY_EVENT_LONG) {
        state->key_held = true;
        state->overclock_active = true;
        state->overclock_hold_ms = 1600u;
        state->speed_scale_q8 = 820u;
        drobotics_odometer_set_target(&state->top_odometer, "999.800", 8u, 16u);
        drobotics_matrix_roll_trigger(&state->matrix_roll,
                                      "OVERCLOCK:3.2X|BPU:MAX-BOOST", 300u);
        return;
    }

    if (ev == DROBOTICS_KEY_EVENT_RELEASE) {
        state->key_held = false;
        return;
    }
}

void drobotics_passport_step(drobotics_passport_state_t *state, uint16_t dt_ms) {
    if (!state) return;

    state->anim_time_ms += dt_ms;

    state->cursor_timer_ms = (uint16_t)(state->cursor_timer_ms + dt_ms);
    if (state->cursor_timer_ms >= 450u) {
        state->cursor_timer_ms = (uint16_t)(state->cursor_timer_ms % 450u);
        state->cursor_visible = !state->cursor_visible;
    }

    if (state->overclock_active) {
        if (state->overclock_hold_ms > dt_ms) {
            state->overclock_hold_ms = (uint16_t)(state->overclock_hold_ms - dt_ms);
        } else {
            state->overclock_hold_ms = 0u;
        }
        if (!state->key_held && state->overclock_hold_ms == 0u) {
            if (state->speed_scale_q8 > 280u) {
                uint16_t decay = (uint16_t)((dt_ms * 3u) / 2u);
                if (decay < 4u) decay = 4u;
                state->speed_scale_q8 = (uint16_t)(state->speed_scale_q8 - decay);
            } else {
                state->speed_scale_q8 = 256u;
                state->overclock_active = false;
                drobotics_odometer_set_target(&state->top_odometer,
                                              TOP_COUNTER_TARGETS[state->badge_mode], 4u, 22u);
                drobotics_matrix_roll_trigger(
                    &state->matrix_roll,
                    TELEMETRY_STRINGS[state->badge_mode][state->telemetry_page & 1u],
                    300u);
            }
        }
    }

    // Advance 3D rotation angles smoothly with interpolated sub-step precision
    int32_t dy_q12 = ((int32_t)dt_ms * (int32_t)state->speed_scale_q8 * 16) / (256 * 25);
    if (dy_q12 < 1 && dt_ms > 0u) dy_q12 = 1;
    state->rot_y_q12 = (state->rot_y_q12 + dy_q12) & 0xFFFu;

    int32_t phase_x = (int32_t)((state->anim_time_ms * 3u) / 4u) & 0xFFFu;
    int32_t phase_z = (int32_t)((state->anim_time_ms * 9u) / 16u) & 0xFFFu;
    state->rot_x_q12 = 143 + (sin_q12(phase_x) * 88) / 4096;
    state->rot_z_q12 = 78  + (cos_q12(phase_z) * 38) / 4096;

    drobotics_odometer_step(&state->top_odometer, dt_ms);
    drobotics_odometer_step(&state->pass_odometer, dt_ms);
    drobotics_matrix_roll_step(&state->matrix_roll, dt_ms);

    state->telemetry_tick_ms += dt_ms;
    if (!state->overclock_active && state->telemetry_tick_ms >= 5000u) {
        state->telemetry_tick_ms = 0u;
        state->telemetry_page = (uint8_t)(state->telemetry_page + 1u);
        drobotics_matrix_roll_trigger(
            &state->matrix_roll,
            TELEMETRY_STRINGS[state->badge_mode][state->telemetry_page & 1u],
            300u);

        if (state->badge_mode == DROBOTICS_BADGE_MODE_VIP_PASS) {
            const char *top_variants[4] = { "503.300", "512.480", "498.620", "503.300" };
            drobotics_odometer_set_target(&state->top_odometer,
                                          top_variants[state->telemetry_page & 3u],
                                          2u, 18u);
        }
    }

    state->periodic_roll_ms += dt_ms;
    if (state->periodic_roll_ms >= 10000u) {
        state->periodic_roll_ms = 0u;
        if (!drobotics_odometer_is_rolling(&state->pass_odometer)) {
            drobotics_odometer_set_target(&state->pass_odometer, state->pass_id, 4u, 26u);
        }
    }
}
