idf_component_register(
    SRCS "PowerController.cpp"
         "ModbusController.cpp"
         "ModbusTest.cpp"
         "ui/ui.c"
         "ui/screens/ui_PowerController.c"
         "ui/components/ui_comp_hook.c"
         "ui/fonts/ui_font_JetBrainsMono36.c"
         "ui/fonts/ui_font_JetBrainsMono48.c"
         "ui/fonts/ui_font_JetBrainsMono24.c"
    INCLUDE_DIRS "." "ui" "ui/screens"
    REQUIRES 
        "esp_brookesia"
        "lvgl"
        "driver"
        "esp_timer"
)