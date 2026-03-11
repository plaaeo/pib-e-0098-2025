#include <assert.h>

#ifdef ESP32
#include <hal/gpio_types.h>
#include <esp_log.h>

namespace port {
    using gpio_t = gpio_num_t;
}

#define ISR_SAFE_ATTR IRAM_ATTR
#define PORT_LOGI ESP_LOGI
#define PORT_LOGE ESP_LOGE
#define PORT_LOGW ESP_LOGW
#define PORT_LOGD ESP_LOGD
#define PORT_LOGV ESP_LOGV

#else
#include <LibPrintf.h>
#include "port/time.hh"

namespace port {
    using gpio_t = int;
}

#define ISR_SAFE_ATTR
#define RTC_DATA_ATTR
#define PORT_LOGI(tag, format, ...) printf("\033[0;32m" "I (%li) [%s] " format "\033[0m", port::get_monotonic_time(), tag, ##__VA_ARGS__)
#define PORT_LOGE(tag, format, ...) printf("\033[0;31m" "E (%li) [%s] " format "\033[0m", port::get_monotonic_time(), tag, ##__VA_ARGS__)
#define PORT_LOGW(tag, format, ...) printf("\033[0;33m" "W (%li) [%s] " format "\033[0m", port::get_monotonic_time(), tag, ##__VA_ARGS__)
#define PORT_LOGD(tag, format, ...) printf("D (%li) [%s] " format "\033[0m", port::get_monotonic_time(), tag, ##__VA_ARGS__)
#define PORT_LOGV(tag, format, ...) printf("V (%li) [%s] " format "\033[0m", port::get_monotonic_time(), tag, ##__VA_ARGS__)

#endif