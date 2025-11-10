#pragma once

#include "esp_brookesia.hpp"
#include "ModbusController.hpp"
#include "ModbusTest.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"

/**
 * @class PowerController
 * @brief XY6506S电源控制器应用类
 * 
 * 基于ESP_Brookesia框架的电源控制应用，支持：
 * - Modbus-RTU通信控制XY6506S电源
 * - 实时数据显示和状态同步
 * - 预设电压电流快速设置
 * - 手动电压电流调节
 * - 开关状态控制
 */
class PowerController : public ESP_Brookesia_PhoneApp {
public:
    PowerController();
    ~PowerController();

    // 重写基类虚函数
    bool init(void) override;
    bool run(void) override;
    bool back(void) override;
    bool close(void) override;
    bool resume(void) override;
    bool pause(void) override;

private:
    // 核心组件
    ModbusController* modbus_controller;    // Modbus通信控制器
    TimerHandle_t update_timer;             // 数据更新定时器
    TaskHandle_t update_task_handle;        // 持久更新任务句柄
    
    // 更新控制
    static const uint32_t UPDATE_INTERVAL_MS = 300;   // 更新间隔300ms(快速响应)
    bool is_running;                        // 应用运行状态
    bool update_requested;                  // 更新请求标志
    
    // 私有方法
    void setupUIEvents();                   // 设置UI事件处理
    void updateDisplayValues();             // 更新显示值（完整更新）
    void updateDisplayValuesQuick();        // 快速显示默认值
    void updateDisplayValuesAsync();        // 异步更新显示值
    void updateSwitchStates();              // 更新开关状态
    bool applyVoltageCurrentSettings();     // 应用电压电流设置
    void runModbusDiagnostic();             // 运行Modbus诊断
    static void updateTask(void* parameter);// 持久更新任务
    
    // 静态回调函数
    static void updateTimerCallback(TimerHandle_t timer);
    static void onPresetButtonClick(lv_event_t* e);
    static void onApplyButtonClick(lv_event_t* e);
    static void onSwitchChanged(lv_event_t* e);
    
    // 预设值定义
    struct PresetValue {
        float voltage;
        float current;
    };
    
    static const PresetValue PRESET_VALUES[];
    static const int PRESET_COUNT;
};