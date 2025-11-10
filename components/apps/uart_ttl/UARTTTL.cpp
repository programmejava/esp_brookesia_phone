#include "UARTTTL.hpp"
#include "esp_log.h"
#include "ui/ui.h"
#include "assets/img_app_uart_ttl.h"
#include <string.h>
#include <stdio.h>

// 常量定义
#define MAX_UI_UPDATE_LEN 1024          // UI单次更新最大长度
#define TEXT_AREA_MAX_LEN 4096          // 文本区域最大长度
#define HEARTBEAT_INTERVAL_MS 2000      // 心跳包发送间隔（毫秒）

static const char *TAG = "AppUARTTTL";
static const char* NVS_NAMESPACE = "uart_ttl_app";

// 声明 UART TTL 应用图标
LV_IMG_DECLARE(img_app_uart_ttl);

// 串口参数选项数组，必须与SquareLine中Dropdown的选项顺序严格一致
const int baudrate_options[] = { 
    4800, 9600, 19200, 38400, 57600, 115200, 230400, 460800, 1500000 
};
const uart_word_length_t databits_options[] = { 
    UART_DATA_5_BITS, UART_DATA_6_BITS, UART_DATA_7_BITS, UART_DATA_8_BITS 
};
const uart_parity_t parity_options[] = { 
    UART_PARITY_DISABLE, UART_PARITY_EVEN, UART_PARITY_ODD 
};
const uart_stop_bits_t stopbits_options[] = { 
    UART_STOP_BITS_1, UART_STOP_BITS_1_5, UART_STOP_BITS_2 
};

UARTTTL::UARTTTL() :
    ESP_Brookesia_PhoneApp("UART TTL", &img_app_uart_ttl, true),
    _update_timer(nullptr),
    _text_area_ttl(nullptr),
    _last_tx_timestamp(0),
    _current_text_len(0),
    _heartbeat_enabled(true),  // 默认开启心跳包功能
    _heartbeat_counter(0)      // 心跳包计数器初始化为0
{
}

UARTTTL::~UARTTTL()
{
    if (_nvs_handle != 0) {
        nvs_close(_nvs_handle);
    }
}

bool UARTTTL::init(void)
{
    // 打开NVS存储
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &_nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error (%s) opening NVS handle!", esp_err_to_name(err));
        return false;
    }
    
    // 加载保存的设置
    loadSettings();
    
    // 初始化UART服务
    _uart_service.begin(_current_config);
    
    ESP_LOGI(TAG, "UART TTL application initialized successfully");
    return true;
}

void UARTTTL::loadSettings()
{
    // 设置默认配置
    _current_config.baud_rate = 115200;
    _current_config.data_bits = UART_DATA_8_BITS;
    _current_config.parity = UART_PARITY_DISABLE;
    _current_config.stop_bits = UART_STOP_BITS_1;
    _heartbeat_enabled = true;
    
    // 从NVS读取保存的配置
    uint32_t temp_val;
    if(nvs_get_u32(_nvs_handle, "uart_baud", &temp_val) == ESP_OK) {
        _current_config.baud_rate = temp_val;
    }
    if(nvs_get_u32(_nvs_handle, "uart_data", &temp_val) == ESP_OK) {
        _current_config.data_bits = (uart_word_length_t)temp_val;
    }
    if(nvs_get_u32(_nvs_handle, "uart_par", &temp_val) == ESP_OK) {
        _current_config.parity = (uart_parity_t)temp_val;
    }
    if(nvs_get_u32(_nvs_handle, "uart_stop", &temp_val) == ESP_OK) {
        _current_config.stop_bits = (uart_stop_bits_t)temp_val;
    }
    if(nvs_get_u32(_nvs_handle, "heartbeat_en", &temp_val) == ESP_OK) {
        _heartbeat_enabled = (temp_val != 0);
    }
    
    ESP_LOGI(TAG, "Settings loaded: baud=%d, data=%d, parity=%d, stop=%d, heartbeat=%s",
             _current_config.baud_rate, _current_config.data_bits, 
             _current_config.parity, _current_config.stop_bits,
             _heartbeat_enabled ? "enabled" : "disabled");
}

