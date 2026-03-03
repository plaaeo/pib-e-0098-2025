#pragma once

namespace sensor {
    /**
     * @brief Armazena valores de calibração de um sensor TDS DFRobot.
     * @note É seguro armazenar esta estrutura em EEPROM ou memória do RTC.
     * @todo Implementar uso de `vref`.
     */
    struct TDSMeterState {        
        float k;

        /**
         * @brief Coleta a condutividade no eletrodo de um sensor TDS DFRobot
         * e converte-a para TDS, em ppm, compensando pela temperatura.
         * @warning A conversão assume que o módulo está conectado a 5 volts.
         */
        float convert(float volts, float temperature_c) const noexcept;
    };
}