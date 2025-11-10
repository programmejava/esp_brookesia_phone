/**
 * @file Calculator.cpp
 * @brief ESP32科学计算器应用程序实现文件
 * @details 实现了一个功能完整的科学计算器，包含：
 *          - 基础四则运算（加、减、乘、除）
 *          - 科学函数（三角函数、对数函数、指数函数等）
 *          - 内存操作（存储、调用、清除等）
 *          - 角度模式切换（角度/弧度）
 *          - 美观的用户界面设计
 * @author ESP32开发团队
 * @date 2025年9月26日
 * @version 2.0
 */

#include <math.h>      // 数学函数库
#include <vector>      // STL向量容器
#include <string>      // STL字符串类
#include <cstring>     // C字符串处理函数
#include "Calculator.hpp"

using namespace std;

// 声明计算器应用图标
LV_IMG_DECLARE(img_app_calculator);

// ========== UI布局相关常量定义 ==========

#define KEYBOARD_H_PERCENT      70                              ///< 键盘区域占屏幕高度的百分比
#define KEYBOARD_FONT           &lv_font_montserrat_20          ///< 键盘按钮字体
#define KEYBOARD_SPECIAL_COLOR  lv_color_make(0, 0x99, 0xff)   ///< 特殊按钮文字颜色（蓝色）
#define KEYBOARD_BG_COLOR       lv_color_make(240, 240, 240)   ///< 键盘背景颜色（浅灰）
#define KEYBOARD_BTN_COLOR      lv_color_make(255, 255, 255)   ///< 普通按钮颜色（白色）
#define KEYBOARD_NUMBER_COLOR   lv_color_make(50, 50, 50)      ///< 数字按钮文字颜色（深灰）

// ========== 显示区域相关常量定义 ==========

#define LABEL_PAD               5                              ///< 标签内边距
#define LABEL_FONT_SMALL        &lv_font_montserrat_28         ///< 小字体（结果显示）
#define LABEL_FONT_BIG          &lv_font_montserrat_36         ///< 大字体（公式显示）
#define LABEL_COLOR             lv_color_make(100, 100, 100)  ///< 标签文字颜色
#define LABEL_FORMULA_LEN_MAX   256                            ///< 公式字符串最大长度

/**
 * @brief 计算器虚拟键盘布局定义
 * @details 定义了8行×5列的按钮布局，包含以下功能分区：
 *          第1行：内存操作按钮（MC, MR, M+, M-, MS, Mv）
 *          第2行：模式切换和常数（2nd, pi, e, C, 退格）
 *          第3行：科学函数（x^2, 1/x, |x|, exp, mod）
 *          第4行：科学函数和运算符（sqrt, (, ), n!, /）
 *          第5行：幂函数和数字（x^y, 7, 8, 9, x）
 *          第6行：指数函数和数字（10^x, 4, 5, 6, -）
 *          第7行：对数函数和数字（log, 1, 2, 3, +）
 *          第8行：函数和基本操作（ln, +/-, 0, ., =）
 */
static const char *keyboard_map[] = {
    // 第1行：内存操作功能区
    "MC", "MR", "M+", "M-", "MS", "Mv", "\n",
    
    // 第2行：模式切换和清除功能区  
    "2nd", "pi", "e", "C", LV_SYMBOL_BACKSPACE, "\n",
    
    // 第3行：基础科学函数区
    "x^2", "1/x", "|x|", "exp", "mod", "\n",
    
    // 第4行：高级科学函数区
    "sqrt", "(", ")", "n!", "/", "\n",
    
    // 第5行：幂运算和数字7-9
    "x^y", "7", "8", "9", "x", "\n",
    
    // 第6行：指数函数和数字4-6
    "10^x", "4", "5", "6", "-", "\n",
    
    // 第7行：对数函数和数字1-3
    "log", "1", "2", "3", "+", "\n",
    
    // 第8行：自然对数和基础操作
    "ln", "+/-", "0", ".", "=", ""
};

/**
 * @brief 科学模式计算器虚拟键盘布局定义
 * @details 定义了科学模式下的84×5列的按键布局，包含三角函数和高级函数
 */
static const char *keyboard_map_scientific[] = {
    // 第1行：内存操作功能区
    "MC", "MR", "M+", "M-", "MS", "Mv", "\n",
    
    // 第2行：模式切换和清除功能区
    "2nd", "pi", "e", "C", LV_SYMBOL_BACKSPACE, "\n",
    
    // 第3行：三角函数区
    "sin", "cos", "tan", "exp", "mod", "\n",
    
    // 第4行：高级科学函数区
    "sqrt", "(", ")", "n!", "/", "\n",
    
    // 第5行：幂运算和数字7-9
    "x^y", "7", "8", "9", "x", "\n",
    
    // 第6行：指数函数和数字4-6
    "10^x", "4", "5", "6", "-", "\n",
    
    // 第7行：对数函数和数字1-3
    "log", "1", "2", "3", "+", "\n",
    
    // 第8行：自然对数和基础操作
    "ln", "+/-", "0", ".", "=", ""
};

/**
 * @brief 构造函数 - 初始化计算器应用
 * @details 调用父类构造函数设置应用名称和图标，初始化计算器状态
 */
Calculator::Calculator():
    ESP_Brookesia_PhoneApp("Calculator", &img_app_calculator, true)
{
    angle_mode = ANGLE_DEG;    // 默认角度模式为度数
    memory_value = 0.0;        // 初始化内存值为0
    has_memory = false;        // 初始化内存状态为空
    is_scientific_mode = false; // 默认为基础模式
}

/**
 * @brief 析构函数 - 清理资源
 * @details 目前无需特殊清理操作，LVGL会自动清理UI对象
 */
Calculator::~Calculator()
{
    // 析构函数目前为空，因为LVGL会自动管理UI对象的生命周期
}

/**
 * @brief 运行计算器应用主函数
 * @details 创建并配置整个计算器用户界面，包括：
 *          1. 计算显示区域（历史记录、模式指示器、公式显示、结果显示）
 *          2. 虚拟键盘区域（按钮矩阵）
 *          3. 设置各种UI样式和事件处理
 * @return bool 成功返回true，失败返回false
 */
