// main/ui/ui_drobotics_passport.h
// D-Robotics 2026 Developer Conference AI Passport Badge UI Module.
// Renders the 3D Halftone Dot-Matrix Rotating Logo, Cassette Futurism HUD,
// Odometer vertical rolling digits, and Matrix Digital Roll telemetry.
#pragma once

#include "bsp_button.h"
#include "drobotics_passport_logic.h"

#ifdef __cplusplus
extern "C" {
#endif

// Initialize and load the D-Robotics 2026 Passport screen into LVGL.
// Must be called while holding bsp_lvgl_lock().
void ui_drobotics_passport_enter(void);

// Stop the animation timer and delete the screen.
// Must be called while holding bsp_lvgl_lock().
void ui_drobotics_passport_exit(void);

// Forward a physical button event from the input dispatch task.
// Acquires bsp_lvgl_lock() internally when updating state.
void ui_drobotics_passport_handle_key(bsp_btn_t btn, bsp_btn_ev_t ev);

// Optional runtime configuration of badge holder identity fields.
void ui_drobotics_passport_configure(const char *holder_name,
                                     const char *role_tag,
                                     const char *pass_id);

#ifdef __cplusplus
}
#endif
