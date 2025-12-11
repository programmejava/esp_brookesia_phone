#pragma once

#include <string>
#include "lvgl.h"
#include "bsp/esp-bsp.h"
#include "../UartBridge.hpp"

// UI 层封装：负责屏幕渲染与事件收集，业务逻辑留在 UartBridge
class UartBridgeUI {
public:
    struct Snapshot {
        UartBridge::PortType port_type;
        UartBridge::SerialSettings serial;
        UartBridge::NetSettings net;
        bool running;
        bool heartbeat_enabled;
    };

    explicit UartBridgeUI(UartBridge &owner);
    ~UartBridgeUI();

    // 创建界面并按照当前配置初始化控件
    void build(const Snapshot &snapshot);
    // 更新 Wi-Fi 状态文案
    void setWifiStatus(const char *text);
    // 更新运行状态文案
    void setStatus(const char *text);
    // 更新统计信息文案
    void setStats(const char *text);
    // 在日志框内追加一行文本
    void appendLog(const char *text);
    // 根据 Wi-Fi/运行状态更新按钮可用性与文案
    void refreshStartButtonState(bool wifi_ok, bool running);
    // 同步 TCP 端口输入框内容
    void updatePort(uint16_t port);

private:
    // LVGL 事件回调：内部仅负责把事件转给业务层
    static void onStartStopClicked(lv_event_t *e);
    static void onPortTypeChanged(lv_event_t *e);
    static void onBaudChanged(lv_event_t *e);
    static void onDataBitsChanged(lv_event_t *e);
    static void onParityChanged(lv_event_t *e);
    static void onStopBitsChanged(lv_event_t *e);
    static void onPortEvent(lv_event_t *e);
    static void onExitClicked(lv_event_t *e);
    static void onClearLogClicked(lv_event_t *e);
    static void onPortResetClicked(lv_event_t *e);
    static void onHeartbeatToggled(lv_event_t *e);
    static void onUiTimer(lv_timer_t *timer);
    void updatePortSummary();

    void setupDropdownSelections(const Snapshot &snapshot);

    UartBridge &_owner;
    lv_obj_t *_label_wifi;
    lv_obj_t *_label_status;
    lv_obj_t *_label_client;
    lv_obj_t *_label_stats;
    lv_obj_t *_label_port_summary;
    lv_obj_t *_btn_start;
    lv_obj_t *_btn_label;
    lv_obj_t *_btn_exit;
    lv_obj_t *_btn_clear_log;
    lv_obj_t *_btn_port_reset;
    lv_obj_t *_dropdown_port;
    lv_obj_t *_dropdown_baud;
    lv_obj_t *_dropdown_databits;
    lv_obj_t *_dropdown_parity;
    lv_obj_t *_dropdown_stopbits;
    lv_obj_t *_switch_heartbeat;
    lv_obj_t *_ta_port;
    lv_obj_t *_kb;
    lv_obj_t *_log;
    lv_timer_t *_ui_timer;
};
