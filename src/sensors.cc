#include "sensors.hh"

namespace sens {
    float read_volts(ADS& ads, std::uint8_t channel) {
        auto aIn = ads.readADC_SingleEnded(channel);
        return ads.computeVolts(aIn);
    }
}