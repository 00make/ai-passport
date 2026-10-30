idf_component_register(
    SRCS "main.c"
         "ui/drobotics_passport_logic.c"
         "ui/ui_drobotics_passport.c"
    INCLUDE_DIRS "." "ui"
    REQUIRES bsp esp_hw_support esp_timer
)
