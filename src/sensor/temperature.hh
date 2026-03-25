#pragma once

#include <math.h>

namespace sensor {
/**
 * @brief Armazena valores de calibração de um sensor de temperatura NTC 10k.
 * @note É seguro armazenar esta estrutura em EEPROM ou memória do RTC.
 */
struct NTC10kState
{
    float vref;
    float offset;
    float a;
    float b;
    float c;

    /**
     * @brief Coleta a temperatura de um NTC 10K e converte-a para celsius
     * usando a fórmula de Steinhart-Hart.
     */
    float convert(float volts) const noexcept
    {
        // Calcular resistência do thermistor
        float R = 10000.0f * ((vref / volts) - 1);

        // Calcular 1/T usando Steinhart-Hart
        float logR = log(R);
        float invT = a + (b * logR) + (c * logR * logR * logR);

        return (1.0f / invT) - 273.15f + offset;
    }
};
}  // namespace sensor