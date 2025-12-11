#include "UartBridgeUI.hpp"

#include <cstdio>
#include <cstring>
#include "../UartBridge.hpp"
#include "../UartBridgeOptions.hpp"

UartBridgeUI::UartBridgeUI(UartBridge &owner)
    : _owner(owner),
      _label_wifi(nullptr),
            _label_status(nullptr),
            _label_client(nullptr),
            _label_stats(nullptr),
            _label_port_summary(nullptr),
            _btn_start(nullptr),
            _btn_label(nullptr),
            _btn_exit(nullptr),
            _btn_clear_log(nullptr),
            _btn_port_reset(nullptr),
                        _dropdown_port(nullptr),
                        _dropdown_baud(nullptr),
                        _dropdown_databits(nullptr),
                        _dropdown_parity(nullptr),
                        _dropdown_stopbits(nullptr),
            _switch_heartbeat(nullptr),
            _ta_port(nullptr),
            _kb(nullptr),
            _log(nullptr),
            _ui_timer(nullptr)
{
}

UartBridgeUI::~UartBridgeUI()
{
    // 释放定时器，避免悬挂回调
    if (_ui_timer) {
        lv_timer_del(_ui_timer);
        _ui_timer = nullptr;
    }
}

void UartBridgeUI::build(const Snapshot &snapshot)
{
    // 新布局：顶部信息 + 控制区 + 日志区，避免拥挤
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr, lv_color_hex(0xF5F6FA), 0);
    lv_obj_set_flex_flow(scr, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(scr, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_top(scr, 12, 0);
    lv_obj_set_style_pad_row(scr, 6, 0);
    lv_obj_set_style_pad_column(scr, 6, 0);

    // 标题栏置顶：组件名 + 用途
    lv_obj_t *title_bar = lv_obj_create(scr);
    lv_obj_set_size(title_bar, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(title_bar, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(title_bar, 0, 0);
    lv_obj_set_style_pad_all(title_bar, 6, 0);
    lv_obj_set_style_pad_row(title_bar, 4, 0);
    lv_obj_set_flex_flow(title_bar, LV_FLEX_FLOW_COLUMN);

    lv_obj_t *lbl_title = lv_label_create(title_bar);
    lv_label_set_text(lbl_title, "UART Bridge [ UART TTL & USB CDC<-> TCP ]");
    lv_obj_set_style_text_color(lbl_title, lv_color_hex(0x222222), 0);

    // 顶栏：Wi-Fi/IP + Exit
    lv_obj_t *top_bar = lv_obj_create(scr);
    lv_obj_set_size(top_bar, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(top_bar, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(top_bar, 0, 0);
    lv_obj_set_flex_flow(top_bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(top_bar, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_all(top_bar, 6, 0);

    _label_wifi = lv_label_create(top_bar);
    lv_label_set_text(_label_wifi, "WiFi: -");

    _btn_exit = lv_btn_create(top_bar);
    lv_obj_set_size(_btn_exit, 70, 30);
    lv_obj_add_event_cb(_btn_exit, onExitClicked, LV_EVENT_CLICKED, this);
    lv_obj_t *lbl_exit = lv_label_create(_btn_exit);
    lv_label_set_text(lbl_exit, "Exit");
    lv_obj_center(lbl_exit);

    // 信息区：状态、客户端、统计、串口摘要
    lv_obj_t *info_box = lv_obj_create(scr);
    lv_obj_set_size(info_box, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(info_box, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_radius(info_box, 8, 0);
    lv_obj_set_style_pad_all(info_box, 8, 0);
    lv_obj_set_style_border_width(info_box, 1, 0);
    lv_obj_set_style_border_color(info_box, lv_color_hex(0xE0E3EC), 0);
    lv_obj_set_flex_flow(info_box, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(info_box, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    lv_obj_t *status_row = lv_obj_create(info_box);
    lv_obj_set_style_bg_opa(status_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(status_row, 0, 0);
    lv_obj_set_size(status_row, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(status_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(status_row, 12, 0);

    _label_status = lv_label_create(status_row);
    lv_label_set_text(_label_status, "Status: Idle");

    _label_client = lv_label_create(status_row);
    lv_label_set_text(_label_client, "Client: none");

    // 占位符撑开空间，将心跳开关推到信息行右侧
    lv_obj_t *status_spacer = lv_obj_create(status_row);
    lv_obj_set_size(status_spacer, 1, 1);
    lv_obj_set_style_bg_opa(status_spacer, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(status_spacer, 0, 0);
    lv_obj_set_flex_grow(status_spacer, 1);

    lv_obj_t *hb_box = lv_obj_create(status_row);
    lv_obj_set_style_bg_opa(hb_box, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(hb_box, 0, 0);
    lv_obj_set_flex_flow(hb_box, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(hb_box, 6, 0);
    lv_obj_set_size(hb_box, LV_SIZE_CONTENT, LV_SIZE_CONTENT);

    lv_obj_t *lbl_hb = lv_label_create(hb_box);
    lv_label_set_text(lbl_hb, "HB");

    _switch_heartbeat = lv_switch_create(hb_box);
    lv_obj_add_event_cb(_switch_heartbeat, onHeartbeatToggled, LV_EVENT_VALUE_CHANGED, this);

    _label_stats = lv_label_create(info_box);
    lv_label_set_text(_label_stats, "Net→UART 0 | UART→Net 0");

    _label_port_summary = lv_label_create(info_box);
    lv_label_set_text(_label_port_summary, "Port: -");

    // 控制区：参数 + Start/Stop + 清日志
    lv_obj_t *ctrl_box = lv_obj_create(scr);
    lv_obj_set_size(ctrl_box, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(ctrl_box, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_radius(ctrl_box, 8, 0);
    lv_obj_set_style_pad_all(ctrl_box, 8, 0);
    lv_obj_set_style_border_width(ctrl_box, 1, 0);
    lv_obj_set_style_border_color(ctrl_box, lv_color_hex(0xE0E3EC), 0);
    lv_obj_set_flex_flow(ctrl_box, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(ctrl_box, 6, 0);

    // 第一行：端口、波特率、数据位
    lv_obj_t *row1 = lv_obj_create(ctrl_box);
    lv_obj_set_style_bg_opa(row1, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row1, 0, 0);
    lv_obj_set_size(row1, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row1, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(row1, 8, 0);

    _dropdown_port = lv_dropdown_create(row1);
    lv_dropdown_set_options_static(_dropdown_port, PORT_OPTIONS);
    lv_obj_set_width(_dropdown_port, 120);
    lv_obj_add_event_cb(_dropdown_port, onPortTypeChanged, LV_EVENT_VALUE_CHANGED, this);

    _dropdown_baud = lv_dropdown_create(row1);
    lv_dropdown_set_options_static(_dropdown_baud, BAUD_OPTIONS);
    lv_obj_set_width(_dropdown_baud, 120);
    lv_obj_add_event_cb(_dropdown_baud, onBaudChanged, LV_EVENT_VALUE_CHANGED, this);

    _dropdown_databits = lv_dropdown_create(row1);
    lv_dropdown_set_options_static(_dropdown_databits, DATABITS_OPTIONS);
    lv_obj_set_width(_dropdown_databits, 80);
    lv_obj_add_event_cb(_dropdown_databits, onDataBitsChanged, LV_EVENT_VALUE_CHANGED, this);

    // 第二行：校验位、停止位、端口输入 + 重置
    lv_obj_t *row2 = lv_obj_create(ctrl_box);
    lv_obj_set_style_bg_opa(row2, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row2, 0, 0);
    lv_obj_set_size(row2, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row2, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(row2, 8, 0);
    lv_obj_set_style_pad_row(row2, 4, 0);

    _dropdown_parity = lv_dropdown_create(row2);
    lv_dropdown_set_options_static(_dropdown_parity, PARITY_OPTIONS);
    lv_obj_set_width(_dropdown_parity, 100);
    lv_obj_add_event_cb(_dropdown_parity, onParityChanged, LV_EVENT_VALUE_CHANGED, this);

    _dropdown_stopbits = lv_dropdown_create(row2);
    lv_dropdown_set_options_static(_dropdown_stopbits, STOPBITS_OPTIONS);
    lv_obj_set_width(_dropdown_stopbits, 90);
    lv_obj_add_event_cb(_dropdown_stopbits, onStopBitsChanged, LV_EVENT_VALUE_CHANGED, this);

    lv_obj_t *port_box = lv_obj_create(row2);
    lv_obj_set_style_bg_opa(port_box, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(port_box, 0, 0);
    lv_obj_set_flex_flow(port_box, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(port_box, 6, 0);
    lv_obj_set_size(port_box, LV_SIZE_CONTENT, LV_SIZE_CONTENT);

    lv_obj_t *label_port = lv_label_create(port_box);
    lv_label_set_text(label_port, "TCP:");

    _ta_port = lv_textarea_create(port_box);
    lv_obj_set_width(_ta_port, 120);
    lv_textarea_set_max_length(_ta_port, 5);
    lv_textarea_set_one_line(_ta_port, true);
    lv_textarea_set_accepted_chars(_ta_port, "0123456789");
    lv_textarea_set_cursor_click_pos(_ta_port, true);
    lv_obj_add_event_cb(_ta_port, onPortEvent, LV_EVENT_FOCUSED, this);
    lv_obj_add_event_cb(_ta_port, onPortEvent, LV_EVENT_CLICKED, this);
    lv_obj_add_event_cb(_ta_port, onPortEvent, LV_EVENT_DEFOCUSED, this);
    lv_obj_add_event_cb(_ta_port, onPortEvent, LV_EVENT_READY, this);

    _btn_port_reset = lv_btn_create(port_box);
    lv_obj_set_size(_btn_port_reset, 60, 30);
    lv_obj_add_event_cb(_btn_port_reset, onPortResetClicked, LV_EVENT_CLICKED, this);
    lv_obj_t *lbl_reset = lv_label_create(_btn_port_reset);
    lv_label_set_text(lbl_reset, "Reset");
    lv_obj_center(lbl_reset);

    // 第三行：Start/Stop + 清空日志
    lv_obj_t *row3 = lv_obj_create(ctrl_box);
    lv_obj_set_style_bg_opa(row3, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row3, 0, 0);
    lv_obj_set_size(row3, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row3, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(row3, 10, 0);

    _btn_start = lv_btn_create(row3);
    lv_obj_set_width(_btn_start, 140);
    lv_obj_set_height(_btn_start, 42);
    lv_obj_add_event_cb(_btn_start, onStartStopClicked, LV_EVENT_CLICKED, this);
    _btn_label = lv_label_create(_btn_start);
    lv_label_set_text(_btn_label, "Start");
    lv_obj_center(_btn_label);

    _btn_clear_log = lv_btn_create(row3);
    lv_obj_set_width(_btn_clear_log, 100);
    lv_obj_set_height(_btn_clear_log, 42);
    lv_obj_add_event_cb(_btn_clear_log, onClearLogClicked, LV_EVENT_CLICKED, this);
    lv_obj_t *lbl_clear = lv_label_create(_btn_clear_log);
    lv_label_set_text(lbl_clear, "Clear Log");
    lv_obj_center(lbl_clear);

    // 日志区
    lv_obj_t *log_box = lv_obj_create(scr);
    lv_obj_set_size(log_box, lv_pct(100), lv_pct(45));
    lv_obj_set_style_bg_color(log_box, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_radius(log_box, 8, 0);
    lv_obj_set_style_pad_all(log_box, 8, 0);
    lv_obj_set_style_border_width(log_box, 1, 0);
    lv_obj_set_style_border_color(log_box, lv_color_hex(0xE0E3EC), 0);
    lv_obj_set_flex_flow(log_box, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(log_box, 4, 0);

    lv_obj_t *log_header = lv_obj_create(log_box);
    lv_obj_set_style_bg_opa(log_header, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(log_header, 0, 0);
    lv_obj_set_size(log_header, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(log_header, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(log_header, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START);

    lv_obj_t *lbl_log_title = lv_label_create(log_header);
    lv_label_set_text(lbl_log_title, "Logs");

    // 日志文本框
    _log = lv_textarea_create(log_box);
    lv_obj_set_size(_log, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(_log, 1);
    lv_textarea_set_max_length(_log, 6000);
    lv_textarea_set_text(_log, "");
    lv_textarea_set_cursor_click_pos(_log, false);

    if (snapshot.heartbeat_enabled) {
        lv_obj_add_state(_switch_heartbeat, LV_STATE_CHECKED);
    } else {
        lv_obj_clear_state(_switch_heartbeat, LV_STATE_CHECKED);
    }

    setupDropdownSelections(snapshot);
    updatePort(snapshot.net.port);
    updatePortSummary();

    lv_scr_load(scr);

    // 数字键盘，默认隐藏，聚焦端口输入时弹出
    _kb = lv_keyboard_create(scr);
    lv_obj_set_size(_kb, lv_pct(100), 180);
    lv_obj_align(_kb, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_keyboard_set_mode(_kb, LV_KEYBOARD_MODE_NUMBER);
    lv_obj_add_flag(_kb, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(_kb, LV_OBJ_FLAG_FLOATING);
    lv_keyboard_set_textarea(_kb, _ta_port);

    _ui_timer = lv_timer_create(onUiTimer, 1000, this);
}

void UartBridgeUI::setWifiStatus(const char *text)
{
    lv_label_set_text(_label_wifi, text);
}

void UartBridgeUI::setStatus(const char *text)
{
    lv_label_set_text(_label_status, text);
}

void UartBridgeUI::setStats(const char *text)
{
    // 文本格式: "Client: xxx | Net->UART a | UART->Net b"
    if (!text) return;
    const char *bar = strchr(text, '|');
    if (bar && _label_client) {
        std::string client{text, static_cast<size_t>(bar - text)};
        lv_label_set_text(_label_client, client.c_str());
        // 跳过分隔符和空格
        const char *rest = bar + 1;
        while (*rest == ' ') ++rest;
        lv_label_set_text(_label_stats, rest);
    } else {
        if (_label_stats) lv_label_set_text(_label_stats, text);
    }
}

void UartBridgeUI::appendLog(const char *text)
{
    // 使用屏幕互斥锁防止渲染与 LVGL 线程冲突
    if (!_log) return;
    if (bsp_display_lock(0)) {
        lv_textarea_add_text(_log, text);
        lv_textarea_add_text(_log, "\n");
        const char *content = lv_textarea_get_text(_log);
        size_t len = content ? strlen(content) : 0;
        if (len > 5800) { // 超阈值则保留尾部 4000 字符
            size_t keep = 4000;
            const char *start = content + (len > keep ? len - keep : 0);
            lv_textarea_set_text(_log, start);
        }
        bsp_display_unlock();
    }
}

void UartBridgeUI::refreshStartButtonState(bool wifi_ok, bool running)
{
    if (!_btn_start || !_btn_label) return;

    if (running) {
        lv_obj_clear_state(_btn_start, LV_STATE_DISABLED);
        lv_obj_set_style_bg_color(_btn_start, lv_color_hex(0x2EB872), LV_PART_MAIN);
    } else if (wifi_ok) {
        lv_obj_clear_state(_btn_start, LV_STATE_DISABLED);
        lv_obj_set_style_bg_color(_btn_start, lv_color_hex(0x2D7FF9), LV_PART_MAIN);
    } else {
        lv_obj_add_state(_btn_start, LV_STATE_DISABLED);
    }

    lv_label_set_text(_btn_label, running ? "Stop" : "Start");
}

void UartBridgeUI::updatePort(uint16_t port)
{
    if (!_ta_port) return;
    char port_buf[8];
    snprintf(port_buf, sizeof(port_buf), "%u", static_cast<unsigned>(port));
    lv_textarea_set_text(_ta_port, port_buf);
}

void UartBridgeUI::setupDropdownSelections(const Snapshot &snapshot)
{
    // 依据当前配置选择对应项，保持 UI 与配置一致
    lv_dropdown_set_selected(_dropdown_port, snapshot.port_type == UartBridge::PortType::TTL ? 0 : 1);

    for (size_t i = 0; i < sizeof(BAUD_VALUES) / sizeof(BAUD_VALUES[0]); ++i) {
        if (snapshot.serial.baud == BAUD_VALUES[i]) {
            lv_dropdown_set_selected(_dropdown_baud, i);
            break;
        }
    }
    for (size_t i = 0; i < sizeof(DATABITS_VALUES) / sizeof(DATABITS_VALUES[0]); ++i) {
        if (snapshot.serial.data_bits == DATABITS_VALUES[i]) {
            lv_dropdown_set_selected(_dropdown_databits, i);
            break;
        }
    }
    for (size_t i = 0; i < sizeof(PARITY_VALUES) / sizeof(PARITY_VALUES[0]); ++i) {
        if (snapshot.serial.parity == PARITY_VALUES[i]) {
            lv_dropdown_set_selected(_dropdown_parity, i);
            break;
        }
    }
    for (size_t i = 0; i < sizeof(STOPBITS_VALUES) / sizeof(STOPBITS_VALUES[0]); ++i) {
        if (snapshot.serial.stop_bits == STOPBITS_VALUES[i]) {
            lv_dropdown_set_selected(_dropdown_stopbits, i);
            break;
        }
    }
}

void UartBridgeUI::updatePortSummary()
{
    if (!_label_port_summary) return;
    const char *port_type = lv_dropdown_get_selected(_dropdown_port) == 0 ? "TTL" : "USB-CDC";
    char baud_txt[16], data_txt[8], parity_txt[8], stop_txt[8];
    lv_dropdown_get_selected_str(_dropdown_baud, baud_txt, sizeof(baud_txt));
    lv_dropdown_get_selected_str(_dropdown_databits, data_txt, sizeof(data_txt));
    lv_dropdown_get_selected_str(_dropdown_parity, parity_txt, sizeof(parity_txt));
    lv_dropdown_get_selected_str(_dropdown_stopbits, stop_txt, sizeof(stop_txt));
    char buf[96];
    snprintf(buf, sizeof(buf), "Port: %s | %s %s%s%s", port_type, baud_txt, data_txt, parity_txt, stop_txt);
    lv_label_set_text(_label_port_summary, buf);
}

void UartBridgeUI::onStartStopClicked(lv_event_t *e)
{
    auto *self = static_cast<UartBridgeUI *>(lv_event_get_user_data(e));
    if (!self) return;
    self->_owner.handleStartStopRequested();
}

void UartBridgeUI::onPortTypeChanged(lv_event_t *e)
{
    auto *self = static_cast<UartBridgeUI *>(lv_event_get_user_data(e));
    if (!self) return;
    int sel = lv_dropdown_get_selected(self->_dropdown_port);
    self->_owner.handlePortTypeChanged(sel);
    self->updatePortSummary();
}

void UartBridgeUI::onBaudChanged(lv_event_t *e)
{
    auto *self = static_cast<UartBridgeUI *>(lv_event_get_user_data(e));
    if (!self) return;
    int sel = lv_dropdown_get_selected(self->_dropdown_baud);
    self->_owner.handleBaudChanged(sel);
    self->updatePortSummary();
}

void UartBridgeUI::onDataBitsChanged(lv_event_t *e)
{
    auto *self = static_cast<UartBridgeUI *>(lv_event_get_user_data(e));
    if (!self) return;
    int sel = lv_dropdown_get_selected(self->_dropdown_databits);
    self->_owner.handleDataBitsChanged(sel);
    self->updatePortSummary();
}

void UartBridgeUI::onParityChanged(lv_event_t *e)
{
    auto *self = static_cast<UartBridgeUI *>(lv_event_get_user_data(e));
    if (!self) return;
    int sel = lv_dropdown_get_selected(self->_dropdown_parity);
    self->_owner.handleParityChanged(sel);
    self->updatePortSummary();
}

void UartBridgeUI::onStopBitsChanged(lv_event_t *e)
{
    auto *self = static_cast<UartBridgeUI *>(lv_event_get_user_data(e));
    if (!self) return;
    int sel = lv_dropdown_get_selected(self->_dropdown_stopbits);
    self->_owner.handleStopBitsChanged(sel);
    self->updatePortSummary();
}

void UartBridgeUI::onPortEvent(lv_event_t *e)
{
    auto *self = static_cast<UartBridgeUI *>(lv_event_get_user_data(e));
    if (!self) return;
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_FOCUSED || code == LV_EVENT_CLICKED) {
        if (self->_kb) {
            lv_obj_clear_flag(self->_kb, LV_OBJ_FLAG_HIDDEN);
            lv_obj_move_foreground(self->_kb);
            lv_keyboard_set_textarea(self->_kb, self->_ta_port);
        }
    } else if (code == LV_EVENT_DEFOCUSED || code == LV_EVENT_READY) {
        const char *txt = lv_textarea_get_text(self->_ta_port);
        self->_owner.handlePortEdited(txt);
        if (self->_kb) lv_obj_add_flag(self->_kb, LV_OBJ_FLAG_HIDDEN);
    }
}

void UartBridgeUI::onExitClicked(lv_event_t *e)
{
    auto *self = static_cast<UartBridgeUI *>(lv_event_get_user_data(e));
    if (!self) return;
    self->_owner.handleExitRequested();
}

void UartBridgeUI::onClearLogClicked(lv_event_t *e)
{
    auto *self = static_cast<UartBridgeUI *>(lv_event_get_user_data(e));
    if (!self || !self->_log) return;
    lv_textarea_set_text(self->_log, "");
}

void UartBridgeUI::onPortResetClicked(lv_event_t *e)
{
    auto *self = static_cast<UartBridgeUI *>(lv_event_get_user_data(e));
    if (!self) return;
    const char *def_port = "7777";
    lv_textarea_set_text(self->_ta_port, def_port);
    self->_owner.handlePortEdited(def_port);
}

void UartBridgeUI::onHeartbeatToggled(lv_event_t *e)
{
    auto *self = static_cast<UartBridgeUI *>(lv_event_get_user_data(e));
    if (!self || !self->_switch_heartbeat) return;
    bool enabled = lv_obj_has_state(self->_switch_heartbeat, LV_STATE_CHECKED);
    self->_owner.handleHeartbeatToggled(enabled);
}

void UartBridgeUI::onUiTimer(lv_timer_t *timer)
{
    auto *self = static_cast<UartBridgeUI *>(timer->user_data);
    if (!self) return;
    self->_owner.handleUiTick();
}
