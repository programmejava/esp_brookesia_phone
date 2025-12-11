#include "UartBridge.hpp"

#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <unistd.h>
#include "esp_log.h"
#include "esp_err.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "lwip/inet.h"
#include "lwip/sockets.h"
#include "esp_timer.h"
#include "bsp/esp-bsp.h"
#include "ui/UartBridgeUI.hpp"
#include "UartBridgeOptions.hpp"

static const char *TAG = "UartBridge";
static const char *NVS_NAMESPACE = "uart_bridge";
static const char *NVS_KEY_PORT_TYPE = "ptype";
static const char *NVS_KEY_BAUD = "baud";
static const char *NVS_KEY_DATABITS = "db";
static const char *NVS_KEY_PARITY = "par";
static const char *NVS_KEY_STOPBITS = "sb";
static const char *NVS_KEY_TCP_PORT = "tcp_port";
static const char *NVS_KEY_HEARTBEAT = "hb";

UartBridge::UartBridge():
    ESP_Brookesia_PhoneApp("UART Bridge", nullptr, true),
    _port_type(PortType::TTL),
    _serial{115200, 8, UART_PARITY_DISABLE, UART_STOP_BITS_1},
    _net{7777},
    _running(false),
    _stop_flag(false),
    _ap_active(false),
    _client_connected(false),
    _heartbeat_enabled(false),
    _bytes_up_net_to_uart(0),
    _bytes_down_uart_to_net(0),
    _bridge_task(nullptr),
    _listen_fd(-1),
    _heartbeat_counter(0),
    _last_heartbeat_ms(0),
    _nvs(0)
{
}

UartBridge::~UartBridge()
{
    stopBridge();
    if (_nvs) {
        nvs_close(_nvs);
        _nvs = 0;
    }
}

bool UartBridge::init(void)
{
    // 打开 NVS 命名空间，用于保存用户选择的串口/网络配置
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &_nvs);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS open failed: %s", esp_err_to_name(err));
    }
    loadSettings();

    // 默认关闭桥接内部心跳，由 UI 开关控制
    _usb.setHeartbeatEnabled(_heartbeat_enabled);
    return true;
}

bool UartBridge::run(void)
{
    // 创建 UI 并同步当前配置
    UartBridgeUI::Snapshot snapshot{
        _port_type,
        _serial,
        _net,
        _running,
        _heartbeat_enabled
    };
    _ui = std::make_unique<UartBridgeUI>(*this);
    _ui->build(snapshot);

    resetStats();

    updateWifiStatus();
    applySerialSettings();
    refreshStartButtonState();
    return true;
}

bool UartBridge::back(void)
{
    // 统一与其它组件手势行为：右滑触发 back 时直接退出应用
    stopBridge();
    return notifyCoreClosed();
}

bool UartBridge::close(void)
{
    stopBridge();
    _ui.reset();
    return true;
}

bool UartBridge::resume(void)
{
    updateWifiStatus();
    refreshStartButtonState();
    return true;
}

void UartBridge::updateWifiStatus()
{
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (!netif) {
        if (_ap_active) {
            esp_netif_t *ap_netif = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
            esp_netif_ip_info_t ap_ip{};
            if (ap_netif && esp_netif_get_ip_info(ap_netif, &ap_ip) == ESP_OK && ap_ip.ip.addr != 0) {
                char buf[48];
                ip4_addr_t *addr = reinterpret_cast<ip4_addr_t *>(&ap_ip.ip);
                snprintf(buf, sizeof(buf), "AP: %s", ip4addr_ntoa(addr));
                if (_ui) _ui->setWifiStatus(buf);
            } else if (_ui) {
                _ui->setWifiStatus("AP: starting...");
            }
        } else if (_ui) {
            _ui->setWifiStatus("WiFi: not init");
        }
        return;
    }
    esp_netif_ip_info_t ip;
    if (esp_netif_get_ip_info(netif, &ip) == ESP_OK && ip.ip.addr != 0) {
        char buf[48];
        ip4_addr_t *addr = reinterpret_cast<ip4_addr_t *>(&ip.ip);
        snprintf(buf, sizeof(buf), "WiFi: %s", ip4addr_ntoa(addr));
        if (_ui) _ui->setWifiStatus(buf);
    } else {
        if (_ap_active) {
            esp_netif_t *ap_netif = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
            esp_netif_ip_info_t ap_ip{};
            if (ap_netif && esp_netif_get_ip_info(ap_netif, &ap_ip) == ESP_OK && ap_ip.ip.addr != 0) {
                char buf[48];
                ip4_addr_t *addr = reinterpret_cast<ip4_addr_t *>(&ap_ip.ip);
                snprintf(buf, sizeof(buf), "AP: %s", ip4addr_ntoa(addr));
                if (_ui) _ui->setWifiStatus(buf);
            } else if (_ui) {
                _ui->setWifiStatus("AP: starting...");
            }
        } else if (_ui) {
            _ui->setWifiStatus("WiFi: disconnected");
        }
    }
}