void UARTTTL::saveSettings()
{
    ESP_LOGI(TAG, "Saving settings: baud=%d, data=%d, parity=%d, stop=%d, heartbeat=%s",
             _current_config.baud_rate, _current_config.data_bits, 
             _current_config.parity, _current_config.stop_bits,
             _heartbeat_enabled ? "enabled" : "disabled");
             
    // 保存串口配置参数到NVS
    nvs_set_u32(_nvs_handle, "uart_baud", _current_config.baud_rate);
    nvs_set_u32(_nvs_handle, "uart_data", _current_config.data_bits);
    nvs_set_u32(_nvs_handle, "uart_par", _current_config.parity);
    nvs_set_u32(_nvs_handle, "uart_stop", _current_config.stop_bits);
    nvs_set_u32(_nvs_handle, "heartbeat_en", _heartbeat_enabled ? 1 : 0);
    
    // 提交更改到NVS
    esp_err_t err = nvs_commit(_nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to commit settings to NVS: %s", esp_err_to_name(err));
    }
}

bool UARTTTL::run(void)
{
    // 初始化UI界面
    uart_ttl_ui_init();
    extraUiInit();
    
    // 创建UI更新定时器（30ms刷新间隔）
    _update_timer = lv_timer_create(uiUpdateTimerCb, 30, this);
    lv_timer_pause(_update_timer);  // 初始状态暂停

    // 显示欢迎信息
    const char* welcome_msg = "Welcome! Click START to begin.\r\n";
    lv_textarea_set_text(_text_area_ttl, welcome_msg);
    _current_text_len = strlen(welcome_msg);
    
    ESP_LOGI(TAG, "UART TTL application started");
    return true;
}

bool UARTTTL::back(void)
{
    // 检查当前屏幕，如果在设置页面则返回主页面
    lv_obj_t* current_screen = lv_scr_act();
    if (current_screen == ui_ScreenSettings) {
        lv_scr_load(ui_ScreenTTL);
        return false;  // 不关闭应用，只是切换屏幕
    }
    
    // 在主屏幕按返回键则关闭应用
    return notifyCoreClosed();
}

bool UARTTTL::close(void)
{
    ESP_LOGI(TAG, "Closing application, stopping UART service");
    
    // 清理定时器
    if (_update_timer) {
        lv_timer_del(_update_timer);
        _update_timer = nullptr;
    }
    
    // 停止UART接收
    _uart_service.stopReceiving();
    
    // 清空UI引用
    _text_area_ttl = nullptr;
    
    return true;
}

bool UARTTTL::resume(void)
{
    ESP_LOGI(TAG, "Resuming application, resetting UI state");
    
    // 重置按钮状态为初始状态（START可用，STOP禁用）
    lv_obj_clear_state(ui_ButtonTTLStart, LV_STATE_DISABLED);
    lv_obj_add_state(ui_ButtonTTLStop, LV_STATE_DISABLED);
    
    return true;
}

void UARTTTL::extraUiInit(void)
{
    // 获取UI控件指针
    lv_obj_t* btn_start = ui_ButtonTTLStart;
    lv_obj_t* btn_stop = ui_ButtonTTLStop;
    lv_obj_t* btn_setting = ui_ButtonTTLSetting;
    lv_obj_t* btn_exit = ui_ButtonTTLExit;
    lv_obj_t* switch_heartbeat = ui_SwitchTTL1;
    _text_area_ttl = ui_TextAreaTTL;
    
    // 绑定按钮事件回调函数
    lv_obj_add_event_cb(btn_start, onButtonStartClicked, LV_EVENT_CLICKED, this);
    lv_obj_add_event_cb(btn_stop, onButtonStopClicked, LV_EVENT_CLICKED, this);
    lv_obj_add_event_cb(btn_setting, onButtonSettingsClicked, LV_EVENT_CLICKED, this);
    lv_obj_add_event_cb(btn_exit, onButtonExitClicked, LV_EVENT_CLICKED, this);
    lv_obj_add_event_cb(switch_heartbeat, onSwitchHeartbeatToggled, LV_EVENT_VALUE_CHANGED, this);

    // 设置心跳开关的初始状态
    if (_heartbeat_enabled) {
        lv_obj_add_state(switch_heartbeat, LV_STATE_CHECKED);
    } else {
        lv_obj_clear_state(switch_heartbeat, LV_STATE_CHECKED);
    }

    // 设置初始按钮状态
    lv_obj_add_state(btn_stop, LV_STATE_DISABLED);  // STOP按钮初始禁用

    // 初始化设置屏幕事件
    setupSettingsScreenEvents();
}

void UARTTTL::setupSettingsScreenEvents()
{
    // 绑定设置屏幕相关事件
    lv_obj_add_event_cb(ui_ScreenSettings, onScreenSettingsLoaded, LV_EVENT_SCREEN_LOADED, this);
    lv_obj_add_event_cb(ui_ButtonTTLSettingsApply, onButtonSettingsApplyClicked, LV_EVENT_CLICKED, this);
    lv_obj_add_event_cb(ui_ButtonTTLSettingsBack, onButtonSettingsBackClicked, LV_EVENT_CLICKED, this);
}

