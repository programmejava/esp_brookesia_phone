#pragma once

#include "esp_timer.h"
#include "lvgl.h"
#include "bsp/display.h"

class GlobalScreenSaver {
public:
    static GlobalScreenSaver& getInstance();
    
    void init();
    void setTimeoutSeconds(int timeout_seconds);
    void onUserActivity();
    void turnOffScreen();
    void turnOnScreen();
    
    bool isScreenOff() const { return _screen_is_off; }
    
private:
    GlobalScreenSaver() = default;
    ~GlobalScreenSaver();
    
    static void screenSaverTimerCallback(void* arg);
    static void globalTouchEventCallback(lv_event_t* e);
    
    void startTimer();
    void stopTimer();
    int getCurrentBrightness();  // 从NVS获取当前亮度设置
    
    esp_timer_handle_t _screen_saver_timer = nullptr;
    int _timeout_seconds = 30;  // 默认30秒
    bool _screen_is_off = false;
    bool _is_initialized = false;
};