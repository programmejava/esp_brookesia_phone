# PowerController 组件框架

这是一个ESP32电源控制器应用组件的基础框架，基于ESP_Brookesia框架开发。

## 框架特性

### 🎯 **基础框架结构**
- 继承自 `ESP_Brookesia_PhoneApp`
- 完整的应用生命周期管理
- 基本的日志输出系统
- 简洁的代码结构

### � **文件结构**
```
power_controller/
├── PowerController.hpp    # 类定义头文件
├── PowerController.cpp    # 基础实现文件
├── CMakeLists.txt        # CMake构建配置
├── README.md            # 本说明文件
└── assets/              # 资源文件目录
    └── README.md        # 资源文件说明
```

### 🏗️ **类设计**

#### PowerController 类
继承自 `ESP_Brookesia_PhoneApp`，提供应用基础框架。

**主要方法：**
- `init()`: 初始化组件
- `run()`: 启动应用主逻辑
- `back()`: 处理返回操作
- `close()`: 关闭应用，清理资源
- `pause()`: 暂停应用
- `resume()`: 恢复应用

**成员变量：**
- `_last_update_time`: 上次更新时间戳

## 使用方法

### 1. 创建实例
```cpp
PowerController *power_controller = new PowerController();
```

### 2. 安装到系统
```cpp
phone->installApp(power_controller);
```

### 3. 扩展功能
基于此框架添加具体的功能实现。

## 开发说明

### 当前状态
这是一个**纯净的框架版本**，包含：
- ✅ 基础类结构定义
- ✅ 应用生命周期方法框架
- ✅ 简洁的日志输出
- ✅ 最小依赖配置

### 可扩展功能
在此框架基础上可以添加：
- � 电池监控功能
- ⚡ 电源管理功能
- �️ 系统信息显示
- 🎛️ 用户界面设计
- � 配置存储管理

### 扩展步骤
1. 在 `run()` 方法中添加主要功能逻辑
2. 在头文件中添加所需的成员变量和方法
3. 根据需要添加UI界面设计
4. 实现具体的硬件控制代码
5. 添加应用图标资源

## 依赖组件

- `esp_brookesia`: 应用框架基础

## 注意事项

1. 这是一个最小化的框架，可根据需求逐步扩展
2. 添加新功能时需要在CMakeLists.txt中添加相应的依赖
3. UI功能需要添加lvgl依赖
4. 硬件控制需要添加driver等相关依赖

## 版本历史

- v1.0 (2025-10-29): 纯净框架版本，仅包含基础应用结构