bool Calculator::run(void)
{
    // ========== 获取应用窗口尺寸 ==========
    lv_area_t area = getVisualArea();          // 获取可视区域
    _width = area.x2 - area.x1;               // 计算窗口宽度
    _height = area.y2 - area.y1;              // 计算窗口高度
    formula_len = 1;                          // 初始化公式长度为1（显示"0"）

    // ========== 计算各区域高度 ==========
    int keyboard_h = (int)(_height * KEYBOARD_H_PERCENT / 100.0);  // 键盘区域高度
    int label_h = _height - keyboard_h;                            // 显示区域总高度
    int text_h = label_h - 2 * LABEL_PAD;                        // 文本显示区域净高度

    // ========== 创建虚拟键盘（按钮矩阵）==========
    keyboard = lv_btnmatrix_create(lv_scr_act());                        // 创建按钮矩阵对象
    lv_btnmatrix_set_map(keyboard, keyboard_map);                        // 设置按钮布局映射
    lv_obj_set_size(keyboard, _width, keyboard_h);                       // 设置键盘尺寸
    lv_obj_set_style_text_font(keyboard, KEYBOARD_FONT, 0);             // 设置按钮文字字体
    lv_obj_align(keyboard, LV_ALIGN_BOTTOM_MID, 0, 0);                 // 对齐到屏幕底部中央
    lv_obj_add_event_cb(keyboard, keyboard_event_cb, LV_EVENT_ALL, this); // 添加按钮事件处理回调
    
    // ========== 设置键盘整体样式 ==========
    lv_obj_set_style_bg_color(keyboard, KEYBOARD_BG_COLOR, 0);          // 设置键盘背景色
    lv_obj_set_style_border_width(keyboard, 1, 0);                      // 设置边框宽度
    lv_obj_set_style_border_color(keyboard, lv_color_make(200, 200, 200), 0); // 设置边框颜色
    lv_obj_set_style_radius(keyboard, 8, 0);                            // 设置圆角半径
    lv_obj_set_style_pad_all(keyboard, 3, 0);                          // 设置内边距
    lv_obj_set_style_pad_gap(keyboard, 2, 0);                          // 设置按钮间距
    
    // ========== 设置按钮默认样式 ==========
    lv_obj_set_style_bg_color(keyboard, KEYBOARD_BTN_COLOR, LV_PART_ITEMS);     // 按钮背景色
    lv_obj_set_style_border_width(keyboard, 1, LV_PART_ITEMS);                  // 按钮边框宽度
    lv_obj_set_style_border_color(keyboard, lv_color_make(200, 200, 200), LV_PART_ITEMS); // 按钮边框颜色
    lv_obj_set_style_radius(keyboard, 6, LV_PART_ITEMS);                        // 按钮圆角
    lv_obj_set_style_shadow_width(keyboard, 2, LV_PART_ITEMS);                  // 按钮阴影宽度
    lv_obj_set_style_shadow_color(keyboard, lv_color_make(0, 0, 0), LV_PART_ITEMS); // 阴影颜色
    lv_obj_set_style_shadow_opa(keyboard, LV_OPA_20, LV_PART_ITEMS);            // 阴影透明度
    lv_obj_set_style_text_color(keyboard, KEYBOARD_NUMBER_COLOR, LV_PART_ITEMS); // 按钮文字颜色

    // ========== 创建显示区域容器 ==========
    lv_obj_t *label_obj = lv_obj_create(lv_scr_act());                   // 创建显示区域主容器
    lv_obj_set_size(label_obj, _width, label_h);                         // 设置显示区域尺寸
    lv_obj_align(label_obj, LV_ALIGN_TOP_MID, 0, 0);                    // 对齐到屏幕顶部中央
	lv_obj_set_style_radius(label_obj, 8, 0);                            // 设置容器圆角
	lv_obj_set_style_border_width(label_obj, 1, 0);                      // 设置边框宽度
	lv_obj_set_style_border_color(label_obj, lv_color_make(200, 200, 200), 0); // 设置边框颜色
	lv_obj_set_style_bg_color(label_obj, lv_color_make(250, 250, 250), 0); // 设置背景颜色（浅灰）
	lv_obj_set_style_pad_all(label_obj, 10, 0);                          // 设置内边距
    lv_obj_set_style_text_font(label_obj, LABEL_FONT_SMALL, 0);          // 设置默认字体
    lv_obj_set_flex_flow(label_obj, LV_FLEX_FLOW_COLUMN);               // 设置子元素垂直排列
    lv_obj_set_flex_align(label_obj, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_END); // 底部对齐
    lv_obj_set_style_pad_row(label_obj, LABEL_PAD, 0);                  // 设置行间距

    // ========== 创建模式和内存指示器区域 ==========
    lv_obj_t *indicator_obj = lv_obj_create(label_obj);                  // 创建指示器容器
    lv_obj_set_size(indicator_obj, _width, text_h / 6);                  // 设置指示器区域大小
	lv_obj_set_style_radius(indicator_obj, 0, 0);                        // 无圆角
	lv_obj_set_style_border_width(indicator_obj, 0, 0);                  // 无边框
	lv_obj_set_style_pad_all(indicator_obj, 0, 0);                       // 无内边距
    lv_obj_set_style_bg_opa(indicator_obj, LV_OPA_TRANSP, 0);           // 透明背景
    lv_obj_set_flex_flow(indicator_obj, LV_FLEX_FLOW_ROW);              // 水平排列子元素

    // ========== 创建角度模式指示器（左侧）==========
    mode_label = lv_label_create(indicator_obj);                         // 创建角度模式标签
    lv_obj_set_style_text_font(mode_label, &lv_font_montserrat_16, 0);  // 设置字体大小
    lv_obj_set_style_text_color(mode_label, lv_color_make(0, 100, 200), 0); // 设置文字颜色（蓝色）
    lv_label_set_text(mode_label, "DEG");                               // 默认显示角度模式
    lv_obj_align(mode_label, LV_ALIGN_LEFT_MID, 10, 0);                 // 左对齐，偏移10像素

    // ========== 创建内存状态指示器（右侧）==========
    memory_label = lv_label_create(indicator_obj);                       // 创建内存状态标签
    lv_obj_set_style_text_font(memory_label, &lv_font_montserrat_16, 0); // 设置字体大小
    lv_obj_set_style_text_color(memory_label, lv_color_make(200, 100, 0), 0); // 设置文字颜色（橙色）
    lv_label_set_text(memory_label, "");                                 // 初始为空（无内存数据）
    lv_obj_align(memory_label, LV_ALIGN_RIGHT_MID, -10, 0);             // 右对齐，偏移-10像素

    // ========== 创建计算历史记录显示区域 ==========
    history_label = lv_textarea_create(label_obj);                       // 创建历史记录文本区域
	lv_obj_set_style_radius(history_label, 4, 0);                        // 设置圆角
	lv_obj_set_style_border_width(history_label, 0, 0);                  // 无边框
	lv_obj_set_style_bg_opa(history_label, LV_OPA_TRANSP, 0);           // 透明背景
	lv_obj_set_style_pad_all(history_label, 5, 0);                       // 设置内边距
    lv_obj_set_size(history_label, _width - 20, text_h / 4);             // 设置历史记录区域大小（占1/4高度）
    lv_obj_add_flag(history_label, LV_OBJ_FLAG_SCROLLABLE);              // 启用滚动功能
    lv_obj_set_style_text_align(history_label, LV_TEXT_ALIGN_RIGHT, 0);  // 文字右对齐
    lv_obj_set_style_opa(history_label, LV_OPA_TRANSP, LV_PART_CURSOR);  // 隐藏光标
    lv_obj_set_style_text_color(history_label, lv_color_make(120, 120, 120), 0); // 灰色文字
    lv_obj_set_style_text_font(history_label, &lv_font_montserrat_18, 0); // 中等字体
    lv_textarea_set_text(history_label, "");                             // 初始为空

    // ========== 创建公式输入显示区域 ==========
    lv_obj_t *formula_label_obj = lv_obj_create(label_obj);              // 创建公式显示容器
    lv_obj_set_size(formula_label_obj, _width - 20, text_h / 3);         // 设置容器大小（占1/3高度）
	lv_obj_set_style_radius(formula_label_obj, 4, 0);                    // 设置圆角
	lv_obj_set_style_border_width(formula_label_obj, 0, 0);              // 无边框
	lv_obj_set_style_pad_all(formula_label_obj, 8, 0);                   // 设置内边距
    lv_obj_set_style_bg_opa(formula_label_obj, LV_OPA_TRANSP, 0);       // 透明背景

    formula_label = lv_label_create(formula_label_obj);                   // 创建公式显示标签
    lv_obj_set_size(formula_label, LV_SIZE_CONTENT, LV_SIZE_CONTENT);    // 自适应大小
    lv_obj_align(formula_label, LV_ALIGN_RIGHT_MID, 0, 0);              // 右对齐居中
    lv_obj_set_style_text_align(formula_label, LV_TEXT_ALIGN_RIGHT, 0);  // 文字右对齐
    lv_obj_set_style_text_font(formula_label, LABEL_FONT_BIG, 0);       // 大字体显示
    lv_obj_set_style_text_color(formula_label, lv_color_make(30, 30, 30), 0); // 深色文字
    lv_label_set_text(formula_label, "0");                               // 初始显示"0"

    // ========== 创建计算结果显示区域 ==========
    lv_obj_t *result_label_obj = lv_obj_create(label_obj);               // 创建结果显示容器
    lv_obj_set_size(result_label_obj, _width - 20, text_h / 3);          // 设置容器大小（占1/3高度）
	lv_obj_set_style_radius(result_label_obj, 4, 0);                     // 设置圆角
	lv_obj_set_style_border_width(result_label_obj, 0, 0);               // 无边框
	lv_obj_set_style_pad_all(result_label_obj, 8, 0);                    // 设置内边距
    lv_obj_set_style_bg_opa(result_label_obj, LV_OPA_TRANSP, 0);        // 透明背景

    result_label = lv_label_create(result_label_obj);                     // 创建结果显示标签
    lv_obj_set_size(result_label, LV_SIZE_CONTENT, LV_SIZE_CONTENT);     // 自适应大小
    lv_obj_align(result_label, LV_ALIGN_RIGHT_MID, 0, 0);               // 右对齐居中
    lv_obj_set_style_text_color(result_label, lv_color_make(100, 100, 100), 0); // 中灰色文字
    lv_obj_set_style_text_align(result_label, LV_TEXT_ALIGN_RIGHT, 0);    // 文字右对齐
    lv_obj_set_style_text_font(result_label, LABEL_FONT_SMALL, 0);       // 小字体显示
    lv_label_set_text(result_label, "= 0");                              // 初始显示"= 0"

    return true;
}

