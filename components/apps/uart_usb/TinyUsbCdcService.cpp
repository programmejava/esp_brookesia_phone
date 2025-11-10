#include <string.h>
#include <cstdio>
#include "TinyUsbCdcService.hpp"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/ringbuf.h"
#include "usb/usb_host.h"
#include "usb/cdc_acm_host.h"

static const char* TAG = "UsbCdcService";

// 初始化静态成员变量
RingbufHandle_t TinyUsbCdcService::_s_rx_ring_buffer = nullptr;
volatile bool   TinyUsbCdcService::_s_is_device_connected = false;
cdc_acm_dev_hdl_t TinyUsbCdcService::_s_cdc_device_handle = nullptr;
uint16_t TinyUsbCdcService::_s_device_vid = 0;
uint16_t TinyUsbCdcService::_s_device_pid = 0;

TinyUsbCdcService::TinyUsbCdcService() : 
    _host_task_handle(nullptr), 
    _scan_task_handle(nullptr), 
    _heartbeat_task_handle(nullptr), 
    _scan_task_should_stop(false),
    _heartbeat_task_should_stop(false),
    _current_device_type(DEVICE_TYPE_UNKNOWN) 
{
    // 初始化默认配置
    _current_config.baud_rate = 115200;
    _current_config.data_bits = 8;
    _current_config.parity = 0;    // None
    _current_config.stop_bits = 0; // 1 bit
    
    // 初始化心跳包启用状态（默认开启）
    _heartbeat_enabled = true;
}
TinyUsbCdcService::~TinyUsbCdcService() { end(); }

bool TinyUsbCdcService::begin() {
    ESP_LOGI(TAG, "Initializing USB Host Service...");

    if (!_s_rx_ring_buffer) {
        _s_rx_ring_buffer = xRingbufferCreate(RX_RING_BUFFER_SIZE, RINGBUF_TYPE_BYTEBUF);
        if (!_s_rx_ring_buffer) { 
            ESP_LOGE(TAG, "Failed to create ring buffer");
            return false; 
        }
    }
    
    const usb_host_config_t host_config = { .intr_flags = ESP_INTR_FLAG_LEVEL1 };
    if (usb_host_install(&host_config) != ESP_OK) {
        ESP_LOGE(TAG, "USB Host install failed");
        return false;
    }
    
    const cdc_acm_host_driver_config_t driver_config = {
        .driver_task_stack_size = 4096,
        .driver_task_priority = 5,
        .xCoreID = 0,
        .new_dev_cb = NULL // 我们使用自己的扫描任务，不使用这个回调
    };
    if (cdc_acm_host_install(&driver_config) != ESP_OK) {
        ESP_LOGE(TAG, "CDC ACM Host install failed");
        usb_host_uninstall();
        return false;
    }

    if (xTaskCreate(host_lib_task, "usb_host_task", 4096, NULL, 5, &_host_task_handle) != pdPASS) {
        ESP_LOGE(TAG, "Failed to create host_lib_task");
        end();
        return false;
    }
    
    ESP_LOGI(TAG, "USB Host Service initialized successfully");
    return true;
}

void TinyUsbCdcService::end() {
    stopScan();
    stopHeartbeat();
    if (_s_cdc_device_handle) {
        cdc_acm_host_close(_s_cdc_device_handle);
        _s_cdc_device_handle = nullptr;
    }
    cdc_acm_host_uninstall();
    if (_host_task_handle) {
        vTaskDelete(_host_task_handle);
        _host_task_handle = nullptr;
    }
    usb_host_uninstall();
    if (_s_rx_ring_buffer) {
        vRingbufferDelete(_s_rx_ring_buffer);
        _s_rx_ring_buffer = nullptr;
    }
    _s_is_device_connected = false;
    ESP_LOGI(TAG, "USB Host Service deinitialized");
}

void TinyUsbCdcService::startScan() {
    if (_scan_task_handle == nullptr) {
        ESP_LOGI(TAG, "Starting device scan task...");
        _scan_task_should_stop = false;  // 重置停止标志
        
        BaseType_t result = xTaskCreate(device_scan_task, "cdc_scan_task", 4096, this, 4, &_scan_task_handle);
        if (result == pdPASS) {
            ESP_LOGI(TAG, "Device scan task created successfully");
        } else {
            ESP_LOGE(TAG, "Failed to create device scan task: %d", result);
            _scan_task_handle = nullptr;
        }
    } else {
        ESP_LOGW(TAG, "Scan task already running, skipping creation");
    }
}

