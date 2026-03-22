#include "net/clock.hh"

namespace net {
    port::time_us Clock::get_time_us() {
        return port::get_rtc_time() + network_time_offset_us;
    };

    void Clock::synchronize(port::time_us externalTimestamp_us, port::time_us monoTimeAtRx_us) {
        auto wallTimeAtRx_us = port::get_rtc_time() - (port::get_monotonic_time() - monoTimeAtRx_us);
        network_time_offset_us = externalTimestamp_us - wallTimeAtRx_us;
    };
}