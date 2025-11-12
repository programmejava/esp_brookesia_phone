#include "global_screen_saver.hpp"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"

static const char *TAG = "GlobalScreenSaver";

GlobalScreenSaver& GlobalScreenSaver::getInstance() {
    static GlobalScreenSaver instance;
    return instance;
}

GlobalScreenSaver::~GlobalScreenSaver() {
    if (_screen_saver_timer != nullptr) {
        esp_timer_delete(_screen_saver_timer);
        _screen_saver_timer = nullptr;
    }
}

void GlobalScreenSaver::init() {
    if (_is_initialized) {
        return;
    }
    
    // Create high-precision timer
    esp_timer_create_args_t timer_config = {
        .callback = screenSaverTimerCallback,
        .arg = this,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "screen_saver_timer",
        .skip_unhandled_events = false
    };
    
    esp_err_t ret = esp_timer_create(&timer_config, &_screen_saver_timer);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create screen saver timer: %s", esp_err_to_name(ret));
        return;
    }
    
    // Load timeout from NVS (使用独立的屏保命名空间)
    nvs_handle_t nvs_handle;
    ret = nvs_open("screen_saver", NVS_READONLY, &nvs_handle);
    if (ret == ESP_OK) {
        size_t required_size = 0;
        ret = nvs_get_blob(nvs_handle, "timeout", NULL, &required_size);
        if (ret == ESP_OK && required_size == sizeof(int)) {
            nvs_get_blob(nvs_handle, "timeout", &_timeout_seconds, &required_size);
            ESP_LOGI(TAG, "Loaded screen saver timeout: %d seconds", _timeout_seconds);
        }
        nvs_close(nvs_handle);
    }
    
    // Create a high-frequency timer to monitor touch input device directly
    // This ensures touch detection works even when screen is off or UI is changed
    lv_timer_create([](lv_timer_t * timer) {
        static lv_indev_state_t last_state = LV_INDEV_STATE_RELEASED;
        static bool first_run = true;
        
        GlobalScreenSaver* instance = &GlobalScreenSaver::getInstance();
        
        lv_indev_t *indev = lv_indev_get_next(NULL);
        while (indev != NULL) {
            if (lv_indev_get_type(indev) == LV_INDEV_TYPE_POINTER) {
                lv_indev_state_t current_state = indev->proc.state;
                
                // Detect touch press events
                if (current_state == LV_INDEV_STATE_PRESSED && last_state == LV_INDEV_STATE_RELEASED) {
                    ESP_LOGI(TAG, "Direct touch input detected - waking up");
                    instance->onUserActivity();
                }
                
                last_state = current_state;
                break;
            }
            indev = lv_indev_get_next(indev);
        }
        
        if (first_run) {
            ESP_LOGI(TAG, "Direct touch monitoring timer started");
            first_run = false;
        }
    }, 20, nullptr);  // Check every 20ms for very fast response
    
    ESP_LOGI(TAG, "High-frequency touch monitoring initialized");
    
    // Start the timer
    startTimer();
    
    _is_initialized = true;
    ESP_LOGI(TAG, "GlobalScreenSaver initialized with %d seconds timeout", _timeout_seconds);
}

void GlobalScreenSaver::setTimeoutSeconds(int timeout_seconds) {
    if (timeout_seconds <= 0) {
        ESP_LOGW(TAG, "Invalid timeout: %d, using default 30 seconds", timeout_seconds);
        timeout_seconds = 30;
    }
    
    _timeout_seconds = timeout_seconds;
    
    // Save to NVS (使用独立的屏保命名空间)
    nvs_handle_t nvs_handle;
    esp_err_t ret = nvs_open("screen_saver", NVS_READWRITE, &nvs_handle);
    if (ret == ESP_OK) {
        ret = nvs_set_blob(nvs_handle, "timeout", &_timeout_seconds, sizeof(_timeout_seconds));
        if (ret == ESP_OK) {
            nvs_commit(nvs_handle);
            ESP_LOGI(TAG, "Saved screen saver timeout: %d seconds", _timeout_seconds);
        }
        nvs_close(nvs_handle);
    }
    
    // Restart timer with new timeout
    if (_is_initialized && !_screen_is_off) {
        startTimer();
    }
}

void GlobalScreenSaver::onUserActivity() {
    if (!_is_initialized) {
        return;
    }
    
    ESP_LOGI(TAG, "User activity detected");
    
    // If screen is off, turn it on
    if (_screen_is_off) {
        turnOnScreen();
    }
    
    // Restart timer
    startTimer();
}

