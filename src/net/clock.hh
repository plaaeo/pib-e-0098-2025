#pragma once

#include <stdint.h>

#include "port/time.hh"

namespace net {
/**
 * @brief Utilitário usado para contar o tempo na rede com precisão.
 * Usa um RTC como fonte de tempo precisa.
 */
struct Clock
{
    port::time_us network_time_offset_us;

    /**
     * @brief Retorna o tempo atual do RTC com um offset aplicado para refletir
     * o tempo da rede em microsegundos.
     */
    port::time_us get_time_us() const
    {
        return port::get_rtc_time() + network_time_offset_us;
    };

    /**
     * @brief Sincroniza o timer com um tempo recebido da rede.
     * @param externalTimestamp_us Uma medida de tempo externa recebida da rede
     * em microsegundos.
     * @param monoTimeAtRx_us O resultado de `port::get_monotonic_time()` no
     * momento exato em que o pacote foi recebido (deve ser executado ao receber
     * um IRQ do radio para maior precisão).
     */
    void synchronize(port::time_us externalTimestamp_us,
                     port::time_us monoTimeAtRx_us)
    {
        auto wallTimeAtRx_us = port::get_rtc_time();
        wallTimeAtRx_us -= port::get_monotonic_time() - monoTimeAtRx_us;
        network_time_offset_us = externalTimestamp_us - wallTimeAtRx_us;
    };
};
}  // namespace net