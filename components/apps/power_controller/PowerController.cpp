/**
 * @file PowerController.cpp
 * @brief XY6506S电源控制器应用程序实现文件
 * @details 实现完整的电源控制功能，包括Modbus通信、UI控制和状态同步
 * @author ESP32开发团队
 * @date 2025年11月4日
 * @version 1.0
 */

#include "PowerController.hpp"
#include "esp_log.h"
#include "esp_timer.h"
#include "ui/ui.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *TAG = "PowerController";

// 预设值定义
const PowerController::PresetValue PowerController::PRESET_VALUES[] = {
    {3.3f, 3.0f},   // 3V3 3A
    {3.3f, 5.0f},   // 3V3 5A  
    {5.0f, 3.0f},   // 5V 3A
    {5.0f, 5.0f},   // 5V 5A
    {12.0f, 3.0f},  // 12V 3A
    {12.0f, 5.0f}   // 12V 5A
};

const int PowerController::PRESET_COUNT = sizeof(PRESET_VALUES) / sizeof(PRESET_VALUES[0]);

/**
 * @brief 构造函数
 * @details 初始化电源控制器应用，设置应用名称和图标
 */
PowerController::PowerController()
    : ESP_Brookesia_PhoneApp("Power Control", nullptr, true),
      modbus_controller(nullptr), update_timer(nullptr), update_task_handle(nullptr),
      is_running(false), update_requested(false)
{
    ESP_LOGI(TAG, "PowerController created");
}

/**
 * @brief 析构函数
 * @details 清理资源，释放内存
 */
PowerController::~PowerController()
{
    // 停止运行状态
    is_running = false;
    
    // 停止定时器
    if (update_timer != nullptr) {
        xTimerStop(update_timer, 0);
        xTimerDelete(update_timer, 0);
        update_timer = nullptr;
    }
    
    // 停止更新任务
    if (update_task_handle != nullptr) {
        vTaskDelete(update_task_handle);
        update_task_handle = nullptr;
    }
    
    // 清理Modbus控制器
    if (modbus_controller != nullptr) {
        delete modbus_controller;
        modbus_controller = nullptr;
    }
    
    ESP_LOGI(TAG, "PowerController destroyed");
}

/**
 * @brief 初始化函数
 * @details 重写父类的初始化函数
 * @return true 初始化成功，false 初始化失败
 */
bool PowerController::init(void)
{
    ESP_LOGI(TAG, "Initializing PowerController - DIAGNOSTIC MODE");
    
    // 启用ModbusController的调试日志
    esp_log_level_set("ModbusController", ESP_LOG_DEBUG);
    
    // 创建Modbus控制器，准备进行诊断测试
    modbus_controller = new ModbusController();
    if (modbus_controller == nullptr) {
        ESP_LOGE(TAG, "Failed to create ModbusController");
        return false;
    }
    
    // 尝试初始化Modbus控制器进行诊断
    ESP_LOGI(TAG, "Initializing Modbus controller for diagnostic testing...");
    if (modbus_controller->initialize()) {
        ESP_LOGI(TAG, "✅ Modbus controller initialized successfully - ready for testing");
        
        // 执行设备地址扫描
        ESP_LOGI(TAG, "🔍 Starting device address scan...");
        modbus_controller->scanForDevices();
    } else {
        ESP_LOGW(TAG, "⚠️ Modbus initialization failed - will operate in safe mode");
        // 不删除控制器，保留用于诊断
    }
    
    /*
    // 创建Modbus控制器
    modbus_controller = new ModbusController();
    if (modbus_controller == nullptr) {
        ESP_LOGE(TAG, "Failed to create ModbusController");
        return false;
    }
    
    // 初始化Modbus通信
    if (!modbus_controller->initialize()) {
        ESP_LOGE(TAG, "Failed to initialize Modbus communication");
        delete modbus_controller;
        modbus_controller = nullptr;
        return false;
    }
    */
    
    // 通信正常，启用完整功能定时器
    update_timer = xTimerCreate(
        "PowerUpdate",                          // 定时器名称
        pdMS_TO_TICKS(UPDATE_INTERVAL_MS),     // 定时器周期
        pdTRUE,                                // 自动重载
        this,                                  // 定时器ID (传递this指针)
        updateTimerCallback                    // 回调函数
    );
    
    if (update_timer == nullptr) {
        ESP_LOGE(TAG, "Failed to create update timer");
        if (modbus_controller) {
            modbus_controller->deinitialize();
            delete modbus_controller;
            modbus_controller = nullptr;
        }
        return false;
    }
    
    ESP_LOGI(TAG, "Update timer created successfully - ready for real-time operation");
    
    // 在创建任务之前先设置运行状态，避免任务立即退出
    ESP_LOGI(TAG, "Setting is_running to true before creating task");
    is_running = true;
    
    // 创建持久更新任务
    BaseType_t task_result = xTaskCreate(
        updateTask,                    // 任务函数
        "PowerUpdate",                 // 任务名称
        4096,                          // 堆栈大小
        this,                          // 传递this指针
        5,                             // 优先级
        &update_task_handle           // 任务句柄
    );
    
    if (task_result != pdPASS) {
        ESP_LOGE(TAG, "Failed to create update task");
        is_running = false;
        xTimerDelete(update_timer, 0);
        update_timer = nullptr;
        if (modbus_controller) {
            modbus_controller->deinitialize();
            delete modbus_controller;
            modbus_controller = nullptr;
        }
        return false;
    }
    
    ESP_LOGI(TAG, "Update task created successfully");
    ESP_LOGI(TAG, "PowerController initialized successfully");
    return true;
}

