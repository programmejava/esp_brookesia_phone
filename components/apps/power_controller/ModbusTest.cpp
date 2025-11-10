/**
 * @file ModbusTest.cpp
 * @brief Modbus通信测试工具实现
 */

#include "ModbusTest.hpp"
#include "esp_timer.h"
#include <string.h>

const char* ModbusTest::TAG = "ModbusTest";

ModbusTest::ModbusTest() : controller(nullptr) {}

ModbusTest::~ModbusTest() {
    if (controller) {
        delete controller;
    }
}

bool ModbusTest::init() {
    controller = new ModbusController();
    if (!controller) {
        ESP_LOGE(TAG, "Failed to create ModbusController");
        return false;
    }
    
    return controller->initialize();
}

bool ModbusTest::testUARTConnection() {
    ESP_LOGI(TAG, "=== UART连接测试 ===");
    
    if (!controller) {
        ESP_LOGE(TAG, "Controller not initialized");
        return false;
    }
    
    // 发送简单的测试字节
    const uint8_t test_data[] = {0x01, 0x02, 0x03, 0x04};
    uart_port_t uart_port = UART_NUM_1;
    
    ESP_LOGI(TAG, "发送测试数据...");
    int written = uart_write_bytes(uart_port, test_data, sizeof(test_data));
    ESP_LOGI(TAG, "写入字节数: %d / %d", written, (int)sizeof(test_data));
    
    // 等待发送完成
    esp_err_t err = uart_wait_tx_done(uart_port, pdMS_TO_TICKS(100));
    ESP_LOGI(TAG, "TX完成状态: %s", esp_err_to_name(err));
    
    // 检查是否有回环数据（如果TX和RX短接）
    vTaskDelay(pdMS_TO_TICKS(100));
    size_t available = 0;
    uart_get_buffered_data_len(uart_port, &available);
    ESP_LOGI(TAG, "接收缓冲区数据: %d bytes", (int)available);
    
    if (available > 0) {
        uint8_t recv_data[16];
        int read_bytes = uart_read_bytes(uart_port, recv_data, sizeof(recv_data), pdMS_TO_TICKS(100));
        ESP_LOGI(TAG, "读取到 %d 字节", read_bytes);
        
        if (read_bytes > 0) {
            ESP_LOG_BUFFER_HEX(TAG, recv_data, read_bytes);
            // 检查是否为回环数据
            if (read_bytes == sizeof(test_data) && memcmp(test_data, recv_data, sizeof(test_data)) == 0) {
                ESP_LOGW(TAG, "检测到回环数据 - TX和RX可能短接或者设备在回环模式");
            }
        }
    }
    
    return true;
}

bool ModbusTest::testModbusCommunication() {
    ESP_LOGI(TAG, "=== Modbus通信测试 ===");
    
    if (!controller) {
        ESP_LOGE(TAG, "Controller not initialized");
        return false;
    }
    
    // 测试读取单个寄存器（设备地址 0x01）
    ESP_LOGI(TAG, "测试读取输出电压寄存器 (地址 0x01, 寄存器 0x0000)");
    
    uint16_t voltage_data;
    bool success = controller->readHoldingRegisters(0x0000, 1, &voltage_data);
    
    if (success) {
        ESP_LOGI(TAG, "✅ 读取成功! 寄存器值: 0x%04X (%d)", voltage_data, voltage_data);
        float voltage = voltage_data / 100.0f;
        ESP_LOGI(TAG, "   转换后电压: %.2fV", voltage);
        return true;
    } else {
        ESP_LOGE(TAG, "❌ 读取失败");
        
        // 尝试读取其他寄存器
        ESP_LOGI(TAG, "尝试读取其他寄存器...");
        for (uint16_t reg = 0; reg < 5; reg++) {
            uint16_t data;
            if (controller->readHoldingRegisters(reg, 1, &data)) {
                ESP_LOGI(TAG, "✅ 寄存器 0x%04X = 0x%04X (%d)", reg, data, data);
                return true;
            }
            vTaskDelay(pdMS_TO_TICKS(100));
        }
        
        return false;
    }
}

void ModbusTest::scanDeviceAddresses() {
    ESP_LOGI(TAG, "=== 扫描设备地址 ===");
    ESP_LOGI(TAG, "扫描地址范围: 0x01 - 0x10");
    
    if (!controller) {
        ESP_LOGE(TAG, "Controller not initialized");
        return;
    }
    
    bool found_device = false;
    
    for (uint8_t addr = 1; addr <= 16; addr++) {
        ESP_LOGI(TAG, "测试设备地址: 0x%02X", addr);
        
        // 构建读取请求（读取第一个寄存器）
        uint8_t request[8];
        request[0] = addr;                  // 设备地址
        request[1] = 0x03;                  // 功能码：读保持寄存器
        request[2] = 0x00;                  // 起始地址高字节
        request[3] = 0x00;                  // 起始地址低字节
        request[4] = 0x00;                  // 数量高字节
        request[5] = 0x01;                  // 数量低字节
        
        // 计算CRC
        uint16_t crc = 0xFFFF;
        for (int i = 0; i < 6; i++) {
            crc ^= request[i];
            for (int j = 0; j < 8; j++) {
                if (crc & 0x0001) {
                    crc = (crc >> 1) ^ 0xA001;
                } else {
                    crc = crc >> 1;
                }
            }
        }
        request[6] = crc & 0xFF;
        request[7] = (crc >> 8) & 0xFF;
        
        // 发送并等待响应
        uart_port_t uart_port = UART_NUM_1;
        uart_flush_input(uart_port);
        
        int written = uart_write_bytes(uart_port, request, 8);
        if (written == 8) {
            uart_wait_tx_done(uart_port, pdMS_TO_TICKS(100));
            
            // 等待响应
            vTaskDelay(pdMS_TO_TICKS(100));
            size_t available = 0;
            uart_get_buffered_data_len(uart_port, &available);
            
            if (available > 0) {
                uint8_t response[32];
                int read_bytes = uart_read_bytes(uart_port, response, sizeof(response), pdMS_TO_TICKS(100));
                
                if (read_bytes >= 3 && response[0] == addr && response[1] == 0x03) {
                    ESP_LOGI(TAG, "🎯 找到设备! 地址: 0x%02X, 响应长度: %d", addr, read_bytes);
                    ESP_LOG_BUFFER_HEX(TAG, response, read_bytes);
                    found_device = true;
                }
            }
        }
        
        vTaskDelay(pdMS_TO_TICKS(200));  // 设备间隔
    }
    
    if (!found_device) {
        ESP_LOGW(TAG, "未找到响应的设备");
    }
}

