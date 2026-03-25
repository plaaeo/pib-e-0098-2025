#pragma once
#include <stdint.h>

#ifdef ESP32
#    include <Adafruit_ADS1X15.h>
#endif

#include "port/types.hh"

/**
 * @todo Separar constantes de calibração em variáveis do EEPROM
 */
namespace sensor {
/**
 * @brief Uma interface genérica para medir interfaces analógicas. Implementado
 * para permitir a troca entre o uso de ADCs externos e internos sem necessidade
 * de adaptar código.
 */
class AnalogInterface
{
protected:
    uint32_t m_ChannelCount;

    AnalogInterface(uint32_t channels);

public:
    /**
     * @returns A quantidade de canais presente na interface.
     */
    inline uint32_t channel_count() const;

    /**
     * @brief Mede a tensão no canal especificado. O canal
     * deve ser válido para esta interface.
     */
    virtual float measure_volts(uint32_t channel) = 0;

    AnalogInterface(AnalogInterface &&) = delete;
    AnalogInterface(const AnalogInterface &) = delete;
    AnalogInterface &operator=(AnalogInterface &&) = delete;
    AnalogInterface &operator=(const AnalogInterface &) = delete;
};

#ifdef ESP32
/**
 * @brief Uma interface analógica para ADCs externos do modelo ADS1X15.
 */
class ADS1X15Interface : public AnalogInterface
{
public:
    ADS1X15Interface();

    /**
     * @brief Inicializa a interface.
     */
    bool begin(TwoWire &wire);

    /**
     * @brief Mede a tensão no canal especificado. O canal
     * deve ser válido (channel < channel_count()).
     */
    virtual float measure_volts(uint32_t channel) override;

protected:
    Adafruit_ADS1X15 m_ADC;
};
#endif

/**
 * @brief Uma interface analógica para ADCs internos usando a interface
 * do `analogRead` do framework Arduino.
 *
 * @note Ao utilizar em uma placa ESP32, as funções `set_resolution` e
 * `set_vref` não influenciarão o resultado das leituras, pois o framework
 * `arduino-esp32` já realiza conversões de leituras para milivolts com correção
 * de erro.
 */
class ArduinoInterface : public AnalogInterface
{
public:
    ArduinoInterface(uint8_t resolution_bits, float vref);

    /**
     * @brief Define a resolução das leituras da interface analógica.
     * Esta função NÃO executa `analogReadResolution`.
     *
     * @note Ao utilizar em uma placa ESP32, esta função não influenciará
     * o resultado das leituras, pois o framework `arduino-esp32` já realiza
     * conversões de leituras para milivolts com correção de erro.
     */
    void set_resolution(uint8_t resolution_bits);

    /**
     * @brief Define a tensão de referência para leituras da interface
     * analógica. Esta função NÃO executa `analogReference`.
     *
     * @note Ao utilizar em uma placa ESP32, esta função não influenciará
     * o resultado das leituras, pois o framework `arduino-esp32` já realiza
     * conversões de leituras para milivolts com correção de erro.
     */
    void set_vref(float vref);

    /**
     * @brief Mede a tensão no canal especificado. O canal
     * deve ser um pino analógico (A0-A*).
     */
    virtual float measure_volts(uint32_t channel) override;

protected:
    uint8_t m_Resolution;
    float   m_Vref;
};
}  // namespace sensor