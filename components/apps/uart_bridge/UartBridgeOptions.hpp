#pragma once

#include <cstdint>
#include "driver/uart.h"

// UI 下拉框选项与对应数值映射，供业务与 UI 共同使用
inline constexpr const char *PORT_OPTIONS = "TTL\nUSB-CDC";
inline constexpr const char *BAUD_OPTIONS = "9600\n19200\n38400\n57600\n115200\n230400\n460800\n921600";
inline constexpr int BAUD_VALUES[] = {9600, 19200, 38400, 57600, 115200, 230400, 460800, 921600};
inline constexpr const char *DATABITS_OPTIONS = "5\n6\n7\n8";
inline constexpr uint8_t DATABITS_VALUES[] = {5, 6, 7, 8};
inline constexpr const char *PARITY_OPTIONS = "None\nOdd\nEven";
inline constexpr uint8_t PARITY_VALUES[] = {UART_PARITY_DISABLE, UART_PARITY_ODD, UART_PARITY_EVEN};
inline constexpr const char *STOPBITS_OPTIONS = "1\n2";
inline constexpr uint8_t STOPBITS_VALUES[] = {UART_STOP_BITS_1, UART_STOP_BITS_2};
