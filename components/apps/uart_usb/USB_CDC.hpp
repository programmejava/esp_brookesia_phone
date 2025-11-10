#pragma once

#include "esp_brookesia.hpp"
#include "lvgl.h"
#include "TinyUsbCdcService.hpp" // [修改点 1] 包含了新的头文件
#include "ui/usb_icon.h" // [新增] USB图标支持

// (中文注释) 函数声明, 引用由SquareLine导出的UI初始化函数
extern "C" void ui_usb_init(void);
extern "C" void ui_usb_destroy(void);

class USB_CDC : public ESP_Brookesia_PhoneApp {
public:
    USB_CDC();
    ~USB_CDC();

    bool init(void) override;
    bool run(void) override;
    bool back(void) override;
    bool close(void) override;

private:
    void extraUiInit(void);

    static void uiUpdateTimerCb(lv_timer_t *timer);
    static void onButtonStartClicked(lv_event_t *e);
    static void onButtonStopClicked(lv_event_t *e);
    static void onButtonExitClicked(lv_event_t *e);
    static void onButtonSettingsClicked(lv_event_t *e);
    
    // [新增] 设置界面相关方法
    void showSettingsScreen(void);
    void hideSettingsScreen(void);
    static void onButtonApplyClicked(lv_event_t *e);
    static void onButtonBackClicked(lv_event_t *e);

    // [新增] 文本管理方法
    void addTextToDisplay(const char* text);
    void addTextToDisplayImproved(const char* text, size_t actual_len);  // 改进版本
    void smartTextAreaClear(void);
    
    // [新增] 心跳包开关控制
    static void onSwitchHeartbeatChanged(lv_event_t *e);
    void updateHeartbeatState(bool enabled);

    // [新增] 串口配置
    struct SerialSettings {
        uint32_t baud_rate;     // 波特率
        uint8_t data_bits;      // 数据位
        uint8_t parity;         // 校验位
        uint8_t stop_bits;      // 停止位
    };
    SerialSettings _current_settings;
    
    // [新增] 心跳包开关状态
    bool _heartbeat_enabled;

    // -- (中文注释) 成员变量 --
    TinyUsbCdcService _usb_cdc_service; // [修改点 2] 更改了成员变量的类型
    lv_timer_t*   _update_timer;
    bool          _last_conn_state;
    size_t        _current_text_len;
    lv_obj_t*     _main_screen;         // [新增] 保存主界面引用
};