/**
 * @brief 运行电源控制器应用
 * @details 启动应用并显示UI界面
 * @return true 运行成功，false 运行失败
 */
bool PowerController::run(void)
{
    ESP_LOGI(TAG, "Running PowerController");
    
    // 初始化UI界面
    ui_power_controller_init();
    
    // 设置UI事件处理
    setupUIEvents();
    
    // 获取需要对齐的UI元素
    extern lv_obj_t * ui_LabelVoltageValue;
    extern lv_obj_t * ui_LabelCurrentValue; 
    extern lv_obj_t * ui_LabelPowerValue;
    extern lv_obj_t * ui_LabelVoltageSetValue;
    extern lv_obj_t * ui_LabelCurrentSetValue;
    extern lv_obj_t * ui_LabelVoltageInputValue;
    
    // 设置测量值显示的右对齐
    if (ui_LabelVoltageValue && ui_LabelCurrentValue && ui_LabelPowerValue) {
        lv_obj_set_style_text_align(ui_LabelVoltageValue, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_align(ui_LabelCurrentValue, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_align(ui_LabelPowerValue, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN | LV_STATE_DEFAULT);
    }
    
    // 设置设定值显示的右对齐
    if (ui_LabelVoltageSetValue && ui_LabelCurrentSetValue) {
        lv_obj_set_style_text_align(ui_LabelVoltageSetValue, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_align(ui_LabelCurrentSetValue, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN | LV_STATE_DEFAULT);
    }
    
    // 设置输入电压显示的右对齐
    if (ui_LabelVoltageInputValue) {
        lv_obj_set_style_text_align(ui_LabelVoltageInputValue, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN | LV_STATE_DEFAULT);
    }
    
    // 设置面板宽度
    extern lv_obj_t * ui_PanelVoltageLabel;
    extern lv_obj_t * ui_PanelCurrentLabel;
    extern lv_obj_t * ui_PanelPower;
    extern lv_obj_t * ui_PanelVoltageValue;
    extern lv_obj_t * ui_PanelCurrentValue;
    extern lv_obj_t * ui_PanelPowerValue;
    extern lv_obj_t * ui_PanelVoltageSetValue;
    extern lv_obj_t * ui_PanelCurrentSetValue;
    
    if (ui_PanelVoltageLabel && ui_PanelCurrentLabel && ui_PanelPower) {
        lv_obj_set_width(ui_PanelVoltageLabel, 160);
        lv_obj_set_width(ui_PanelCurrentLabel, 160);
        lv_obj_set_width(ui_PanelPower, 160);
    }
    
    if (ui_PanelVoltageValue && ui_PanelCurrentValue && ui_PanelPowerValue) {
        lv_obj_set_width(ui_PanelVoltageValue, 250);
        lv_obj_set_width(ui_PanelCurrentValue, 250);
        lv_obj_set_width(ui_PanelPowerValue, 250);
    }
    
    if (ui_PanelVoltageSetValue && ui_PanelCurrentSetValue) {
        lv_obj_set_width(ui_PanelVoltageSetValue, 250);
        lv_obj_set_width(ui_PanelCurrentSetValue, 250);
    }
    
    // OPTIMIZED MODE: 启用优化的实时更新机制
    // 使用更长的更新间隔和错误恢复机制
    ESP_LOGI(TAG, "Starting OPTIMIZED real-time update mode");
    
    // 设置完成UI设置标记
    ESP_LOGI(TAG, "UI events setup completed");
    
    // 🔥 重要修复：重新进入时必须重置运行状态和重建更新任务
    is_running = true;
    
    // 检查更新任务是否还存在，如果不存在则重新创建
    if (update_task_handle == nullptr) {
        ESP_LOGI(TAG, "Update task not found, recreating...");
        BaseType_t task_result = xTaskCreate(
            updateTask,                    // 任务函数
            "PowerUpdate",                 // 任务名称
            4096,                          // 堆栈大小
            this,                          // 传递this指针
            5,                             // 优先级
            &update_task_handle           // 任务句柄
        );
        
        if (task_result != pdPASS) {
            ESP_LOGE(TAG, "Failed to recreate update task");
            is_running = false;
            return false;
        }
        ESP_LOGI(TAG, "Update task recreated successfully");
    } else {
        ESP_LOGI(TAG, "Update task already exists, ready for timer notifications");
    }
    
    // 给系统一点时间完成UI初始化，避免在系统启动时就开始通信
    ESP_LOGI(TAG, "Waiting for system stabilization before starting Modbus communication...");
    vTaskDelay(pdMS_TO_TICKS(2000)); // 等待2秒让系统完全启动
    
    // 启用实时更新定时器 - 使用300ms间隔实现快速响应
    if (update_timer && xTimerStart(update_timer, pdMS_TO_TICKS(1000)) == pdPASS) {
        ESP_LOGI(TAG, "✅ Real-time update timer started with 300ms interval for fast response");
    } else {
        ESP_LOGW(TAG, "⚠️ Failed to start update timer, using manual mode");
    }
    
    // 快速显示默认值，然后异步更新
    updateDisplayValuesQuick();
    
    // 立即触发一次数据更新来测试通信
    ESP_LOGI(TAG, "Starting async display update");
    updateDisplayValuesAsync();
    
    ESP_LOGI(TAG, "PowerController started successfully");
    return true;
}

/**
 * @brief 返回按钮处理函数
 * @details 当用户点击返回按钮时调用此函数
 * @return true 处理成功，false 处理失败
 */
bool PowerController::back(void)
{
    ESP_LOGI(TAG, "PowerController back");
    
    // 返回notifyCoreClosed()的结果，让框架正确处理
    return notifyCoreClosed();
}

/**
 * @brief 关闭应用函数
 * @details 当应用需要关闭时调用此函数
 * @return true 关闭成功，false 关闭失败
 */
bool PowerController::close(void)
{
    ESP_LOGI(TAG, "Closing PowerController");
    
    // 快速停止定时器和更新任务
    is_running = false;
    if (update_timer != nullptr) {
        xTimerStop(update_timer, pdMS_TO_TICKS(100)); // 减少等待时间
    }
    
    // 通知更新任务停止
    if (update_task_handle) {
        xTaskNotifyGive(update_task_handle);
        // 等待任务自行删除，然后清理句柄
        vTaskDelay(pdMS_TO_TICKS(50)); // 给任务时间退出
        update_task_handle = nullptr; // 清空句柄，下次进入时重新创建
        ESP_LOGI(TAG, "Update task handle cleared for next run");
    }
    
    ESP_LOGI(TAG, "PowerController closed successfully");
    
    return true;
}

/**
 * @brief 恢复应用
 * @details 从暂停状态恢复应用
 * @return true 恢复成功，false 恢复失败
 */
bool PowerController::resume(void)
{
    ESP_LOGI(TAG, "Resuming PowerController");
    
    // 快速恢复
    is_running = true;
    if (update_timer != nullptr) {
        xTimerStart(update_timer, pdMS_TO_TICKS(100)); // 快速启动
        
        // 通知更新任务开始工作
        if (update_task_handle) {
            xTaskNotifyGive(update_task_handle);
        }
        
        // 异步更新避免阻塞
        updateDisplayValuesQuick();
    }
    
    return true;
}

/**
 * @brief 暂停应用
 * @details 暂停应用运行，保存状态
 * @return true 暂停成功，false 暂停失败
 */
bool PowerController::pause(void)
{
    ESP_LOGI(TAG, "Pausing PowerController");
    
    // 停止定时器和更新任务
    is_running = false;
    if (update_timer != nullptr) {
        xTimerStop(update_timer, pdMS_TO_TICKS(1000));
    }
    
    // 通知更新任务停止
    if (update_task_handle) {
        xTaskNotifyGive(update_task_handle);
    }
    
    return true;
}

// ==================== 私有方法实现 ====================

void PowerController::setupUIEvents(void)
{
    // 获取UI元素并设置事件处理
    extern lv_obj_t * ui_Button3V33A;
    extern lv_obj_t * ui_Button3V35A; 
    extern lv_obj_t * ui_Button5V3A;
    extern lv_obj_t * ui_Button5V5A;
    extern lv_obj_t * ui_Button12V3A;
    extern lv_obj_t * ui_Button12V5A;
    extern lv_obj_t * ui_ButtonADJApply;
    extern lv_obj_t * ui_SwitchPower;
    extern lv_obj_t * ui_SwitchBeep;
    extern lv_obj_t * ui_SwitchKeyLock;
    extern lv_obj_t * ui_SwitchSleep;
    
    // 设置预设按钮事件
    if (ui_Button3V33A) {
        lv_obj_set_user_data(ui_Button3V33A, (void*)0);
        lv_obj_add_event_cb(ui_Button3V33A, onPresetButtonClick, LV_EVENT_CLICKED, this);
    }
    if (ui_Button3V35A) {
        lv_obj_set_user_data(ui_Button3V35A, (void*)1);
        lv_obj_add_event_cb(ui_Button3V35A, onPresetButtonClick, LV_EVENT_CLICKED, this);
    }
    if (ui_Button5V3A) {
        lv_obj_set_user_data(ui_Button5V3A, (void*)2);
        lv_obj_add_event_cb(ui_Button5V3A, onPresetButtonClick, LV_EVENT_CLICKED, this);
    }
    if (ui_Button5V5A) {
        lv_obj_set_user_data(ui_Button5V5A, (void*)3);
        lv_obj_add_event_cb(ui_Button5V5A, onPresetButtonClick, LV_EVENT_CLICKED, this);
    }
    if (ui_Button12V3A) {
        lv_obj_set_user_data(ui_Button12V3A, (void*)4);
        lv_obj_add_event_cb(ui_Button12V3A, onPresetButtonClick, LV_EVENT_CLICKED, this);
    }
    if (ui_Button12V5A) {
        lv_obj_set_user_data(ui_Button12V5A, (void*)5);
        lv_obj_add_event_cb(ui_Button12V5A, onPresetButtonClick, LV_EVENT_CLICKED, this);
    }
    
    // 设置应用按钮事件
    if (ui_ButtonADJApply) {
        lv_obj_add_event_cb(ui_ButtonADJApply, onApplyButtonClick, LV_EVENT_CLICKED, this);
    }
    
    // 设置开关事件
    if (ui_SwitchPower) {
        lv_obj_add_event_cb(ui_SwitchPower, onSwitchChanged, LV_EVENT_VALUE_CHANGED, this);
    }
    if (ui_SwitchBeep) {
        lv_obj_add_event_cb(ui_SwitchBeep, onSwitchChanged, LV_EVENT_VALUE_CHANGED, this);
    }
    if (ui_SwitchKeyLock) {
        lv_obj_add_event_cb(ui_SwitchKeyLock, onSwitchChanged, LV_EVENT_VALUE_CHANGED, this);
    }
    if (ui_SwitchSleep) {
        lv_obj_add_event_cb(ui_SwitchSleep, onSwitchChanged, LV_EVENT_VALUE_CHANGED, this);
    }
    
    ESP_LOGI(TAG, "UI events setup completed");
}

// 快速显示默认值，提升启动速度
void PowerController::updateDisplayValuesQuick(void)
{
    if (!is_running) {
        return;
    }
    
    // 获取UI标签
    extern lv_obj_t * ui_LabelVoltageValue;
    extern lv_obj_t * ui_LabelCurrentValue; 
    extern lv_obj_t * ui_LabelPowerValue;
    extern lv_obj_t * ui_LabelVoltageSetValue;
    extern lv_obj_t * ui_LabelCurrentSetValue;
    extern lv_obj_t * ui_LabelVoltageInputValue;
    
    // 快速显示默认值，不等待Modbus通信
    if (ui_LabelVoltageValue) lv_label_set_text(ui_LabelVoltageValue, "0.00");
    if (ui_LabelCurrentValue) lv_label_set_text(ui_LabelCurrentValue, "0.000");
    if (ui_LabelPowerValue) lv_label_set_text(ui_LabelPowerValue, "0.00");
    if (ui_LabelVoltageSetValue) lv_label_set_text(ui_LabelVoltageSetValue, "0.00");
    if (ui_LabelCurrentSetValue) lv_label_set_text(ui_LabelCurrentSetValue, "0.000");
    if (ui_LabelVoltageInputValue) lv_label_set_text(ui_LabelVoltageInputValue, "0.00");
    
    ESP_LOGI(TAG, "Quick display values initialized");
    
    // 异步启动实际数据更新
    xTaskCreate([](void* param) {
        PowerController* controller = (PowerController*)param;
        vTaskDelay(pdMS_TO_TICKS(100)); // 短暂延迟让UI稳定
        controller->updateDisplayValuesAsync();
        vTaskDelete(NULL);
    }, "AsyncUpdate", 4096, this, 5, NULL);
}

// 异步更新显示值，避免阻塞UI
void PowerController::updateDisplayValuesAsync(void)
{
    if (!is_running || !modbus_controller) {
        return;
    }
    
    ESP_LOGI(TAG, "Starting async display update");
    
    // 读取设备数据，使用短超时
    if (modbus_controller->readAllDeviceData()) {
        updateDisplayValues();
        ESP_LOGI(TAG, "✅ Async display update completed successfully");
    } else {
        ESP_LOGW(TAG, "⚠️ Async display update failed, will retry in next cycle");
    }
}

void PowerController::updateDisplayValues(void)
{
    // 增加安全检查，避免在定时器中栈溢出
    if (!is_running || !modbus_controller) {
        return;
    }
    
    // 添加额外的互斥锁保护，防止并发访问
    static bool updating = false;
    if (updating) {
        ESP_LOGD(TAG, "Update already in progress, skipping");
        return;
    }
    updating = true;
    
    // 读取XY6506S实时数据 - 使用超时保护
    ESP_LOGD(TAG, "Reading device data...");
    if (!modbus_controller->readAllDeviceData()) {
        ESP_LOGW(TAG, "Failed to read device data");
        updating = false;
        return;
    }
    
    const PowerDeviceData& data = modbus_controller->getDeviceData();
    if (!data.data_valid) {
        ESP_LOGW(TAG, "Device data is not valid");
        updating = false;
        return;
    }
    
    // 获取UI标签
    extern lv_obj_t * ui_LabelVoltageValue;
    extern lv_obj_t * ui_LabelCurrentValue; 
    extern lv_obj_t * ui_LabelPowerValue;
    extern lv_obj_t * ui_LabelVoltageSetValue;
    extern lv_obj_t * ui_LabelCurrentSetValue;
    extern lv_obj_t * ui_LabelVoltageInputValue;
    
    char text_buffer[32];
    
    // 更新输出电压
    if (ui_LabelVoltageValue) {
        snprintf(text_buffer, sizeof(text_buffer), "%.2f", data.output_voltage);
        lv_label_set_text(ui_LabelVoltageValue, text_buffer);
    }
    
    // 更新输出电流
    if (ui_LabelCurrentValue) {
        snprintf(text_buffer, sizeof(text_buffer), "%.3f", data.output_current);
        lv_label_set_text(ui_LabelCurrentValue, text_buffer);
    }
    
    // 更新输出功率
    if (ui_LabelPowerValue) {
        snprintf(text_buffer, sizeof(text_buffer), "%.2f", data.output_power);
        lv_label_set_text(ui_LabelPowerValue, text_buffer);
    }
    
    // 更新设定电压
    if (ui_LabelVoltageSetValue) {
        snprintf(text_buffer, sizeof(text_buffer), "%.2f", data.set_voltage);
        lv_label_set_text(ui_LabelVoltageSetValue, text_buffer);
    }
    
    // 更新设定电流
    if (ui_LabelCurrentSetValue) {
        snprintf(text_buffer, sizeof(text_buffer), "%.3f", data.set_current);
        lv_label_set_text(ui_LabelCurrentSetValue, text_buffer);
    }
    
    // 更新输入电压
    if (ui_LabelVoltageInputValue) {
        snprintf(text_buffer, sizeof(text_buffer), "%.2f", data.input_voltage);
        lv_label_set_text(ui_LabelVoltageInputValue, text_buffer);
    }
    
    // 重置更新标志
    updating = false;
    ESP_LOGD(TAG, "Display values updated successfully");
}

void PowerController::updateSwitchStates(void)
{
    if (!modbus_controller || !is_running) {
        return;
    }
    
    const PowerDeviceData& data = modbus_controller->getDeviceData();
    if (!data.data_valid) {
        return;
    }
    
    // 添加调试日志显示从机状态
    ESP_LOGI(TAG, "🔄 Updating switch states - Power:%s, Beep:%s, KeyLock:%s, Sleep:%s", 
             data.output_switch ? "ON" : "OFF",
             data.beep_switch ? "ON" : "OFF", 
             data.key_lock ? "LOCKED" : "UNLOCKED",
             data.sleep_mode ? "ON" : "OFF");
    
    // 获取UI开关
    extern lv_obj_t * ui_SwitchPower;
    extern lv_obj_t * ui_SwitchBeep;
    extern lv_obj_t * ui_SwitchKeyLock;
    extern lv_obj_t * ui_SwitchSleep;
    
    // 更新开关状态
    if (ui_SwitchPower) {
        if (data.output_switch) {
            lv_obj_add_state(ui_SwitchPower, LV_STATE_CHECKED);
        } else {
            lv_obj_clear_state(ui_SwitchPower, LV_STATE_CHECKED);
        }
    }
    
    if (ui_SwitchBeep) {
        if (data.beep_switch) {
            lv_obj_add_state(ui_SwitchBeep, LV_STATE_CHECKED);
        } else {
            lv_obj_clear_state(ui_SwitchBeep, LV_STATE_CHECKED);
        }
    }
    
    if (ui_SwitchKeyLock) {
        if (data.key_lock) {
            lv_obj_add_state(ui_SwitchKeyLock, LV_STATE_CHECKED);
        } else {
            lv_obj_clear_state(ui_SwitchKeyLock, LV_STATE_CHECKED);
        }
    }
    
    if (ui_SwitchSleep) {
        if (data.sleep_mode) {
            lv_obj_add_state(ui_SwitchSleep, LV_STATE_CHECKED);
        } else {
            lv_obj_clear_state(ui_SwitchSleep, LV_STATE_CHECKED);
        }
    }
}

bool PowerController::applyVoltageCurrentSettings(void)
{
    if (!modbus_controller) {
        ESP_LOGE(TAG, "ModbusController not available");
        return false;
    }
    
    // 获取输入框
    extern lv_obj_t * ui_TextAreaADJVoltage;
    extern lv_obj_t * ui_TextAreaADJCurrent;
    extern lv_obj_t * ui_LabelVoltageSetValue;
    extern lv_obj_t * ui_LabelCurrentSetValue;
    
    if (!ui_TextAreaADJVoltage || !ui_TextAreaADJCurrent) {
        ESP_LOGE(TAG, "UI text areas not found");
        return false;
    }
    
    // 获取输入值
    const char* voltage_str = lv_textarea_get_text(ui_TextAreaADJVoltage);
    const char* current_str = lv_textarea_get_text(ui_TextAreaADJCurrent);
    
    if (!voltage_str || !current_str || strlen(voltage_str) == 0 || strlen(current_str) == 0) {
        ESP_LOGW(TAG, "Empty voltage or current input");
        return false;
    }
    
    // 转换为数值
    float voltage = atof(voltage_str);
    float current = atof(current_str);
    
    // 使用Modbus验证输入值
    if (!modbus_controller->validateVoltage(voltage)) {
        ESP_LOGE(TAG, "Invalid voltage: %.2fV (max: %.2fV)", voltage, 
                 modbus_controller->getDeviceData().input_voltage);
        return false;
    }
    
    if (!modbus_controller->validateCurrent(current)) {
        ESP_LOGE(TAG, "Invalid current: %.3fA (max: 6.0A)", current);
        return false;
    }
    
    // 发送设置命令到XY6506S
    ESP_LOGI(TAG, "Applying settings to XY6506S: %.2fV/%.3fA", voltage, current);
    if (!modbus_controller->setVoltageAndCurrent(voltage, current)) {
        ESP_LOGE(TAG, "Failed to set voltage/current on device");
        return false;
    }
    
    // 更新UI显示
    char buffer[16];
    if (ui_LabelVoltageSetValue) {
        snprintf(buffer, sizeof(buffer), "%.2f", voltage);
        lv_label_set_text(ui_LabelVoltageSetValue, buffer);
    }
    if (ui_LabelCurrentSetValue) {
        snprintf(buffer, sizeof(buffer), "%.3f", current);
        lv_label_set_text(ui_LabelCurrentSetValue, buffer);
    }
    
    ESP_LOGI(TAG, "Successfully applied settings: %.2fV/%.3fA", voltage, current);
    return true;
    
    /*
    // 验证输入值
    if (!modbus_controller->validateVoltage(voltage)) {
        ESP_LOGE(TAG, "Invalid voltage: %.2fV (max: %.2fV)", voltage, 
                 modbus_controller->getDeviceData().input_voltage);
        return false;
    }
    
    if (!modbus_controller->validateCurrent(current)) {
        ESP_LOGE(TAG, "Invalid current: %.3fA (max: 9.1A)", current);
        return false;
    }
    
    // 设置电压电流
    if (modbus_controller->setVoltageAndCurrent(voltage, current)) {
        ESP_LOGI(TAG, "Successfully set voltage: %.2fV, current: %.3fA", voltage, current);
        
        // 清空输入框
        lv_textarea_set_text(ui_TextAreaADJVoltage, "");
        lv_textarea_set_text(ui_TextAreaADJCurrent, "");
        
        return true;
    } else {
        ESP_LOGE(TAG, "Failed to set voltage/current");
        return false;
    }
    */
}

// ==================== 静态回调函数 ====================

void PowerController::updateTimerCallback(TimerHandle_t timer)
{
    PowerController* controller = (PowerController*)pvTimerGetTimerID(timer);
    if (controller && controller->is_running && controller->update_task_handle) {
        // 通知持久更新任务进行更新
        ESP_LOGD("PowerController", "🔔 Timer callback triggered - notifying update task");
        controller->update_requested = true;
        xTaskNotifyGive(controller->update_task_handle);
    } else {
        ESP_LOGW("PowerController", "⚠️ Timer callback skipped - controller=%p, running=%d, task=%p", 
                controller, controller ? controller->is_running : 0, 
                controller ? controller->update_task_handle : nullptr);
    }
}

void PowerController::onPresetButtonClick(lv_event_t* e)
{
    PowerController* controller = (PowerController*)lv_event_get_user_data(e);
    lv_obj_t* obj = lv_event_get_target(e);
    
    if (!controller) {
        return;
    }
    
    // 获取预设值索引
    int preset_index = (int)(uintptr_t)lv_obj_get_user_data(obj);
    
    if (preset_index >= 0 && preset_index < PRESET_COUNT) {
        const PresetValue& preset = PRESET_VALUES[preset_index];
        
        // 安全模式：直接更新UI显示，不使用Modbus
        ESP_LOGI(TAG, "Preset %d clicked: %.1fV/%.1fA (SAFE MODE)", preset_index, preset.voltage, preset.current);
        
        // 更新UI显示的设定值
        extern lv_obj_t * ui_LabelVoltageSetValue;
        extern lv_obj_t * ui_LabelCurrentSetValue;
        extern lv_obj_t * ui_TextAreaADJVoltage;
        extern lv_obj_t * ui_TextAreaADJCurrent;
        
        char buffer[16];
        if (ui_LabelVoltageSetValue) {
            snprintf(buffer, sizeof(buffer), "%.1f", preset.voltage);
            lv_label_set_text(ui_LabelVoltageSetValue, buffer);
        }
        if (ui_LabelCurrentSetValue) {
            snprintf(buffer, sizeof(buffer), "%.1f", preset.current);
            lv_label_set_text(ui_LabelCurrentSetValue, buffer);
        }
        if (ui_TextAreaADJVoltage) {
            snprintf(buffer, sizeof(buffer), "%.1f", preset.voltage);
            lv_textarea_set_text(ui_TextAreaADJVoltage, buffer);
        }
        if (ui_TextAreaADJCurrent) {
            snprintf(buffer, sizeof(buffer), "%.1f", preset.current);
            lv_textarea_set_text(ui_TextAreaADJCurrent, buffer);
        }
    }
}

void PowerController::onApplyButtonClick(lv_event_t* e)
{
    PowerController* controller = (PowerController*)lv_event_get_user_data(e);
    
    if (controller) {
        // 恢复正常应用功能 - 应用电压电流设置
        ESP_LOGI(TAG, "应用按钮被点击，开始应用电压电流设置...");
        controller->applyVoltageCurrentSettings();
    }
}

void PowerController::onSwitchChanged(lv_event_t* e)
{
    PowerController* controller = (PowerController*)lv_event_get_user_data(e);
    lv_obj_t* obj = lv_event_get_target(e);
    
    if (!controller) {
        return;
    }
    
    bool is_checked = lv_obj_has_state(obj, LV_STATE_CHECKED);
    
    // 获取UI开关
    extern lv_obj_t * ui_SwitchPower;
    extern lv_obj_t * ui_SwitchBeep;
    extern lv_obj_t * ui_SwitchKeyLock;
    extern lv_obj_t * ui_SwitchSleep;
    
    // 发送真实的Modbus开关控制命令到XY6506S
    if (controller && controller->modbus_controller) {
        if (obj == ui_SwitchPower) {
            ESP_LOGI(TAG, "Setting output switch: %s", is_checked ? "ON" : "OFF");
            controller->modbus_controller->setOutputSwitch(is_checked);
        }
        else if (obj == ui_SwitchBeep) {
            ESP_LOGI(TAG, "Setting beep switch: %s", is_checked ? "ON" : "OFF");
            controller->modbus_controller->setBeepSwitch(is_checked);
        }
        else if (obj == ui_SwitchKeyLock) {
            ESP_LOGI(TAG, "Setting key lock: %s", is_checked ? "LOCKED" : "UNLOCKED");
            controller->modbus_controller->setKeyLock(is_checked);
        }
        else if (obj == ui_SwitchSleep) {
            ESP_LOGI(TAG, "Setting sleep mode: %s", is_checked ? "SLEEP" : "NORMAL");
            controller->modbus_controller->setSleepMode(is_checked);
        }
    }
}

void PowerController::runModbusDiagnostic() {
    ESP_LOGI(TAG, "启动Modbus诊断模式...");
    
    // 延迟一下让系统稳定
    vTaskDelay(pdMS_TO_TICKS(1000));
    
    if (modbus_controller) {
        ESP_LOGI(TAG, "使用现有的ModbusController进行诊断测试...");
        
        // 直接进行简单的通信测试
        ESP_LOGI(TAG, "=== 直接Modbus通信测试 ===");
        ESP_LOGI(TAG, "尝试读取输出电压显示值 (地址 0x01, 寄存器 0x0002)");
        
        uint16_t voltage_data;
        bool success = modbus_controller->readHoldingRegisters(0x0002, 1, &voltage_data);
        
        if (success) {
            ESP_LOGI(TAG, "✅ 读取成功! 寄存器值: 0x%04X (%d)", voltage_data, voltage_data);
            float voltage = voltage_data / 100.0f;
            ESP_LOGI(TAG, "   转换后电压: %.2fV", voltage);
            
            // 继续测试其他寄存器
            ESP_LOGI(TAG, "继续测试其他寄存器...");
            
            uint16_t current_data, power_data, input_data;
            if (modbus_controller->readHoldingRegisters(0x0003, 1, &current_data)) {
                ESP_LOGI(TAG, "✅ 输出电流: %.3fA", current_data / 1000.0f);
            }
            if (modbus_controller->readHoldingRegisters(0x0004, 1, &power_data)) {
                ESP_LOGI(TAG, "✅ 输出功率: %.2fW", power_data / 100.0f);
            }
            if (modbus_controller->readHoldingRegisters(0x0005, 1, &input_data)) {
                ESP_LOGI(TAG, "✅ 输入电压: %.2fV", input_data / 100.0f);
            }
        } else {
            ESP_LOGE(TAG, "❌ 读取失败");
            ESP_LOGI(TAG, "检查事项:");
            ESP_LOGI(TAG, "1. 连线: TX(GPIO51) -> XY6506S RX, RX(GPIO52) -> XY6506S TX");
            ESP_LOGI(TAG, "2. XY6506S电源设置: Modbus地址=1, 波特率=115200, 8N1");
            ESP_LOGI(TAG, "3. 确认XY6506S处于Modbus-RTU模式");
            ESP_LOGI(TAG, "4. 检查地线连接和信号电平");
            ESP_LOGI(TAG, "5. ESP32P4现已配置为115200波特率（XY6506S出厂默认）");
        }
    } else {
        ESP_LOGE(TAG, "ModbusController未初始化");
    }
    
    ESP_LOGI(TAG, "Modbus诊断完成");
}

// ==================== 持久更新任务 ====================

void PowerController::updateTask(void* parameter)
{
    PowerController* controller = (PowerController*)parameter;
    if (!controller) {
        ESP_LOGE(TAG, "Invalid controller in update task");
        vTaskDelete(NULL);
        return;
    }
    
    ESP_LOGI(TAG, "Update task started, waiting for notifications...");
    
    while (controller->is_running) {
        // 等待定时器通知
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        
        ESP_LOGD(TAG, "📨 Update task received notification - running=%d, update_requested=%d, modbus=%p", 
                controller->is_running, controller->update_requested, controller->modbus_controller);
        
        // 检查是否还在运行且有更新请求
        if (!controller->is_running || !controller->update_requested) {
            ESP_LOGD(TAG, "⚠️ Update task skipping - running=%d, update_requested=%d", 
                    controller->is_running, controller->update_requested);
            continue;
        }
        
        controller->update_requested = false;
        
        // 执行实际的更新操作
        ESP_LOGD(TAG, "🔄 Executing scheduled update...");
        
        if (controller->modbus_controller) {
            ESP_LOGD(TAG, "📡 Reading device data from XY6506S...");
            controller->updateDisplayValuesAsync();
            controller->updateSwitchStates();
            ESP_LOGD(TAG, "✅ Device data update completed");
        } else {
            ESP_LOGW(TAG, "⚠️ No Modbus controller available for update");
        }
    }
    
    ESP_LOGI(TAG, "Update task ending");
    vTaskDelete(NULL);
}