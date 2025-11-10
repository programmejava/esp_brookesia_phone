#pragma once

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

// USB应用图标相关函数声明
const lv_img_dsc_t* get_usb_app_icon(void);
const char* get_usb_app_icon_symbol(void);
lv_color_t get_usb_app_icon_color(void);

#ifdef __cplusplus
}
#endif