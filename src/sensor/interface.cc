#include "types.hh"
#include "sensor/interface.hh"

namespace sensor {
    AnalogInterface::AnalogInterface(uint32_t channels) : m_ChannelCount(channels) { };
    
    uint32_t AnalogInterface::channel_count() const {
        return m_ChannelCount;
    };

    ADS1X15Interface::ADS1X15Interface()
        : AnalogInterface(4)
    {}

    bool ADS1X15Interface::begin(TwoWire &wire) {
        if (!m_ADC.begin(ADS1X15_ADDRESS, &wire)) {
            ESP_LOGE("ADS1X15Interface", "failed to initialize ADS1X15 interface");
            return false;
        }

        return true;
    };

    float ADS1X15Interface::measure_volts(uint32_t channel) {
        if (channel > m_ChannelCount) return 0;

        return m_ADC.computeVolts(
            m_ADC.readADC_SingleEnded(channel)
        );
    };

    ArduinoInterface::ArduinoInterface(uint8_t resolution_bits, float vref)
        : AnalogInterface(NUM_ANALOG_INPUTS)
        , m_Resolution(resolution_bits)
        , m_Vref(vref)
    {}

    void ArduinoInterface::set_resolution(uint8_t resolution_bits) {
        m_Resolution = resolution_bits;
    };

    void ArduinoInterface::set_vref(float vref) {
        m_Vref = vref;
    };

    float ArduinoInterface::measure_volts(uint32_t channel) {
#if ESP32
        /**
         * @note O ESP32 realiza correções de erro nos canais analógicos ao usar a função
         * `analogReadMilliVolts` ao invés de `analogRead`, linearizando o resultado.
         */
        return analogReadMilliVolts(channel) * 1000.0f;
#else
        float max = 1 << m_Resolution;
        return (analogRead(channel) * m_Vref) / max;
#endif
    };
}