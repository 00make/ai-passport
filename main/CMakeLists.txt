idf_component_register(
    SRCS "main.c"
         "ui/ivy_sprites_data.c"
         "ui/ivy_card_logic.c"
         "ui/ui_ivy_card.c"
    INCLUDE_DIRS "." "ui"
    REQUIRES bsp esp_timer
)
