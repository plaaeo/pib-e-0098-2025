#include "sensor/ph.hh"

namespace sensor {
    float Ph4502cState::convert(float volts) const {
        return (a * volts) + b;
    }
}