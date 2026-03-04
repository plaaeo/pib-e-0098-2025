#pragma once

#include "sensor/reading.hh"
#include "repr/codec.hh"

namespace repr {
    /**
     * @brief Utilitário coversor de pH.
     */
    constexpr static auto META_PH = LinearSensorCodec {
        .bits = 8,
        .min = 0.0f,
        .max = 14.0f,
        .validity_threshold = 1.0f,
    };

    /**
     * @brief Utilitário coversor de TDS.
     * @todo Verificar mínimos/máximos do medidor em laboratório.
     */
    constexpr static auto META_TDS = LinearSensorCodec {
        .bits = 12,
        .min = 0.0f,
        .max = 1200.0f,
        .validity_threshold = 100.0f,
    };

    /**
     * @brief Utilitário coversor de temperatura.
     */
    constexpr static auto META_TEMPERATURE = LinearSensorCodec {
        .bits = 8,
        .min = -10.0f,
        .max = 110.0f,
        .validity_threshold = 0.0f,
    };

    // Garantir que a codificação do pH tem erro menor que ±0.5 pH
    static_assert(
        META_PH.step() < 0.5f,
        "O erro de (de)codificação do pH excede 0.5 pH"
    );

    // Garantir que a codificação do TDS tem erro menor que ±1 ppm
    static_assert(
        META_TDS.step() < 1.0f,
        "O erro de (de)codificação do TDS excede 1 ppm"
    );

    // Garantir que a codificação da temperatura tem erro menor que ±1°C.
    static_assert(
        META_TEMPERATURE.step() < 1.0f,
        "O erro de (de)codificação da temperatura excede 1°C"
    );

    /**
     * @brief Representa uma estrutura `sensor::Reading` comprimida, utilizada para transmitir
     * eficientemente coletas de sensores via LoRa.
     */
    struct CompressedReading {
        uint32_t time;
        uint16_t temperature;
        uint16_t tds;
        uint16_t ph;

        /**
         * @brief Representa o tamanho desta estrutura em bits.
         */
        constexpr static auto BIT_SIZE = sizeof(uint32_t) * 8 + META_PH.bits + META_TDS.bits + META_TEMPERATURE.bits;

        /**
         * @brief Comprime uma estrutura `sensor::Reading`.
         */
        static CompressedReading compress(const sensor::Reading &reading) {
            return (CompressedReading) {
                .time = reading.time,
                .temperature = META_TEMPERATURE.compress(reading.tds),
                .tds = META_TDS.compress(reading.tds),
                .ph = META_PH.compress(reading.ph),
            };
        }

        /**
         * @brief Reconstrói a estrutura `sensor::Reading` original a partir dos
         * valores comprimidos.
         */
        sensor::Reading decompress() {
            return (sensor::Reading) {
                .time = time,
                .temperature = META_TEMPERATURE.decompress(tds),
                .tds = META_TDS.decompress(tds),
                .ph = META_PH.decompress(ph),
            };
        }
    };
}