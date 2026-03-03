#pragma once

namespace sensor {
    /**
     * @brief Armazena valores de calibração de um sensor de temperatura NTC 10k.
     * @note É seguro armazenar esta estrutura em EEPROM ou memória do RTC.
     */
    struct NTC10kState {
        float vref;
        float offset;
        float a;
        float b;
        float c;

        /**
         * @brief Coleta a temperatura de um NTC 10K e converte-a para celsius
         * usando a fórmula de Steinhart-Hart.
         */
        float convert(float volts) const noexcept;
    };
}