/**
 * @brief 返回按钮回调函数
 * @details 当用户点击导航栏的左侧返回按钮时调用此函数
 *          通知系统核心该应用已关闭，释放相关资源
 * @return true 成功处理返回操作
 */
bool Calculator::back(void)
{
    notifyCoreClosed();    // 通知系统核心应用已关闭

    return true;
}

/**
 * @brief 应用关闭回调函数
 * @details 当应用需要被关闭时调用此函数
 *          可以在这里执行清理操作，如保存状态、释放资源等
 * @return true 成功处理关闭操作
 */
bool Calculator::close(void)
{
    // 目前无需特殊的关闭处理逻辑
    return true;
}

/**
 * @brief 应用初始化函数
 * @details 在应用启动时进行必要的初始化操作
 *          目前计算器的初始化主要在构造函数和run()函数中完成
 * @return true 初始化成功
 */
bool Calculator::init(void)
{
    // 目前无需特殊的初始化逻辑
    return true;
}

/**
 * @brief 检查当前输入是否以无意义的0开头
 * @details 用于判断是否需要删除前导0，避免出现"007"这样的输入
 *          检查两种情况：
 *          1. 整个输入只是一个"0"
 *          2. 运算符后跟着一个"0"
 * @return true 以无意义的0开头，false 不以无意义的0开头
 */
bool Calculator::isStartZero(void)
{
    const char *text = lv_label_get_text(formula_label);  // 获取当前公式文本
    int text_len = strlen(text);                          // 计算文本长度

    // 情况1：整个输入只是一个"0"
    if ((text_len == 1) && (text[0] == '0')) {
        return true;
    }
    
    // 情况2：运算符后跟着"0"（如 "5+0"中的0）
    if ((text[text_len - 1] == '0') &&
        (text[text_len - 2] > '9') &&
        (text[text_len - 2] < '0')) {
        return true;
    }

    return false;
}

/**
 * @brief 检查当前输入是否以数字结尾
 * @details 用于验证输入的有效性，判断是否可以继续输入数字或运算符
 * @return true 以数字结尾，false 不以数字结尾
 */
bool Calculator::isStartNum(void)
{
    const char *text = lv_label_get_text(formula_label);  // 获取当前公式文本
    int text_len = strlen(text);                          // 计算文本长度

    // 检查最后一个字符是否为数字（0-9）
    if ((text[text_len - 1] >= '0') && (text[text_len - 1] <= '9')) {
        return true;
    }

    return false;
}

/**
 * @brief 检查当前输入是否以百分号结尾
 * @details 用于判断百分比操作的有效性，确定后续操作的合法性
 * @return true 以%结尾，false 不以%结尾
 */
bool Calculator::isStartPercent(void)
{
    const char *text = lv_label_get_text(formula_label);  // 获取当前公式文本
    int text_len = strlen(text);                          // 计算文本长度

    // 检查最后一个字符是否为百分号
    if (text[text_len - 1] == '%') {
        return true;
    }

    return false;
}

/**
 * @brief 检查小数点输入是否合法
 * @details 确保当前数字中没有重复的小数点，避免出现"3.14.15"这样的输入
 *          从字符串末尾向前搜索，遇到小数点则返回false，
 *          遇到非数字字符则说明是新的数字，可以输入小数点
 * @return true 可以输入小数点，false 不能输入小数点
 */
bool Calculator::isLegalDot(void)
{
    const char *text = lv_label_get_text(formula_label);  // 获取当前公式文本
    int text_len = strlen(text);                          // 计算文本长度

    // 从字符串末尾向前检查当前数字中是否已有小数点
    while (text_len-- > 0) {
        if (text[text_len] == '.') {
            // 当前数字中已有小数点，不能再输入
            return false;
        }
        else if ((text[text_len] < '0') || (text[text_len] > '9')) {
            // 遇到非数字字符，说明是新的数字开始，可以输入小数点
            return true;
        }
    }

    // 整个字符串都是数字，且没有小数点，可以输入
    return true;
}

/**
 * @brief 基础计算函数
 * @details 使用栈算法计算包含四则运算和百分比的数学表达式
 *          算法流程：
 *          1. 遍历输入字符串，解析数字（包括小数）
 *          2. 遇到运算符时，根据前一个运算符的优先级进行计算
 *          3. 加减法直接入栈，乘除法立即计算
 *          4. 最后将栈中所有数值相加得到结果
 * @param input 输入的数学表达式字符串
 * @return 计算结果
 */