void ModbusTest::testDifferentBaudRates() {
    ESP_LOGI(TAG, "=== 测试不同波特率 ===");
    
    const int baud_rates[] = {9600, 19200, 38400, 57600, 115200};
    const int num_rates = sizeof(baud_rates) / sizeof(baud_rates[0]);
    
    uart_port_t uart_port = UART_NUM_1;
    
    for (int i = 0; i < num_rates; i++) {
        ESP_LOGI(TAG, "测试波特率: %d", baud_rates[i]);
        
        // 重新配置UART波特率
        uart_config_t uart_config = {
            .baud_rate = baud_rates[i],
            .data_bits = UART_DATA_8_BITS,
            .parity = UART_PARITY_DISABLE,
            .stop_bits = UART_STOP_BITS_1,
            .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
            .rx_flow_ctrl_thresh = 122,
        };
        
        esp_err_t err = uart_param_config(uart_port, &uart_config);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "配置波特率失败: %s", esp_err_to_name(err));
            continue;
        }
        
        vTaskDelay(pdMS_TO_TICKS(100));  // 让UART稳定
        
        // 尝试简单通信
        uint8_t request[] = {0x01, 0x03, 0x00, 0x00, 0x00, 0x01, 0x84, 0x0A};  // 预计算的CRC
        
        uart_flush_input(uart_port);
        uart_write_bytes(uart_port, request, sizeof(request));
        uart_wait_tx_done(uart_port, pdMS_TO_TICKS(100));
        
        vTaskDelay(pdMS_TO_TICKS(200));  // 等待响应
        
        size_t available = 0;
        uart_get_buffered_data_len(uart_port, &available);
        
        if (available > 0) {
            ESP_LOGI(TAG, "✅ 波特率 %d: 收到 %d 字节响应", baud_rates[i], (int)available);
            
            uint8_t response[16];
            int read_bytes = uart_read_bytes(uart_port, response, sizeof(response), pdMS_TO_TICKS(100));
            if (read_bytes > 0) {
                ESP_LOG_BUFFER_HEX(TAG, response, read_bytes);
            }
        } else {
            ESP_LOGD(TAG, "波特率 %d: 无响应", baud_rates[i]);
        }
        
        vTaskDelay(pdMS_TO_TICKS(300));
    }
    
    // 恢复默认波特率
    uart_config_t uart_config = {
        .baud_rate = 9600,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .rx_flow_ctrl_thresh = 122,
    };
    uart_param_config(uart_port, &uart_config);
}

void ModbusTest::runFullDiagnostic() {
    ESP_LOGI(TAG, "\n");
    ESP_LOGI(TAG, "🔧 ===== 开始Modbus通信诊断 =====");
    ESP_LOGI(TAG, "\n");
    
    if (!init()) {
        ESP_LOGE(TAG, "初始化失败，无法继续诊断");
        return;
    }
    
    // 1. UART连接测试
    testUARTConnection();
    vTaskDelay(pdMS_TO_TICKS(1000));
    
    // 2. 基本Modbus通信测试
    testModbusCommunication();
    vTaskDelay(pdMS_TO_TICKS(1000));
    
    // 3. 扫描设备地址
    scanDeviceAddresses();
    vTaskDelay(pdMS_TO_TICKS(1000));
    
    // 4. 测试不同波特率
    testDifferentBaudRates();
    
    ESP_LOGI(TAG, "\n");
    ESP_LOGI(TAG, "🏁 ===== 诊断完成 =====");
    ESP_LOGI(TAG, "\n");
    
    ESP_LOGI(TAG, "📋 诊断建议:");
    ESP_LOGI(TAG, "1. 检查连线: TX(GPIO31) -> XY6506S RX, RX(GPIO33) -> XY6506S TX");
    ESP_LOGI(TAG, "2. 检查XY6506S电源设置: Modbus地址、波特率、奇偶校验");
    ESP_LOGI(TAG, "3. 确认XY6506S处于Modbus模式（而非其他通信协议）");
    ESP_LOGI(TAG, "4. 检查地线连接");
    ESP_LOGI(TAG, "5. 测试用万用表验证TX/RX信号电平");
}