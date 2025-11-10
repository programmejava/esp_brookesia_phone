#include "UartService.hpp"
#include "esp_log.h"
#include <string.h>

static const char* TAG = "UartService";

UartService::UartService() : 
    _rx_ring_buffer(nullptr), 
    _rx_task_handle(nullptr), 
    _is_running(false)
{
}

UartService::~UartService()
{
    end();
}

void UartService::begin(const UartConfig& initial_config)
{
    // 先尝试删除可能残留的UART驱动实例（增加健壮性）
    uart_driver_delete(UART_SERVICE_PORT);
    
    // 配置UART参数
    uart_config_t uart_config = {
        .baud_rate = initial_config.baud_rate,
        .data_bits = initial_config.data_bits,
        .parity = initial_config.parity,
        .stop_bits = initial_config.stop_bits,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    
    ESP_LOGI(TAG, "Initializing UART on port %d: TX=%d, RX=%d, Baud=%d", 
             UART_SERVICE_PORT, UART_SERVICE_TX_PIN, UART_SERVICE_RX_PIN, uart_config.baud_rate);
    
    // 安装UART驱动
    ESP_ERROR_CHECK(uart_driver_install(UART_SERVICE_PORT, UART_DRIVER_BUF_SIZE, 0, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(UART_SERVICE_PORT, &uart_config));
    ESP_ERROR_CHECK(uart_set_pin(UART_SERVICE_PORT, UART_SERVICE_TX_PIN, UART_SERVICE_RX_PIN, 
                                  UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));

    // 创建接收数据的环形缓冲区
    _rx_ring_buffer = xRingbufferCreate(RX_RING_BUFFER_SIZE, RINGBUF_TYPE_BYTEBUF);
    if (_rx_ring_buffer == nullptr) {
        ESP_LOGE(TAG, "Failed to create ring buffer, halting service initialization");
        uart_driver_delete(UART_SERVICE_PORT);
        return;
    }

    // 创建UART接收任务
    _is_running = false;
    BaseType_t result = xTaskCreate(uartRxTask, "uart_rx_task", 4096, this, 10, &_rx_task_handle);
    if (result != pdPASS || _rx_task_handle == nullptr) {
        ESP_LOGE(TAG, "Failed to create UART RX task!");
        vRingbufferDelete(_rx_ring_buffer);
        _rx_ring_buffer = nullptr;
        uart_driver_delete(UART_SERVICE_PORT);
        return;
    }

    ESP_LOGI(TAG, "UART service initialized successfully");
}
void UartService::end()
{
    ESP_LOGI(TAG, "Shutting down UART service...");
    
    // 停止接收
    _is_running = false;
    
    // 删除接收任务
    if (_rx_task_handle) { 
        vTaskDelete(_rx_task_handle); 
        _rx_task_handle = nullptr; 
    }
    
    // 删除环形缓冲区
    if (_rx_ring_buffer) { 
        vRingbufferDelete(_rx_ring_buffer); 
        _rx_ring_buffer = nullptr; 
    }
    
    // 卸载UART驱动
    uart_driver_delete(UART_SERVICE_PORT);
    
    ESP_LOGI(TAG, "UART service shut down successfully");
}

void UartService::reconfigure(const UartConfig& new_config)
{
    ESP_LOGI(TAG, "Reconfiguring UART service with new parameters");
    
    // 停止并彻底清理旧的服务
    if (_rx_task_handle) { 
        vTaskDelete(_rx_task_handle); 
        _rx_task_handle = nullptr; 
    }
    if (_rx_ring_buffer) { 
        vRingbufferDelete(_rx_ring_buffer); 
        _rx_ring_buffer = nullptr; 
    }
    uart_driver_delete(UART_SERVICE_PORT);

    // 使用新配置重新初始化服务
    begin(new_config);
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
    UartService* self = static_cast<UartService*>(arg);
    uint8_t* buffer = (uint8_t*)malloc(UART_DRIVER_BUF_SIZE);
    
    if (buffer == nullptr) {
        ESP_LOGE(TAG, "RX task failed to allocate memory, task exiting");
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "UART RX task started");

    while (true) {
        if (self->_is_running) {
            // 尝试读取UART数据（带超时）
            int rx_len = uart_read_bytes(UART_SERVICE_PORT, buffer, UART_DRIVER_BUF_SIZE, pdMS_TO_TICKS(20));
            if (rx_len > 0) {
                // 将数据送入环形缓冲区
                if (xRingbufferSend(self->_rx_ring_buffer, buffer, rx_len, (TickType_t)0) != pdTRUE) {
                    ESP_LOGW(TAG, "Ring buffer full, %d bytes dropped", rx_len);
                }
            }
        } else {
            // 未运行时休眠以节省CPU
            vTaskDelay(pdMS_TO_TICKS(200));
        }
    }
    
    // 清理资源（理论上不会执行到这里）
    free(buffer);
    ESP_LOGI(TAG, "UART RX task ended");
}