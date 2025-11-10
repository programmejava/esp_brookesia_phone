# ESP32P4 XY6506S电源控制系统

一个基于ESP32P4开发板和XY6506S可编程电源的智能控制系统，支持实时监控、参数设置和双向通信。

## 📋 项目概述

### 系统特性
- **实时监控**: 300ms快速响应的电压、电流、功率显示
- **智能控制**: 支持输出开关、蜂鸣器、按键锁、休眠模式控制
- **预设功能**: 6个常用电压电流预设值，一键快速设置
- **双向同步**: 设备状态变化自动同步到界面显示
- **高可靠性**: 完善的错误处理和连接恢复机制
- **现代UI**: 基于LVGL的直观触摸界面

### 技术规格
- **硬件平台**: ESP32P4开发板 (双核处理器)
- **通信协议**: Modbus-RTU (115200波特率, 8N1)
- **显示框架**: LVGL UI库
- **开发环境**: ESP-IDF v5.5
- **目标设备**: XY6506S可编程电源 (0-65V/0-6A)

## 🛠️ 硬件连接

### 接线图
```
ESP32P4开发板          XY6506S电源模块
┌─────────────┐       ┌─────────────┐
│ GPIO51 (TX) │────── │ RS485-A     │
│ GPIO52 (RX) │────── │ RS485-B     │
│ GND         │────── │ GND         │
│ 5V          │────── │ VCC         │
└─────────────┘       └─────────────┘
```

### 硬件要求
- ESP32P4开发板 × 1
- XY6506S可编程电源模块 × 1
- RS485转串口模块 (如果需要)
- 连接线若干

### GPIO配置
| 功能 | GPIO引脚 | 说明 |
|------|----------|------|
| Modbus TX | GPIO51 | 发送数据到XY6506S |
| Modbus RX | GPIO52 | 接收XY6506S数据 |
| 串口波特率 | 115200 | 8数据位,无校验,1停止位 |

## 📦 软件架构

### 核心组件

#### 1. PowerController (电源控制器)
```cpp
class PowerController {
    // 主控制器类，负责整体协调
    bool initialize();          // 初始化系统
    bool run();                // 启动UI和实时更新
    bool close();              // 安全关闭
    void updateDisplayValues(); // 更新显示数据
}
```

#### 2. ModbusController (通信控制器)
```cpp
class ModbusController {
    // Modbus-RTU通信实现
    bool readHoldingRegisters();   // 读取寄存器
    bool writeHoldingRegister();   // 写入寄存器
    bool setVoltage();            // 设置电压
    bool setCurrent();            // 设置电流
    bool setOutputSwitch();       // 控制输出开关
}
```

#### 3. 实时更新系统
- **更新频率**: 300ms (比原来1000ms提升233%)
- **任务模式**: FreeRTOS任务 + 定时器通知
- **错误恢复**: 自动重连和状态恢复
- **性能优化**: 减少互斥锁等待时间和日志开销

### 文件结构
```
components/apps/power_controller/
├── PowerController.hpp         # 主控制器头文件
├── PowerController.cpp         # 主控制器实现
├── ModbusController.hpp        # Modbus通信头文件
├── ModbusController.cpp        # Modbus通信实现
└── ui/                        # UI界面文件
    ├── ui_power_controller.c   # 自动生成的UI代码
    └── ui_power_controller.h   # UI头文件
```

## 🚀 快速开始

### 1. 环境准备
```bash
# 安装ESP-IDF v5.5
git clone https://github.com/espressif/esp-idf.git
cd esp-idf && git checkout v5.5.x
./install.sh

# 设置环境变量
source ./export.sh
```

### 2. 项目编译
```bash
# 克隆项目
cd esp_brookesia_phone

# 配置项目
idf.py menuconfig

# 编译项目
idf.py build
```

### 3. 烧录程序
```bash
# 烧录到ESP32P4开发板
idf.py -p COM3 flash

# 监控串口输出
idf.py -p COM3 monitor

# 或一次性烧录并监控
idf.py -p COM3 flash monitor
```

### 4. 首次启动
1. **设备扫描**: 系统自动扫描Modbus设备(地址0x01-0x0A)
2. **连接确认**: 检测到XY6506S设备并显示连接成功
3. **界面初始化**: UI界面显示当前电源状态
4. **实时更新**: 开始300ms周期的状态更新

## 💡 使用指南

### 主界面功能

#### 实时显示区域
- **输出电压**: 当前输出电压值 (0.00-65.00V)
- **输出电流**: 当前输出电流值 (0.000-6.000A)  
- **输出功率**: 自动计算的功率值 (0.00-390.00W)
- **设定电压**: 用户设置的目标电压
- **设定电流**: 用户设置的目标电流限制
- **输入电压**: 电源模块的输入电压

#### 控制开关
- **电源开关**: 控制输出通道开启/关闭
- **蜂鸣器**: 控制按键音效开启/关闭
- **按键锁**: 锁定/解锁物理按键
- **休眠模式**: 控制显示屏休眠状态