void TinyUsbCdcService::stopScan() {
    if (_scan_task_handle) {
        ESP_LOGI(TAG, "Stopping scan task...");
        _scan_task_should_stop = true;  // 设置停止标志
        
        // 等待任务自然退出，最多等待2秒 (增加等待时间)
        for (int i = 0; i < 200 && _scan_task_handle; i++) {
            vTaskDelay(pdMS_TO_TICKS(10));
            if (i % 50 == 0) {  // 每500ms打印一次状态
                ESP_LOGD(TAG, "Waiting for scan task to exit... (%d/200)", i);
            }
        }
        
        // 如果任务还没退出，强制删除
        if (_scan_task_handle) {
            ESP_LOGW(TAG, "Scan task did not exit gracefully, forcing deletion");
            vTaskDelete(_scan_task_handle);
            _scan_task_handle = nullptr;
            ESP_LOGW(TAG, "Forced to delete scan task");
        } else {
            ESP_LOGI(TAG, "Scan task stopped gracefully");
        }
    }
}

// [新增] 强制断开USB设备连接
void TinyUsbCdcService::forceDisconnectDevice() {
    ESP_LOGI(TAG, "Force disconnecting USB device...");
    
    // 停止心跳任务
    stopHeartbeat();
    
    // 如果有连接的设备，关闭它
    if (_s_cdc_device_handle) {
        ESP_LOGI(TAG, "Closing device handle (VID:0x%04X PID:0x%04X)...", _s_device_vid, _s_device_pid);
        cdc_acm_host_close(_s_cdc_device_handle);
        _s_cdc_device_handle = nullptr;
        ESP_LOGI(TAG, "Device handle closed");
    }
    
    // 清理设备状态
    _s_is_device_connected = false;
    _s_device_vid = 0;
    _s_device_pid = 0;
    _current_device_type = DEVICE_TYPE_UNKNOWN;
    
    ESP_LOGI(TAG, "Device disconnection completed");
}

// [新增] 心跳控制方法
void TinyUsbCdcService::startHeartbeat() {
    // 添加更严格的检查，防止重复创建任务
    if (_heartbeat_task_handle != nullptr) {
        ESP_LOGW(TAG, "Heartbeat task already running, skipping creation");
        return;
    }
    
    if (_s_is_device_connected) {
        _heartbeat_task_should_stop = false;  // 重置停止标志
        BaseType_t result = xTaskCreate(heartbeat_task, "cdc_heartbeat", 4096, this, 3, &_heartbeat_task_handle);
        if (result == pdPASS) {
            ESP_LOGI(TAG, "Heartbeat task started successfully (stack: 4096 bytes)");
        } else {
            ESP_LOGE(TAG, "Failed to create heartbeat task: %d", result);
            _heartbeat_task_handle = nullptr;
        }
    } else {
        ESP_LOGW(TAG, "Cannot start heartbeat: device not connected");
    }
}

void TinyUsbCdcService::stopHeartbeat() {
    if (_heartbeat_task_handle) {
        ESP_LOGI(TAG, "Stopping heartbeat task...");
        _heartbeat_task_should_stop = true;  // 设置停止标志
        
        // 等待任务自然退出，增加等待时间到3秒
        for (int i = 0; i < 300 && _heartbeat_task_handle; i++) {
            vTaskDelay(pdMS_TO_TICKS(10));
            if (i % 100 == 0) {  // 每1秒打印一次状态
                ESP_LOGD(TAG, "Waiting for heartbeat task to exit... (%d/300)", i);
            }
        }
        
        // 如果任务还没退出，强制删除
        if (_heartbeat_task_handle) {
            ESP_LOGW(TAG, "Heartbeat task did not exit gracefully, forcing deletion");
            vTaskDelete(_heartbeat_task_handle);
            _heartbeat_task_handle = nullptr;
            ESP_LOGW(TAG, "Forced to delete heartbeat task");
        } else {
            ESP_LOGI(TAG, "Heartbeat task stopped gracefully");
        }
    }
}