void UartBridge::setStatus(const char *text)
{
    if (_ui) _ui->setStatus(text);
}

void UartBridge::appendLog(const char *text)
{
    if (_ui) _ui->appendLog(text);
}

void UartBridge::refreshStartButtonState()
{
    bool wifi_ok = isWifiConnected();
    if (_ui) {
        _ui->refreshStartButtonState(wifi_ok, _running);
    }
}

void UartBridge::loadSettings()
{
    // 从 NVS 读取最近一次的串口与网络配置，支持掉电记忆
    if (!_nvs) return;
    int32_t val = 0;
    if (nvs_get_i32(_nvs, NVS_KEY_PORT_TYPE, &val) == ESP_OK && (val == 0 || val == 1)) {
        _port_type = static_cast<PortType>(val);
    }
    if (nvs_get_i32(_nvs, NVS_KEY_BAUD, &val) == ESP_OK) {
        _serial.baud = val;
    }
    if (nvs_get_i32(_nvs, NVS_KEY_DATABITS, &val) == ESP_OK) {
        _serial.data_bits = val;
    }
    if (nvs_get_i32(_nvs, NVS_KEY_PARITY, &val) == ESP_OK) {
        _serial.parity = val;
    }
    if (nvs_get_i32(_nvs, NVS_KEY_STOPBITS, &val) == ESP_OK) {
        _serial.stop_bits = val;
    }
    if (nvs_get_i32(_nvs, NVS_KEY_TCP_PORT, &val) == ESP_OK && val > 0 && val < 65536) {
        _net.port = static_cast<uint16_t>(val);
    }
    if (nvs_get_i32(_nvs, NVS_KEY_HEARTBEAT, &val) == ESP_OK) {
        _heartbeat_enabled = (val != 0);
    }
}

void UartBridge::saveSettings()
{
    // 将当前配置写回 NVS，避免 UI 操作后丢失配置
    if (!_nvs) return;
    nvs_set_i32(_nvs, NVS_KEY_PORT_TYPE, static_cast<int>(_port_type));
    nvs_set_i32(_nvs, NVS_KEY_BAUD, _serial.baud);
    nvs_set_i32(_nvs, NVS_KEY_DATABITS, _serial.data_bits);
    nvs_set_i32(_nvs, NVS_KEY_PARITY, _serial.parity);
    nvs_set_i32(_nvs, NVS_KEY_STOPBITS, _serial.stop_bits);
    nvs_set_i32(_nvs, NVS_KEY_TCP_PORT, _net.port);
    nvs_set_i32(_nvs, NVS_KEY_HEARTBEAT, _heartbeat_enabled ? 1 : 0);
    nvs_commit(_nvs);
}

void UartBridge::applySerialSettings()
{
    // 同步串口参数到 TTL UART 与 USB CDC，确保两个入口保持一致
    UartConfig cfg = {
        .baud_rate = _serial.baud,
        .data_bits = static_cast<uart_word_length_t>(_serial.data_bits - 5 + UART_DATA_5_BITS),
        .parity = static_cast<uart_parity_t>(_serial.parity),
        .stop_bits = static_cast<uart_stop_bits_t>(_serial.stop_bits)
    };

    // TTL 使用共享模式：仅重配，不卸载驱动，避免影响已有 UART TTL 应用
    _uart.reconfigure(cfg);
    _uart.startReceiving();

    TinyUsbCdcService::SerialConfig scfg = {
        .baud_rate = static_cast<uint32_t>(_serial.baud),
        .data_bits = static_cast<uint8_t>(_serial.data_bits),
        .parity = static_cast<uint8_t>(_serial.parity),
        .stop_bits = static_cast<uint8_t>(_serial.stop_bits == UART_STOP_BITS_2 ? 2 : 1)
    };
    _usb.configureSerialPort(scfg);
}

