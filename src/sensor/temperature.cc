#include "sensor/temperature.hh"
#include <math.h>

namespace sensor {
    float NTC10kState::convert(float volts) const {
        // Calcular resistência do thermistor
        float R = 10000.0f * ((vref / volts) - 1);

        // Calcular 1/T usando Steinhart-Hart
        float logR = log(R);
        float invT = a + (b * logR) + (c * logR * logR * logR);

        return (1.0f / invT) - 273.15f + offset;
    }
}