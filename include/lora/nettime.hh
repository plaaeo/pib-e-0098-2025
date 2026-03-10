#pragma once

#include <cstdint>
#include <time.h>

#include <esp_timer.h>
#include <rtc.h>

namespace lora {
    /**
     * @brief Utilitário usado para contar o tempo na rede com precisão.
     * Usa o RTC do ESP32 como fonte de tempo precisa.
     */
    struct NetworkTimer {
        int64_t network_time_offset_us;

        /**
         * @brief Retorna o tempo atual do RTC com um offset aplicado para refletir o
         * tempo da rede em microsegundos.
         */
        int64_t get_time_us() {
            return static_cast<int64_t>(
                esp_rtc_get_time_us()
            ) + network_time_offset_us;
        };

        /**
         * @brief Sincroniza o timer com um tempo recebido da rede.
         * @param externalTimestamp_us Uma medida de tempo externa recebida da rede em microsegundos.
         * @param hrtTimeAtRx_us O resultado de `esp_timer_get_time()` no momento exato em que
         * o pacote foi recebido (deve ser executado ao receber um IRQ do radio para maior precisão).
         */
        void synchronize(int64_t externalTimestamp_us, int64_t hrtTimeAtRx_us) {
            network_time_offset_us = 0;
            auto wallTimeAtRx_us = get_time_us() - (esp_timer_get_time() - hrtTimeAtRx_us);
            network_time_offset_us = externalTimestamp_us - wallTimeAtRx_us;
        };
    };
}