bool UartBridge::isWifiConnected()
{
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (!netif) return false;
    esp_netif_ip_info_t ip;
    if (esp_netif_get_ip_info(netif, &ip) != ESP_OK) return false;
    return ip.ip.addr != 0;
}

bool UartBridge::ensureApFallback()
{
    // 若 STA 已连接则无需开启 AP
    if (isWifiConnected()) {
        _ap_active = false;
        return true;
    }

    // 确保 netif / event loop 初始化
    esp_netif_init();
    esp_event_loop_create_default();

    // 确保默认 AP/STA netif 存在（重复创建会返回已存在）
    esp_netif_t *ap_netif = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
    if (!ap_netif) {
        ap_netif = esp_netif_create_default_wifi_ap();
    }
    esp_netif_t *sta_netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (!sta_netif) {
        sta_netif = esp_netif_create_default_wifi_sta();
    }

    wifi_mode_t mode = WIFI_MODE_NULL;
    esp_err_t mode_ret = esp_wifi_get_mode(&mode);
    if (mode_ret == ESP_ERR_WIFI_NOT_INIT) {
        wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
        esp_wifi_init(&cfg);
        mode = WIFI_MODE_NULL;
    }

    // 配置 AP 参数
    wifi_config_t ap_config = {};
    strcpy((char *)ap_config.ap.ssid, "ESPBridgeAP");
    ap_config.ap.ssid_len = strlen((char *)ap_config.ap.ssid);
    strcpy((char *)ap_config.ap.password, "12345678");
    ap_config.ap.channel = 1;
    ap_config.ap.max_connection = 4;
    ap_config.ap.authmode = WIFI_AUTH_WPA_WPA2_PSK;

    // 仅设置 AP 配置，不改变 STA 配置
    wifi_mode_t target_mode = WIFI_MODE_APSTA;
    if (mode != target_mode) {
        esp_wifi_set_mode(target_mode);
    }
    esp_wifi_set_config(WIFI_IF_AP, &ap_config);
    esp_wifi_start();

    _ap_active = true;
    return true;
}

bool UartBridge::startBridge()
{
    // 校验网络，启动串口、USB CDC，然后创建 TCP 转发任务
    if (_running) return true;
    if (!isWifiConnected()) {
        if (!ensureApFallback()) {
            setStatus("Status: WiFi/AP not ready");
            return false;
        }
    }

    applySerialSettings();
    if (_port_type == PortType::USB_CDC) {
        if (!_usb.begin()) {
            setStatus("Status: USB init failed");
            return false;
        }
        _usb.startScan();
        if (!_usb.isConnected()) {
            setStatus("Status: USB scanning (plug device, may take seconds)");
        }
    }

    _stop_flag = false;
    _running = true;
    if (xTaskCreatePinnedToCore(bridgeTaskThunk, "uart_bridge", 4096, this, 4, &_bridge_task, 1) != pdPASS) {
        _running = false;
        setStatus("Status: start task failed");
        return false;
    }
    setStatus("Status: Running");
    refreshStartButtonState();
    return true;
}

