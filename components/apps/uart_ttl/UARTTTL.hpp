#pragma once

#include "esp_brookesia.hpp"
#include "lvgl.h"
#include "UartService.hpp"
#include "nvs_flash.h"

extern "C" void uart_ttl_ui_init(void);

/**
 * @class UARTTTL
 * @brief UART TTL调试工具应用类
 * 
 * 这是一个基于ESP_Brookesia框架的UART串口调试工具，
 * 提供图形界面来配置串口参数、接收和显示串口数据、
 * 发送心跳包等功能。
 */
class UARTTTL : public ESP_Brookesia_PhoneApp {
public:
    UARTTTL();
    ~UARTTTL();

    // 重写基类虚函数
    bool init(void) override;
    bool run(void) override;
    bool back(void) override;
    bool close(void) override;
    bool resume(void) override;

private:
    // UI初始化相关方法
    void extraUiInit(void);
    void setupSettingsScreenEvents(void);

    // 静态回调函数（用于LVGL事件处理）
    static void uiUpdateTimerCb(lv_timer_t *timer);
    static void onButtonStartClicked(lv_event_t *e);
    static void onButtonStopClicked(lv_event_t *e);
    static void onButtonExitClicked(lv_event_t *e);
    static void onButtonSettingsClicked(lv_event_t *e);
    static void onSwitchHeartbeatToggled(lv_event_t *e);
    static void onScreenSettingsLoaded(lv_event_t *e);
    static void onButtonSettingsApplyClicked(lv_event_t *e);
    static void onButtonSettingsBackClicked(lv_event_t *e);
    
    // NVS存储操作方法
    void loadSettings();
    void saveSettings();
    
    // [新增] 文本处理方法
    void addTextToDisplay(const char* text);
    void addTextToDisplayImproved(const char* text, size_t actual_len);
    void smartTextAreaClear(void);

    // 成员变量
    UartService _uart_service;          // UART服务对象
    lv_timer_t* _update_timer;          // UI更新定时器
    lv_obj_t*   _text_area_ttl;         // 文本显示区域控件指针
    uint32_t    _last_tx_timestamp;     // 上次发送心跳包的时间戳
    size_t      _current_text_len;      // 当前文本区域的文本长度
    UartConfig  _current_config;        // 当前UART配置
    nvs_handle_t _nvs_handle;           // NVS存储句柄
    bool        _heartbeat_enabled;     // 心跳包发送开关状态
    uint32_t    _heartbeat_counter;     // 心跳包序号计数器
};