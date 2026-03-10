#include <assert.h>

#ifdef ESP32

using GpioPin_t = gpio_num_t;

#define ISR_SAFE_ATTR IRAM_ATTR

#else

using GpioPin_t = int;
#define ISR_SAFE_ATTR
#define RTC_DATA_ATTR
#define ESP_LOGI(...)
#define ESP_LOGE(...)
#define ESP_LOGW(...)
#define ESP_LOGD(...)
#define ESP_LOGV(...)

#define likely(x) x
#define unlikely(x) x

#endif