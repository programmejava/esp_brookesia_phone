/**
 * @file ModbusController.cpp
 * @brief XY6506S电源Modbus-RTU通信控制器实现文件
 * @details 实现与XY6506S电源设备的Modbus-RTU协议通信
 * @author ESP32开发团队
 * @date 2025年11月4日
 * @version 1.0
 */

#include "ModbusController.hpp"
#include "esp_timer.h"
#include <string.h>

const char* ModbusController::TAG = "ModbusController";

ModbusController::ModbusController() 
    : modbus_mutex(nullptr), last_communication_ms(0), is_initialized(false) {
    memset(&device_data, 0, sizeof(device_data));
}

ModbusController::~ModbusController() {
    deinitialize();
}

bool ModbusController::initialize() {
    if (is_initialized) {
        ESP_LOGW(TAG, "Already initialized");
        return true;
    }
    
    // 创建互斥锁
    modbus_mutex = xSemaphoreCreateMutex();
    if (modbus_mutex == nullptr) {
        ESP_LOGE(TAG, "Failed to create mutex");
        return false;
    }
    
    // 配置UART
    uart_config_t uart_config = {
        .baud_rate = UART_BAUD_RATE,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .rx_flow_ctrl_thresh = 122,
    };
    
    // 安装UART驱动
    esp_err_t err = uart_driver_install(UART_PORT, UART_BUF_SIZE, UART_BUF_SIZE, 0, NULL, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to install UART driver: %s", esp_err_to_name(err));
        vSemaphoreDelete(modbus_mutex);
        return false;
    }
    
    // 配置UART参数
    err = uart_param_config(UART_PORT, &uart_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to config UART: %s", esp_err_to_name(err));
        uart_driver_delete(UART_PORT);
        vSemaphoreDelete(modbus_mutex);
        return false;
    }
    
    // 设置UART引脚
    err = uart_set_pin(UART_PORT, UART_TX_PIN, UART_RX_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set UART pins: %s", esp_err_to_name(err));
        uart_driver_delete(UART_PORT);
        vSemaphoreDelete(modbus_mutex);
        return false;
    }
    
    is_initialized = true;
    ESP_LOGI(TAG, "Modbus controller initialized successfully");
    ESP_LOGI(TAG, "UART Port: %d, TX: GPIO%d, RX: GPIO%d, Baud: %d", 
             UART_PORT, UART_TX_PIN, UART_RX_PIN, UART_BAUD_RATE);
    
    return true;
}

void ModbusController::deinitialize() {
    if (!is_initialized) {
        return;
    }
    
    uart_driver_delete(UART_PORT);
    
    if (modbus_mutex != nullptr) {
        vSemaphoreDelete(modbus_mutex);
        modbus_mutex = nullptr;
    }
    
    is_initialized = false;
    ESP_LOGI(TAG, "Modbus controller deinitialized");
}

uint16_t ModbusController::calculateCRC(const uint8_t* data, size_t length) {
    uint16_t crc = 0xFFFF;
    
    for (size_t i = 0; i < length; i++) {
        crc ^= data[i];
        for (int j = 0; j < 8; j++) {
            if (crc & 0x0001) {
                crc = (crc >> 1) ^ 0xA001;
            } else {
                crc = crc >> 1;
            }
        }
    }
    
    return crc;
}

void ModbusController::ensureFrameInterval() {
    uint32_t current_ms = esp_timer_get_time() / 1000;
    uint32_t elapsed_ms = current_ms - last_communication_ms;
    
    if (elapsed_ms < MIN_FRAME_INTERVAL_MS) {
        uint32_t wait_ms = MIN_FRAME_INTERVAL_MS - elapsed_ms;
        vTaskDelay(pdMS_TO_TICKS(wait_ms));
    }
}