void UARTTTL::uiUpdateTimerCb(lv_timer_t *timer)
{
    UARTTTL* app = static_cast<UARTTTL*>(timer->user_data);
    if (!app || !app->_text_area_ttl) { 
        return; 
    }

    // 处理接收到的UART数据
    if (app->_uart_service.available() > 0)
    {
        char local_buf[MAX_UI_UPDATE_LEN + 1];
        size_t total_read_len = 0;
        
        // 批量读取数据，避免频繁的小数据包读取
        while(app->_uart_service.available() > 0 && total_read_len < MAX_UI_UPDATE_LEN) {
            size_t len = app->_uart_service.read((uint8_t*)local_buf + total_read_len, 
                                               MAX_UI_UPDATE_LEN - total_read_len);
            if(len > 0) {
                total_read_len += len;
            } else {
                break;
            }
        }
        
        if (total_read_len > 0) {
            // 检查文本区域长度是否超限，超限则清空
            if (app->_current_text_len + total_read_len > TEXT_AREA_MAX_LEN) {
                const char* clear_msg = "[System] Log cleared due to size limit.\r\n";
                lv_textarea_set_text(app->_text_area_ttl, clear_msg);
                app->_current_text_len = strlen(clear_msg);
            }
            
            // 添加接收到的数据到文本区域
            local_buf[total_read_len] = '\0';
            lv_textarea_add_text(app->_text_area_ttl, local_buf);
            app->_current_text_len += total_read_len;
        }
    }
    
    // 处理心跳包发送（仅在开启心跳功能时）
    if (app->_heartbeat_enabled && 
        lv_tick_elaps(app->_last_tx_timestamp) >= HEARTBEAT_INTERVAL_MS) {
        
        // 增加心跳包计数器
        app->_heartbeat_counter++;
        
        // 获取系统运行时间（毫秒）
        uint32_t system_time_ms = lv_tick_get();
        uint32_t system_time_sec = system_time_ms / 1000;
        uint32_t system_time_min = system_time_sec / 60;
        uint32_t system_time_hour = system_time_min / 60;
        
        // 格式化心跳包消息：包含序号和系统运行时间
        char heartbeat_msg[128];
        snprintf(heartbeat_msg, sizeof(heartbeat_msg), 
                "Heartbeat #%u [%02u:%02u:%02u.%03u]\r\n",
                (unsigned int)app->_heartbeat_counter,
                (unsigned int)(system_time_hour % 24),
                (unsigned int)(system_time_min % 60), 
                (unsigned int)(system_time_sec % 60),
                (unsigned int)(system_time_ms % 1000));
        
        // 发送心跳包
        app->_uart_service.write((const uint8_t*)heartbeat_msg, strlen(heartbeat_msg));
        app->_last_tx_timestamp = lv_tick_get();
    }
}

void UARTTTL::onButtonStartClicked(lv_event_t* e)
{
    UARTTTL* app = static_cast<UARTTTL*>(lv_event_get_user_data(e));
    
    // 启动UART接收服务
    app->_uart_service.startReceiving();
    
    // 恢复UI更新定时器
    lv_timer_resume(app->_update_timer);
    
    // 重置心跳包时间戳和计数器
    app->_last_tx_timestamp = 0;
    app->_heartbeat_counter = 0;  // 重置心跳包计数器

    // 添加系统消息
    const char* msg = "\r\n[System] Service started.\r\n";
    lv_textarea_add_text(app->_text_area_ttl, msg);
    app->_current_text_len += strlen(msg);

    // 更新按钮状态
    lv_obj_add_state(ui_ButtonTTLStart, LV_STATE_DISABLED);
    lv_obj_clear_state(ui_ButtonTTLStop, LV_STATE_DISABLED);
    
    ESP_LOGI(TAG, "UART service started");
}

void UARTTTL::onButtonStopClicked(lv_event_t *e)
{
    UARTTTL* app = static_cast<UARTTTL*>(lv_event_get_user_data(e));
    
    // 停止UART接收服务
    app->_uart_service.stopReceiving();
    
    // 暂停UI更新定时器
    lv_timer_pause(app->_update_timer);

    // 添加系统消息
    const char* msg = "\r\n[System] Service stopped.\r\n";
    lv_textarea_add_text(app->_text_area_ttl, msg);
    app->_current_text_len += strlen(msg);

    // 更新按钮状态
    lv_obj_clear_state(ui_ButtonTTLStart, LV_STATE_DISABLED);
    lv_obj_add_state(ui_ButtonTTLStop, LV_STATE_DISABLED);
    
    ESP_LOGI(TAG, "UART service stopped");
}

