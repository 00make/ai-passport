#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "ivy_sprites_data.h"

#ifdef __cplusplus
extern "C" {
#endif

#define IVY_STAGE_W       240
#define IVY_STAGE_H       260
#define IVY_PARTICLE_MAX  24

typedef enum {
    IVY_MODE_STORY = 0,
    IVY_MODE_TURNAROUND = 1,
    IVY_MODE_EXPRESSIONS = 2,
    IVY_MODE_COUNT = 3,
} ivy_hero_mode_t;

typedef enum {
    IVY_STORY_WORKING = 0,
    IVY_STORY_CLOCK_OUT_SEQ = 1,
    IVY_STORY_OFFWORK_LOOP = 2,
} ivy_story_phase_t;

typedef enum {
    IVY_TURN_FRONT = 0,
    IVY_TURN_SIDE = 1,
    IVY_TURN_BACK = 2,
    IVY_TURN_COUNT = 3,
} ivy_turn_view_t;

typedef enum {
    IVY_EXPR_LAUGH = 0,
    IVY_EXPR_SHOCK = 1,
    IVY_EXPR_DEADPAN = 2,
    IVY_EXPR_SMIRK = 3,
    IVY_EXPR_COUNT = 4,
} ivy_expr_kind_t;

typedef enum {
    IVY_TAB_PROFILE = 0,
    IVY_TAB_STATS = 1,
    IVY_TAB_VIP = 2,
    IVY_TAB_COUNT = 3,
} ivy_card_tab_t;

typedef enum {
    IVY_BTN_UP = 0,
    IVY_BTN_DOWN = 1,
    IVY_BTN_OK = 2,
} ivy_btn_t;

typedef enum {
    IVY_EV_CLICK = 0,
    IVY_EV_DOUBLE = 1,
    IVY_EV_LONG = 2,
} ivy_btn_ev_t;

typedef enum {
    IVY_PARTICLE_NONE = 0,
    IVY_PARTICLE_SWEAT = 1,
    IVY_PARTICLE_STAR = 2,
    IVY_PARTICLE_HEART = 3,
    IVY_PARTICLE_CONFETTI = 4,
} ivy_particle_kind_t;

typedef struct {
    int16_t x_q4;
    int16_t y_q4;
    int16_t vx_q4;
    int16_t vy_q4;
    uint16_t color565;
    uint8_t ttl;
    uint8_t max_ttl;
    ivy_particle_kind_t kind;
} ivy_particle_t;

typedef struct {
    ivy_hero_mode_t hero_mode;
    ivy_story_phase_t story_phase;
    ivy_turn_view_t turn_view;
    ivy_expr_kind_t expr_kind;
    ivy_card_tab_t card_tab;
    bool poster_mode;
    bool offwork_cheered;

    uint32_t tick;
    uint32_t story_tick;
    uint16_t bounce_ticks;
    uint16_t jump_ticks;
    uint32_t rng_state;
    bool banner_burst_done;

    ivy_particle_t particles[IVY_PARTICLE_MAX];
} ivy_card_state_t;

typedef struct {
    ivy_sprite_id_t main_sprite;
    bool show_offwork_banner;
    int16_t sprite_dx;
    int16_t sprite_dy;
    int16_t banner_dy;
    uint8_t mood_pct;
    const char *mode_pill;
    const char *stage_badge_utf8;
    const char *stage_hint_ascii;
} ivy_render_snapshot_t;

/** Initialize IVY business card state to default story theater. */
void ivy_card_logic_init(ivy_card_state_t *st);

/** Handle UP / DOWN / OK button input events. */
void ivy_card_logic_on_button(ivy_card_state_t *st, ivy_btn_t btn, ivy_btn_ev_t ev);

/** Advance timers, story sequence, and particles by one animation tick (~30 ms). */
void ivy_card_logic_tick(ivy_card_state_t *st);

/** Compute deterministic render snapshot for the current frame. */
void ivy_card_logic_get_snapshot(const ivy_card_state_t *st, ivy_render_snapshot_t *out);

/** Look up a 12x12 monochrome Chinese glyph bitmap (12 uint16_t rows, MSB on left). */
const uint16_t *ivy_glyph12_find(uint32_t codepoint);

#ifdef __cplusplus
}
#endif
