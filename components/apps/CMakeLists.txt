set(APPS_DIR ./)
file(GLOB_RECURSE APPS_C_SRCS ${APPS_DIR}/*.c)
file(GLOB_RECURSE APPS_CPP_SRCS ${APPS_DIR}/*.cpp)
# Ensure CMake reconfigures when new app sources are added (e.g., uart_bridge)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    ${APPS_DIR}/uart_bridge/UartBridge.cpp
    ${APPS_DIR}/uart_bridge/UartBridge.hpp
    ${APPS_DIR}/uart_bridge/UartBridgeOptions.hpp
    ${APPS_DIR}/uart_bridge/ui/UartBridgeUI.cpp
    ${APPS_DIR}/uart_bridge/ui/UartBridgeUI.hpp)

idf_component_register(
    SRCS ${APPS_C_SRCS} ${APPS_CPP_SRCS}
    INCLUDE_DIRS ${APPS_DIR}
    REQUIRES lvgl__lvgl esp_event esp_wifi nvs_flash esp_driver_jpeg esp_mm esp-brookesia bsp_extra esp32_p4_function_ev_board esp_video pedestrian_detect human_face_detect espressif__esp_lcd_touch_gt911 espressif__esp_tinyusb espressif__usb_host_cdc_acm)

target_compile_options(
    ${COMPONENT_LIB}
    PRIVATE
    $<$<COMPILE_LANGUAGE:C>:
        -Wno-format
        -Wno-incompatible-pointer-types
        -Wno-int-conversion
    >
    $<$<COMPILE_LANGUAGE:CXX>:
        -Wno-format
        -Wno-conversion-null
        -Wno-attributes
        -Wno-write-strings
    >
    -DLV_LVGL_H_INCLUDE_SIMPLE
)