bool ModbusController::sendModbusFrame(const uint8_t* frame, size_t length) {
    if (!is_initialized) {
        ESP_LOGE(TAG, "Not initialized");
        return false;
    }
    
    ensureFrameInterval();
    
    // 清空接收缓冲区
    uart_flush_input(UART_PORT);
    
    // 记录发送的帧（用于调试）
    ESP_LOGD(TAG, "📤 Sending Modbus frame (%d bytes): %02X %02X %02X %02X %02X %02X %02X %02X", 
             (int)length, frame[0], frame[1], frame[2], frame[3], 
             length > 4 ? frame[4] : 0, length > 5 ? frame[5] : 0,
             length > 6 ? frame[6] : 0, length > 7 ? frame[7] : 0);
    
    // 发送数据
    int written = uart_write_bytes(UART_PORT, frame, length);
    if (written != length) {
        ESP_LOGE(TAG, "Failed to write complete frame, written: %d, expected: %d", written, (int)length);
        return false;
    }
    
    // 等待发送完成
    esp_err_t err = uart_wait_tx_done(UART_PORT, pdMS_TO_TICKS(100));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to wait for TX done: %s", esp_err_to_name(err));
        return false;
    }
    
    last_communication_ms = esp_timer_get_time() / 1000;
    return true;
}

bool ModbusController::receiveModbusFrame(uint8_t* frame, size_t* length, uint32_t timeout_ms) {
    if (!is_initialized) {
        ESP_LOGE(TAG, "Not initialized");
        return false;
    }
    
    size_t received = 0;
    uint32_t start_time = esp_timer_get_time() / 1000;
    size_t expected_length = 0;  // 动态确定期望的响应长度
    bool header_received = false;
    
    while (true) {
        uint32_t current_time = esp_timer_get_time() / 1000;
        if (current_time - start_time > timeout_ms) {
            if (received > 0) {
                ESP_LOGD(TAG, "📥 Received %d bytes before timeout: %02X %02X %02X %02X %02X %02X %02X %02X %02X", 
                         (int)received,
                         received > 0 ? frame[0] : 0, received > 1 ? frame[1] : 0,
                         received > 2 ? frame[2] : 0, received > 3 ? frame[3] : 0,
                         received > 4 ? frame[4] : 0, received > 5 ? frame[5] : 0,
                         received > 6 ? frame[6] : 0, received > 7 ? frame[7] : 0,
                         received > 8 ? frame[8] : 0);
                *length = received;
                return true;  // 返回已接收的数据
            } else {
                ESP_LOGW(TAG, "Receive timeout, no data received");
                break;
            }
        }
        
        int available = 0;
        esp_err_t err = uart_get_buffered_data_len(UART_PORT, (size_t*)&available);
        if (err != ESP_OK || available == 0) {
            vTaskDelay(pdMS_TO_TICKS(1));
            continue;
        }
        
        // 读取可用数据，但不超过缓冲区大小
        size_t max_to_read = *length - received;
        size_t to_read = (max_to_read < available) ? max_to_read : available;
        
        int read_bytes = uart_read_bytes(UART_PORT, frame + received, to_read, pdMS_TO_TICKS(10));
        
        if (read_bytes > 0) {
            received += read_bytes;
            
            // 如果我们有足够的头部信息，计算期望的总长度
            if (!header_received && received >= 3) {
                if (frame[1] == 0x03) {  // 读保持寄存器响应
                    expected_length = 3 + frame[2] + 2;  // 地址+功能码+长度字节+数据+CRC
                    header_received = true;
                    ESP_LOGD(TAG, "📏 Expected response length: %d bytes", (int)expected_length);
                }
            }
            
            // 如果我们知道期望长度并且已经收到足够数据，就完成
            if (header_received && received >= expected_length) {
                ESP_LOGD(TAG, "📥 Complete frame received: %d bytes", (int)received);
                break;
            }
        }
        
        // 防止无限循环
        if (received >= *length) {
            break;
        }
    }
    
    *length = received;
    return received > 0;
}

