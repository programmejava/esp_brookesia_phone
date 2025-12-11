#pragma once

#include <memory>
#include <string>
#include "esp_brookesia.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "lwip/sockets.h"
#include "nvs.h"
#include "uart_ttl/UartService.hpp"
#include "uart_usb/TinyUsbCdcService.hpp"

// 前置声明 UI 类，避免头文件循环依赖
class UartBridgeUI;

class UartBridge : public ESP_Brookesia_PhoneApp {
public:
    UartBridge();
    ~UartBridge();

    bool init(void) override;
    bool run(void) override;
    bool back(void) override;
    bool close(void) override;
    bool resume(void) override;
    
    // 可选的串口来源
    enum class PortType {
        TTL = 0,
        USB_CDC = 1,
    };

    // 串口参数（保存在 NVS）
    struct SerialSettings {
        int baud;
        uint8_t data_bits;
        uint8_t parity;
        uint8_t stop_bits;
    };

    // 网络参数（当前仅 TCP 端口）
    struct NetSettings {
        uint16_t port;
    };

private:
    friend class UartBridgeUI;

    /* Config */
    void loadSettings();
    void saveSettings();
    void applySerialSettings();

    /* Bridge control */
    bool isWifiConnected();
    bool ensureApFallback();
    bool startBridge();
    void stopBridge();
    void bridgeTask();
    void resetStats();

    /* UI 交互回调（由 UI 层调用） */
    void handleStartStopRequested();
    void handlePortTypeChanged(int sel);
    void handleBaudChanged(int sel);
    void handleDataBitsChanged(int sel);
    void handleParityChanged(int sel);
    void handleStopBitsChanged(int sel);
    void handlePortEdited(const char *text);
    void handleHeartbeatToggled(bool enabled);
    void handleExitRequested();
    void handleUiTick();

    /* UI 辅助（由业务调用驱动 UI 展示） */
    void updateWifiStatus();
    void setStatus(const char *text);
    void appendLog(const char *text);
    void refreshStartButtonState();

    /* Callbacks */
    static void bridgeTaskThunk(void *arg);

    /* State */
    PortType _port_type;
    SerialSettings _serial;
    NetSettings _net;
    bool _running;
    bool _stop_flag;
    bool _ap_active;
    bool _client_connected;
    bool _heartbeat_enabled;
    uint64_t _bytes_up_net_to_uart;   // 从网络到串口/USB 的字节数
    uint64_t _bytes_down_uart_to_net; // 从串口/USB 到网络的字节数
    std::string _client_ip;
    TaskHandle_t _bridge_task;
    int _listen_fd;
    uint32_t _heartbeat_counter;
    int64_t _last_heartbeat_ms;
    nvs_handle_t _nvs;

    /* Services */
    UartService _uart;
    TinyUsbCdcService _usb;

    /* UI 封装指针 */
    std::unique_ptr<UartBridgeUI> _ui;
};