void TinyUsbCdcService::configureSerialPort(uint32_t baud_rate) {
    if (!_s_is_device_connected || !_s_cdc_device_handle) {
        ESP_LOGW(TAG, "No device connected for serial configuration");
        return;
    }
    
    ESP_LOGI(TAG, "Configuring %s for %lu baud", getDeviceTypeName(), baud_rate);
    
    // CH340系列设备不支持标准CDC控制请求，跳过配置
    if (_current_device_type == DEVICE_TYPE_CH340) {
        ESP_LOGI(TAG, "Skipping configuration for CH340 device (uses hardware defaults)");
        return;
    }
    
    // 对于支持标准CDC的设备，发送配置请求
    ESP_LOGI(TAG, "Attempting standard CDC configuration for %lu 8N1", baud_rate);
    
    // 配置串口参数: 115200 8N1
    cdc_acm_line_coding_t line_coding = {
        .dwDTERate = baud_rate,      // 波特率
        .bCharFormat = 0,            // 1个停止位
        .bParityType = 0,            // 无校验
        .bDataBits = 8               // 8位数据位
    };
    
    // 发送 SET_LINE_CODING 控制请求 (USB CDC 规范 6.3.10)
    esp_err_t ret = cdc_acm_host_send_custom_request(
        _s_cdc_device_handle,
        0x21,                        // bmRequestType: Host-to-device, Class, Interface
        0x20,                        // bRequest: SET_LINE_CODING
        0x00,                        // wValue: 0
        0x00,                        // wIndex: Interface number (通常为0)
        sizeof(cdc_acm_line_coding_t), // wLength: 数据长度
        (uint8_t*)&line_coding       // data: line coding 结构体
    );
    
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Serial port configured successfully: %lu 8N1", baud_rate);
        
        // 对于某些设备，设置控制线状态有助于通信稳定
        if (_current_device_type == DEVICE_TYPE_CP210X || _current_device_type == DEVICE_TYPE_PL2303) {
            ret = cdc_acm_host_send_custom_request(
                _s_cdc_device_handle,
                0x21,                // bmRequestType  
                0x22,                // bRequest: SET_CONTROL_LINE_STATE
                0x03,                // wValue: DTR=1, RTS=1 (for flow control)
                0x00,                // wIndex: Interface number
                0,                   // wLength: 0
                nullptr              // data: none
            );
            
            if (ret == ESP_OK) {
                ESP_LOGI(TAG, "Control line state configured (DTR=1, RTS=1)");
            } else {
                ESP_LOGW(TAG, "Control line state config failed: %s", esp_err_to_name(ret));
            }
        }
    } else {
        ESP_LOGW(TAG, "Serial port config failed: %s (device may not support CDC)", esp_err_to_name(ret));
        ESP_LOGI(TAG, "Device may still work with default settings");
    }
}

