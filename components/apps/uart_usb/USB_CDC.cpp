#include "USB_CDC.hpp"
#include "esp_log.h"
#include "ui/ui.h"
#include <string.h>

#define MAX_UI_UPDATE_LEN 1024
#define TEXT_AREA_MAX_LEN 12288         // 最大文本长度（12KB）- 增加容量
#define TEXT_AREA_CLEAR_TRIGGER 10240   // 触发清空的长度（10KB）- 减少清理频率  
#define TEXT_AREA_KEEP_LEN 4096         // 清空后保留的长度（4KB）- 保留更多历史

static const char *TAG = "AppUSBCDC";

USB_CDC::USB_CDC() : 
    ESP_Brookesia_PhoneApp("USB CDC", get_usb_app_icon(), false),
    _update_timer(nullptr),
    _last_conn_state(false),
    _current_text_len(0),
    _main_screen(nullptr)
{
    // 初始化默认串口设置
    _current_settings.baud_rate = 115200;
    _current_settings.data_bits = 8;
    _current_settings.parity = 0;    // 0=None, 1=Odd, 2=Even
    _current_settings.stop_bits = 0; // 0=1bit, 1=1.5bit, 2=2bit
    
    // 初始化心跳包开关状态（默认开启）
    _heartbeat_enabled = true;
    
    // 同步心跳包状态到TinyUsbCdcService
    _usb_cdc_service.setHeartbeatEnabled(_heartbeat_enabled);
    
    // 同步配置到服务中
    TinyUsbCdcService::SerialConfig config = {
        .baud_rate = _current_settings.baud_rate,
        .data_bits = _current_settings.data_bits,
        .parity = _current_settings.parity,
        .stop_bits = _current_settings.stop_bits
    };
    _usb_cdc_service.setCurrentConfig(config);
}

USB_CDC::~USB_CDC()
{
    ESP_LOGI(TAG, "USB_CDC destructor called.");
    
    // [修复蓝屏问题] 确保析构函数中的资源清理
    if (_update_timer) {
        ESP_LOGW(TAG, "Destructor: cleaning up timer that wasn't properly closed");
        lv_timer_pause(_update_timer);
        vTaskDelay(pdMS_TO_TICKS(50));
        lv_timer_del(_update_timer);
        _update_timer = nullptr;
    }
    
    // 确保USB服务完全停止
    _usb_cdc_service.stopHeartbeat();
    _usb_cdc_service.stopScan();
    _usb_cdc_service.forceDisconnectDevice();
    
    ESP_LOGI(TAG, "USB_CDC destructor completed.");
}

bool USB_CDC::init(void)
{
    ESP_LOGI(TAG, "Initializing USB CDC Service for the first time.");
    if (!_usb_cdc_service.begin()) {
        ESP_LOGE(TAG, "Failed to initialize UsbCdcService. The app may not work.");
        return false;
    }
    return true;
}

bool USB_CDC::run(void)
{
    ui_usb_init(); 
    extraUiInit();

    // 保存主界面引用
    _main_screen = lv_scr_act();

    _update_timer = lv_timer_create(uiUpdateTimerCb, 50, this);  // 减少到50ms提高响应性
    lv_timer_pause(_update_timer);

    // 安全检查并配置UI对象
    if (uic_TextAreaUSB) {
        // 配置TextArea以更好地处理大量文本
        lv_textarea_set_one_line(uic_TextAreaUSB, false);  // 允许多行
        lv_obj_set_style_text_font(uic_TextAreaUSB, &lv_font_montserrat_12, 0);  // 使用较小字体节省空间
        
        // 设置文本区域为只读模式，避免意外编辑
        lv_textarea_set_text_selection(uic_TextAreaUSB, false);
        lv_obj_clear_flag(uic_TextAreaUSB, LV_OBJ_FLAG_CLICKABLE);
        
        // 启用滚动并配置滚动行为
        lv_obj_set_scroll_snap_y(uic_TextAreaUSB, LV_SCROLL_SNAP_NONE);
        // lv_obj_set_style_scroll_on_focus(uic_TextAreaUSB, false, 0); // 此函数在当前LVGL版本中不可用
        
        // 首先清空TextArea，确保没有残留数据
        lv_textarea_set_text(uic_TextAreaUSB, "");
        
        const char* welcome_msg = "[USB] USB CDC Terminal Ready\n" 
                                 "Click START to begin scanning for USB devices...\n"
                                 "----------------------------------------\n";
        lv_textarea_set_text(uic_TextAreaUSB, welcome_msg);
        _current_text_len = strlen(welcome_msg);
        
        ESP_LOGI(TAG, "TextArea configured for optimal text display");
    } else {
        ESP_LOGE(TAG, "Failed to initialize TextArea UI object");
        return false;
    }
    
    _last_conn_state = false;
    
    return true;
}

