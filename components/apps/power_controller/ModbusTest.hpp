/**
 * @file ModbusTest.hpp
 * @brief Modbus通信测试工具
 * @author ESP32开发团队
 * @date 2025年11月4日
 */

#ifndef MODBUS_TEST_HPP
#define MODBUS_TEST_HPP

#include "ModbusController.hpp"

class ModbusTest {
private:
    static const char* TAG;
    ModbusController* controller;
    
public:
    ModbusTest();
    ~ModbusTest();
    
    /**
     * @brief 初始化测试环境
     */
    bool init();
    
    /**
     * @brief 测试UART连接
     */
    bool testUARTConnection();
    
    /**
     * @brief 测试基本Modbus通信
     */
    bool testModbusCommunication();
    
    /**
     * @brief 扫描设备地址
     */
    void scanDeviceAddresses();
    
    /**
     * @brief 测试不同波特率
     */
    void testDifferentBaudRates();
    
    /**
     * @brief 运行完整诊断
     */
    void runFullDiagnostic();
};

#endif // MODBUS_TEST_HPP