// [新增] 完整的串口参数配置方法
void TinyUsbCdcService::configureSerialPort(const SerialConfig& config) {
    if (!_s_is_device_connected || !_s_cdc_device_handle) {
        ESP_LOGW(TAG, "No device connected for serial configuration");
        return;
    }
    
    const char* parity_str[] = {"None", "Odd", "Even"};
    const char* stop_str[] = {"1", "1.5", "2"};
    
    ESP_LOGI(TAG, "Configuring %s for %lu %d%s%s", 
             getDeviceTypeName(), 
             config.baud_rate, 
             config.data_bits,
             (config.parity < 3) ? parity_str[config.parity] : "?",
             (config.stop_bits < 3) ? stop_str[config.stop_bits] : "?");
    
    // CH340系列设备使用厂商特定配置方法
    if (_current_device_type == DEVICE_TYPE_CH340) {
        ESP_LOGI(TAG, "Using CH340-specific configuration method");
        configureCH340SerialPort(config.baud_rate);
        // 注意：CH340只支持波特率配置，数据位/校验位/停止位通常固定为8N1
        if (config.data_bits != 8 || config.parity != 0 || config.stop_bits != 0) {
            ESP_LOGW(TAG, "CH340 only supports 8N1 format, other parameters ignored");
        }
        return;
    }
    
    ESP_LOGI(TAG, "Applying configuration to %s device...", getDeviceTypeName());
    
    // 对于支持标准CDC的设备，发送配置请求
    cdc_acm_line_coding_t line_coding = {
        .dwDTERate = config.baud_rate,      // 波特率
        .bCharFormat = config.stop_bits,    // 停止位
        .bParityType = config.parity,       // 校验位
        .bDataBits = config.data_bits       // 数据位
    };
    
    // 发送 SET_LINE_CODING 控制请求 (USB CDC 规范 6.3.10)
    esp_err_t ret = cdc_acm_host_send_custom_request(
        _s_cdc_device_handle,
        0x21,                        // bmRequestType: Host-to-device, Class, Interface
        0x20,                        // bRequest: SET_LINE_CODING
        0x00,                        // wValue: 0
        0x00,                        // wIndex: Interface number (通常为0)
        sizeof(cdc_acm_line_coding_t), // wLength: 数据长度
        (uint8_t*)&line_coding       // data: line coding 结构体
    );
    
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Serial port configured successfully: %lu %d%s%s", 
                 config.baud_rate, 
                 config.data_bits,
                 (config.parity < 3) ? parity_str[config.parity] : "?",
                 (config.stop_bits < 3) ? stop_str[config.stop_bits] : "?");
        
        // [新增] 验证配置是否真正应用 - 读取当前配置
        cdc_acm_line_coding_t verify_coding;
        esp_err_t verify_ret = cdc_acm_host_send_custom_request(
            _s_cdc_device_handle,
            0xA1,                        // bmRequestType: Device-to-host, Class, Interface
            0x21,                        // bRequest: GET_LINE_CODING
            0x00,                        // wValue: 0
            0x00,                        // wIndex: Interface number
            sizeof(cdc_acm_line_coding_t), // wLength: 数据长度
            (uint8_t*)&verify_coding     // data: 接收配置数据
        );
        
        if (verify_ret == ESP_OK) {
            ESP_LOGI(TAG, "Configuration verification: %lu %d%s%s (actual)", 
                     verify_coding.dwDTERate, 
                     verify_coding.bDataBits,
                     (verify_coding.bParityType < 3) ? parity_str[verify_coding.bParityType] : "?",
                     (verify_coding.bCharFormat < 3) ? stop_str[verify_coding.bCharFormat] : "?");
                     
            if (verify_coding.dwDTERate != config.baud_rate) {
                ESP_LOGW(TAG, "Warning: Baud rate mismatch! Set: %lu, Actual: %lu", 
                         config.baud_rate, verify_coding.dwDTERate);
            }
        } else {
            ESP_LOGW(TAG, "Could not verify configuration: %s", esp_err_to_name(verify_ret));
        }
        
        // 对于某些设备，设置控制线状态有助于通信稳定
        if (_current_device_type == DEVICE_TYPE_CP210X || _current_device_type == DEVICE_TYPE_PL2303) {
            ret = cdc_acm_host_send_custom_request(
                _s_cdc_device_handle,
                0x21,                // bmRequestType  
                0x22,                // bRequest: SET_CONTROL_LINE_STATE
                0x03,                // wValue: DTR=1, RTS=1 (for flow control)
                0x00,                // wIndex: Interface number
                0,                   // wLength: 0
                nullptr              // data: none
            );
            
            if (ret == ESP_OK) {
                ESP_LOGI(TAG, "Control line state configured (DTR=1, RTS=1)");
            } else {
                ESP_LOGW(TAG, "Control line state config failed: %s", esp_err_to_name(ret));
            }
        }
    } else {
        ESP_LOGW(TAG, "Serial port config failed: %s (device may not support CDC)", esp_err_to_name(ret));
        ESP_LOGI(TAG, "Device may still work with default settings");
    }
}

// USB Host 库的主事件循环任务
void TinyUsbCdcService::host_lib_task(void* arg) {
    while (true) {
        usb_host_lib_handle_events(portMAX_DELAY, NULL);
    }
}

