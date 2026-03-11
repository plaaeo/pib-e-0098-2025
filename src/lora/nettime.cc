#include "lora/nettime.hh"

namespace lora {
    port::time_us NetworkTimer::get_time_us() {
        return port::get_rtc_time() + network_time_offset_us;
    };

    void NetworkTimer::synchronize(port::time_us externalTimestamp_us, port::time_us monoTimeAtRx_us) {
        auto wallTimeAtRx_us = port::get_rtc_time() - (port::get_monotonic_time() - monoTimeAtRx_us);
        network_time_offset_us = externalTimestamp_us - wallTimeAtRx_us;
    };
}