void UartBridge::stopBridge()
{
    // 停止任务与清理运行状态，释放 TCP 监听
    // 如果未运行，仍然确保底层资源被释放，避免占用 UART/USB
    if (!_running) {
        if (_listen_fd >= 0) {
            shutdown(_listen_fd, SHUT_RDWR);
            ::close(_listen_fd);
            _listen_fd = -1;
        }
        _uart.stopReceiving();
        _uart.end();
        _usb.stopHeartbeat();
        _usb.stopScan();
        _usb.forceDisconnectDevice();
        _client_connected = false;
        resetStats();
        return;
    }

    _stop_flag = true;
    if (_listen_fd >= 0) {
        shutdown(_listen_fd, SHUT_RDWR);
        ::close(_listen_fd);
        _listen_fd = -1;
    }

    // 等待桥接任务自行退出并关闭套接字，避免留下占用导致下一次 listen 失败
    for (int i = 0; i < 50 && _bridge_task; ++i) { // 最多等待约500ms
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    if (_bridge_task) {
        ESP_LOGW(TAG, "bridge task did not exit in time, forcing delete");
        vTaskDelete(_bridge_task);
        _bridge_task = nullptr;
    }

    // 停止串口/USB 服务，释放硬件端口，避免影响 UART TTL 应用
    _uart.stopReceiving();
    _uart.end();
    _usb.stopHeartbeat();
    _usb.stopScan();
    _usb.forceDisconnectDevice();

    _running = false;
    _client_connected = false;
    resetStats();
    setStatus("Status: Stopped");
    refreshStartButtonState();
}

void UartBridge::bridgeTaskThunk(void *arg)
{
    UartBridge *self = static_cast<UartBridge *>(arg);
    if (self) {
        self->bridgeTask();
        self->_bridge_task = nullptr;
    }
    vTaskDelete(NULL);
}

void UartBridge::bridgeTask()
{
    // 核心桥接循环：监听 TCP，收发数据并转发到 UART 或 USB CDC
    int listen_fd = -1;
    int client_fd = -1;
    listen_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
    if (listen_fd < 0) {
        appendLog("socket failed");
        return;
    }
    _listen_fd = listen_fd;
    int yes = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    struct sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(_net.port);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(listen_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        appendLog("bind failed");
        ::close(listen_fd);
        return;
    }
    if (listen(listen_fd, 1) < 0) {
        appendLog("listen failed");
        ::close(listen_fd);
        return;
    }
    appendLog("Listening...");

    fd_set readset;
    uint8_t buf[512];

    while (!_stop_flag) {
        struct sockaddr_in from = {};
        socklen_t fromlen = sizeof(from);
        client_fd = accept(listen_fd, (struct sockaddr *)&from, &fromlen);
        if (client_fd < 0) {
            if (_stop_flag) break;
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }
        char ipbuf[32];
        inet_ntop(AF_INET, &from.sin_addr, ipbuf, sizeof(ipbuf));
        char logbuf[64];
        snprintf(logbuf, sizeof(logbuf), "Client: %s", ipbuf);
        appendLog(logbuf);
        _client_connected = true;
        _client_ip = ipbuf;
        _bytes_up_net_to_uart = 0;
        _bytes_down_uart_to_net = 0;

        while (!_stop_flag) {
            FD_ZERO(&readset);
            FD_SET(client_fd, &readset);
            struct timeval tv = {.tv_sec = 0, .tv_usec = 100000};
            int sel = select(client_fd + 1, &readset, NULL, NULL, &tv);
            if (sel < 0) {
                break;
            }
            if (sel > 0 && FD_ISSET(client_fd, &readset)) {
                int r = recv(client_fd, buf, sizeof(buf), 0);
                if (r > 0) {
                    if (_port_type == PortType::TTL) {
                        _uart.write(buf, r);
                    } else {
                        _usb.write(buf, r);
                    }
                    _bytes_up_net_to_uart += r;
                } else {
                    break;
                }
            }

            size_t avail = (_port_type == PortType::TTL) ? _uart.available() : _usb.availableBridge();
            if (avail > 0) {
                size_t to_read = avail > sizeof(buf) ? sizeof(buf) : avail;
                size_t got = (_port_type == PortType::TTL) ? _uart.read(buf, to_read) : _usb.readBridge(buf, to_read);
                if (got > 0) {
                    int s = send(client_fd, buf, got, 0);
                    if (s > 0) {
                        _bytes_down_uart_to_net += s;
                    }
                }
            }
        }
        shutdown(client_fd, SHUT_RDWR);
        ::close(client_fd);
        client_fd = -1;
        appendLog("Client disconnected");
        _client_connected = false;
    }

    if (listen_fd >= 0) {
        ::close(listen_fd);
    }
    _listen_fd = -1;
}

void UartBridge::handleStartStopRequested()
{
    // UI 点击开始/停止后触发，统一走原有的启动/停止流程
    if (_running) {
        stopBridge();
    } else {
        startBridge();
    }
}

void UartBridge::handlePortTypeChanged(int sel)
{
    // 0->TTL，1->USB CDC，其余值忽略
    _port_type = (sel == 0) ? PortType::TTL : PortType::USB_CDC;
    saveSettings();
    refreshStartButtonState();
}

void UartBridge::handleBaudChanged(int sel)
{
    // 依据下拉索引写回波特率配置
    if (sel >= 0 && sel < static_cast<int>(sizeof(BAUD_VALUES) / sizeof(BAUD_VALUES[0]))) {
        _serial.baud = BAUD_VALUES[sel];
        saveSettings();
        applySerialSettings();
    }
}

void UartBridge::handleDataBitsChanged(int sel)
{
    if (sel >= 0 && sel < static_cast<int>(sizeof(DATABITS_VALUES) / sizeof(DATABITS_VALUES[0]))) {
        _serial.data_bits = DATABITS_VALUES[sel];
        saveSettings();
        applySerialSettings();
    }
}

void UartBridge::handleParityChanged(int sel)
{
    if (sel >= 0 && sel < static_cast<int>(sizeof(PARITY_VALUES) / sizeof(PARITY_VALUES[0]))) {
        _serial.parity = PARITY_VALUES[sel];
        saveSettings();
        applySerialSettings();
    }
}

void UartBridge::handleStopBitsChanged(int sel)
{
    if (sel >= 0 && sel < static_cast<int>(sizeof(STOPBITS_VALUES) / sizeof(STOPBITS_VALUES[0]))) {
        _serial.stop_bits = STOPBITS_VALUES[sel];
        saveSettings();
        applySerialSettings();
    }
}

void UartBridge::handleHeartbeatToggled(bool enabled)
{
    _heartbeat_enabled = enabled;
    saveSettings();

    // 同步到底层 USB CDC 心跳逻辑
    _usb.setHeartbeatEnabled(enabled);
    if (_port_type == PortType::USB_CDC) {
        if (enabled && _usb.isConnected()) {
            _usb.startHeartbeat();
        } else {
            _usb.stopHeartbeat();
        }
    }
}

void UartBridge::handlePortEdited(const char *text)
{
    // 文本框失焦后写回端口，非法值直接忽略
    if (!text) return;
    int p = atoi(text);
    if (p > 0 && p < 65536) {
        _net.port = static_cast<uint16_t>(p);
        saveSettings();
    } else if (_ui) {
        // 恢复为上次有效值，避免留存非法输入
        _ui->updatePort(_net.port);
    }
}

void UartBridge::handleExitRequested()
{
    // 停止桥接并通知核心退出应用
    stopBridge();
    notifyCoreClosed();
}

void UartBridge::resetStats()
{
    _bytes_up_net_to_uart = 0;
    _bytes_down_uart_to_net = 0;
    _client_connected = false;
    _client_ip.clear();
}

void UartBridge::handleUiTick()
{
    // 定时刷新 Wi-Fi 状态与按钮可用性
    updateWifiStatus();
    refreshStartButtonState();

    // USB CDC 模式下刷新设备就绪状态，提示枚举耗时
    if (_port_type == PortType::USB_CDC && _ui) {
        if (_usb.isConnected()) {
            _ui->setStatus("Status: USB ready");
        } else if (_running) {
            _ui->setStatus("Status: USB scanning...");
        }
    }

    // 统计信息展示
    if (_ui) {
        char stats[128];
        const char *cstate = _client_connected ? _client_ip.c_str() : "none";
        snprintf(stats, sizeof(stats), "Client: %s | Net->UART %llu | UART->Net %llu",
                 cstate,
                 static_cast<unsigned long long>(_bytes_up_net_to_uart),
                 static_cast<unsigned long long>(_bytes_down_uart_to_net));
        _ui->setStats(stats);
    }

    // 心跳：可选地向当前端口发送心跳包（默认关，可由 UI 开关控制）
    if (_running && _heartbeat_enabled) {
        int64_t now_ms = esp_timer_get_time() / 1000;
        const int64_t interval_ms = 3000;
        if (now_ms - _last_heartbeat_ms >= interval_ms) {
            char hb[64];
            int len = snprintf(hb, sizeof(hb), "HB #%lu\r\n", static_cast<unsigned long>(_heartbeat_counter++));
            if (len > 0) {
                if (_port_type == PortType::TTL) {
                    _uart.write(reinterpret_cast<const uint8_t *>(hb), len);
                } else {
                    _usb.write(reinterpret_cast<const uint8_t *>(hb), len);
                }
            }
            _last_heartbeat_ms = now_ms;
        }
    }
}
