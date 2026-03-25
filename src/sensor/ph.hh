#pragma once

namespace sensor {
/**
 * @brief Armazena valores de calibração de um sensor de pH que utiliza
 * o módulo de leitura PH4502-C.
 * @note É seguro armazenar esta estrutura em EEPROM ou memória do RTC.
 * @todo Implementar uso de `vref`.
 */
struct Ph4502cState
{
    float a;
    float b;

    /**
     * @brief Converte a tensão da saída `Pout` do leitor para um valor de pH.
     * @warning A conversão assume que o módulo está conectado a 5 volts.
     */
    float convert(float volts) const noexcept { return (a * volts) + b; };
};
}  // namespace sensor