#include "ui/ivy_card_logic.h"
#include "ui/ivy_sprites_data.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static int count_active_particles(const ivy_card_state_t *st) {
    int count = 0;
    for (int i = 0; i < IVY_PARTICLE_MAX; ++i) {
        if (st->particles[i].kind != IVY_PARTICLE_NONE && st->particles[i].ttl > 0) {
            count++;
        }
    }
    return count;
}

static void assert_all_utf8_glyphs_exist(const char *s) {
    assert(s != NULL && *s != '\0');
    const uint8_t *p = (const uint8_t *)s;
    while (*p) {
        if (p[0] < 0x80u) {
            p += 1;
        } else if ((p[0] & 0xE0u) == 0xC0u) {
            assert(p[1] != 0);
            uint32_t cp = ((uint32_t)(p[0] & 0x1Fu) << 6) | (uint32_t)(p[1] & 0x3Fu);
            assert(ivy_glyph12_find(cp) != NULL);
            p += 2;
        } else {
            assert((p[0] & 0xF0u) == 0xE0u && p[1] != 0 && p[2] != 0);
            uint32_t cp = ((uint32_t)(p[0] & 0x0Fu) << 12)
                        | ((uint32_t)(p[1] & 0x3Fu) << 6)
                        | (uint32_t)(p[2] & 0x3Fu);
            assert(ivy_glyph12_find(cp) != NULL);
            p += 3;
        }
    }
}

static void assert_snapshot_glyphs(const ivy_render_snapshot_t *snap) {
    assert_all_utf8_glyphs_exist(snap->stage_badge_utf8);
    assert_all_utf8_glyphs_exist(snap->mode_pill);
    assert_all_utf8_glyphs_exist(snap->stage_hint_ascii);
}

static void test_sprites_and_glyphs(void) {
    for (int i = 0; i < IVY_SPRITE_COUNT; ++i) {
        const ivy_sprite_t *sp = ivy_sprite_get((ivy_sprite_id_t)i);
        assert(sp != NULL);
        assert(sp->w > 0);
        assert(sp->h > 0);
        assert(sp->palette != NULL);
        assert(sp->pixels != NULL);
        assert(sp->palette[0] == 0);
    }
    assert(ivy_sprite_get((ivy_sprite_id_t)-1) == NULL);
    assert(ivy_sprite_get(IVY_SPRITE_COUNT) == NULL);

    /* Core cute icons and gift-card CJK glyphs */
    assert(ivy_glyph12_find(0x2665u) != NULL); /* ♥ */
    assert(ivy_glyph12_find(0x2726u) != NULL); /* ✦ */
    assert(ivy_glyph12_find(0x2605u) != NULL); /* ★ */
    assert(ivy_glyph12_find(0x266Au) != NULL); /* ♪ */
    assert(ivy_glyph12_find(0x273Fu) != NULL); /* ✿ */
    assert(ivy_glyph12_find(0x00B7u) != NULL); /* · */
    assert(ivy_glyph12_find(0x793Cu) != NULL); /* 礼 */
    assert(ivy_glyph12_find(0x7269u) != NULL); /* 物 */
    assert(ivy_glyph12_find(0x5B9Du) != NULL); /* 宝 */
    assert(ivy_glyph12_find(0x8D1Du) != NULL); /* 贝 */
    assert(ivy_glyph12_find(0x4E0Bu) != NULL); /* 下 */
    assert(ivy_glyph12_find(0x73EDu) != NULL); /* 班 */
    assert(ivy_glyph12_find(0x5566u) != NULL); /* 啦 */
    assert(ivy_glyph12_find(0x0041u) == NULL);

    /* All dock strings */
    static const char *dock_strings[] = {
        "♥ IVY 专属甜心卡",
        "状态: 努力搬砖 · 等你接下班 ♥",
        "状态: 准备下班 · 开心加倍 ✦",
        "身份: 甜酷小仙女 · 快乐满分 ♥",
        "OK互动 · 上换模式 · 下翻页",
        "✦ 我家宝贝属性卡",
        "可爱值: 999+  甜酷值: 满分 ♥",
        "心动指数: ♥♥♥♥♥ 超喜欢你",
        "✿ 送给宝贝的小礼物",
        "愿宝贝每天准点下班 超开心 ♥",
        "专属小礼物 · 永远偏爱你 ♥",
    };
    for (size_t i = 0; i < sizeof(dock_strings) / sizeof(dock_strings[0]); ++i) {
        assert_all_utf8_glyphs_exist(dock_strings[i]);
    }
}

