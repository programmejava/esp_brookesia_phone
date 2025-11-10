SET(SOURCES screens/ui_ScreenUSB.c
    screens/ui_ScreenUSBSettings.c
    ui.c
    components/ui_comp_hook.c
    fonts/ui_font_Number.c
    usb_icon.c)

add_library(ui ${SOURCES})
