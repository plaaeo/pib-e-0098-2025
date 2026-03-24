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
        float convert(float volts, float temperature_c) const noexcept  {
            /**
             * TODO: Calibrar com valores medidos em laboratório
             * - No ar: de 0.003V, 0.000938V, 0.00075V (21.9degC)
             * - Água destilada: de 0.009V, 0.008063V, 0.017250V (18.5degC)
             * - Solução 84uS/cm: de 0.15075V, 0.151313V, 0.133125V
             * - Solução 1413uS/cm: de 1.936687V, 1.941375V, 1.998V, 2.037V
             */

            // Medir condutividade elétrica
            float ec = 133.42f * volts;
            ec = (ec - 255.86f) * volts;
            ec = (ec + 857.39f) * volts;
            
            // Compensar com a temperatura
            float tc = 1.0f + 0.02f * (temperature_c - 25.0f);
            return k * ec / tc;
        }
    };
}