// [新增] 自动扫描并连接设备的后台任务
void TinyUsbCdcService::device_scan_task(void *arg) {
    TinyUsbCdcService* self = static_cast<TinyUsbCdcService*>(arg);
    ESP_LOGI(TAG, "Device scan task started.");

    while (!self->_scan_task_should_stop) {  // 检查停止标志
        if (!_s_is_device_connected) {
            // 尝试连接到常见的USB-to-Serial设备 (CH340, FT232, CP210x等)
            const uint16_t common_vid_pid[][2] = {
                // CH340系列 (QinHeng Electronics) - 最常见的廉价USB转串口
                {0x1A86, 0x7523}, // CH340 - 最常见
                {0x1A86, 0x7522}, // CH340K, CH340E
                {0x1A86, 0x7584}, // CH340B
                {0x1A86, 0x5523}, // CH341A
                
                // FTDI系列 - 工业级标准
                {0x0403, 0x6001}, // FT232R - 常见
                {0x0403, 0x6010}, // FT2232H - 双通道
                {0x0403, 0x6011}, // FT4232H - 四通道
                {0x0403, 0x6014}, // FT232H - 高速
                {0x0403, 0x6015}, // FT X-Series
                
                // Silicon Labs CP210x系列 - 支持标准CDC
                {0x10C4, 0xEA60}, // CP210x - 最常见
                {0x10C4, 0xEA70}, // CP210x变体
                {0x10C4, 0xEA71}, // CP210x变体
                
                // Prolific PL2303系列 - 老牌厂商
                {0x067B, 0x2303}, // PL2303 - 常见
                {0x067B, 0x2304}, // PL2303HX
                
                // 其他常见厂商
                {0x2341, 0x0043}, // Arduino Uno R3
                {0x16C0, 0x0483}, // Teensyduino Serial
                {0x239A, 0x800B}, // Adafruit Feather 32u4
            };
            
            bool connected = false;
            for (int i = 0; i < sizeof(common_vid_pid) / sizeof(common_vid_pid[0]) && !self->_scan_task_should_stop; i++) {
                ESP_LOGD(TAG, "Trying to connect to VID:0x%04X PID:0x%04X", 
                         common_vid_pid[i][0], common_vid_pid[i][1]);
                
                const cdc_acm_host_device_config_t dev_config = {
                    .connection_timeout_ms = 1000,  // 减少超时时间，从3秒减到1秒
                    .out_buffer_size = 512,
                    .in_buffer_size = 512,
                    .event_cb = self->device_event_callback,
                    .data_cb = self->data_received_callback,
                    .user_arg = self
                };

                // 尝试打开设备
                if (cdc_acm_host_open(common_vid_pid[i][0], common_vid_pid[i][1], 0, 
                                     &dev_config, &self->_s_cdc_device_handle) == ESP_OK) {
                    ESP_LOGI(TAG, "Successfully opened CDC device VID:0x%04X PID:0x%04X", 
                             common_vid_pid[i][0], common_vid_pid[i][1]);
                    
                    // 记录设备信息并检测设备类型
                    _s_device_vid = common_vid_pid[i][0];
                    _s_device_pid = common_vid_pid[i][1];
                    self->_current_device_type = self->detectDeviceType(_s_device_vid, _s_device_pid);
                    
                    ESP_LOGI(TAG, "Device type detected: %s", self->getDeviceTypeName());
                    
                    _s_is_device_connected = true;
                    connected = true;
                    
                    // 等待设备稳定，但检查停止标志
                    for (int j = 0; j < 10 && !self->_scan_task_should_stop; j++) {
                        vTaskDelay(pdMS_TO_TICKS(100));
                    }
                    
                    if (self->_scan_task_should_stop) break;  // 如果要求停止，立即退出
                    
                    // 设备连接成功，等待设备稳定
                    ESP_LOGI(TAG, "Device connected successfully, waiting for stability...");
                    for (int j = 0; j < 10 && !self->_scan_task_should_stop; j++) {
                        vTaskDelay(pdMS_TO_TICKS(100));
                    }
                    
                    if (!self->_scan_task_should_stop) {
                        // 根据心跳包启用状态决定是否启动心跳
                        if (self->_heartbeat_enabled) {
                            self->startHeartbeat();
                            ESP_LOGI(TAG, "Heartbeat started (enabled by switch)");
                        } else {
                            ESP_LOGI(TAG, "Heartbeat not started (disabled by switch)");
                        }
                    }
                    
                    break;
                }
            }
            
            if (!connected) {
                ESP_LOGD(TAG, "No CDC devices found, retrying...");
            }
        }
        
        // 分段延时，增加停止检查的频率
        for (int i = 0; i < 20 && !self->_scan_task_should_stop; i++) {
            vTaskDelay(pdMS_TO_TICKS(100)); // 总共2秒，但每100ms检查一次停止标志
        }
    }
    
    // 任务退出清理
    ESP_LOGI(TAG, "Device scan task exiting...");
    self->_scan_task_handle = nullptr;  // 清除任务句柄
    vTaskDelete(NULL);  // 删除自己
}

// [新增] 心跳任务 - 定期发送测试数据（优化版本）
void TinyUsbCdcService::heartbeat_task(void* arg) {
    TinyUsbCdcService* self = static_cast<TinyUsbCdcService*>(arg);
    ESP_LOGI(TAG, "Heartbeat task started (optimized)");
    
    uint32_t counter = 0;
    // 删除port_configured标志，避免重复配置
    
    // 预分配心跳消息缓冲区，避免栈上频繁分配
    char* heartbeat_msg = (char*)malloc(128);
    if (!heartbeat_msg) {
        ESP_LOGE(TAG, "Failed to allocate heartbeat message buffer");
        self->_heartbeat_task_handle = nullptr;
        vTaskDelete(NULL);
        return;
    }
    
    while (!self->_heartbeat_task_should_stop) {  // 检查停止标志
        if (_s_is_device_connected && _s_cdc_device_handle) {
            // 构造心跳消息（使用预分配的缓冲区）
            int msg_len = snprintf(heartbeat_msg, 128, 
                    "ESP32P4 Heartbeat #%lu - Time: %lu ms\r\n", 
                    counter++, xTaskGetTickCount() * portTICK_PERIOD_MS);
            
            if (msg_len > 0 && msg_len < 128) {
                // 发送心跳消息
                esp_err_t ret = cdc_acm_host_data_tx_blocking(_s_cdc_device_handle, 
                                                             (const uint8_t*)heartbeat_msg, 
                                                             msg_len, 
                                                             1000);
                
                if (ret == ESP_OK) {
                    ESP_LOGD(TAG, "Heartbeat #%lu sent successfully", counter - 1);
                } else {
                    ESP_LOGW(TAG, "Failed to send heartbeat: %s", esp_err_to_name(ret));
                }
            }
        } else {
            ESP_LOGD(TAG, "Device not connected, skipping heartbeat");
        }
        
        // 每3秒发送一次心跳，但分段检查停止标志
        for (int i = 0; i < 30 && !self->_heartbeat_task_should_stop; i++) {
            vTaskDelay(pdMS_TO_TICKS(100)); // 总共3秒，但每100ms检查一次停止标志
        }
    }
    
    // 任务退出清理
    free(heartbeat_msg);  // 释放内存
    ESP_LOGI(TAG, "Heartbeat task exiting...");
    self->_heartbeat_task_handle = nullptr;  // 清除任务句柄
    vTaskDelete(NULL);  // 删除自己
}


