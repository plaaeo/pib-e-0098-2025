#pragma once
#include <cstdint>
#include <cmath>

namespace repr {
    /**
     * @brief Um utilitário usado para converter linearmente valores válidos de sensores
     * para representações menores.
     */
    struct LinearSensorCodec {
        const uint8_t bits;
        const float min;
        const float max;
        const float validity_threshold;
        
        /**
         * @brief Retorna a máscara de bits utilizada por este valor.
         */
        constexpr uint16_t mask() const noexcept {
            return (uint16_t)((1U << bits) - 1);
        }

        /**
         * @brief Retorna um float representando o quanto vale uma unidade
         * comprimida deste valor na unidade de medida original.
         */
        constexpr float step() const noexcept {
            return (max - min) / (float)(mask());
        }

        /**
         * @brief Converte um valor para sua representação comprimida.
         */
        constexpr uint16_t compress(float value) const noexcept {
            if (value < min) {
                return value < min - validity_threshold
                    ? mask()    // Filtrar valor inválido
                    : 0;        // Limitar para o menor valor possível
            } else if (value > max) {
                return value > max + validity_threshold
                    ? mask()        // Filtrar valor inválido
                    : mask() - 1;   // Limitar para o maior valor possível
            } else {
                return (uint16_t)(value / step());
            }
        }

        /**
         * @brief Converte um valor de sua representação comprimida de volta
         * para sua unidade de medida original.
         * @warning Ao receber como entrada um valor inválido, retorna `NaN`.
         */
        constexpr float decompress(uint16_t value) const noexcept {
            // Retornar NaN caso seja um valor inválido.
            if (value == mask()) {
                return NAN;
            }
            
            return value * step();
        }
    };
}