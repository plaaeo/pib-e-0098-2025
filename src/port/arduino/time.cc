#include <Arduino.h>
#include <stdint.h>

#include "port/time.hh"
namespace port {
ISR_SAFE_ATTR port::time_us get_monotonic_time()
{
    return micros();
};
}  // namespace port