static void test_story_sequence_and_replay(void) {
    ivy_card_state_t st;
    ivy_render_snapshot_t snap;

    ivy_card_logic_init(&st);
    assert(st.hero_mode == IVY_MODE_STORY);
    assert(st.story_phase == IVY_STORY_WORKING);
    assert(st.card_tab == IVY_TAB_PROFILE);
    assert(!st.poster_mode);

    ivy_card_logic_get_snapshot(&st, &snap);
    assert(snap.main_sprite == IVY_SPRITE_WORK_0);
    assert(!snap.show_offwork_banner);
    assert(snap.mood_pct == 25);
    assert_snapshot_glyphs(&snap);

    /* Clock out on OK click */
    ivy_card_logic_on_button(&st, IVY_BTN_OK, IVY_EV_CLICK);
    assert(st.story_phase == IVY_STORY_CLOCK_OUT_SEQ);
    ivy_card_logic_get_snapshot(&st, &snap);
    assert(snap.main_sprite == IVY_SPRITE_WORK_1);
    assert_snapshot_glyphs(&snap);

    for (int i = 0; i < 14; ++i) ivy_card_logic_tick(&st);
    ivy_card_logic_get_snapshot(&st, &snap);
    assert(snap.main_sprite == IVY_SPRITE_WORK_2);
    assert_snapshot_glyphs(&snap);

    for (int i = 0; i < 14; ++i) ivy_card_logic_tick(&st);
    ivy_card_logic_get_snapshot(&st, &snap);
    assert(snap.main_sprite == IVY_SPRITE_WORK_3);
    assert(snap.show_offwork_banner);
    assert(snap.mood_pct == 100);
    assert(count_active_particles(&st) > 0);
    assert_snapshot_glyphs(&snap);

    for (int i = 0; i < 24; ++i) ivy_card_logic_tick(&st);
    assert(st.story_phase == IVY_STORY_OFFWORK_LOOP);

    ivy_card_logic_get_snapshot(&st, &snap);
    assert(snap.main_sprite == IVY_SPRITE_WORK_4);
    assert(snap.show_offwork_banner);
    assert_snapshot_glyphs(&snap);

    for (int i = 0; i < 20; ++i) ivy_card_logic_tick(&st);
    ivy_card_logic_get_snapshot(&st, &snap);
    assert(snap.main_sprite == IVY_SPRITE_WORK_5);

    for (int i = 0; i < 22; ++i) ivy_card_logic_tick(&st);
    ivy_card_logic_get_snapshot(&st, &snap);
    assert(snap.main_sprite == IVY_SPRITE_WORK_6 || snap.main_sprite == IVY_SPRITE_WORK_7);

    /* First OK click cheers */
    ivy_card_logic_on_button(&st, IVY_BTN_OK, IVY_EV_CLICK);
    assert(st.offwork_cheered);
    assert(st.jump_ticks == 14);
    assert(st.story_phase == IVY_STORY_OFFWORK_LOOP);
    ivy_card_logic_get_snapshot(&st, &snap);
    assert_snapshot_glyphs(&snap);

    /* Second OK click resets back to working */
    ivy_card_logic_on_button(&st, IVY_BTN_OK, IVY_EV_CLICK);
    assert(st.story_phase == IVY_STORY_WORKING);
    assert(!st.offwork_cheered);
    ivy_card_logic_get_snapshot(&st, &snap);
    assert(snap.mood_pct == 25);

    /* Double-click OK from OFFWORK_LOOP also resets directly even without first cheer */
    ivy_card_logic_on_button(&st, IVY_BTN_OK, IVY_EV_CLICK);
    for (int i = 0; i < 60; ++i) ivy_card_logic_tick(&st);
    assert(st.story_phase == IVY_STORY_OFFWORK_LOOP);
    ivy_card_logic_on_button(&st, IVY_BTN_OK, IVY_EV_DOUBLE);
    assert(st.story_phase == IVY_STORY_WORKING);
}