// 设备事件回调（增强版）
void TinyUsbCdcService::device_event_callback(const cdc_acm_host_dev_event_data_t *event, void *user_ctx) {
    TinyUsbCdcService* self = static_cast<TinyUsbCdcService*>(user_ctx);
    
    switch (event->type) {
        case CDC_ACM_HOST_DEVICE_DISCONNECTED:
            ESP_LOGI(TAG, "CDC device disconnected (VID:0x%04X PID:0x%04X)", _s_device_vid, _s_device_pid);
            if (event->data.cdc_hdl == _s_cdc_device_handle) {
                // 安全停止心跳任务
                if (self) {
                    ESP_LOGI(TAG, "Stopping heartbeat due to device disconnection...");
                    self->stopHeartbeat();
                }
                
                // 关闭设备句柄
                if (_s_cdc_device_handle) {
                    cdc_acm_host_close(_s_cdc_device_handle);
                }
                
                // 清理设备状态
                _s_cdc_device_handle = nullptr;
                _s_is_device_connected = false;
                _s_device_vid = 0;
                _s_device_pid = 0;
                
                ESP_LOGI(TAG, "Device cleanup completed");
            }
            break;
        case CDC_ACM_HOST_ERROR:
            ESP_LOGE(TAG, "CDC ACM error: %d (device may be unstable)", event->data.error);
            if (self) {
                ESP_LOGW(TAG, "Stopping heartbeat due to CDC error...");
                self->stopHeartbeat();
            }
            _s_is_device_connected = false;
            break;
        case CDC_ACM_HOST_SERIAL_STATE:
            ESP_LOGD(TAG, "CDC ACM serial state changed");
            break;
        case CDC_ACM_HOST_NETWORK_CONNECTION:
            ESP_LOGD(TAG, "CDC ACM network connection: %s", 
                     event->data.network_connected ? "connected" : "disconnected");
            break;
        default:
            ESP_LOGD(TAG, "Unknown CDC event type: %d", event->type);
            break;
    }
}

// 数据接收回调
bool TinyUsbCdcService::data_received_callback(const uint8_t *data, size_t data_len, void *user_ctx) {
    if (_s_rx_ring_buffer && data && data_len > 0) {
        xRingbufferSend(_s_rx_ring_buffer, data, data_len, (TickType_t)0);
    }
    return true;
}

size_t TinyUsbCdcService::read(uint8_t *buffer, size_t max_len) {
    if (!_s_rx_ring_buffer || max_len == 0) return 0;
    size_t item_size = 0;
    uint8_t *item = (uint8_t*)xRingbufferReceive(_s_rx_ring_buffer, &item_size, (TickType_t)0);
    if (item) {
        size_t copy_len = (max_len < item_size) ? max_len : item_size;
        memcpy(buffer, item, copy_len);
        vRingbufferReturnItem(_s_rx_ring_buffer, (void*)item);
        return copy_len;
    }
    return 0;
}

size_t TinyUsbCdcService::available() {
    if (!_s_rx_ring_buffer) return 0;
    
    UBaseType_t items_waiting = 0;
    size_t free_bytes = 0, max_item_size = 0;
    
    // 获取ring buffer详细信息
    vRingbufferGetInfo(_s_rx_ring_buffer, NULL, &free_bytes, NULL, NULL, &items_waiting);
    
    if (items_waiting == 0) {
        return 0;
    }
    
    // 估算可用数据大小（ring buffer总大小 - 空闲字节）
    size_t estimated_data = RX_RING_BUFFER_SIZE - free_bytes;
    
    // 返回估算的可用字节数，但至少返回1表示有数据
    return (estimated_data > 0) ? estimated_data : 1;
}

