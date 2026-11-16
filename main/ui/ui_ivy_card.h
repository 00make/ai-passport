#pragma once

#include "bsp_button.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Customizable profile strings shown on the bottom digital business card. */
#define IVY_CARD_NAME          "IVY"
#define IVY_CARD_ROLE          "OFF-WORK CEO"
#define IVY_CARD_HANDLE        "@IVY_2026"
#define IVY_CARD_TAGLINE       "CLOCK OUT ON TIME!"
#define IVY_CARD_BADGE_NO      "VIP #IVY-2026"

/**
 * Initialize and load the IVY Interactive Personal Digital Business Card screen.
 * Caller must hold bsp_lvgl_lock().
 */
void ui_ivy_card_init(void);

/**
 * Forward physical button events to the IVY card state machine and refresh UI.
 * Caller must hold bsp_lvgl_lock().
 */
void ui_ivy_card_on_button(bsp_btn_t btn, bsp_btn_ev_t ev);

#ifdef __cplusplus
}
#endif
