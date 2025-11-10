/**
 * @file ModbusController.hpp
 * @brief XY6506S电源Modbus-RTU通信控制器头文件
 * @details 实现与XY6506S电源设备的Modbus-RTU协议通信
 * @author ESP32开发团队
 * @date 2025年11月4日
 * @version 1.0
 */

#ifndef MODBUS_CONTROLLER_HPP
#define MODBUS_CONTROLLER_HPP

#include <stdint.h>
#include <stdbool.h>
#include "driver/uart.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"

/**
 * @brief XY6506S电源寄存器地址定义 (根据官方手册)
 */
enum XY6506S_Registers {
    // 基本寄存器（读写）
    REG_V_SET          = 0x0000,    // 电压设置 (÷100 = V)
    REG_I_SET          = 0x0001,    // 电流设置 (÷1000 = A)
    
    // 测量值寄存器（只读）
    REG_VOUT           = 0x0002,    // 输出电压显示值 (÷100 = V)
    REG_IOUT           = 0x0003,    // 输出电流显示值 (÷1000 = A)
    REG_POWER          = 0x0004,    // 输出功率显示值 (÷100 = W)
    REG_UIN            = 0x0005,    // 输入电压显示值 (÷100 = V)
    
    // 容量和时间寄存器（只读）
    REG_AH_LOW         = 0x0006,    // 输出AH低16位 (maH)
    REG_AH_HIGH        = 0x0007,    // 输出AH高16位 (maH)
    REG_WH_LOW         = 0x0008,    // 输出WH低16位 (mwH)
    REG_WH_HIGH        = 0x0009,    // 输出WH高16位 (mwH)
    REG_OUT_H          = 0x000A,    // 开启时长-小时
    REG_OUT_M          = 0x000B,    // 开启时长-分钟
    REG_OUT_S          = 0x000C,    // 开启时长-秒
    
    // 温度寄存器（只读）
    REG_T_IN           = 0x000D,    // 内部温度值 (÷10 = °C/°F)
    REG_T_EX           = 0x000E,    // 外部温度值 (÷10 = °C/°F)
    
    // 控制寄存器（读写）
    REG_LOCK           = 0x000F,    // 按键锁 (0=解锁, 1=锁定)
    REG_PROTECT        = 0x0010,    // 保护状态 (0=正常, 1-11=各种保护)
    REG_CVCC           = 0x0011,    // 恒压恒流状态 (0=CV, 1=CC)
    REG_ONOFF          = 0x0012,    // 开关输出 (0=关闭, 1=开启)
    REG_F_C            = 0x0013,    // 温度符号 (°C/°F)
    REG_B_LED          = 0x0014,    // 背光亮度等级 (0-5)
    REG_SLEEP          = 0x0015,    // 息屏时间 (分钟)
    
    // 设备信息寄存器（只读）
    REG_MODEL          = 0x0016,    // 产品型号
    REG_VERSION        = 0x0017,    // 固件版本号
    
    // 通信配置寄存器（读写）
    REG_SLAVE_ADD      = 0x0018,    // 从机地址 (1-247)
    REG_BAUDRATE_L     = 0x0019,    // 波特率设置
    
    // 其他配置寄存器（读写）
    REG_BUZZER         = 0x001C     // 蜂鸣器开关 (0=关闭, 1=开启)
};

/**
 * @brief 电源设备数据结构
 */
struct PowerDeviceData {
    // 测量值
    float output_voltage;       // 输出电压 (V)
    float output_current;       // 输出电流 (A)
    float output_power;         // 输出功率 (W)
    float input_voltage;        // 输入电压 (V)
    float set_voltage;          // 设定电压 (V)
    float set_current;          // 设定电流 (A)
    
    // 开关状态
    bool output_switch;         // 输出开关状态
    bool beep_switch;          // 蜂鸣器开关状态
    bool key_lock;             // 按键锁定状态
    bool sleep_mode;           // 休眠模式状态
    
    // 数据有效性
    bool data_valid;           // 数据是否有效
    uint32_t last_update_ms;   // 最后更新时间戳
};

/**
 * @brief Modbus-RTU通信控制器类
 */
class ModbusController {
private:
    static const char* TAG;
    