bool ModbusController::readHoldingRegisters(uint16_t start_addr, uint16_t count, uint16_t* data) {
    if (!is_initialized) {
        ESP_LOGE(TAG, "Not initialized");
        return false;
    }
    
    // 快速互斥锁等待时间，避免异步任务阻塞
    if (xSemaphoreTake(modbus_mutex, pdMS_TO_TICKS(50)) != pdTRUE) {
        ESP_LOGD(TAG, "Mutex busy, skipping this read cycle");
        return false;
    }
    
    bool success = false;
    
    // 构建Modbus RTU请求帧
    uint8_t request[8];
    request[0] = DEVICE_ADDRESS;        // 设备地址
    request[1] = 0x03;                  // 功能码：读保持寄存器
    request[2] = (start_addr >> 8) & 0xFF;  // 起始地址高字节
    request[3] = start_addr & 0xFF;         // 起始地址低字节
    request[4] = (count >> 8) & 0xFF;       // 寄存器数量高字节
    request[5] = count & 0xFF;              // 寄存器数量低字节
    
    // 计算并添加CRC
    uint16_t crc = calculateCRC(request, 6);
    request[6] = crc & 0xFF;            // CRC低字节
    request[7] = (crc >> 8) & 0xFF;     // CRC高字节
    
    // 发送请求
    if (sendModbusFrame(request, 8)) {
        // 接收响应
        uint8_t response[256];
        size_t response_len = sizeof(response);
        
        if (receiveModbusFrame(response, &response_len, RESPONSE_TIMEOUT_MS)) {
            // 验证响应
            if (response_len >= 5 && 
                response[0] == DEVICE_ADDRESS && 
                response[1] == 0x03 && 
                response[2] == count * 2) {
                
                // 验证CRC
                uint16_t received_crc = (response[response_len - 1] << 8) | response[response_len - 2];
                uint16_t calculated_crc = calculateCRC(response, response_len - 2);
                
                if (received_crc == calculated_crc) {
                    // 提取数据
                    for (int i = 0; i < count; i++) {
                        data[i] = (response[3 + i * 2] << 8) | response[4 + i * 2];
                    }
                    success = true;
                } else {
                    ESP_LOGE(TAG, "CRC mismatch in response");
                }
            } else {
                ESP_LOGE(TAG, "Invalid response format");
            }
        } else {
            ESP_LOGE(TAG, "No response received");
        }
    }
    
    xSemaphoreGive(modbus_mutex);
    return success;
}

bool ModbusController::writeSingleRegister(uint16_t addr, uint16_t value) {
    if (!is_initialized) {
        ESP_LOGE(TAG, "Not initialized");
        return false;
    }
    
    // 快速互斥锁等待时间，避免异步任务阻塞
    if (xSemaphoreTake(modbus_mutex, pdMS_TO_TICKS(50)) != pdTRUE) {
        ESP_LOGD(TAG, "Mutex busy, skipping this write cycle");
        return false;
    }
    
    bool success = false;
    
    // 构建Modbus RTU请求帧
    uint8_t request[8];
    request[0] = DEVICE_ADDRESS;        // 设备地址
    request[1] = 0x06;                  // 功能码：写单个寄存器
    request[2] = (addr >> 8) & 0xFF;    // 寄存器地址高字节
    request[3] = addr & 0xFF;           // 寄存器地址低字节
    request[4] = (value >> 8) & 0xFF;   // 寄存器值高字节
    request[5] = value & 0xFF;          // 寄存器值低字节
    
    // 计算并添加CRC
    uint16_t crc = calculateCRC(request, 6);
    request[6] = crc & 0xFF;            // CRC低字节
    request[7] = (crc >> 8) & 0xFF;     // CRC高字节
    
    // 发送请求
    if (sendModbusFrame(request, 8)) {
        // 接收响应
        uint8_t response[8];
        size_t response_len = sizeof(response);
        
        if (receiveModbusFrame(response, &response_len, RESPONSE_TIMEOUT_MS)) {
            // 验证响应（写单个寄存器的响应应该是请求的回显）
            if (response_len == 8 && memcmp(request, response, 8) == 0) {
                success = true;
            } else {
                ESP_LOGE(TAG, "Invalid write response");
            }
        } else {
            ESP_LOGE(TAG, "No write response received");
        }
    }
    
    xSemaphoreGive(modbus_mutex);
    return success;
}