#### 预设按钮 (6个快速设置)
| 预设 | 电压 | 电流 | 应用场景 |
|------|------|------|----------|
| 预设0 | 3.3V | 3.0A | 单片机开发 |
| 预设1 | 3.3V | 5.0A | 高功率3.3V应用 |
| 预设2 | 5.0V | 3.0A | USB标准电压 |
| 预设3 | 5.0V | 5.0A | 高功率5V应用 |
| 预设4 | 12.0V | 3.0A | 常用12V设备 |
| 预设5 | 12.0V | 5.0A | 大功率12V设备 |

#### 手动设置
- **电压设置**: 滑动条或数值输入 (0.00-65.00V, 精度0.01V)
- **电流设置**: 滑动条或数值输入 (0.000-6.000A, 精度0.001A)
- **应用按钮**: 将设置的参数应用到电源模块

### 操作流程

#### 基本操作
1. **开启输出**: 点击电源开关，开启电源输出
2. **选择预设**: 点击预设按钮快速设置常用参数
3. **应用设置**: 点击"应用"按钮确认参数修改
4. **监控状态**: 实时观察电压电流功率变化

#### 高级功能
1. **精确调节**: 使用滑动条微调电压电流
2. **安全保护**: 电流限制自动保护负载
3. **快速切换**: 预设值之间快速切换
4. **状态同步**: 物理按键操作自动同步到界面

## ⚙️ 技术细节

### Modbus-RTU通信协议

#### XY6506S寄存器映射
| 寄存器地址 | 功能描述 | 数据类型 | 取值范围 | 单位 |
|------------|----------|----------|----------|------|
| 0x0000 | 设备型号标识 | uint16 | 0x0203 | - |
| 0x0001 | 固件版本 | uint16 | - | - |
| 0x0002 | 输出电压显示值 | uint16 | 0-6500 | 0.01V |
| 0x0003 | 输出电流显示值 | uint16 | 0-6000 | 0.001A |
| 0x0004 | 输出功率显示值 | uint16 | 0-39000 | 0.01W |
| 0x0005 | 输入电压显示值 | uint16 | 0-6500 | 0.01V |
| 0x0009 | 电压设定值 | uint16 | 0-6500 | 0.01V |
| 0x000A | 电流设定值 | uint16 | 0-6000 | 0.001A |
| 0x0012 | 输出开关状态 | uint16 | 0/1 | 布尔 |
| 0x0013 | 蜂鸣器开关 | uint16 | 0/1 | 布尔 |
| 0x0014 | 按键锁状态 | uint16 | 0/1 | 布尔 |
| 0x0015 | 休眠模式 | uint16 | 0/1 | 布尔 |

#### 通信参数
- **设备地址**: 0x01 (默认)
- **波特率**: 115200 bps
- **数据格式**: 8位数据, 无校验, 1位停止位
- **响应超时**: 200ms (优化后)
- **帧间间隔**: 1ms (优化后)
- **CRC校验**: Modbus标准CRC-16

### 性能优化详情

#### 响应时间优化
```cpp
// 优化前 vs 优化后
UPDATE_INTERVAL_MS: 1000ms → 300ms     (提升233%)
RESPONSE_TIMEOUT_MS: 500ms → 200ms     (提升150%)
MIN_FRAME_INTERVAL_MS: 2ms → 1ms       (提升100%)
MUTEX_WAIT_TIME_MS: 200ms → 50ms       (提升300%)
```

#### 内存优化
- **任务栈大小**: 4096字节 (动态调整)
- **缓冲区管理**: 动态长度检测
- **字符串优化**: 减少临时对象创建

#### 实时性保证
- **FreeRTOS任务**: 优先级5 (较高优先级)
- **定时器机制**: 精确300ms间隔触发
- **异步更新**: 非阻塞式数据更新
- **错误恢复**: 通信失败自动重试

### 错误处理机制

#### 通信错误
```cpp
enum class ModbusError {
    SUCCESS = 0,
    TIMEOUT_ERROR,      // 响应超时
    CRC_ERROR,          // CRC校验失败
    DEVICE_NOT_FOUND,   // 设备未找到
    INVALID_RESPONSE    // 响应格式错误
};
```

#### 自动恢复策略
1. **超时重试**: 最多重试3次
2. **设备重扫**: 连接失败时重新扫描设备
3. **状态重置**: 严重错误时重置通信状态
4. **安全模式**: 通信完全失败时进入只读模式

## 🔧 配置说明

### 编译配置选项

#### menuconfig设置
```bash
idf.py menuconfig
```

重要配置项：
- `Component config → ESP32P4-Function-EV-Board → GPIO Configuration`
- `Component config → FreeRTOS → Kernel → configTICK_RATE_HZ = 1000`
- `Component config → Log output → Default log verbosity = Info`

#### 自定义配置
```cpp
// PowerController.hpp中的配置常量
#define UPDATE_INTERVAL_MS 300          // 更新间隔(毫秒)
#define MAX_RETRY_COUNT 3               // 最大重试次数
#define MODBUS_DEVICE_ADDR 0x01         // Modbus设备地址
#define UART_BAUD_RATE 115200           // 串口波特率
```

