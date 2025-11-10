#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/ringbuf.h"
#include "driver/uart.h"

// 硬件配置定义
// 已更新为由您最终选择的、接线方便的备用引脚
#define UART_SERVICE_PORT       (UART_NUM_1) // 使用UART1端口
#define UART_SERVICE_TX_PIN     (29)         // TX引脚: GPIO29
#define UART_SERVICE_RX_PIN     (30)         // RX引脚: GPIO30

// 缓冲区配置定义
#define UART_DRIVER_BUF_SIZE    (4096)       // UART驱动缓冲区大小
#define RX_RING_BUFFER_SIZE     (4096)       // 接收环形缓冲区大小

/**
 * @struct UartConfig
 * @brief 用于封装UART配置参数的结构体
 */
struct UartConfig {
    int baud_rate;                    // 波特率
    uart_word_length_t data_bits;     // 数据位长度
    uart_parity_t parity;             // 校验位类型
    uart_stop_bits_t stop_bits;       // 停止位数量
};

/**
 * @class UartService
 * @brief UART串口服务类
 * 
 * 提供UART初始化、数据收发、动态重配置等功能。
 * 使用FreeRTOS任务和环形缓冲区来处理异步数据接收。
 */
class UartService {
public:
    UartService();
    ~UartService();

    /**
     * @brief 初始化UART服务
     * @param initial_config 初始UART配置参数
     */
    void begin(const UartConfig& initial_config);
    
    /**
     * @brief 结束UART服务，释放资源
     */
    void end();
    
    /**
     * @brief 开始接收数据
     */
    void startReceiving();
    
    /**
     * @brief 停止接收数据
     */
    void stopReceiving();
    
    /**
     * @brief 读取接收到的数据
     * @param buffer 数据缓冲区
     * @param max_len 最大读取长度
     * @return 实际读取的字节数
     */
    size_t read(uint8_t *buffer, size_t max_len);
    
    /**
     * @brief 获取可读数据的字节数
     * @return 可读字节数
     */
    size_t available();
    
    /**
     * @brief 发送数据
     * @param data 要发送的数据
     * @param len 数据长度
     */
    void write(const uint8_t *data, size_t len);

    /**
     * @brief 动态重新配置UART服务
     * @param new_config 包含新串口参数的结构体
     */
    void reconfigure(const UartConfig& new_config);

private:
    /**
     * @brief UART接收任务静态函数
     * @param arg 任务参数（UartService实例指针）
     */
    static void uartRxTask(void* arg);
    
    RingbufHandle_t _rx_ring_buffer;   // 接收数据环形缓冲区
    TaskHandle_t    _rx_task_handle;   // 接收任务句柄
    volatile bool   _is_running;       // 服务运行状态标志
};