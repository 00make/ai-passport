#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    IVY_SPRITE_WORK_0 = 0,
    IVY_SPRITE_WORK_1 = 1,
    IVY_SPRITE_WORK_2 = 2,
    IVY_SPRITE_WORK_3 = 3,
    IVY_SPRITE_WORK_4 = 4,
    IVY_SPRITE_WORK_5 = 5,
    IVY_SPRITE_WORK_6 = 6,
    IVY_SPRITE_WORK_7 = 7,
    IVY_SPRITE_OFFWORK_BANNER = 8,
    IVY_SPRITE_TURN_FRONT = 9,
    IVY_SPRITE_TURN_SIDE = 10,
    IVY_SPRITE_TURN_BACK = 11,
    IVY_SPRITE_EXPR_LAUGH = 12,
    IVY_SPRITE_EXPR_SHOCK = 13,
    IVY_SPRITE_EXPR_DEADPAN = 14,
    IVY_SPRITE_EXPR_SMIRK = 15,
    IVY_SPRITE_COUNT = 16,
} ivy_sprite_id_t;

typedef struct {
    uint16_t w;
    uint16_t h;
    const uint16_t *palette;
    const uint8_t *pixels;
} ivy_sprite_t;

const ivy_sprite_t *ivy_sprite_get(ivy_sprite_id_t id);

#ifdef __cplusplus
}
#endif