### 调试配置

#### 日志级别设置
```cpp
// 不同组件的日志级别
ESP_LOG_LEVEL_SET("PowerController", ESP_LOG_INFO);
ESP_LOG_LEVEL_SET("ModbusController", ESP_LOG_DEBUG);
ESP_LOG_LEVEL_SET("UI_Events", ESP_LOG_WARN);
```

#### 性能监控
```cpp
// 启用性能统计
#define ENABLE_PERFORMANCE_STATS 1
#define ENABLE_TIMING_DEBUG 0        // 仅调试时开启
```

## 📊 故障排除

### 常见问题及解决方案

#### 1. 设备连接失败
**症状**: 串口显示"Device scan completed - no devices found"
**原因**: 
- 硬件连接错误
- 波特率设置不匹配
- XY6506S设备地址不正确

**解决方法**:
```bash
# 检查硬件连接
1. 确认GPIO51连接到RS485-A
2. 确认GPIO52连接到RS485-B  
3. 确认GND连接正确

# 检查设备地址
idf.py monitor | grep "Trying device address"
# 应该显示在地址0x01找到设备
```

#### 2. 状态更新缓慢
**症状**: 界面更新延迟超过1秒
**检查项**:
```cpp
// 检查更新间隔设置
#define UPDATE_INTERVAL_MS 300  // 应该是300而不是1000

// 检查任务运行状态
ESP_LOGI(TAG, "Task running: %d", is_running);
```

#### 3. 重新进入应用失败
**症状**: 退出后再次进入，状态不更新
**解决**: 系统已修复此问题，确保使用最新版本

#### 4. Modbus通信超时
**症状**: 大量"Receive timeout"错误
**调试步骤**:
```cpp
// 增加调试输出
ESP_LOG_LEVEL_SET("ModbusController", ESP_LOG_DEBUG);

// 检查波特率匹配
// XY6506S默认115200，确保ESP32P4也是115200
```

### 性能监控命令

#### 实时监控脚本
```bash
# 监控关键日志
idf.py monitor | grep -E "(PowerController|ModbusController|Timer callback)"

# 监控更新频率
idf.py monitor | grep "Starting async display update" | while read line; do
    echo "$(date '+%H:%M:%S.%3N') $line"
done
```

#### 内存使用检查
```bash
# 查看任务状态
idf.py monitor | grep "Free.*KB"

# 堆内存监控
idf.py monitor | grep "heap"
```

## 🔄 版本历史

### v2.0.0 (当前版本) - 2024.11.04
**重大更新**:
- ✅ 响应时间优化：从1000ms提升到300ms
- ✅ 修复重新进入应用的状态更新问题
- ✅ 完善错误处理和自动恢复机制
- ✅ 优化通信参数和性能

**技术改进**:
- 动态任务重建机制
- 优化的Modbus通信参数
- 减少互斥锁等待时间
- 改进的日志管理

### v1.0.0 - 基础版本
**核心功能**:
- 基本Modbus-RTU通信
- LVGL用户界面
- 电压电流控制
- 预设值功能

## 🤝 贡献指南

### 开发环境设置
1. Fork本项目
2. 创建功能分支: `git checkout -b feature/new-feature`
3. 提交更改: `git commit -am 'Add new feature'`
4. 推送分支: `git push origin feature/new-feature` 
5. 创建Pull Request

### 代码规范
- 使用ESP-IDF官方代码风格
- 添加详细的函数注释
- 包含单元测试（如适用）
- 更新相关文档

### 测试要求
- 硬件在环测试
- 长时间稳定性测试
- 边界条件测试
- 性能回归测试

## 📞 技术支持

### 联系方式
- **问题反馈**: 通过GitHub Issues
- **功能建议**: 通过GitHub Discussions
- **技术文档**: 参考ESP-IDF官方文档

### 常用资源
- [ESP-IDF编程指南](https://docs.espressif.com/projects/esp-idf/)
- [LVGL图形库文档](https://docs.lvgl.io/)
- [Modbus协议规范](http://www.modbus.org/docs/Modbus_Application_Protocol_V1_1b3.pdf)
- [XY6506S用户手册](./docs/XY6506S_Manual.pdf)

## 📄 许可证

本项目采用MIT许可证 - 详见 [LICENSE](LICENSE) 文件

---

## 🎯 未来规划

### 计划功能
- [ ] 数据记录和历史曲线
- [ ] 多设备级联控制  
- [ ] 网络远程控制
- [ ] 自定义波形输出
- [ ] 负载测试模式

### 性能目标
- [ ] 响应时间进一步优化至100ms
- [ ] 支持更多电源模块型号
- [ ] 增加安全保护功能
- [ ] 提供REST API接口

---

**注意**: 使用本系统时请确保电气安全，避免短路或过载操作。建议在专业人员指导下进行高压大电流测试。