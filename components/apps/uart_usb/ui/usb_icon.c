// USB图标 - 基于ESP_Brookesia框架标准实现
// 为ESP32P4开发板的USB CDC应用提供图标

#include "usb_icon.h"
#include "../assets/img_app_usbcdc.h"

#ifdef __cplusplus
extern "C" {
#endif

// 获取USB应用图标
const lv_img_dsc_t* get_usb_app_icon(void) {
    // 返回USB CDC图标（PNG自动生成）
    return &img_app_usbcdc;
}

// 备用方案：使用字体符号
const char* get_usb_app_icon_symbol(void) {
    return LV_SYMBOL_USB;  // 使用LVGL内置USB符号
}

// 获取图标颜色主题
lv_color_t get_usb_app_icon_color(void) {
    return lv_color_hex(0x2196F3);  // 蓝色，常用于USB相关UI
}

#ifdef __cplusplus
}
#endif