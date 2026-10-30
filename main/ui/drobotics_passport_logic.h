// main/ui/drobotics_passport_logic.h
// Pure logic, 100% integer fixed-point 3D halftone SDF/ray-intersection renderer,
// odometer vertical rolling digits, and matrix digital roll state machines for
// the D-Robotics 2026 Developer Conference AI Passport Badge.
// Zero ESP-IDF / LVGL dependencies so it can be unit-tested on the host.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// ---------------------------------------------------------------------------
// 1. Preset Identity & Visual Constants
// ---------------------------------------------------------------------------
#define DROBOTICS_HOLDER_NAME_DEFAULT  "ZHANG ZHENYAO | 00MAKE"
#define DROBOTICS_ROLE_TAG_DEFAULT     "EMBODIED AI ARCHITECT"
#define DROBOTICS_PASS_ID_DEFAULT      "#2026-0001"
#define DROBOTICS_CONF_TITLE_DEFAULT   "2026 DEV.CONF // RDK"

// P3 Amber Monochrome CRT Palette (24-bit RGB hex references)
#define DROBOTICS_COLOR_BG             0x050300u
#define DROBOTICS_COLOR_DIM            0x4A2200u
#define DROBOTICS_COLOR_AMBER          0xFF8800u
#define DROBOTICS_COLOR_BRIGHT         0xFFCC00u
#define DROBOTICS_COLOR_HOT            0xFFF5CCu

// 1:1 Square 3D Logo Viewport Grid Dimensions:
// 48 x 48 halftone cells of 4x4 px = 192 x 192 px square viewport (1.25x scale)
#define DROBOTICS_LOGO_CELL_STEP       4
#define DROBOTICS_LOGO_GRID_W          48
#define DROBOTICS_LOGO_GRID_H          48
#define DROBOTICS_LOGO_VIEW_W          (DROBOTICS_LOGO_GRID_W * DROBOTICS_LOGO_CELL_STEP)
#define DROBOTICS_LOGO_VIEW_H          (DROBOTICS_LOGO_GRID_H * DROBOTICS_LOGO_CELL_STEP)

// Odometer & Matrix Digital Roll limits
#define DROBOTICS_ODOMETER_MAX_SLOTS   12
#define DROBOTICS_MATRIX_MAX_LEN       32

// ---------------------------------------------------------------------------
// 2. Display Modes & 3D Render Styles
// ---------------------------------------------------------------------------
typedef enum {
    DROBOTICS_BADGE_MODE_VIP_PASS = 0,   // D-ROBOTICS + VIP Pass ID
    DROBOTICS_BADGE_MODE_BRAND_CN = 1,   // 地瓜机器人 + VIP Pass ID
    DROBOTICS_BADGE_MODE_RDK_TELEM = 2,  // RDK DEVELOPER + VIP Pass ID
    DROBOTICS_BADGE_MODE_COUNT = 3
} drobotics_badge_mode_t;

typedef enum {
    DROBOTICS_STYLE_AMBER_HALFTONE = 0,  // Classic Amber CRT Halftone Dots
    DROBOTICS_STYLE_GREEN_PHOSPHOR = 1,  // Retro Green CRT Phosphor + ASCII/Matrix
    DROBOTICS_STYLE_SQUARE_LED     = 2,  // High-Contrast Square Pixel LED Matrix
    DROBOTICS_STYLE_COUNT          = 3
} drobotics_render_style_t;

typedef enum {
    DROBOTICS_KEY_EVENT_PRESS = 0,
    DROBOTICS_KEY_EVENT_CLICK,
    DROBOTICS_KEY_EVENT_DOUBLE,
    DROBOTICS_KEY_EVENT_LONG,
    DROBOTICS_KEY_EVENT_RELEASE,
} drobotics_key_ev_t;

// ---------------------------------------------------------------------------
// 3. Odometer Vertical Rolling Digit Slot State Machine (Mode A)
// ---------------------------------------------------------------------------
typedef struct {
    char     current_char;      // Currently settled character (or departing char)
    char     next_char;         // Incoming character during vertical roll
    char     target_char;       // Final target character after spin reels settle
    uint8_t  spins_remaining;   // Number of intermediate symbols left to roll through
    uint16_t delay_ms;          // Staggered start delay (ms)
    uint16_t elapsed_ms;        // Elapsed time in current single-step transition
    uint16_t step_duration_ms;  // Duration of current single-step transition
    uint8_t  offset_q8;         // Eased vertical scroll progress 0..255 (0=current, 255=next)
    int8_t   direction;         // +1 = roll up, -1 = roll down
    bool     rolling;           // True while slot is actively rolling
} drobotics_odometer_slot_t;