double Calculator::calculate(const char *input)
{
    vector<double> stk;         // 数字栈，用于存储待计算的数值
    int input_len = strlen(input);  // 输入字符串长度
    double num = 0;             // 当前解析的数字
    bool dot_flag = false;      // 小数点标记
    int dot_len = 0;            // 小数位数计数器
    char pre_sign = '+';        // 前一个运算符，默认为正号

    // 遍历输入字符串进行解析和计算
    for (int i = 0; i < input_len; i++) {
        if (input[i] == '.') {
            // 遇到小数点，设置小数标记
            dot_flag = true;
            dot_len = 0;
        }
        else if (isdigit(input[i])) {
            // 解析数字字符
            if (!dot_flag) {
                // 整数部分：num = num * 10 + 当前数字
                num = num * 10 + input[i] - '0';
            }
            else {
                // 小数部分：累加小数位
                num += (input[i] - '0') / pow(10.0, ++dot_len);
            }
        }
        else if (input[i] == '%') {
            // 百分号：将数字除以100
            num /= 100.0;
        }
        else if (i != input_len - 1) {
            // 遇到运算符（非最后一个字符）
            dot_flag = false;
            dot_len = 0;
            
            // 根据前一个运算符进行计算
            switch (pre_sign) {
                case '+':
                    stk.push_back(num);     // 加法：直接入栈
                    break;
                case '-':
                    stk.push_back(-num);    // 减法：负数入栈
                    break;
                case 'x':
                    stk.back() *= num;      // 乘法：与栈顶元素相乘
                    break;
                default:  // 除法
                    if (num != 0) {
                        stk.back() /= num;  // 栈顶元素除以当前数字
                    }
                    else {
                        return 0;           // 除零错误，返回0
                    }
            }
            num = 0;                        // 重置当前数字
            pre_sign = input[i];            // 更新运算符
        }

        // 处理最后一个数字
        if (i == input_len - 1) {
            switch (pre_sign) {
                case '+':
                    stk.push_back(num);
                    break;
                case '-':
                    stk.push_back(-num);
                    break;
                case 'x':
                    stk.back() *= num;
                    break;
                default:  // 除法
                    if (num != 0) {
                        stk.back() /= num;
                    }
                    else {
                        return 0;
                    }
            }
            num = 0;
        }
    }

    // 将栈中所有数值相加得到最终结果
    for (int i = 0; i < stk.size(); i++) {
        num += stk.at(i);
    }

    return num;
}

/**
 * @brief 角度转弧度
 * @details 将角度制的角度值转换为弧度制，用于三角函数计算
 *          转换公式：弧度 = 角度 × π / 180
 * @param deg 角度值（0-360度）
 * @return 对应的弧度值（0-2π弧度）
 */
double Calculator::degToRad(double deg)
{
    return deg * M_PI / 180.0;
}

/**
 * @brief 弧度转角度
 * @details 将弧度制的角度值转换为角度制
 *          转换公式：角度 = 弧度 × 180 / π
 * @param rad 弧度值（0-2π弧度）
 * @return 对应的角度值（0-360度）
 */
double Calculator::radToDeg(double rad)
{
    return rad * 180.0 / M_PI;
}

/**
 * @brief 应用科学函数计算
 * @details 根据函数名称对给定值执行相应的科学函数运算
 *          支持三角函数、对数函数、指数函数等
 *          三角函数会根据当前角度模式进行角度/弧度转换
 * @param func 函数名称字符串（如"sin", "cos", "ln", "log"等）
 * @param value 输入的数值
 * @return 函数运算结果，如果函数名称不识别则返回原值
 */
double Calculator::applyFunction(const char *func, double value)
{
    // 三角函数 - 根据角度模式进行转换
    if (strcmp(func, "sin") == 0) {
        return sin(angle_mode == ANGLE_DEG ? degToRad(value) : value);
    } else if (strcmp(func, "cos") == 0) {
        return cos(angle_mode == ANGLE_DEG ? degToRad(value) : value);
    } else if (strcmp(func, "tan") == 0) {
        return tan(angle_mode == ANGLE_DEG ? degToRad(value) : value);
    } 
    // 对数函数
    else if (strcmp(func, "ln") == 0) {
        return log(value);      // 自然对数（以e为底）
    } else if (strcmp(func, "log") == 0) {
        return log10(value);    // 常用对数（以10为底）
    } 
    // 根式函数
    else if (strcmp(func, "sqrt") == 0 || strcmp(func, "√") == 0) {
        return sqrt(value);     // 平方根
    } 
    // 指数函数
    else if (strcmp(func, "exp") == 0) {
        return exp(value);      // e的x次幂
    }
    
    // 未知函数名称，返回原值
    return value;
}

/**
 * @brief 科学计算表达式求值函数
 * @details 处理包含各种科学函数、数学常数、括号和复杂运算符的完整数学表达式
 *          支持复杂表达式如：sqrt(4)+8x2-(3-1)+log(10)+|-1|
 *          支持的功能：
 *          1. 数学常数替换（π、e）
 *          2. 幂运算处理（^）
 *          3. 自然对数和常用对数（ln、log）
 *          4. 三角函数计算（sin、cos、tan，支持角度/弧度模式）
 *          5. 平方根函数（sqrt）
 *          6. 绝对值运算（|x|）
 *          7. 普通括号表达式计算（如 (7-3) * 2）
 *          8. 其他科学函数（exp等）
 *          算法采用多步骤递归下降解析和字符串替换相结合的方式，
 *          按照数学运算优先级依次处理各种函数和运算符
 * @param input 包含各种科学函数、常数和括号的完整数学表达式字符串
 * @return 计算结果
 */