void GlobalScreenSaver::turnOffScreen() {
    if (_screen_is_off) {
        return;
    }
    
    ESP_LOGI(TAG, "Turning off screen");
    // Note: We don't save brightness here anymore, we'll read it fresh when turning on
    bsp_display_brightness_set(0);  // Turn off by setting brightness to 0
    _screen_is_off = true;
}

void GlobalScreenSaver::turnOnScreen() {
    if (!_screen_is_off) {
        return;
    }
    
    ESP_LOGI(TAG, "Turning on screen");
    // Always read the latest brightness setting from NVS when turning on
    int latest_brightness = getCurrentBrightness();
    ESP_LOGI(TAG, "Restoring to latest brightness: %d%%", latest_brightness);
    bsp_display_brightness_set(latest_brightness);
    _screen_is_off = false;
    
    // Restart timer after turning on screen
    startTimer();
}

void GlobalScreenSaver::screenSaverTimerCallback(void* arg) {
    GlobalScreenSaver* instance = static_cast<GlobalScreenSaver*>(arg);
    if (instance != nullptr) {
        ESP_LOGI(TAG, "Screen saver timer expired, turning off screen");
        instance->turnOffScreen();
    }
}

void GlobalScreenSaver::globalTouchEventCallback(lv_event_t* e) {
    lv_event_code_t code = lv_event_get_code(e);
    GlobalScreenSaver* instance = static_cast<GlobalScreenSaver*>(lv_event_get_user_data(e));
    
    if (instance != nullptr && (code == LV_EVENT_PRESSED || code == LV_EVENT_CLICKED || code == LV_EVENT_RELEASED)) {
        ESP_LOGI(TAG, "Touch event detected: %d", code);
        instance->onUserActivity();
    }
}

void GlobalScreenSaver::startTimer() {
    if (_screen_saver_timer == nullptr) {
        ESP_LOGE(TAG, "Timer not initialized");
        return;
    }
    
    // Stop existing timer first
    stopTimer();
    
    // Start timer with configured timeout
    uint64_t timeout_us = (uint64_t)_timeout_seconds * 1000000ULL;  // Convert to microseconds
    esp_err_t ret = esp_timer_start_once(_screen_saver_timer, timeout_us);
    
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Screen saver timer started for %d seconds", _timeout_seconds);
    } else {
        ESP_LOGE(TAG, "Failed to start screen saver timer: %s", esp_err_to_name(ret));
    }
}

void GlobalScreenSaver::stopTimer() {
    if (_screen_saver_timer != nullptr) {
        esp_timer_stop(_screen_saver_timer);
    }
}

int GlobalScreenSaver::getCurrentBrightness() {
    const int32_t SCREEN_BRIGHTNESS_DEFAULT = 20;  // 默认亮度
    int32_t brightness = SCREEN_BRIGHTNESS_DEFAULT;
    
    // 从NVS读取用户设置的亮度值 (使用与Settings相同的命名空间和数据类型)
    nvs_handle_t nvs_handle;
    esp_err_t ret = nvs_open("storage", NVS_READONLY, &nvs_handle);
    if (ret == ESP_OK) {
        ret = nvs_get_i32(nvs_handle, "brightness", &brightness);
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "Loaded brightness from NVS: %d", (int)brightness);
        } else if (ret == ESP_ERR_NVS_NOT_FOUND) {
            ESP_LOGW(TAG, "No brightness setting in NVS, using default: %d", (int)SCREEN_BRIGHTNESS_DEFAULT);
            brightness = SCREEN_BRIGHTNESS_DEFAULT;
        } else {
            ESP_LOGW(TAG, "Failed to read brightness from NVS (error: %s), using default: %d", 
                     esp_err_to_name(ret), (int)SCREEN_BRIGHTNESS_DEFAULT);
            brightness = SCREEN_BRIGHTNESS_DEFAULT;
        }
        nvs_close(nvs_handle);
    } else {
        ESP_LOGW(TAG, "Failed to open NVS for brightness, using default: %d", (int)SCREEN_BRIGHTNESS_DEFAULT);
        brightness = SCREEN_BRIGHTNESS_DEFAULT;
    }
    
    // 确保亮度值在合理范围内
    const int32_t SCREEN_BRIGHTNESS_MIN = 20;
    const int32_t SCREEN_BRIGHTNESS_MAX = 100;
    brightness = (brightness < SCREEN_BRIGHTNESS_MIN) ? SCREEN_BRIGHTNESS_MIN : 
                 (brightness > SCREEN_BRIGHTNESS_MAX) ? SCREEN_BRIGHTNESS_MAX : brightness;
    
    return (int)brightness;
}