void UARTTTL::onButtonSettingsClicked(lv_event_t *e)
{
    ESP_LOGD(TAG, "Settings button clicked, switching to settings screen");
    lv_scr_load(ui_ScreenSettings);
}

void UARTTTL::onButtonExitClicked(lv_event_t *e)
{
    UARTTTL* app = static_cast<UARTTTL*>(lv_event_get_user_data(e));
    if (app) {
        ESP_LOGI(TAG, "Exit button clicked, closing application");
        app->notifyCoreClosed();
    }
}

void UARTTTL::onSwitchHeartbeatToggled(lv_event_t *e)
{
    UARTTTL* app = static_cast<UARTTTL*>(lv_event_get_user_data(e));
    lv_obj_t* switch_obj = lv_event_get_target(e);
    
    // 获取开关状态并更新内部变量
    app->_heartbeat_enabled = lv_obj_has_state(switch_obj, LV_STATE_CHECKED);
    
    // 保存设置到NVS
    app->saveSettings();
    
    ESP_LOGI(TAG, "Heartbeat function %s", app->_heartbeat_enabled ? "enabled" : "disabled");
    
    // 在文本区域显示状态变化
    const char* status_msg = app->_heartbeat_enabled ? 
        "\r\n[System] Heartbeat enabled.\r\n" : 
        "\r\n[System] Heartbeat disabled.\r\n";
    lv_textarea_add_text(app->_text_area_ttl, status_msg);
    app->_current_text_len += strlen(status_msg);
}

void UARTTTL::onScreenSettingsLoaded(lv_event_t *e)
{
    UARTTTL* app = static_cast<UARTTTL*>(lv_event_get_user_data(e));
    ESP_LOGD(TAG, "Settings screen loaded, updating dropdown values");
    
    // 根据当前配置设置波特率下拉框
    for(size_t i = 0; i < sizeof(baudrate_options)/sizeof(int); ++i) {
        if(baudrate_options[i] == app->_current_config.baud_rate) {
            lv_dropdown_set_selected(ui_DropdownTTLSettingBaudrate, i);
            break;
        }
    }
    
    // 设置数据位下拉框
    for(size_t i = 0; i < sizeof(databits_options)/sizeof(uart_word_length_t); ++i) {
        if(databits_options[i] == app->_current_config.data_bits) {
            lv_dropdown_set_selected(ui_DropdownTTLSettingDatabits, i);
            break;
        }
    }
    
    // 设置校验位下拉框
    for(size_t i = 0; i < sizeof(parity_options)/sizeof(uart_parity_t); ++i) {
        if(parity_options[i] == app->_current_config.parity) {
            lv_dropdown_set_selected(ui_DropdownTTLSettingParity, i);
            break;
        }
    }
    
    // 设置停止位下拉框
    for(size_t i = 0; i < sizeof(stopbits_options)/sizeof(uart_stop_bits_t); ++i) {
        if(stopbits_options[i] == app->_current_config.stop_bits) {
            lv_dropdown_set_selected(ui_DropdownTTLSettingStopbits, i);
            break;
        }
    }
}

void UARTTTL::onButtonSettingsApplyClicked(lv_event_t *e)
{
    UARTTTL* app = static_cast<UARTTTL*>(lv_event_get_user_data(e));
    
    // 从下拉框获取新的配置参数
    uint16_t idx = lv_dropdown_get_selected(ui_DropdownTTLSettingBaudrate);
    app->_current_config.baud_rate = baudrate_options[idx];
    
    idx = lv_dropdown_get_selected(ui_DropdownTTLSettingDatabits);
    app->_current_config.data_bits = databits_options[idx];
    
    idx = lv_dropdown_get_selected(ui_DropdownTTLSettingParity);
    app->_current_config.parity = parity_options[idx];

    idx = lv_dropdown_get_selected(ui_DropdownTTLSettingStopbits);
    app->_current_config.stop_bits = stopbits_options[idx];

    // 保存配置到NVS
    app->saveSettings();
    
    // 重新配置UART服务
    app->_uart_service.reconfigure(app->_current_config);
    
    ESP_LOGI(TAG, "UART configuration applied and saved");
    
    // 返回主屏幕
    lv_scr_load(ui_ScreenTTL);
}

void UARTTTL::onButtonSettingsBackClicked(lv_event_t *e)
{
    ESP_LOGD(TAG, "Settings back button clicked, returning to main screen");
    lv_scr_load(ui_ScreenTTL);
}