void TinyUsbCdcService::write(const uint8_t *data, size_t len) {
    if (_s_is_device_connected && _s_cdc_device_handle && len > 0) {
        cdc_acm_host_data_tx_blocking(_s_cdc_device_handle, data, len, 100);
    }
}

bool TinyUsbCdcService::isConnected() { 
    return _s_is_device_connected; 
}

// [新增] 设备类型检测
TinyUsbCdcService::UsbDeviceType TinyUsbCdcService::detectDeviceType(uint16_t vid, uint16_t pid) {
    // CH340/CH341系列芯片 - 这些芯片不支持标准CDC控制请求
    if (vid == 0x1A86) {
        switch (pid) {
            case 0x7523: // CH340
            case 0x7522: // CH340K, CH340E  
            case 0x7584: // CH340B
            case 0x5523: // CH341A
                return DEVICE_TYPE_CH340;
        }
    }
    
    // FTDI系列 - 部分支持CDC，但有自己的控制方式
    if (vid == 0x0403) {
        switch (pid) {
            case 0x6001: // FT232R
            case 0x6010: // FT2232H
            case 0x6011: // FT4232H
            case 0x6014: // FT232H
            case 0x6015: // FT X-Series
                return DEVICE_TYPE_FT232;
        }
    }
    
    // Silicon Labs CP210x系列 - 支持标准CDC
    if (vid == 0x10C4) {
        switch (pid) {
            case 0xEA60: // CP210x
            case 0xEA70: // CP210x
            case 0xEA71: // CP210x
                return DEVICE_TYPE_CP210X;
        }
    }
    
    // Prolific PL2303系列 - 支持标准CDC
    if (vid == 0x067B) {
        switch (pid) {
            case 0x2303: // PL2303
            case 0x2304: // PL2303HX
                return DEVICE_TYPE_PL2303;
        }
    }
    
    // [新增] 更多常见设备支持
    // Arduino系列和其他CDC设备
    if (vid == 0x2341) { // Arduino LLC
        return DEVICE_TYPE_CDC_STANDARD;
    }
    
    // STMicroelectronics CDC设备
    if (vid == 0x0483) {
        return DEVICE_TYPE_CDC_STANDARD;
    }
    
    // Atmel/Microchip CDC设备
    if (vid == 0x03EB || vid == 0x04D8) {
        return DEVICE_TYPE_CDC_STANDARD;
    }
    
    ESP_LOGI(TAG, "Unknown device VID:PID = %04X:%04X, trying standard CDC", vid, pid);
    
    // 其他设备假设为标准CDC
    return DEVICE_TYPE_CDC_STANDARD;
}

const char* TinyUsbCdcService::getDeviceTypeName() const {
    switch (_current_device_type) {
        case DEVICE_TYPE_CH340: return "CH340/CH341 (Non-standard)";
        case DEVICE_TYPE_FT232: return "FTDI FT232 (Proprietary)";
        case DEVICE_TYPE_CP210X: return "Silicon Labs CP210x (Standard CDC)";
        case DEVICE_TYPE_PL2303: return "Prolific PL2303 (Standard CDC)";
        case DEVICE_TYPE_CDC_STANDARD: return "Standard CDC Device";
        default: return "Unknown Device";
    }
}

void TinyUsbCdcService::configureDeviceSpecific() {
    ESP_LOGI(TAG, "Applying saved configuration: %lu %d%s%d", 
             _current_config.baud_rate, _current_config.data_bits,
             (_current_config.parity == 0) ? "N" : (_current_config.parity == 1) ? "O" : "E",
             _current_config.stop_bits + 1);
             
    switch (_current_device_type) {
        case DEVICE_TYPE_CH340:
            ESP_LOGI(TAG, "CH340 device detected - attempting vendor-specific configuration");
            // CH340需要特殊的vendor控制请求来配置串口参数
            configureCH340SerialPort(_current_config.baud_rate);
            break;
            
        case DEVICE_TYPE_FT232:
            ESP_LOGI(TAG, "FTDI device detected - attempting basic configuration");
            // FTDI设备有自己的控制方式，这里只做基本尝试
            configureSerialPort(_current_config);
            break;
            
        case DEVICE_TYPE_CP210X:
        case DEVICE_TYPE_PL2303:
        case DEVICE_TYPE_CDC_STANDARD:
            ESP_LOGI(TAG, "Standard CDC device detected - full configuration");
            configureSerialPort(_current_config);
            break;
            
        default:
            ESP_LOGW(TAG, "Unknown device type - attempting standard configuration");
            configureSerialPort(115200);
            break;
    }
}

