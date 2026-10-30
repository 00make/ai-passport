// tests/test_drobotics_passport_logic.c
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "ui/drobotics_passport_logic.h"

static void test_presets_and_identity(void) {
    drobotics_passport_state_t st;
    drobotics_passport_init(&st);

    assert(strcmp(st.holder_name, "ZHANG ZHENYAO | 00MAKE") == 0);
    assert(strcmp(st.role_tag, "EMBODIED AI ARCHITECT") == 0);
    assert(strcmp(st.pass_id, "#2026-0001") == 0);
    assert(st.badge_mode == DROBOTICS_BADGE_MODE_VIP_PASS);
    assert(st.render_style == DROBOTICS_STYLE_AMBER_HALFTONE);
}

static void test_odometer_roll_and_lock(void) {
    drobotics_passport_state_t st;
    drobotics_passport_init(&st);

    // Initially rolling towards #2026-0001
    assert(drobotics_odometer_is_rolling(&st.pass_odometer));

    // Step through 3 seconds (120 frames of 25ms)
    for (int i = 0; i < 120; i++) {
        drobotics_passport_step(&st, 25);
    }

    assert(!drobotics_odometer_is_rolling(&st.pass_odometer));
    char settled[16];
    drobotics_odometer_get_settled_string(&st.pass_odometer, settled, sizeof(settled));
    assert(strcmp(settled, "#2026-0001") == 0);
}

static void test_matrix_digital_roll_300ms(void) {
    drobotics_matrix_roll_t mr;
    drobotics_matrix_roll_init(&mr, "POS:28.4X|RDK-S100|BPU:98.4%", 0x12345678u);
    drobotics_matrix_roll_trigger(&mr, "POS:28.4X|RDK-S100|BPU:98.4%", 300);

    assert(mr.active);
    // Delimiters '|' must be preserved during scramble
    assert(mr.display[9] == '|');
    assert(mr.display[18] == '|');

    // Step 150ms (mid-roll)
    drobotics_matrix_roll_step(&mr, 150);
    assert(mr.active);

    // Step remaining 160ms -> must lock to exact target
    drobotics_matrix_roll_step(&mr, 160);
    assert(!mr.active);
    assert(strcmp(mr.display, "POS:28.4X|RDK-S100|BPU:98.4%") == 0);
}

static void test_button_interactions(void) {
    drobotics_passport_state_t st;
    drobotics_passport_init(&st);

    // Settle initial roll
    for (int i = 0; i < 120; i++) {
        drobotics_passport_step(&st, 25);
    }
    assert(!drobotics_odometer_is_rolling(&st.pass_odometer));

    // 1. Single-click cycles badge mode and re-triggers odometer shuffle + matrix roll
    drobotics_passport_handle_key(&st, 2, DROBOTICS_KEY_EVENT_CLICK);
    assert(st.badge_mode == DROBOTICS_BADGE_MODE_BRAND_CN);
    assert(drobotics_odometer_is_rolling(&st.pass_odometer));
    assert(st.matrix_roll.active);

    // Settle again and verify Pass ID still locks to #2026-0001
    for (int i = 0; i < 140; i++) {
        drobotics_passport_step(&st, 25);
    }
    char settled[16];
    drobotics_odometer_get_settled_string(&st.pass_odometer, settled, sizeof(settled));
    assert(strcmp(settled, "#2026-0001") == 0);

    // 2. Double-click cycles 3D render style
    drobotics_passport_handle_key(&st, 2, DROBOTICS_KEY_EVENT_DOUBLE);
    assert(st.render_style == DROBOTICS_STYLE_GREEN_PHOSPHOR);
    drobotics_passport_handle_key(&st, 2, DROBOTICS_KEY_EVENT_DOUBLE);
    assert(st.render_style == DROBOTICS_STYLE_SQUARE_LED);
    drobotics_passport_handle_key(&st, 2, DROBOTICS_KEY_EVENT_DOUBLE);
    assert(st.render_style == DROBOTICS_STYLE_AMBER_HALFTONE);

    // 3. Long-press activates Overclock boost, release smoothly returns to 1.0x
    drobotics_passport_handle_key(&st, 2, DROBOTICS_KEY_EVENT_LONG);
    assert(st.overclock_active);
    assert(st.speed_scale_q8 > 256u);

    drobotics_passport_handle_key(&st, 2, DROBOTICS_KEY_EVENT_RELEASE);
    for (int i = 0; i < 160; i++) {
        drobotics_passport_step(&st, 25);
    }
    assert(!st.overclock_active);
    assert(st.speed_scale_q8 == 256u);
}

static void test_3d_logo_renderer_and_chinese_glyphs(void) {
    drobotics_passport_state_t st;
    drobotics_passport_init(&st);

    uint8_t grid_a[DROBOTICS_LOGO_GRID_H][DROBOTICS_LOGO_GRID_W];
    uint8_t grid_b[DROBOTICS_LOGO_GRID_H][DROBOTICS_LOGO_GRID_W];
    drobotics_render_logo_grid(&st, grid_a);

    int lit_cells = 0;
    int crease_cells = 0;
    int left_half_lit = 0;
    int right_half_lit = 0;
    for (int y = 0; y < DROBOTICS_LOGO_GRID_H; y++) {
        for (int x = 0; x < DROBOTICS_LOGO_GRID_W; x++) {
            uint8_t v = grid_a[y][x];
            uint8_t intensity = v & 0x0Fu;
            if (intensity > 0) {
                lit_cells++;
                if (x < DROBOTICS_LOGO_GRID_W / 2) left_half_lit++;
                else right_half_lit++;
            }
            if (v & 0x80u) {
                crease_cells++;
            }
        }
    }

    // Both left cylinder and right D-arch must produce lit halftone cells and edge creases
    assert(lit_cells > 300);
    assert(left_half_lit > 80);
    assert(right_half_lit > 120);
    assert(crease_cells > 40);

    // Advance rotation and verify the 3D projection changes dynamically
    for (int i = 0; i < 15; i++) {
        drobotics_passport_step(&st, 25);
    }
    drobotics_render_logo_grid(&st, grid_b);
    int diff_cells = 0;
    for (int y = 0; y < DROBOTICS_LOGO_GRID_H; y++) {
        for (int x = 0; x < DROBOTICS_LOGO_GRID_W; x++) {
            if (grid_a[y][x] != grid_b[y][x]) diff_cells++;
        }
    }
    assert(diff_cells > 50);

    // Verify all 5 Chinese 16x16 glyphs ("地瓜机器人") have non-empty strokes
    for (uint8_t idx = 0; idx < 5u; idx++) {
        const uint16_t *glyph = drobotics_chinese_glyph_16x16(idx);
        int non_zero_rows = 0;
        for (int r = 0; r < 16; r++) {
            if (glyph[r] != 0u) non_zero_rows++;
        }
        assert(non_zero_rows >= 12);
    }
}

int main(void) {
    test_presets_and_identity();
    test_odometer_roll_and_lock();
    test_matrix_digital_roll_300ms();
    test_button_interactions();
    test_3d_logo_renderer_and_chinese_glyphs();
    printf("test_drobotics_passport_logic: PASS\n");
    return 0;
}