static void test_modes_tabs_and_poster(void) {
    ivy_card_state_t st;
    ivy_render_snapshot_t snap;
    ivy_card_logic_init(&st);

    /* Mode cycle UP -> TURNAROUND */
    ivy_card_logic_on_button(&st, IVY_BTN_UP, IVY_EV_CLICK);
    assert(st.hero_mode == IVY_MODE_TURNAROUND);
    ivy_card_logic_get_snapshot(&st, &snap);
    assert(snap.main_sprite == IVY_SPRITE_TURN_FRONT);
    assert_snapshot_glyphs(&snap);

    ivy_card_logic_on_button(&st, IVY_BTN_OK, IVY_EV_CLICK);
    ivy_card_logic_get_snapshot(&st, &snap);
    assert(snap.main_sprite == IVY_SPRITE_TURN_SIDE);
    assert_snapshot_glyphs(&snap);

    ivy_card_logic_on_button(&st, IVY_BTN_OK, IVY_EV_CLICK);
    ivy_card_logic_get_snapshot(&st, &snap);
    assert(snap.main_sprite == IVY_SPRITE_TURN_BACK);
    assert_snapshot_glyphs(&snap);

    ivy_card_logic_on_button(&st, IVY_BTN_OK, IVY_EV_CLICK);
    ivy_card_logic_get_snapshot(&st, &snap);
    assert(snap.main_sprite == IVY_SPRITE_TURN_FRONT);

    /* Mode cycle UP -> EXPRESSIONS */
    ivy_card_logic_on_button(&st, IVY_BTN_UP, IVY_EV_CLICK);
    assert(st.hero_mode == IVY_MODE_EXPRESSIONS);
    ivy_card_logic_get_snapshot(&st, &snap);
    assert(snap.main_sprite == IVY_SPRITE_EXPR_LAUGH);
    assert_snapshot_glyphs(&snap);

    ivy_card_logic_on_button(&st, IVY_BTN_OK, IVY_EV_CLICK);
    ivy_card_logic_get_snapshot(&st, &snap);
    assert(snap.main_sprite == IVY_SPRITE_EXPR_SHOCK);
    assert_snapshot_glyphs(&snap);

    ivy_card_logic_on_button(&st, IVY_BTN_OK, IVY_EV_CLICK);
    ivy_card_logic_get_snapshot(&st, &snap);
    assert(snap.main_sprite == IVY_SPRITE_EXPR_DEADPAN);
    assert_snapshot_glyphs(&snap);

    ivy_card_logic_on_button(&st, IVY_BTN_OK, IVY_EV_CLICK);
    ivy_card_logic_get_snapshot(&st, &snap);
    assert(snap.main_sprite == IVY_SPRITE_EXPR_SMIRK);
    assert_snapshot_glyphs(&snap);

    ivy_card_logic_on_button(&st, IVY_BTN_OK, IVY_EV_CLICK);
    ivy_card_logic_get_snapshot(&st, &snap);
    assert(snap.main_sprite == IVY_SPRITE_EXPR_LAUGH);

    /* Mode cycle UP -> STORY */
    ivy_card_logic_on_button(&st, IVY_BTN_UP, IVY_EV_CLICK);
    assert(st.hero_mode == IVY_MODE_STORY);

    /* Tab cycle via DOWN */
    assert(st.card_tab == IVY_TAB_PROFILE);
    ivy_card_logic_on_button(&st, IVY_BTN_DOWN, IVY_EV_CLICK);
    assert(st.card_tab == IVY_TAB_STATS);
    ivy_card_logic_on_button(&st, IVY_BTN_DOWN, IVY_EV_CLICK);
    assert(st.card_tab == IVY_TAB_VIP);
    ivy_card_logic_on_button(&st, IVY_BTN_DOWN, IVY_EV_CLICK);
    assert(st.card_tab == IVY_TAB_PROFILE);

    /* Poster mode toggle via OK long press */
    assert(!st.poster_mode);
    ivy_card_logic_on_button(&st, IVY_BTN_OK, IVY_EV_LONG);
    assert(st.poster_mode);
    ivy_card_logic_on_button(&st, IVY_BTN_OK, IVY_EV_LONG);
    assert(!st.poster_mode);
}

int main(void) {
    test_sprites_and_glyphs();
    test_story_sequence_and_replay();
    test_modes_tabs_and_poster();
    printf("test_ivy_card_logic: ALL PASS\n");
    return 0;
}