bool ModbusController::readAllDeviceData() {
    // 读取所有测量值寄存器 (0x0000-0x0005)
    uint16_t reg_data[6];
    if (!readHoldingRegisters(REG_V_SET, 6, reg_data)) {
        ESP_LOGE(TAG, "Failed to read measurement registers");
        device_data.data_valid = false;
        return false;
    }
    
    // 转换测量值 (根据XY6506S手册寄存器映射)
    device_data.set_voltage = reg_data[0] / 100.0f;     // 0x0000: V-SET
    device_data.set_current = reg_data[1] / 1000.0f;    // 0x0001: I-SET  
    device_data.output_voltage = reg_data[2] / 100.0f;  // 0x0002: VOUT
    device_data.output_current = reg_data[3] / 1000.0f; // 0x0003: IOUT
    device_data.output_power = reg_data[4] / 100.0f;    // 0x0004: POWER
    device_data.input_voltage = reg_data[5] / 100.0f;   // 0x0005: UIN
    
    // 读取控制状态寄存器
    uint16_t key_lock, sleep_mode, output_switch, beep_switch;
    
    bool all_ok = true;
    all_ok &= readHoldingRegisters(REG_LOCK, 1, &key_lock);
    all_ok &= readHoldingRegisters(REG_SLEEP, 1, &sleep_mode);
    all_ok &= readHoldingRegisters(REG_ONOFF, 1, &output_switch);
    all_ok &= readHoldingRegisters(REG_BUZZER, 1, &beep_switch);
    
    if (all_ok) {
        device_data.key_lock = (key_lock != 0);
        device_data.sleep_mode = (sleep_mode != 0);
        device_data.output_switch = (output_switch != 0);
        device_data.beep_switch = (beep_switch != 0);
        
        device_data.data_valid = true;
        device_data.last_update_ms = esp_timer_get_time() / 1000;
        
        ESP_LOGD(TAG, "📊 Device data: V=%.2fV, I=%.3fA, P=%.2fW, Vin=%.2fV, Vset=%.2fV, Iset=%.3fA", 
                 device_data.output_voltage, device_data.output_current, device_data.output_power,
                 device_data.input_voltage, device_data.set_voltage, device_data.set_current);
        
        ESP_LOGD(TAG, "🎛️ Switch states from device: Power=%s, Beep=%s, KeyLock=%s, Sleep=%s",
                 device_data.output_switch ? "ON" : "OFF",
                 device_data.beep_switch ? "ON" : "OFF", 
                 device_data.key_lock ? "LOCKED" : "UNLOCKED",
                 device_data.sleep_mode ? "ON" : "OFF");
        
        return true;
    } else {
        ESP_LOGE(TAG, "Failed to read control registers");
        device_data.data_valid = false;
        return false;
    }
}

bool ModbusController::setVoltageAndCurrent(float voltage, float current) {
    if (!validateVoltage(voltage) || !validateCurrent(current)) {
        ESP_LOGE(TAG, "Invalid voltage (%.2fV) or current (%.3fA) value", voltage, current);
        return false;
    }
    
    // 转换为寄存器值
    uint16_t voltage_reg = (uint16_t)(voltage * 100);
    uint16_t current_reg = (uint16_t)(current * 1000);
    
    // 先写电压，再写电流
    bool success = writeSingleRegister(REG_V_SET, voltage_reg);
    if (success) {
        success = writeSingleRegister(REG_I_SET, current_reg);
    }
    
    if (success) {
        ESP_LOGI(TAG, "Set voltage: %.2fV, current: %.3fA", voltage, current);
    } else {
        ESP_LOGE(TAG, "Failed to set voltage/current");
    }
    
    return success;
}