typedef struct {
    uint8_t                   slot_count;
    drobotics_odometer_slot_t slots[DROBOTICS_ODOMETER_MAX_SLOTS];
    uint32_t                  rng_state;
} drobotics_odometer_t;

// ---------------------------------------------------------------------------
// 4. Matrix Digital Roll / Hex Waterfall State Machine (Mode B)
// ---------------------------------------------------------------------------
typedef struct {
    char     target[DROBOTICS_MATRIX_MAX_LEN];
    char     display[DROBOTICS_MATRIX_MAX_LEN];
    uint8_t  length;
    uint16_t elapsed_ms;
    uint16_t total_duration_ms; // Default 300ms high-freq scramble + progressive lock
    uint32_t rng_state;
    bool     active;
} drobotics_matrix_roll_t;

// ---------------------------------------------------------------------------
// 5. Complete Passport Application State
// ---------------------------------------------------------------------------
typedef struct {
    // Configurable identity fields
    char                     holder_name[32];
    char                     role_tag[32];
    char                     pass_id[16];

    // Active modes
    drobotics_badge_mode_t   badge_mode;
    drobotics_render_style_t render_style;

    // 3D rotation angles in Q12 angle units (4096 = 2*PI = 360 deg)
    int32_t                  rot_x_q12;
    int32_t                  rot_y_q12;
    int32_t                  rot_z_q12;
    uint32_t                 anim_time_ms;

    // Overclock boost state (Long Press)
    bool                     key_held;
    bool                     overclock_active;
    uint16_t                 overclock_hold_ms; // Remaining min boost hold window
    uint16_t                 speed_scale_q8;    // 256 = 1.0x, 820 = 3.2x overclock

    // Live FPS & telemetry counters
    uint16_t                 fps_x10;           // e.g. 500 = 50.0 Hz
    uint32_t                 telemetry_tick_ms;
    uint32_t                 periodic_roll_ms;
    uint8_t                  telemetry_page;
    bool                     cursor_visible;
    uint16_t                 cursor_timer_ms;

    // Odometer & Matrix Digital Roll instances
    drobotics_odometer_t     top_odometer;      // Top HUD counter (e.g. "503.300")
    drobotics_odometer_t     pass_odometer;     // Bottom Pass ID ("#2026-0001")
    drobotics_matrix_roll_t  matrix_roll;       // Telemetry decode state
} drobotics_passport_state_t;

// ---------------------------------------------------------------------------
// 6. Public Pure-Logic API
// ---------------------------------------------------------------------------
void drobotics_passport_init(drobotics_passport_state_t *state);

void drobotics_passport_set_identity(drobotics_passport_state_t *state,
                                     const char *holder_name,
                                     const char *role_tag,
                                     const char *pass_id);

void drobotics_passport_handle_key(drobotics_passport_state_t *state,
                                   int btn_id,
                                   drobotics_key_ev_t ev);

void drobotics_passport_step(drobotics_passport_state_t *state, uint16_t dt_ms);

// Odometer helpers
void drobotics_odometer_init(drobotics_odometer_t *odo, const char *initial_text, uint32_t seed);
void drobotics_odometer_set_target(drobotics_odometer_t *odo,
                                   const char *target_text,
                                   uint8_t base_spins,
                                   uint16_t stagger_ms);
void drobotics_odometer_step(drobotics_odometer_t *odo, uint16_t dt_ms);
bool drobotics_odometer_is_rolling(const drobotics_odometer_t *odo);
void drobotics_odometer_get_settled_string(const drobotics_odometer_t *odo,
                                           char *out_buf,
                                           size_t buf_size);

// Matrix digital roll helpers
void drobotics_matrix_roll_init(drobotics_matrix_roll_t *mr, const char *initial_text, uint32_t seed);
void drobotics_matrix_roll_trigger(drobotics_matrix_roll_t *mr,
                                   const char *target_text,
                                   uint16_t duration_ms);
void drobotics_matrix_roll_step(drobotics_matrix_roll_t *mr, uint16_t dt_ms);

// 3D Halftone Logo Renderer (100% integer fixed-point Q10):
// Computes a 48x48 square grid of cell intensities (0..15) and edge crease flags (bit 7)
// at 1.25x scale for the current 3D rotation angles.
void drobotics_render_logo_grid(const drobotics_passport_state_t *state,
                                uint8_t grid_out[DROBOTICS_LOGO_GRID_H][DROBOTICS_LOGO_GRID_W]);

// Converts rotation angle in Q12 (4096 = 360 deg) to tenths of a degree (0..3599)
int32_t drobotics_angle_q12_to_deg10(int32_t angle_q12);

// Verifies 16x16 Chinese dot-matrix glyph bitmap for index 0..4 ("地瓜机器人")
const uint16_t *drobotics_chinese_glyph_16x16(uint8_t index);

#ifdef __cplusplus
}
#endif
