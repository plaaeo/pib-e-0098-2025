#include <assert.h>

#ifdef ESP32
#include <hal/gpio_types.h>

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

namespace port {
    using gpio_t = int;
}

#define ISR_SAFE_ATTR
#define RTC_DATA_ATTR
#define PORT_LOGI(tag, format, ...) printf("(I) [%s] " format, tag, ##__VA_ARGS__)
#define PORT_LOGE(tag, format, ...) printf("(E) [%s] " format, tag, ##__VA_ARGS__)
#define PORT_LOGW(tag, format, ...) printf("(W) [%s] " format, tag, ##__VA_ARGS__)
#define PORT_LOGD(tag, format, ...) printf("(D) [%s] " format, tag, ##__VA_ARGS__)
#define PORT_LOGV(tag, format, ...) printf("(V) [%s] " format, tag, ##__VA_ARGS__)

#endif