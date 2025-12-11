#include "UartService.hpp"
#include "esp_log.h"
#include <string.h>

static const char* TAG = "UartService";

UartService::UartService() : 
    _rx_ring_buffer(nullptr), 
    _is_running(false)
{
}

UartService::~UartService()
{
    end();
}

void UartService::begin(const UartConfig& initial_config)
{
    // 配置UART参数
    uart_config_t uart_config = {
        .baud_rate = initial_config.baud_rate,
        .data_bits = initial_config.data_bits,
        .parity = initial_config.parity,
        .stop_bits = initial_config.stop_bits,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    
    // 初始化全局互斥
    if (s_consumer_mux == nullptr) {
        s_consumer_mux = xSemaphoreCreateMutex();
    }

    bool installed = uart_is_driver_installed(UART_SERVICE_PORT);
    if (!installed) {
        ESP_LOGI(TAG, "Installing shared UART driver (port=%d, TX=%d, RX=%d, baud=%d)",
                 UART_SERVICE_PORT, UART_SERVICE_TX_PIN, UART_SERVICE_RX_PIN, uart_config.baud_rate);
        ESP_ERROR_CHECK(uart_driver_install(UART_SERVICE_PORT, UART_DRIVER_BUF_SIZE, 0, 0, NULL, 0));
        s_driver_installed = true;
    }
    // 参数配置（即使已安装也更新波特率等，但不改引脚以避免影响他方）
    uart_param_config(UART_SERVICE_PORT, &uart_config);
    if (!installed) {
        uart_set_pin(UART_SERVICE_PORT, UART_SERVICE_TX_PIN, UART_SERVICE_RX_PIN,
                     UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    }

    // 注册消费者并启动共享RX任务（若未运行）
    registerConsumer();
    if (!_rx_ring_buffer) {
        ESP_LOGE(TAG, "Failed to create ring buffer for this consumer");
        return;
    }
    if (!s_rx_task_running) {
        BaseType_t res = xTaskCreate(uartRxTask, "uart_rx_shared", 4096, nullptr, 10, &s_rx_task_handle);
        if (res == pdPASS && s_rx_task_handle) {
            s_rx_task_running = true;
        } else {
            ESP_LOGE(TAG, "Failed to start shared UART RX task");
        }
    }

    _is_running = false;
    ESP_LOGI(TAG, "UART service initialized successfully");
}
void UartService::end()
{
    ESP_LOGI(TAG, "Shutting down UART service...");
    
    // 停止接收
    _is_running = false;

    // 注销消费者并删除本实例环形缓冲区
    unregisterConsumer();
    
    ESP_LOGI(TAG, "UART service shut down successfully");
}

void UartService::reconfigure(const UartConfig& new_config)
{
    ESP_LOGI(TAG, "Reconfiguring UART service with new parameters");
    
    // 清理旧缓冲区并重新注册消费者
    unregisterConsumer();

    uart_config_t uart_config = {
        .baud_rate = new_config.baud_rate,
        .data_bits = new_config.data_bits,
        .parity = new_config.parity,
        .stop_bits = new_config.stop_bits,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    // 驱动不存在则安装；存在则仅更新参数
    bool installed = uart_is_driver_installed(UART_SERVICE_PORT);
    if (!installed) {
        ESP_LOGI(TAG, "Driver not installed, installing in reconfigure()");
        ESP_ERROR_CHECK(uart_driver_install(UART_SERVICE_PORT, UART_DRIVER_BUF_SIZE, 0, 0, NULL, 0));
        s_driver_installed = true;
        ESP_ERROR_CHECK(uart_param_config(UART_SERVICE_PORT, &uart_config));
        ESP_ERROR_CHECK(uart_set_pin(UART_SERVICE_PORT, UART_SERVICE_TX_PIN, UART_SERVICE_RX_PIN,
                                     UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
    } else {
        uart_param_config(UART_SERVICE_PORT, &uart_config);
    }

    // 重新注册本实例为消费者
    registerConsumer();

    ESP_LOGI(TAG, "UART reconfiguration completed");
}

void UartService::startReceiving()
{
    _is_running = true;
    ESP_LOGD(TAG, "UART receiving started");
}

void UartService::stopReceiving()
{
    _is_running = false;
    ESP_LOGD(TAG, "UART receiving stopped");
}

size_t UartService::read(uint8_t* buffer, size_t max_len)
{
    if (!_rx_ring_buffer || max_len == 0) {
        return 0;
    }
    
    size_t item_size = 0;
    uint8_t *item = (uint8_t*)xRingbufferReceive(_rx_ring_buffer, &item_size, (TickType_t)0);
    if (item != nullptr) {
        size_t copy_len = (max_len < item_size) ? max_len : item_size;
        memcpy(buffer, item, copy_len);
        vRingbufferReturnItem(_rx_ring_buffer, (void*)item);
        return copy_len;
    }
    return 0;
}

size_t UartService::available()
{
    if (!_rx_ring_buffer) {
        return 0;
    }
    
    size_t free_size = xRingbufferGetCurFreeSize(_rx_ring_buffer);
    return RX_RING_BUFFER_SIZE - free_size;
}

void UartService::write(const uint8_t* data, size_t len)
{
    if (len > 0) {
        uart_write_bytes(UART_SERVICE_PORT, (const char*)data, len);
    }
}

void UartService::uartRxTask(void* arg)
{
    uint8_t* buffer = (uint8_t*)malloc(UART_DRIVER_BUF_SIZE);
    if (buffer == nullptr) {
        ESP_LOGE(TAG, "RX task failed to allocate memory, task exiting");
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "Shared UART RX task started");

    while (true) {
        int rx_len = uart_read_bytes(UART_SERVICE_PORT, buffer, UART_DRIVER_BUF_SIZE, pdMS_TO_TICKS(20));
        if (rx_len > 0) {
            // 扇出给所有活跃消费者
            if (s_consumer_mux) xSemaphoreTake(s_consumer_mux, portMAX_DELAY);
            for (auto &slot : s_consumers) {
                if (slot.active && slot.buf) {
                    if (xRingbufferSend(slot.buf, buffer, rx_len, (TickType_t)0) != pdTRUE) {
                        // 静默丢弃，避免刷屏；如需调试可加日志
                    }
                }
            }
            if (s_consumer_mux) xSemaphoreGive(s_consumer_mux);
        } else {
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }
}

void UartService::registerConsumer()
{
    // 创建本实例缓冲区
    _rx_ring_buffer = xRingbufferCreate(RX_RING_BUFFER_SIZE, RINGBUF_TYPE_BYTEBUF);
    if (!_rx_ring_buffer) {
        ESP_LOGE(TAG, "Failed to create ring buffer for consumer");
        return;
    }
    if (s_consumer_mux) xSemaphoreTake(s_consumer_mux, portMAX_DELAY);
    for (auto &slot : s_consumers) {
        if (!slot.active) {
            slot.buf = _rx_ring_buffer;
            slot.active = true;
            break;
        }
    }
    if (s_consumer_mux) xSemaphoreGive(s_consumer_mux);
}

void UartService::unregisterConsumer()
{
    if (s_consumer_mux) xSemaphoreTake(s_consumer_mux, portMAX_DELAY);
    for (auto &slot : s_consumers) {
        if (slot.active && slot.buf == _rx_ring_buffer) {
            slot.active = false;
            slot.buf = nullptr;
            break;
        }
    }
    if (s_consumer_mux) xSemaphoreGive(s_consumer_mux);

    if (_rx_ring_buffer) {
        vRingbufferDelete(_rx_ring_buffer);
        _rx_ring_buffer = nullptr;
    }
}