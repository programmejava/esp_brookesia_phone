#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/ringbuf.h"
#include "esp_log.h"
#include "usb/usb_host.h"
#include "usb/cdc_acm_host.h"

#define RX_RING_BUFFER_SIZE     (4096)

class TinyUsbCdcService {
public:
    TinyUsbCdcService();
    ~TinyUsbCdcService();

    bool begin();
    void end();
    size_t read(uint8_t *buffer, size_t max_len);
    size_t available();
    void write(const uint8_t *data, size_t len);
    bool isConnected();

    // [新增] UI层调用的公共方法
    void startScan();
    void stopScan();
    void forceDisconnectDevice();  // [新增] 强制断开设备连接
    
    // [新增] 测试功能
    void startHeartbeat();
    void stopHeartbeat();
    void configureSerialPort(uint32_t baud_rate = 115200);
    
    // [新增] 完整的串口参数配置
    struct SerialConfig {
        uint32_t baud_rate;     // 波特率
        uint8_t data_bits;      // 数据位 (5,6,7,8)
        uint8_t parity;         // 校验位 (0=None, 1=Odd, 2=Even)
        uint8_t stop_bits;      // 停止位 (0=1bit, 1=1.5bit, 2=2bit)
    };
    void configureSerialPort(const SerialConfig& config);
    
    // [新增] 设置和获取当前配置
    void setCurrentConfig(const SerialConfig& config);
    SerialConfig getCurrentConfig() const;
    
    // [新增] 心跳包启用状态管理
    void setHeartbeatEnabled(bool enabled);
    bool isHeartbeatEnabled() const;

    // [新增] 设备类型检测
    enum UsbDeviceType {
        DEVICE_TYPE_UNKNOWN = 0,
        DEVICE_TYPE_CH340,      // CH340/CH341系列 - 不支持标准CDC
        DEVICE_TYPE_FT232,      // FTDI FT232系列 - 部分支持CDC
        DEVICE_TYPE_CP210X,     // Silicon Labs CP210x系列 - 支持标准CDC
        DEVICE_TYPE_PL2303,     // Prolific PL2303系列 - 支持标准CDC
        DEVICE_TYPE_CDC_STANDARD // 标准CDC设备
    };
    
    UsbDeviceType getDeviceType() const { return _current_device_type; }
    const char* getDeviceTypeName() const;
    
    // [新增] 设备类型检测和配置方法
    UsbDeviceType detectDeviceType(uint16_t vid, uint16_t pid);
    void configureDeviceSpecific();
    void configureCH340SerialPort(uint32_t baud_rate = 115200);

private:
    static void host_lib_task(void* arg);
    static void device_event_callback(const cdc_acm_host_dev_event_data_t *event, void *user_ctx);
    static bool data_received_callback(const uint8_t *data, size_t data_len, void *user_ctx);
    
    // [新增] 自动扫描任务
    static void device_scan_task(void* arg);
    // [新增] 心跳任务
    static void heartbeat_task(void* arg);
    
    TaskHandle_t _host_task_handle;
    TaskHandle_t _scan_task_handle; // [新增] 扫描任务句柄
    TaskHandle_t _heartbeat_task_handle; // [新增] 心跳任务句柄
    
    // [新增] 任务控制标志 - 用于优雅停止任务
    volatile bool _scan_task_should_stop;
    volatile bool _heartbeat_task_should_stop;

    static RingbufHandle_t _s_rx_ring_buffer; 
    static volatile bool   _s_is_device_connected;
    static cdc_acm_dev_hdl_t _s_cdc_device_handle;
    
    // [新增] 设备信息
    static uint16_t _s_device_vid;
    static uint16_t _s_device_pid;
    UsbDeviceType _current_device_type;
    
    // [新增] 保存当前串口配置
    SerialConfig _current_config;
    
    // [新增] 心跳包启用状态
    bool _heartbeat_enabled;
};