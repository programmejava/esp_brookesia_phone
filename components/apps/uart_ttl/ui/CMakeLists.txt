SET(SOURCES screens/ui_ScreenTTL.c
    screens/ui_ScreenSettings.c
    ui.c
    components/ui_comp_hook.c
    ui_helpers.c
    ui_events.c
    fonts/ui_font_Number.c)

add_library(ui ${SOURCES})