double Calculator::evaluateScientific(const char *input)
{
    std::string expr = input;
    
    // 第一步：替换数学常数
    size_t pos = 0;
    // 替换圆周率π
    while ((pos = expr.find("pi", pos)) != std::string::npos) {
        expr.replace(pos, 2, std::to_string(M_PI));
        pos += std::to_string(M_PI).length();
    }
    
    pos = 0;
    // 替换自然常数e（需要避免与exp函数冲突）
    while ((pos = expr.find("e", pos)) != std::string::npos) {
        // 确保不是"exp"函数的一部分
        if (pos == 0 || (expr[pos-1] != 'x' && (pos < 2 || expr.substr(pos-2, 3) != "exp"))) {
            expr.replace(pos, 1, std::to_string(M_E));
            pos += std::to_string(M_E).length();
        } else {
            pos++;
        }
    }
    
    // 第二步：处理幂运算（^）
    pos = 0;
    while ((pos = expr.find("^", pos)) != std::string::npos) {
        // 寻找底数（^号前的数字）
        size_t start = pos;
        while (start > 0 && (isdigit(expr[start-1]) || expr[start-1] == '.')) {
            start--;
        }
        
        // 寻找指数（^号后的数字）
        size_t end = pos + 1;
        while (end < expr.length() && (isdigit(expr[end]) || expr[end] == '.')) {
            end++;
        }
        
        // 如果找到了完整的底数和指数，进行幂运算
        if (start < pos && end > pos + 1) {
            double base = std::stod(expr.substr(start, pos - start));       // 底数
            double exponent = std::stod(expr.substr(pos + 1, end - pos - 1)); // 指数
            double result = pow(base, exponent);                             // 计算结果
            
            // 将幂运算结果替换回原表达式
            expr.replace(start, end - start, std::to_string(result));
            pos = start + std::to_string(result).length();
        } else {
            pos++;
        }
    }
    
    // 第三步：处理mod取模运算
    pos = 0;
    while ((pos = expr.find("mod", pos)) != std::string::npos) {
        // 寻找左操作数（mod前的数字）
        size_t start = pos;
        while (start > 0 && (isdigit(expr[start-1]) || expr[start-1] == '.')) {
            start--;
        }
        
        // 寻找右操作数（mod后的数字）
        size_t end = pos + 3; // "mod"的长度
        while (end < expr.length() && (isdigit(expr[end]) || expr[end] == '.')) {
            end++;
        }
        
        // 如果找到了完整的左右操作数，进行取模运算
        if (start < pos && end > pos + 3) {
            double left = std::stod(expr.substr(start, pos - start));
            double right = std::stod(expr.substr(pos + 3, end - pos - 3));
            if (right != 0) { // 防止除零错误
                double result = fmod(left, right); // 计算取模结果
                expr.replace(start, end - start, std::to_string(result));
                pos = start + std::to_string(result).length();
            } else {
                pos++;
            }
        } else {
            pos++;
        }
    }
    
    // 第四步：处理自然对数函数ln()
    pos = 0;
    while ((pos = expr.find("ln(", pos)) != std::string::npos) {
        size_t paren_count = 1;     // 括号计数器，用于匹配嵌套括号
        size_t end = pos + 3;       // 从"ln("后开始查找
        
        // 找到与左括号匹配的右括号
        while (end < expr.length() && paren_count > 0) {
            if (expr[end] == '(') paren_count++;        // 遇到左括号，计数器+1
            else if (expr[end] == ')') paren_count--;   // 遇到右括号，计数器-1
            end++;
        }
        
        // 如果找到了匹配的括号对
        if (paren_count == 0) {
            // 提取括号内的表达式
            std::string inner = expr.substr(pos + 3, end - pos - 4);
            // 递归计算括号内表达式的值
            double inner_val = evaluateScientific(inner.c_str());
            // 计算自然对数
            double result = log(inner_val);
            // 将整个ln()函数替换为计算结果
            expr.replace(pos, end - pos, std::to_string(result));
            pos += std::to_string(result).length();
        } else {
            pos++;  // 如果括号不匹配，继续向前查找
        }
    }
    
    // 第五步：处理常用对数函数log()
    pos = 0;
    while ((pos = expr.find("log(", pos)) != std::string::npos) {
        size_t paren_count = 1;     // 括号计数器
        size_t end = pos + 4;       // 从"log("后开始查找
        
        // 找到与左括号匹配的右括号
        while (end < expr.length() && paren_count > 0) {
            if (expr[end] == '(') paren_count++;
            else if (expr[end] == ')') paren_count--;
            end++;
        }
        
        // 如果找到了匹配的括号对
        if (paren_count == 0) {
            // 提取括号内的表达式
            std::string inner = expr.substr(pos + 4, end - pos - 5);
            // 递归计算括号内表达式的值
            double inner_val = evaluateScientific(inner.c_str());
            // 计算以10为底的对数
            double result = log10(inner_val);
            // 将整个log()函数替换为计算结果
            expr.replace(pos, end - pos, std::to_string(result));
            pos += std::to_string(result).length();
        } else {
            pos++;  // 如果括号不匹配，继续向前查找
        }
    }
    
    // 第六步：处理三角函数 sin()、cos()、tan()
    std::vector<std::string> trig_funcs = {"sin(", "cos(", "tan("};
    for (const auto& func : trig_funcs) {
        pos = 0;
        while ((pos = expr.find(func, pos)) != std::string::npos) {
            size_t paren_count = 1;
            size_t end = pos + func.length();
            
            while (end < expr.length() && paren_count > 0) {
                if (expr[end] == '(') paren_count++;
                else if (expr[end] == ')') paren_count--;
                end++;
            }
            
            if (paren_count == 0) {
                std::string inner = expr.substr(pos + func.length(), end - pos - func.length() - 1);
                double inner_val = evaluateScientific(inner.c_str());
                double result;
                
                if (func == "sin(") {
                    result = sin(angle_mode == ANGLE_DEG ? degToRad(inner_val) : inner_val);
                } else if (func == "cos(") {
                    result = cos(angle_mode == ANGLE_DEG ? degToRad(inner_val) : inner_val);
                } else { // tan
                    result = tan(angle_mode == ANGLE_DEG ? degToRad(inner_val) : inner_val);
                }
                
                expr.replace(pos, end - pos, std::to_string(result));
                pos += std::to_string(result).length();
            } else {
                pos++;
            }
        }
    }
    
    // 第七步：处理平方根函数 sqrt()
    pos = 0;
    while ((pos = expr.find("sqrt(", pos)) != std::string::npos) {
        size_t paren_count = 1;
        size_t end = pos + 5; // "sqrt("的长度
        
        while (end < expr.length() && paren_count > 0) {
            if (expr[end] == '(') paren_count++;
            else if (expr[end] == ')') paren_count--;
            end++;
        }
        
        if (paren_count == 0) {
            std::string inner = expr.substr(pos + 5, end - pos - 6);
            double inner_val = evaluateScientific(inner.c_str());
            double result = sqrt(inner_val);
            expr.replace(pos, end - pos, std::to_string(result));
            pos += std::to_string(result).length();
        } else {
            pos++;
        }
    }
    
    // 第八步：处理绝对值 |x|
    pos = 0;
    while ((pos = expr.find("|", pos)) != std::string::npos) {
        size_t end = pos + 1;
        // 寻找下一个 |
        while (end < expr.length() && expr[end] != '|') {
            end++;
        }
        
        if (end < expr.length()) {
            std::string inner = expr.substr(pos + 1, end - pos - 1);
            double inner_val = evaluateScientific(inner.c_str());
            double result = fabs(inner_val);
            expr.replace(pos, end - pos + 1, std::to_string(result));
            pos += std::to_string(result).length();
        } else {
            pos++;
        }
    }
    
    // 第九步：处理阶乘运算 n!
    pos = 0;
    while ((pos = expr.find("!", pos)) != std::string::npos) {
        // 找到!前面的数字
        size_t start = pos;
        while (start > 0 && (isdigit(expr[start-1]) || expr[start-1] == '.')) {
            start--;
        }
        
        if (start < pos) {
            double num = std::stod(expr.substr(start, pos - start));
            // 计算阶乘
            if (num >= 0 && num == floor(num) && num <= 170) {
                double factorial = 1;
                for (int i = 2; i <= (int)num; i++) {
                    factorial *= i;
                }
                expr.replace(start, pos - start + 1, std::to_string(factorial));
                pos = start + std::to_string(factorial).length();
            } else {
                pos++;
            }
        } else {
            pos++;
        }
    }
    
    // 第十步：处理普通括号表达式 ()
    pos = 0;
    while ((pos = expr.find("(", pos)) != std::string::npos) {
        // 确保不是函数调用的括号（如 ln(、log(、sin( 等）
        bool is_function = false;
        if (pos >= 2) {
            std::string before = expr.substr(pos-2, 2);
            if (before == "ln" || before == "og" || before == "in" || before == "os" || before == "an" || before == "rt") {
                pos++;
                continue;
            }
        }
        if (pos >= 3) {
            std::string before3 = expr.substr(pos-3, 3);
            if (before3 == "log" || before3 == "sin" || before3 == "cos" || before3 == "tan" || before3 == "exp") {
                pos++;
                continue;
            }
        }
        if (pos >= 4) {
            std::string before4 = expr.substr(pos-4, 4);
            if (before4 == "sqrt") {
                pos++;
                continue;
            }
        }
        
        size_t paren_count = 1;     // 括号计数器，用于匹配嵌套括号
        size_t end = pos + 1;       // 从"("后开始查找
        
        // 找到与左括号匹配的右括号
        while (end < expr.length() && paren_count > 0) {
            if (expr[end] == '(') paren_count++;        // 遇到左括号，计数器+1
            else if (expr[end] == ')') paren_count--;   // 遇到右括号，计数器-1
            end++;
        }
        
        // 如果找到了匹配的括号对
        if (paren_count == 0) {
            // 提取括号内的表达式
            std::string inner = expr.substr(pos + 1, end - pos - 2);
            // 递归计算括号内表达式的值
            double inner_val = evaluateScientific(inner.c_str());
            // 将整个括号表达式替换为计算结果
            expr.replace(pos, end - pos, std::to_string(inner_val));
            pos += std::to_string(inner_val).length();
        } else {
            pos++;  // 如果括号不匹配，继续向前查找
        }
    }
    
    // 第十一步：使用基础计算器处理剩余的四则运算
    return calculate(expr.c_str());
}

