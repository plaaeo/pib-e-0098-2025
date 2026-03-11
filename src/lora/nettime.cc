#include "lora/nettime.hh"

#include <rtc.h>

namespace lora {
    port::time_us NetworkTimer::get_time_us() {
        return static_cast<port::time_us>(
            esp_rtc_get_time_us()
        ) + network_time_offset_us;
    };

    void NetworkTimer::synchronize(port::time_us externalTimestamp_us, port::time_us monoTimeAtRx_us) {
        network_time_offset_us = 0;
        auto wallTimeAtRx_us = get_time_us() - (port::get_monotonic_time() - monoTimeAtRx_us);
        network_time_offset_us = externalTimestamp_us - wallTimeAtRx_us;
    };
}