    // UART配置
    static const uart_port_t UART_PORT = UART_NUM_2;
    static const int UART_TX_PIN = 51;  // GPIO51 作为TX
    static const int UART_RX_PIN = 52;  // GPIO52 作为RX
    static const int UART_BAUD_RATE = 115200;  // XY6506S出厂默认115200
    static const int UART_BUF_SIZE = 256;
    
    // Modbus配置
    static const uint8_t DEVICE_ADDRESS = 0x01;  // 设备地址
    static const uint32_t RESPONSE_TIMEOUT_MS = 200;   // 响应超时优化为200ms（快速响应）
    static const uint32_t MIN_FRAME_INTERVAL_MS = 1;   // 最小帧间隔优化为1ms（高速通信）
    
    // 数据成员
    PowerDeviceData device_data;
    SemaphoreHandle_t modbus_mutex;
    uint32_t last_communication_ms;
    bool is_initialized;
    
    // 私有方法
    uint16_t calculateCRC(const uint8_t* data, size_t length);
    bool sendModbusFrame(const uint8_t* frame, size_t length);
    bool receiveModbusFrame(uint8_t* frame, size_t* length, uint32_t timeout_ms);
    void ensureFrameInterval();
    
    // 调试和扫描方法
    bool scanDeviceAddress(uint8_t start_addr = 1, uint8_t end_addr = 10);
    
public:
    /**
     * @brief 构造函数
     */
    ModbusController();
    
    /**
     * @brief 析构函数
     */
    ~ModbusController();
    
    /**
     * @brief 初始化Modbus通信
     * @return true 初始化成功，false 初始化失败
     */
    bool initialize();
    
    /**
     * @brief 反初始化，释放资源
     */
    void deinitialize();
    
    /**
     * @brief 读取保持寄存器
     * @param start_addr 起始地址
     * @param count 寄存器数量
     * @param data 读取的数据缓冲区
     * @return true 读取成功，false 读取失败
     */
    bool readHoldingRegisters(uint16_t start_addr, uint16_t count, uint16_t* data);
    
    /**
     * @brief 写入单个寄存器
     * @param addr 寄存器地址
     * @param value 写入的值
     * @return true 写入成功，false 写入失败
     */
    bool writeSingleRegister(uint16_t addr, uint16_t value);
    
    /**
     * @brief 读取所有设备数据
     * @return true 读取成功，false 读取失败
     */
    bool readAllDeviceData();
    
    /**
     * @brief 获取设备数据
     * @return 设备数据结构的引用
     */
    const PowerDeviceData& getDeviceData() const { return device_data; }
    
    /**
     * @brief 设置输出电压和电流
     * @param voltage 电压值 (V)
     * @param current 电流值 (A)
     * @return true 设置成功，false 设置失败
     */
    bool setVoltageAndCurrent(float voltage, float current);
    
    /**
     * @brief 设置输出开关
     * @param enable true 开启输出，false 关闭输出
     * @return true 设置成功，false 设置失败
     */
    bool setOutputSwitch(bool enable);
    
    /**
     * @brief 设置蜂鸣器开关
     * @param enable true 开启蜂鸣器，false 关闭蜂鸣器
     * @return true 设置成功，false 设置失败
     */
    bool setBeepSwitch(bool enable);
    
    /**
     * @brief 设置按键锁定
     * @param enable true 锁定按键，false 解锁按键
     * @return true 设置成功，false 设置失败
     */
    bool setKeyLock(bool enable);
    
    /**
     * @brief 设置休眠模式
     * @param enable true 进入休眠，false 退出休眠
     * @return true 设置成功，false 设置失败
     */
    bool setSleepMode(bool enable);
    
    /**
     * @brief 扫描Modbus设备地址
     * @return true 找到设备，false 未找到设备
     */
    bool scanForDevices();
    
    /**
     * @brief 验证电压值是否有效
     * @param voltage 要验证的电压值
     * @return true 电压值有效，false 电压值无效
     */
    bool validateVoltage(float voltage) const;
    
    /**
     * @brief 验证电流值是否有效
     * @param current 要验证的电流值
     * @return true 电流值有效，false 电流值无效
     */
    bool validateCurrent(float current) const;
    
    /**
     * @brief 检查通信状态
     * @return true 通信正常，false 通信异常
     */
    bool isCommunicationOk() const;
};

#endif // MODBUS_CONTROLLER_HPP