bool USB_CDC::back(void)
{
    ESP_LOGI(TAG, "USB_CDC back() called.");
    return notifyCoreClosed();
}

bool USB_CDC::close(void)
{
    ESP_LOGI(TAG, "Closing App UI, cleaning up resources.");
    
    // [修复蓝屏问题] 1. 首先暂停定时器，防止在清理过程中继续执行
    if (_update_timer) {
        ESP_LOGI(TAG, "Pausing update timer before cleanup...");
        lv_timer_pause(_update_timer);
        vTaskDelay(pdMS_TO_TICKS(100));  // 等待当前定时器回调完成
    }
    
    // 2. 停止USB服务（按正确顺序）
    ESP_LOGI(TAG, "Stopping USB services...");
    _usb_cdc_service.stopHeartbeat();      // 先停止心跳
    _usb_cdc_service.stopScan();           // 再停止扫描
    
    // 3. 等待USB服务完全停止
    vTaskDelay(pdMS_TO_TICKS(200));
    
    // 4. 强制断开设备连接
    _usb_cdc_service.forceDisconnectDevice();
    
    // 5. 最后安全删除定时器
    if (_update_timer) {
        ESP_LOGI(TAG, "Safely deleting update timer...");
        lv_timer_del(_update_timer);
        _update_timer = nullptr;
    }
    
    // 6. 重置所有状态变量
    _last_conn_state = false;
    _current_text_len = 0;
    
    // 7. 清理TextArea内容，防止内存泄漏
    if (uic_TextAreaUSB && lv_obj_is_valid(uic_TextAreaUSB)) {
        lv_textarea_set_text(uic_TextAreaUSB, "");
    }
    
    ESP_LOGI(TAG, "USB CDC app cleanup completed successfully");
    
    return true;
}

void USB_CDC::extraUiInit(void)
{
    lv_obj_t* btn_start = uic_ButtonUSBStart;
    lv_obj_t* btn_stop = uic_ButtonUSBStop;
    lv_obj_t* btn_setting = uic_ButtonUSBSetting;
    lv_obj_t* btn_exit = uic_ButtonUSBExit;
    lv_obj_t* switch_heartbeat = uic_SwitchUSBHeartbeat;

    lv_obj_add_event_cb(btn_start, onButtonStartClicked, LV_EVENT_CLICKED, this);
    lv_obj_add_event_cb(btn_stop, onButtonStopClicked, LV_EVENT_CLICKED, this);
    lv_obj_add_event_cb(btn_setting, onButtonSettingsClicked, LV_EVENT_CLICKED, this);
    lv_obj_add_event_cb(btn_exit, onButtonExitClicked, LV_EVENT_CLICKED, this);
    
    // [新增] 心跳包开关事件回调
    lv_obj_add_event_cb(switch_heartbeat, onSwitchHeartbeatChanged, LV_EVENT_VALUE_CHANGED, this);
    
    // [新增] 设置心跳包开关的初始状态
    if (_heartbeat_enabled) {
        lv_obj_add_state(switch_heartbeat, LV_STATE_CHECKED);
    } else {
        lv_obj_clear_state(switch_heartbeat, LV_STATE_CHECKED);
    }

    lv_obj_clear_state(btn_start, LV_STATE_DISABLED);
    lv_obj_add_state(btn_stop, LV_STATE_DISABLED);
}