bool ModbusController::setOutputSwitch(bool enable) {
    uint16_t value = enable ? 1 : 0;
    bool success = writeSingleRegister(REG_ONOFF, value);
    
    if (success) {
        ESP_LOGI(TAG, "Set output switch: %s", enable ? "ON" : "OFF");
    }
    
    return success;
}

bool ModbusController::setBeepSwitch(bool enable) {
    uint16_t value = enable ? 1 : 0;
    bool success = writeSingleRegister(REG_BUZZER, value);
    
    if (success) {
        ESP_LOGI(TAG, "Set beep switch: %s", enable ? "ON" : "OFF");
    }
    
    return success;
}

bool ModbusController::setKeyLock(bool enable) {
    uint16_t value = enable ? 1 : 0;
    bool success = writeSingleRegister(REG_LOCK, value);
    
    if (success) {
        ESP_LOGI(TAG, "Set key lock: %s", enable ? "LOCKED" : "UNLOCKED");
    }
    
    return success;
}

bool ModbusController::setSleepMode(bool enable) {
    uint16_t value = enable ? 1 : 0;
    bool success = writeSingleRegister(REG_SLEEP, value);
    
    if (success) {
        ESP_LOGI(TAG, "Set sleep mode: %s", enable ? "SLEEP" : "NORMAL");
    }
    
    return success;
}

bool ModbusController::validateVoltage(float voltage) const {
    return (voltage >= 0.0f && voltage <= device_data.input_voltage && device_data.input_voltage > 0.0f);
}

bool ModbusController::validateCurrent(float current) const {
    return (current >= 0.0f && current <= 9.1f);
}

bool ModbusController::isCommunicationOk() const {
    uint32_t current_ms = esp_timer_get_time() / 1000;
    return device_data.data_valid && (current_ms - device_data.last_update_ms < 5000);
}

bool ModbusController::scanForDevices() {
    ESP_LOGI(TAG, "🔍 Scanning for Modbus devices...");
    
    if (!is_initialized) {
        ESP_LOGE(TAG, "Not initialized");
        return false;
    }
    
    bool found = false;
    
    // 扫描常见的设备地址 1-10
    for (uint8_t addr = 1; addr <= 10; addr++) {
        ESP_LOGI(TAG, "Trying device address: 0x%02X (%d)", addr, addr);
        
        // 构建测试请求 - 读取寄存器0x0000
        uint8_t request[8];
        request[0] = addr;                      // 设备地址
        request[1] = 0x03;                      // 功能码：读保持寄存器
        request[2] = 0x00; request[3] = 0x00;   // 起始地址0x0000
        request[4] = 0x00; request[5] = 0x01;   // 读取1个寄存器
        
        uint16_t crc = calculateCRC(request, 6);
        request[6] = crc & 0xFF;
        request[7] = (crc >> 8) & 0xFF;
        
        if (sendModbusFrame(request, 8)) {
            uint8_t response[256];
            size_t response_len = sizeof(response);
            
            if (receiveModbusFrame(response, &response_len, 300)) {  // 300ms超时
                if (response_len >= 5 && response[0] == addr && response[1] == 0x03) {
                    uint16_t reg_value = (response[3] << 8) | response[4];
                    ESP_LOGI(TAG, "✅ Device found at address %d (0x%02X), register 0x0000 = 0x%04X (%d)", 
                             addr, addr, reg_value, reg_value);
                    found = true;
                } else {
                    ESP_LOGD(TAG, "Invalid response format from address %d", addr);
                }
            } else {
                ESP_LOGD(TAG, "❌ No response from address 0x%02X (%d)", addr, addr);
            }
        }
        
        // 等待一小段时间再试下一个地址
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    
    if (found) {
        ESP_LOGI(TAG, "🎉 Device scan completed - found one or more devices");
    } else {
        ESP_LOGW(TAG, "⚠️ Device scan completed - no devices found");
    }
    
    return found;
}