/**
 * @brief 键盘事件回调函数
 * @details 处理计算器按键矩阵的所有用户交互事件
 *          主要功能包括：
 *          1. 绘制事件处理 - 为不同类型的按键设置不同的颜色主题
 *          2. 按键点击事件处理 - 处理所有按键的逻辑功能
 *          按键分类及颜色方案：
 *          - 内存操作键(0-5): 蓝色背景，白色文字
 *          - 清除键(9,10): 红色背景，白色文字  
 *          - 运算符键(20,25,30,35,40): 橙色背景，白色文字
 *          - 数字键: 白色背景，深灰色文字
 *          - 科学函数键: 浅蓝色背景，深蓝色文字
 * @param e LVGL事件对象，包含事件类型和相关数据
 */
void Calculator::keyboard_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);           // 获取事件类型
    Calculator *app= (Calculator *)lv_event_get_user_data(e); // 获取计算器实例指针

    // 处理按键绘制事件 - 设置按键的颜色主题
    if (code == LV_EVENT_DRAW_PART_BEGIN) {
        lv_obj_draw_part_dsc_t * dsc = (lv_obj_draw_part_dsc_t *)lv_event_get_draw_part_dsc(e);

        // 当按键矩阵绘制按键时进行颜色设置
        if(dsc->class_p == &lv_btnmatrix_class && dsc->type == LV_BTNMATRIX_DRAW_PART_BTN) {
            // 内存操作按键 (0-5) - 蓝色主题
            if(dsc->id <= 5) {
                dsc->rect_dsc->bg_color = lv_color_make(100, 149, 237); // 钢蓝色背景
                dsc->label_dsc->color = lv_color_white();               // 白色文字
            }
            // 清除和退格按键 (9,10) - 红色主题
            else if(dsc->id == 9 || dsc->id == 10) {
                dsc->rect_dsc->bg_color = lv_color_make(255, 99, 71);   // 番茄红背景
                dsc->label_dsc->color = lv_color_white();               // 白色文字
            }
            // 运算符按键 (20,25,30,35,40) - 橙色主题
            else if(dsc->id == 20 || dsc->id == 25 || dsc->id == 30 || dsc->id == 35 || dsc->id == 40) {
                dsc->rect_dsc->bg_color = lv_color_make(255, 165, 0);   // 橙色背景
                dsc->label_dsc->color = lv_color_white();               // 白色文字
            }
            // 数字按键 - 白色主题
            else if((dsc->id >= 22 && dsc->id <= 24) || (dsc->id >= 27 && dsc->id <= 29) || 
                    (dsc->id >= 32 && dsc->id <= 34) || dsc->id == 38) {
                dsc->rect_dsc->bg_color = lv_color_white();             // 白色背景
                dsc->label_dsc->color = lv_color_make(50, 50, 50);      // 深灰色文字
            }
            // 科学函数按键 - 浅蓝色主题
            else {
                dsc->rect_dsc->bg_color = lv_color_make(173, 216, 230); // 浅蓝色背景
                dsc->label_dsc->color = lv_color_make(25, 25, 112);     // 深蓝色文字
            }
        }
    } 
    // 处理按键值改变事件 - 执行按键对应的功能
    else if (code == LV_EVENT_VALUE_CHANGED) {
        int btn_id = lv_btnmatrix_get_selected_btn(app->keyboard);  // 获取被按下的按键ID
        bool calculate_flag = false;    // 是否需要重新计算标志
        bool equal_flag = false;        // 等号按键标志
        double res_num;                 // 计算结果临时变量
        char res_str[32];              // 结果字符串缓冲区
        char history_str[32];          // 历史记录字符串缓冲区

        // 如果公式标签当前使用小字体，切换为大字体显示
        if (lv_obj_get_style_text_font(app->formula_label, 0) == LABEL_FONT_SMALL) {
            lv_obj_set_style_text_font(app->formula_label, LABEL_FONT_BIG, 0);   // 公式标签改为大字体
            lv_obj_set_style_text_font(app->result_label, LABEL_FONT_SMALL, 0);  // 结果标签改为小字体
        }

        // 根据按键ID执行相应的操作
        switch (btn_id) {
        // === 内存功能按键组 ===
        case 0: // MC - 内存清除 (Memory Clear)
            app->memory_value = 0.0;            // 清除内存中存储的数值
            app->has_memory = false;            // 设置无内存标志
            lv_label_set_text(app->memory_label, "");  // 清除内存指示标签
            break;
            
        case 1: // MR - 内存读取 (Memory Recall)
            if (app->has_memory) {
                // 如果内存中有数值，将其显示到公式标签中
                snprintf(res_str, sizeof(res_str) - 1, "%.6f", app->memory_value);
                lv_label_set_text(app->formula_label, res_str);
                app->formula_len = strlen(res_str);
                calculate_flag = true;          // 标记需要计算
            }
            break;
            
        case 2: // M+ - 内存加法 (Memory Add)
            if (app->isStartNum()) {
                // 计算当前表达式的值并加到内存中
                res_num = app->calculate(lv_label_get_text(app->formula_label));
                app->memory_value += res_num;
                app->has_memory = true;
                lv_label_set_text(app->memory_label, "M");  // 显示内存指示符
            }
            break;
            
        case 3: // M- - 内存减法 (Memory Subtract)
            if (app->isStartNum()) {
                // 计算当前表达式的值并从内存中减去
                res_num = app->calculate(lv_label_get_text(app->formula_label));
                app->memory_value -= res_num;
                app->has_memory = true;
                lv_label_set_text(app->memory_label, "M");  // 显示内存指示符
            }
            break;
            
        case 4: // MS - 内存存储 (Memory Store)
            if (app->isStartNum()) {
                // 计算当前表达式的值并存储到内存中
                res_num = app->calculate(lv_label_get_text(app->formula_label));
                app->memory_value = res_num;
                app->has_memory = true;
                lv_label_set_text(app->memory_label, "M");  // 显示内存指示符
            }
            break;
            
        case 5: // Mv - 内存查看 (Memory View) - 当前未使用
            break;
            
        // === 科学模式切换按键 ===
        case 6: // 2nd - 科学模式切换
            app->is_scientific_mode = !app->is_scientific_mode;
            if (app->is_scientific_mode) {
                // 切换到科学计算器键盘布局
                lv_btnmatrix_set_map(app->keyboard, keyboard_map_scientific);
                lv_label_set_text(app->mode_label, "SCI");  // 显示科学模式
            } else {
                // 切换回基础计算器键盘布局  
                lv_btnmatrix_set_map(app->keyboard, keyboard_map);
                lv_label_set_text(app->mode_label, "DEG");  // 恢复角度模式显示
            }
            break;
            
        // === 数学常数输入按键组 ===
        case 7: // π - 圆周率常数输入
            if (app->isStartZero()) {
                // 如果当前显示为"0"，先删除它
                lv_label_cut_text(app->formula_label, --(app->formula_len), 1);
            }
            lv_label_ins_text(app->formula_label, app->formula_len, "pi"); // 插入"pi"字符串
            app->formula_len += 2;      // 长度增加2（"pi"两个字符）
            calculate_flag = true;      // 标记需要重新计算
            break;
            
        case 8: // e - 自然常数输入
            if (app->isStartZero()) {
                // 如果当前显示为"0"，先删除它
                lv_label_cut_text(app->formula_label, --(app->formula_len), 1);
            }
            lv_label_ins_text(app->formula_label, app->formula_len++, "e"); // 插入"e"字符
            calculate_flag = true;      // 标记需要重新计算
            break;
            
        // === 清除操作按键组 ===
        case 9: // C - 全部清除 (Clear All)
            lv_label_set_text(app->formula_label, "0");    // 重置公式标签为"0"
            app->formula_len = 1;                          // 重置公式长度为1
            calculate_flag = true;                         // 标记需要重新计算
            break;
            
        case 10: // ← - 退格键 (Backspace)
            // 如果当前只有一个字符且为"0"，不执行删除操作
            if ((app->formula_len == 1) && app->isStartZero()) {
                break;
            }
            // 删除最后一个字符
            lv_label_cut_text(app->formula_label, --(app->formula_len), 1);
            // 如果删除后为空，重置为"0"
            if (app->formula_len == 0) {
                lv_label_set_text(app->formula_label, "0");
                app->formula_len = 1;
            }
            calculate_flag = true;      // 标记需要重新计算
            break;
            
        // === 科学函数计算按键组 ===
        case 11: // 在基础模式：x² - 平方运算，在科学模式：sin - 正弦函数
            if (app->is_scientific_mode) {
                // 科学模式：输入sin函数
                if (app->isStartZero()) {
                    lv_label_cut_text(app->formula_label, --(app->formula_len), 1);
                }
                lv_label_ins_text(app->formula_label, app->formula_len, "sin(");
                app->formula_len += 4;
            } else {
                // 基础模式：平方运算 - 添加^2到当前数字后面
                if (app->isStartNum()) {
                    lv_label_ins_text(app->formula_label, app->formula_len, "^2");
                    app->formula_len += 2;
                }
            }
            break;
            
        case 12: // 在基础模式：1/x - 倒数运算，在科学模式：cos - 余弦函数
            if (app->is_scientific_mode) {
                // 科学模式：输入cos函数
                if (app->isStartZero()) {
                    lv_label_cut_text(app->formula_label, --(app->formula_len), 1);
                }
                lv_label_ins_text(app->formula_label, app->formula_len, "cos(");
                app->formula_len += 4;
            } else {
                // 基础模式：倒数运算 - 添加1/()格式
                if (app->isStartNum()) {
                    // 获取当前的数字
                    const char* current_text = lv_label_get_text(app->formula_label);
                    // 在前面添加"1/("，在后面添加")"
                    lv_label_set_text(app->formula_label, ("1/(" + std::string(current_text) + ")").c_str());
                    app->formula_len = strlen(lv_label_get_text(app->formula_label));
                }
            }
            break;
            
        case 13: // 在基础模式：|x| - 绝对值符号输入，在科学模式：tan - 正切函数
            if (app->is_scientific_mode) {
                // 科学模式：输入tan函数
                if (app->isStartZero()) {
                    lv_label_cut_text(app->formula_label, --(app->formula_len), 1);
                }
                lv_label_ins_text(app->formula_label, app->formula_len, "tan(");
                app->formula_len += 4;
            } else {
                // 基础模式：输入绝对值符号
                if (app->isStartZero()) {
                    lv_label_cut_text(app->formula_label, --(app->formula_len), 1);
                }
                lv_label_ins_text(app->formula_label, app->formula_len++, "|");
            }
            break;
            
        case 14: // exp - 自然指数函数 (e^x)
            if (app->isStartNum()) {
                // 获取当前的数字
                const char* current_text = lv_label_get_text(app->formula_label);
                // 在前面添加"exp("，在后面添加")"
                lv_label_set_text(app->formula_label, ("exp(" + std::string(current_text) + ")").c_str());
                app->formula_len = strlen(lv_label_get_text(app->formula_label));
            }
            break;
            
        case 15: // 在基础模式：% - 百分号运算符，在科学模式：mod - 取模运算
            if (app->is_scientific_mode) {
                // 科学模式：输入mod运算符
                lv_label_ins_text(app->formula_label, app->formula_len, "mod");
                app->formula_len += 3;
            } else {
                // 基础模式：百分号运算符
                lv_label_ins_text(app->formula_label, app->formula_len++, "%");
            }
            break;
            
        case 16: // 在基础模式：√x - 平方根运算，在科学模式：sqrt - 平方根函数
            if (app->is_scientific_mode) {
                // 科学模式：输入sqrt函数
                if (app->isStartZero()) {
                    lv_label_cut_text(app->formula_label, --(app->formula_len), 1);
                }
                lv_label_ins_text(app->formula_label, app->formula_len, "sqrt(");
                app->formula_len += 5;
            } else {
                // 基础模式：平方根运算 - 添加sqrt()格式
                if (app->isStartNum()) {
                    // 获取当前的数字
                    const char* current_text = lv_label_get_text(app->formula_label);
                    // 在前面添加"sqrt("，在后面添加")"
                    lv_label_set_text(app->formula_label, ("sqrt(" + std::string(current_text) + ")").c_str());
                    app->formula_len = strlen(lv_label_get_text(app->formula_label));
                }
            }
            break;
            
        case 17: // ( - 左括号
            // 如果当前显示为"0"，先删除它（避免"0("这样的输入）
            if (app->isStartZero()) {
                lv_label_cut_text(app->formula_label, --(app->formula_len), 1);
            }
            lv_label_ins_text(app->formula_label, app->formula_len++, "(");
            break;
            
        case 18: // ) - 右括号
            // 如果当前显示为"0"，先删除它（虽然"0)"较少见，但保持逻辑一致）
            if (app->isStartZero()) {
                lv_label_cut_text(app->formula_label, --(app->formula_len), 1);
            }
            lv_label_ins_text(app->formula_label, app->formula_len++, ")");
            break;
            
        case 19: // n! - 阶乘运算
            if (app->isStartNum()) {
                // 在当前数字后添加!符号
                lv_label_ins_text(app->formula_label, app->formula_len, "!");
                app->formula_len++;
            }
            break;
        // === 四则运算符按键组 ===
        case 20: // ÷ - 除法运算符
            if (app->isStartPercent() || app->isStartNum()) {
                lv_label_ins_text(app->formula_label, app->formula_len++, "/");
            }
            break;
            
        case 25: // × - 乘法运算符
        case 30: // - - 减法运算符  
        case 35: // + - 加法运算符
            if (app->isStartPercent() || app->isStartNum()) {
                const char* op_text = lv_btnmatrix_get_btn_text(app->keyboard, btn_id);
                if (btn_id == 25) op_text = "x"; // 将×转换为x用于计算
                lv_label_ins_text(app->formula_label, app->formula_len++, op_text);
            }
            break;
            
        // === 数字按键组 ===
        case 22: case 23: case 24: // 数字 7, 8, 9
        case 27: case 28: case 29: // 数字 4, 5, 6
        case 32: case 33: case 34: // 数字 1, 2, 3
        case 38: // 数字 0
            // 如果当前显示为"0"，先删除它（避免前导零）
            if (app->isStartZero()) {
                lv_label_cut_text(app->formula_label, --(app->formula_len), 1);
            }
            // 除了百分号结尾的情况，都可以输入数字
            if (!app->isStartPercent()) {
                lv_label_ins_text(app->formula_label, app->formula_len++, 
                    lv_btnmatrix_get_btn_text(app->keyboard, btn_id));
                calculate_flag = true;
            }
            break;
            
        // === 幂运算按键组 ===
        case 21: // x^y - 幂运算符（任意次方）
            if (app->isStartNum()) {
                lv_label_ins_text(app->formula_label, app->formula_len++, "^");
            }
            break;
            
        case 26: // 10^x - 以10为底的幂运算
            if (app->isStartNum()) {
                // 获取当前的数字
                const char* current_text = lv_label_get_text(app->formula_label);
                // 创建10^x格式
                lv_label_set_text(app->formula_label, ("10^" + std::string(current_text)).c_str());
                app->formula_len = strlen(lv_label_get_text(app->formula_label));
            }
            break;
            
        // === 对数函数按键组 ===
        case 31: // log - 常用对数函数（以10为底）
            if (app->isStartZero()) {
                lv_label_cut_text(app->formula_label, --(app->formula_len), 1);
            }
            lv_label_ins_text(app->formula_label, app->formula_len, "log(");
            app->formula_len += 4;      // "log("共4个字符
            break;
            
        case 36: // ln - 自然对数函数（以e为底）
            if (app->isStartZero()) {
                lv_label_cut_text(app->formula_label, --(app->formula_len), 1);
            }
            lv_label_ins_text(app->formula_label, app->formula_len, "ln(");
            app->formula_len += 3;      // "ln("共3个字符
            break;
            
        case 37: // ± - 正负号切换
        {
            const char* text = lv_label_get_text(app->formula_label);
            
            // 如果当前显示为"0"，先删除它，然后添加负号
            if (app->isStartZero()) {
                lv_label_cut_text(app->formula_label, --(app->formula_len), 1);
                lv_label_ins_text(app->formula_label, app->formula_len, "-");
                app->formula_len++;
            }
            else if (app->isStartNum()) {
                if (text[0] == '-') {
                    // 如果当前为负数，删除负号
                    lv_label_cut_text(app->formula_label, 0, 1);
                    app->formula_len--;
                } else {
                    // 如果当前为正数，添加负号
                    lv_label_ins_text(app->formula_label, 0, "-");
                    app->formula_len++;
                }
                calculate_flag = true;
            }
            break;
        }
            
        case 39: // . - 小数点
            if (app->isLegalDot() && app->isStartNum()) {
                lv_label_ins_text(app->formula_label, app->formula_len++, ".");
            }
            break;
            
        case 40: // = - 等号（执行计算）
            calculate_flag = true;      // 标记需要计算
            equal_flag = true;          // 标记这是等号操作
            break;
            
        default:
            break;
        }

        // === 计算结果显示逻辑 ===
        if (calculate_flag) {
            // 设置公式标签为大字体
            lv_obj_set_style_text_font(app->formula_label, LABEL_FONT_BIG, 0);

            // 使用科学计算函数进行求值
            res_num = app->evaluateScientific(lv_label_get_text(app->formula_label));
            
            // 格式化结果显示：整数显示为整数，小数显示为6位小数
            if (int(res_num) == res_num && res_num < 1000000) {
                snprintf(res_str, sizeof(res_str) - 1, "%ld", long(res_num));
            }
            else {
                snprintf(res_str, sizeof(res_str) - 1, "%.6f", res_num);
            }
            
            // 更新结果标签显示
            lv_label_set_text_fmt(app->result_label, "= %s", res_str);
            lv_obj_set_style_text_font(app->result_label, LABEL_FONT_SMALL, 0);
        }

        // === 等号操作的特殊处理 ===
        if (equal_flag) {
            // 切换字体大小：结果显示为大字体
            lv_obj_set_style_text_font(app->result_label, LABEL_FONT_BIG, 0);

            // 将当前计算过程添加到历史记录中
            snprintf(history_str, sizeof(history_str) - 1, "\n%s = %s ", 
                     lv_label_get_text(app->formula_label), res_str);
            
            // 定位到历史记录文本区域末尾，并添加新的计算记录
            lv_textarea_set_cursor_pos(app->history_label, strlen(lv_textarea_get_text(app->history_label)));
            lv_textarea_add_text(app->history_label, history_str);

            // 将结果设置为新的输入起点，为下次计算做准备
            lv_label_set_text_fmt(app->formula_label, "%s", res_str);
            lv_obj_set_style_text_font(app->formula_label, LABEL_FONT_SMALL, 0);  // 公式区域改为小字体
            app->formula_len = strlen(res_str);                                    // 更新公式长度
        }
    }
}

/**
 * @file Calculator.cpp 文件结束
 * @brief ESP32科学计算器应用程序实现文件结束
 * @details 本文件包含了完整的科学计算器实现，提供了丰富的数学计算功能
 *          和用户友好的图形界面，支持内存操作、科学函数计算等高级功能
 */