void USB_CDC::uiUpdateTimerCb(lv_timer_t *timer)
{
    // [修复蓝屏问题] 加强定时器回调的安全检查
    if (!timer || !timer->user_data) {
        ESP_LOGW(TAG, "Timer callback: invalid timer or user_data");
        return;
    }
    
    USB_CDC* app = static_cast<USB_CDC*>(timer->user_data);
    
    // 检查应用对象是否有效
    if (!app) {
        ESP_LOGW(TAG, "Timer callback: invalid app object");
        return;
    }
    
    // 检查UI对象是否仍然有效
    if (!uic_TextAreaUSB || !lv_obj_is_valid(uic_TextAreaUSB)) {
        ESP_LOGW(TAG, "Timer callback: TextArea UI object is invalid");
        return;
    }
    
    // 检查定时器是否已被标记为删除
    if (app->_update_timer != timer) {
        ESP_LOGW(TAG, "Timer callback: timer mismatch, skipping update");
        return;
    }
    
    bool is_connected = app->_usb_cdc_service.isConnected();
    if (is_connected != app->_last_conn_state) {
        app->_last_conn_state = is_connected;
        if (is_connected) {
            // 设备刚连接时，显示连接信息但不立即配置
            // 配置由heartbeat任务或用户操作来处理，避免重复配置
            char config_msg[128];
            const char* parity_str[] = {"N", "O", "E"};  
            const char* stop_str[] = {"1", "1.5", "2"};
            snprintf(config_msg, sizeof(config_msg), 
                    "\n[Status] USB Device Connected! Type: %s\n", 
                    app->_usb_cdc_service.getDeviceTypeName());
            
            app->addTextToDisplay(config_msg);
        } else {
            const char* status_msg = "\n[Status] USB Device Disconnected.\n";
            app->addTextToDisplay(status_msg);
        }
    }

    uint8_t buffer[MAX_UI_UPDATE_LEN + 1];  // +1 为null terminator预留空间
    size_t total_read = 0;
    
    // 改进数据读取策略：避免截断完整数据包
    while (total_read < (MAX_UI_UPDATE_LEN - 1)) {  // 留1字节给null terminator
        size_t available = app->_usb_cdc_service.available();
        if (available == 0) break;
        
        // 增大单次读取大小，减少截断风险
        size_t remaining = (MAX_UI_UPDATE_LEN - 1) - total_read;
        size_t to_read = (remaining > 256) ? 256 : remaining;  // 从64提升到256字节
        
        size_t bytes_read = app->_usb_cdc_service.read(buffer + total_read, to_read);
        if (bytes_read == 0) break;
        total_read += bytes_read;
    }
    
    if (total_read > 0) {
        buffer[total_read] = '\0';  // 现在安全了，因为buffer大小是MAX_UI_UPDATE_LEN+1
        
        // 使用改进的文本处理方法
        app->addTextToDisplayImproved((const char*)buffer, total_read);
    }
}

void USB_CDC::onButtonStartClicked(lv_event_t* e)
{
    USB_CDC* app = static_cast<USB_CDC*>(lv_event_get_user_data(e));
    
    // 显示启动状态
    const char* starting_msg = "\n[System] Starting USB CDC services...\n";
    app->addTextToDisplay(starting_msg);
    
    // 检查设备是否已连接但心跳未运行，如果是则直接启动心跳
    if (app->_usb_cdc_service.isConnected()) {
        ESP_LOGI(TAG, "Device already connected, starting services...");
        
        // 根据心跳包开关状态决定是否启动心跳
        if (app->_heartbeat_enabled) {
            app->_usb_cdc_service.startHeartbeat();
            ESP_LOGI(TAG, "Heartbeat started (enabled by switch)");
        } else {
            ESP_LOGI(TAG, "Heartbeat not started (disabled by switch)");
        }
        
        const char* msg = "\n[System] Services started for connected device.\n";
        app->addTextToDisplay(msg);
    } else {
        // 设备未连接，开始扫描
        ESP_LOGI(TAG, "No device connected, starting scan...");
        app->_usb_cdc_service.startScan();
        const char* msg = "\n[System] Started scanning for USB devices.\nPlease insert a USB-to-Serial device.\n";
        app->addTextToDisplay(msg);
    }

    // 恢复定时器
    lv_timer_resume(app->_update_timer);

    // 更新按钮状态
    lv_obj_add_state(uic_ButtonUSBStart, LV_STATE_DISABLED);
    lv_obj_clear_state(uic_ButtonUSBStop, LV_STATE_DISABLED);
}