// CH340专用配置方法
void TinyUsbCdcService::configureCH340SerialPort(uint32_t baud_rate) {
    if (!_s_is_device_connected || !_s_cdc_device_handle) {
        ESP_LOGW(TAG, "No device connected for CH340 configuration");
        return;
    }
    
    ESP_LOGI(TAG, "Configuring CH340 device for %lu baud", baud_rate);
    
    // CH340 vendor-specific requests for serial configuration
    // 基于CH340的驱动程序分析，以下是常用的控制请求
    
    // 1. 设置波特率 - CH340使用vendor-specific request
    // Request 0x9A: Set baud rate
    uint16_t baud_reg = 0;
    switch (baud_rate) {
        case 2400:   baud_reg = 0xD901; break;
        case 4800:   baud_reg = 0x6402; break;
        case 9600:   baud_reg = 0xB202; break;
        case 19200:  baud_reg = 0xD902; break;
        case 38400:  baud_reg = 0x6403; break;
        case 57600:  baud_reg = 0x9803; break;
        case 115200: baud_reg = 0xCC03; break;
        case 230400: baud_reg = 0xE603; break;
        case 460800: baud_reg = 0xF303; break;
        case 921600: baud_reg = 0xF904; break;
        default:     baud_reg = 0xCC03; break; // 默认115200
    }
    
    esp_err_t ret = cdc_acm_host_send_custom_request(
        _s_cdc_device_handle,
        0x40,                // bmRequestType: Host-to-device, Vendor, Device
        0x9A,                // bRequest: CH340 set baud rate
        0x1312,              // wValue: 固定值
        baud_reg,            // wIndex: 波特率寄存器值
        0,                   // wLength: 0
        nullptr              // data: none
    );
    
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "CH340 baud rate set to %lu", baud_rate);
    } else {
        ESP_LOGW(TAG, "CH340 baud rate setting failed: %s", esp_err_to_name(ret));
    }
    
    // 2. 设置数据格式 - 8N1
    // Request 0x9B: Set data format  
    ret = cdc_acm_host_send_custom_request(
        _s_cdc_device_handle,
        0x40,                // bmRequestType: Host-to-device, Vendor, Device
        0x9B,                // bRequest: CH340 set data format
        0x0008,              // wValue: 8 data bits, no parity, 1 stop bit
        0x0000,              // wIndex: 0
        0,                   // wLength: 0
        nullptr              // data: none
    );
    
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "CH340 data format set to 8N1");
    } else {
        ESP_LOGW(TAG, "CH340 data format setting failed: %s", esp_err_to_name(ret));
    }
    
    // 3. 启用DTR和RTS
    // Request 0xA4: Set control line state
    ret = cdc_acm_host_send_custom_request(
        _s_cdc_device_handle,
        0x40,                // bmRequestType: Host-to-device, Vendor, Device
        0xA4,                // bRequest: CH340 set control lines
        0xDF20,              // wValue: DTR=1, RTS=1
        0x0000,              // wIndex: 0
        0,                   // wLength: 0
        nullptr              // data: none
    );
    
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "CH340 control lines configured (DTR=1, RTS=1)");
    } else {
        ESP_LOGW(TAG, "CH340 control lines setting failed: %s", esp_err_to_name(ret));
    }
    
    ESP_LOGI(TAG, "CH340 configuration completed");
}

// [新增] 设置当前配置
void TinyUsbCdcService::setCurrentConfig(const SerialConfig& config) {
    _current_config = config;
    ESP_LOGI(TAG, "Configuration saved: %lu %d%s%d", 
             config.baud_rate, config.data_bits,
             (config.parity == 0) ? "N" : (config.parity == 1) ? "O" : "E",
             config.stop_bits + 1);
}

// [新增] 获取当前配置
TinyUsbCdcService::SerialConfig TinyUsbCdcService::getCurrentConfig() const {
    return _current_config;
}

// [新增] 设置心跳包启用状态
void TinyUsbCdcService::setHeartbeatEnabled(bool enabled) {
    _heartbeat_enabled = enabled;
    ESP_LOGI(TAG, "Heartbeat enabled state set to: %s", enabled ? "true" : "false");
}

// [新增] 获取心跳包启用状态
bool TinyUsbCdcService::isHeartbeatEnabled() const {
    return _heartbeat_enabled;
}