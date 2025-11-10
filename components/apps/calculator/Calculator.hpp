/**
 * @file Calculator.hpp
 * @brief ESP32科学计算器应用程序头文件
 * @details 这是一个基于LVGL图形库的科学计算器应用，支持基础四则运算、
 *          科学函数计算、内存操作、角度模式切换等功能
 * @author ESP32开发团队
 * @date 2025年9月26日
 */

#pragma once

#include "lvgl.h"
#include "esp_brookesia.hpp"

/**
 * @brief 角度模式枚举
 * @details 用于三角函数计算时的角度单位选择
 */
enum AngleMode {
    ANGLE_DEG,  ///< 角度模式 (0-360度)
    ANGLE_RAD   ///< 弧度模式 (0-2π弧度)
};

/**
 * @brief 科学计算器主类
 * @details 继承自ESP_Brookesia_PhoneApp，实现了一个功能完整的科学计算器
 *          包含基础运算、科学函数、内存操作、角度模式切换等功能
 */
class Calculator: public ESP_Brookesia_PhoneApp
{
public:
    /**
     * @brief 构造函数
     * @details 初始化计算器应用，设置应用名称和图标
     */
	Calculator();
	
    /**
     * @brief 析构函数
     * @details 清理资源，释放内存
     */
	~Calculator();

    /**
     * @brief 运行计算器应用
     * @details 创建用户界面，设置按钮布局和显示区域
     * @return true 运行成功，false 运行失败
     */
    bool run(void);
    
    /**
     * @brief 返回按钮处理函数
     * @details 当用户点击返回按钮时调用此函数
     * @return true 处理成功，false 处理失败
     */
    bool back(void);
    
    /**
     * @brief 关闭应用函数
     * @details 当应用需要关闭时调用此函数
     * @return true 关闭成功，false 关闭失败
     */
    bool close(void);

    /**
     * @brief 初始化函数
     * @details 重写父类的初始化函数
     * @return true 初始化成功，false 初始化失败
     */
    bool init(void) override;

    // ========== 输入验证函数 ==========
    
    /**
     * @brief 检查当前输入是否以0开头
     * @details 用于判断是否需要删除前导0
     * @return true 以0开头，false 不以0开头
     */
    bool isStartZero(void);
    
    /**
     * @brief 检查当前输入是否以数字结尾
     * @details 用于验证输入的有效性
     * @return true 以数字结尾，false 不以数字结尾
     */
    bool isStartNum(void);
    
    /**
     * @brief 检查当前输入是否以百分号结尾
     * @details 用于判断百分比操作的有效性
     * @return true 以%结尾，false 不以%结尾
     */
    bool isStartPercent(void);
    
    /**
     * @brief 检查小数点输入是否合法
     * @details 确保一个数字中只有一个小数点
     * @return true 可以输入小数点，false 不能输入小数点
     */
    bool isLegalDot(void);

    // ========== 计算引擎函数 ==========
    
    /**
     * @brief 基础计算函数
     * @details 计算包含基本四则运算和百分比的表达式
     * @param input 输入的数学表达式字符串
     * @return 计算结果
     */
    double calculate(const char *input);
    
    /**
     * @brief 科学计算函数
     * @details 计算包含科学函数、常数等复杂表达式
     * @param input 输入的科学计算表达式字符串
     * @return 计算结果
     */
    double evaluateScientific(const char *input);
    
    /**
     * @brief 应用科学函数
     * @details 对给定值应用指定的科学函数（如sin, cos, ln等）
     * @param func 函数名称字符串
     * @param value 输入值
     * @return 函数计算结果
     */
    double applyFunction(const char *func, double value);
    
    /**
     * @brief 角度转弧度
     * @details 将角度值转换为弧度值，用于三角函数计算
     * @param deg 角度值
     * @return 弧度值
     */
    double degToRad(double deg);
    
    /**
     * @brief 弧度转角度
     * @details 将弧度值转换为角度值
     * @param rad 弧度值
     * @return 角度值
     */
    double radToDeg(double rad);

    // ========== UI组件对象 ==========
    
    int formula_len;                ///< 当前公式字符串长度
    lv_obj_t *keyboard;            ///< 按钮矩阵对象（虚拟键盘）
    lv_obj_t *history_label;       ///< 历史记录显示标签
    lv_obj_t *formula_label;       ///< 当前公式显示标签
    lv_obj_t *result_label;        ///< 计算结果显示标签
    lv_obj_t *mode_label;          ///< 角度模式指示标签（DEG/RAD）
    lv_obj_t *memory_label;        ///< 内存状态指示标签（M）
    uint16_t _height;              ///< 应用窗口高度
    uint16_t _width;               ///< 应用窗口宽度

    // ========== 计算器状态变量 ==========
    
    AngleMode angle_mode;          ///< 当前角度模式（角度/弧度）
    double memory_value;           ///< 内存中存储的数值
    bool has_memory;               ///< 内存是否有有效数据
    bool is_scientific_mode;       ///< 是否处于科学模式（显示科学函数键盘）

private:
    /**
     * @brief 按键事件回调函数
     * @details 处理虚拟键盘上所有按键的点击事件
     * @param e LVGL事件对象指针
     */
    static void keyboard_event_cb(lv_event_t *e);
    
    /**
     * @brief 模式按钮事件回调函数
     * @details 处理角度模式切换按钮的点击事件
     * @param e LVGL事件对象指针
     */
    static void mode_button_event_cb(lv_event_t *e);
};