void USB_CDC::onButtonStopClicked(lv_event_t *e)
{
    USB_CDC* app = static_cast<USB_CDC*>(lv_event_get_user_data(e));
    
    // 显示停止状态
    const char* stopping_msg = "\n[System] Stopping services and disconnecting device...\n";
    app->addTextToDisplay(stopping_msg);
    
    // 停止扫描任务
    app->_usb_cdc_service.stopScan();
    
    // 停止心跳任务
    app->_usb_cdc_service.stopHeartbeat();
    
    // 强制断开USB设备连接
    app->_usb_cdc_service.forceDisconnectDevice();
    
    // 暂停定时器
    lv_timer_pause(app->_update_timer);
    
    // 更新UI状态
    app->_last_conn_state = false;  // 重置连接状态

    const char* msg = "\n[System] All services stopped. Device disconnected.\n";
    app->addTextToDisplay(msg);

    lv_obj_clear_state(uic_ButtonUSBStart, LV_STATE_DISABLED);
    lv_obj_add_state(uic_ButtonUSBStop, LV_STATE_DISABLED);
}

void USB_CDC::onButtonSettingsClicked(lv_event_t *e)
{
    USB_CDC* app = static_cast<USB_CDC*>(lv_event_get_user_data(e));
    app->showSettingsScreen();
}

void USB_CDC::onButtonExitClicked(lv_event_t* e)
{
    USB_CDC* app = static_cast<USB_CDC*>(lv_event_get_user_data(e));
    app->notifyCoreClosed();
}

// [新增] 显示设置界面
void USB_CDC::showSettingsScreen(void)
{
    // 初始化设置界面
    ui_ScreenUSBSettings_screen_init();
    
    // 设置当前配置到UI控件
    // 波特率
    const uint32_t baud_rates[] = {4800, 9600, 19200, 38400, 57600, 115200, 230400, 460800, 1500000};
    int baud_index = 5; // 默认115200
    for (int i = 0; i < 9; i++) {
        if (baud_rates[i] == _current_settings.baud_rate) {
            baud_index = i;
            break;
        }
    }
    lv_dropdown_set_selected(uic_DropdownUSBBaudrate, baud_index);
    
    // 数据位 (5,6,7,8 -> index 0,1,2,3)
    lv_dropdown_set_selected(uic_DropdownUSBDatabits, _current_settings.data_bits - 5);
    
    // 校验位 (None,Odd,Even -> index 0,1,2)
    lv_dropdown_set_selected(uic_DropdownUSBParity, _current_settings.parity);
    
    // 停止位 (1,1.5,2 -> index 0,1,2)
    lv_dropdown_set_selected(uic_DropdownUSBStopbits, _current_settings.stop_bits);
    
    // 设置按钮事件回调
    lv_obj_add_event_cb(uic_ButtonUSBSettingsApply, onButtonApplyClicked, LV_EVENT_CLICKED, this);
    lv_obj_add_event_cb(uic_ButtonUSBSettingBack, onButtonBackClicked, LV_EVENT_CLICKED, this);
    
    // 切换到设置界面
    lv_scr_load(uic_ScreenUSBSettings);
    
    ESP_LOGI(TAG, "Settings screen displayed");
}

// [新增] 隐藏设置界面，返回主界面
void USB_CDC::hideSettingsScreen(void)
{
    // 确保当前界面是设置界面
    if (lv_scr_act() == uic_ScreenUSBSettings) {
        // 先切换回主界面
        if (_main_screen && lv_obj_is_valid(_main_screen)) {
            lv_scr_load(_main_screen);
            ESP_LOGI(TAG, "Switched back to main screen");
        } else {
            ESP_LOGW(TAG, "Main screen reference is invalid");
        }
        
        // 等待屏幕切换完成后再销毁设置界面
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    
    // 销毁设置界面
    ui_ScreenUSBSettings_screen_destroy();
    
    ESP_LOGI(TAG, "Settings screen hidden, returned to main interface");
}

// [新增] 应用按钮点击事件
void USB_CDC::onButtonApplyClicked(lv_event_t *e)
{
    USB_CDC* app = static_cast<USB_CDC*>(lv_event_get_user_data(e));
    
    // 从UI获取配置
    const uint32_t baud_rates[] = {4800, 9600, 19200, 38400, 57600, 115200, 230400, 460800, 1500000};
    int baud_index = lv_dropdown_get_selected(uic_DropdownUSBBaudrate);
    int data_index = lv_dropdown_get_selected(uic_DropdownUSBDatabits);
    int parity_index = lv_dropdown_get_selected(uic_DropdownUSBParity);
    int stop_index = lv_dropdown_get_selected(uic_DropdownUSBStopbits);
    
    // 新的配置
    SerialSettings new_settings = {
        .baud_rate = baud_rates[baud_index],
        .data_bits = (uint8_t)(data_index + 5),  // 5,6,7,8
        .parity = (uint8_t)parity_index,         // 0,1,2
        .stop_bits = (uint8_t)stop_index         // 0,1,2
    };
    
    // 检查设备连接状态
    if (app->_usb_cdc_service.isConnected()) {
        ESP_LOGI(TAG, "Device is connected, applying configuration safely...");
        ESP_LOGI(TAG, "Device type: %s", app->_usb_cdc_service.getDeviceTypeName());
        
        // 1. 暂停定时器，避免在配置过程中读取数据
        if (app->_update_timer) {
            lv_timer_pause(app->_update_timer);
        }
        
        // 2. 短暂延时，确保当前传输完成
        vTaskDelay(pdMS_TO_TICKS(100));
        
        // 3. 应用新配置
        TinyUsbCdcService::SerialConfig config = {
            .baud_rate = new_settings.baud_rate,
            .data_bits = new_settings.data_bits,
            .parity = new_settings.parity,
            .stop_bits = new_settings.stop_bits
        };
        
        // [修复问题2] 强制应用USB CDC配置
        ESP_LOGI(TAG, "Forcefully applying configuration: %lu baud", new_settings.baud_rate);
        
        // 多次尝试配置以确保生效
        for (int attempt = 0; attempt < 3; attempt++) {
            app->_usb_cdc_service.configureSerialPort(config);
            vTaskDelay(pdMS_TO_TICKS(50));  // 等待配置生效
            
            ESP_LOGI(TAG, "Configuration attempt %d/3 completed", attempt + 1);
        }
        
        // 4. 恢复定时器
        if (app->_update_timer) {
            lv_timer_resume(app->_update_timer);
        }
        
        // 5. 更新本地设置和服务设置
        app->_current_settings = new_settings;
        app->_usb_cdc_service.setCurrentConfig(config); // [重要] 保存配置到服务中
        
        // 在主界面显示应用成功的消息
        char config_msg[128];
        const char* parity_str[] = {"N", "O", "E"};
        const char* stop_str[] = {"1", "1.5", "2"};
        snprintf(config_msg, sizeof(config_msg), 
                "\n[Settings] Applied: %lu %d%s%s\n", 
                new_settings.baud_rate, 
                new_settings.data_bits,
                (new_settings.parity < 3) ? parity_str[new_settings.parity] : "?",
                (new_settings.stop_bits < 3) ? stop_str[new_settings.stop_bits] : "?");
        
        if (uic_TextAreaUSB) {
            app->addTextToDisplay(config_msg);
        }
    } else {
        ESP_LOGI(TAG, "No device connected, saving settings for next connection...");
        
        // 设备未连接时，只保存设置，下次连接时应用
        app->_current_settings = new_settings;
        
        // [重要] 同时保存到服务中，以便设备连接时使用
        TinyUsbCdcService::SerialConfig config = {
            .baud_rate = new_settings.baud_rate,
            .data_bits = new_settings.data_bits,
            .parity = new_settings.parity,
            .stop_bits = new_settings.stop_bits
        };
        app->_usb_cdc_service.setCurrentConfig(config);
        
        const char* msg = "\n[Settings] Configuration saved. Will be applied when device connects.\n";
        if (uic_TextAreaUSB) {
            app->addTextToDisplay(msg);
        }
    }
    
    // 返回主界面
    app->hideSettingsScreen();
}

// [新增] 返回按钮点击事件
void USB_CDC::onButtonBackClicked(lv_event_t *e)
{
    USB_CDC* app = static_cast<USB_CDC*>(lv_event_get_user_data(e));
    app->hideSettingsScreen();
}

// [新增] 智能文本管理 - 添加文本到显示区域
void USB_CDC::addTextToDisplay(const char* text)
{
    if (!text || !uic_TextAreaUSB) {
        return;
    }
    
    size_t text_len = strlen(text);
    
    // 检查是否需要智能清理文本区域
    if (_current_text_len + text_len > TEXT_AREA_CLEAR_TRIGGER) {
        smartTextAreaClear();
    }
    
    // 添加新文本
    lv_textarea_add_text(uic_TextAreaUSB, text);
    _current_text_len += text_len;
    
    // 自动滚动到底部
    lv_obj_scroll_to_y(uic_TextAreaUSB, LV_COORD_MAX, LV_ANIM_OFF);
    
    ESP_LOGD(TAG, "Added %zu chars, total: %zu chars", text_len, _current_text_len);
}

// [新增] 改进的文本处理函数 - 处理数据截断和换行符问题
void USB_CDC::addTextToDisplayImproved(const char* text, size_t actual_len)
{
    // [修复蓝屏问题] 增强安全检查
    if (!text || actual_len == 0) {
        return;
    }
    
    // 检查UI对象是否有效
    if (!uic_TextAreaUSB || !lv_obj_is_valid(uic_TextAreaUSB)) {
        ESP_LOGW(TAG, "TextArea UI object is invalid, skipping text addition");
        return;
    }
    
    // 限制最大处理长度，防止内存问题
    if (actual_len > MAX_UI_UPDATE_LEN) {
        ESP_LOGW(TAG, "Text too long (%zu), truncating to %d", actual_len, MAX_UI_UPDATE_LEN);
        actual_len = MAX_UI_UPDATE_LEN;
    }
    
    // 创建处理后的文本缓冲区，加强内存安全
    char* processed_text = (char*)malloc(actual_len + 128);  // 更多额外空间
    if (!processed_text) {
        ESP_LOGE(TAG, "Failed to allocate memory for text processing (%zu bytes)", actual_len + 128);
        return;
    }
    
    // 初始化缓冲区
    memset(processed_text, 0, actual_len + 128);
    
    size_t processed_len = 0;
    
    // 逐字节处理，确保文本显示正确
    for (size_t i = 0; i < actual_len; i++) {
        char c = text[i];
        
        // 处理不可打印字符
        if (c == '\0') {
            break;  // 遇到null terminator停止
        } else if (c == '\r') {
            // Windows风格的回车符，转换为换行符
            processed_text[processed_len++] = '\n';
        } else if (c == '\n') {
            // Unix风格的换行符，直接保留
            processed_text[processed_len++] = '\n';
        } else if (c >= 32 && c <= 126) {
            // 可打印ASCII字符
            processed_text[processed_len++] = c;
        } else {
            // 过滤掉所有其他字符（包括UTF-8），避免乱码
            // 如果是控制字符或高位字符，跳过不添加
            continue;
        }
    }
    
    // 确保以换行符结束（如果原文本没有的话）
    if (processed_len > 0 && processed_text[processed_len - 1] != '\n') {
        processed_text[processed_len++] = '\n';
    }
    
    processed_text[processed_len] = '\0';
    
    // 检查是否需要智能清理文本区域
    if (_current_text_len + processed_len > TEXT_AREA_CLEAR_TRIGGER) {
        smartTextAreaClear();
    }
    
    // 添加处理后的文本
    lv_textarea_add_text(uic_TextAreaUSB, processed_text);
    _current_text_len += processed_len;
    
    // 自动滚动到底部
    lv_obj_scroll_to_y(uic_TextAreaUSB, LV_COORD_MAX, LV_ANIM_OFF);
    
    ESP_LOGD(TAG, "Added %zu chars (processed from %zu), total: %zu chars", 
             processed_len, actual_len, _current_text_len);
    
    free(processed_text);
}

// [新增] 智能清理文本区域 - 保留最新内容
void USB_CDC::smartTextAreaClear(void)
{
    if (!uic_TextAreaUSB) {
        return;
    }
    
    ESP_LOGI(TAG, "Text area approaching limit (%zu chars), performing smart cleanup...", _current_text_len);
    
    // 获取当前文本内容
    const char* current_text = lv_textarea_get_text(uic_TextAreaUSB);
    if (!current_text) {
        _current_text_len = 0;
        return;
    }
    
    size_t current_len = strlen(current_text);
    
    if (current_len <= TEXT_AREA_KEEP_LEN) {
        // 如果当前文本已经很短，直接返回
        _current_text_len = current_len;
        return;
    }
    
    // 计算要保留的起始位置（保留最后的TEXT_AREA_KEEP_LEN字符）
    size_t keep_start = current_len - TEXT_AREA_KEEP_LEN;
    
    // 尝试从完整行开始保留（寻找换行符）
    const char* line_start = current_text + keep_start;
    const char* next_newline = strchr(line_start, '\n');
    if (next_newline && (next_newline - current_text) < current_len - 100) {
        // 找到换行符且不是太靠近末尾，从下一行开始
        keep_start = (next_newline + 1) - current_text;
    }
    
    // 添加清理提示信息
    const char* cleanup_msg = "[System] Text buffer optimized - showing recent messages...\n";
    
    // 创建新的文本内容
    char* new_content = (char*)malloc(strlen(cleanup_msg) + (current_len - keep_start) + 1);
    if (new_content) {
        strcpy(new_content, cleanup_msg);
        strcat(new_content, current_text + keep_start);
        
        // 设置新内容
        lv_textarea_set_text(uic_TextAreaUSB, new_content);
        _current_text_len = strlen(new_content);
        
        // 自动滚动到底部
        lv_obj_scroll_to_y(uic_TextAreaUSB, LV_COORD_MAX, LV_ANIM_OFF);
        
        ESP_LOGI(TAG, "Text cleanup completed: %zu -> %zu chars (saved %zu chars)", 
                 current_len, _current_text_len, current_len - _current_text_len);
        
        free(new_content);
    } else {
        // 内存不足时的简单清理
        ESP_LOGW(TAG, "Memory insufficient for smart cleanup, using simple clear");
        lv_textarea_set_text(uic_TextAreaUSB, cleanup_msg);
        _current_text_len = strlen(cleanup_msg);
    }
}

// [新增] 心跳包开关事件处理
void USB_CDC::onSwitchHeartbeatChanged(lv_event_t *e)
{
    USB_CDC* app = static_cast<USB_CDC*>(lv_event_get_user_data(e));
    lv_obj_t* switch_obj = lv_event_get_target(e);
    
    // 获取开关状态
    bool is_checked = lv_obj_has_state(switch_obj, LV_STATE_CHECKED);
    
    ESP_LOGI(TAG, "Heartbeat switch changed to: %s", is_checked ? "ON" : "OFF");
    
    // 更新心跳包状态
    app->updateHeartbeatState(is_checked);
}

// [新增] 更新心跳包状态
void USB_CDC::updateHeartbeatState(bool enabled)
{
    _heartbeat_enabled = enabled;
    
    // 同步状态到TinyUsbCdcService
    _usb_cdc_service.setHeartbeatEnabled(enabled);
    
    // 如果设备已连接，立即应用心跳包状态
    if (_usb_cdc_service.isConnected()) {
        if (enabled) {
            // 启动心跳包发送
            _usb_cdc_service.startHeartbeat();
            addTextToDisplay("\n[System] Heartbeat enabled.\n");
        } else {
            // 停止心跳包发送
            _usb_cdc_service.stopHeartbeat();
            addTextToDisplay("\n[System] Heartbeat disabled.\n");
        }
    }
    
    ESP_LOGI(TAG, "Heartbeat state updated: %s", enabled ? "enabled" : "disabled");
}