#include <Arduino.h>
#include <Adafruit_ADS1X15.h>
#include <stdint.h>

#include "sensor/reading.hh"
#include "sensor/interface.hh"
#include "sensor/temperature.hh"
#include "sensor/tds.hh"
#include "sensor/ph.hh"

constexpr auto TAG = "sens";

static struct {
    enum : uint32_t {
        //< Canal do ADC conectado ao sensor de temperatura (A0).
        ADC_CHANNEL_TEMPERATURE = A0,

        //< Canal do ADC conectado ao sensor de pH (A1).
        ADC_CHANNEL_PH = A1,

        //< Canal do ADC conectado ao sensor de TDS (A2).
        ADC_CHANNEL_TDS = A2,
    };

    //< Valores de calibração do sensor de temperatura
    sensor::NTC10kState temperature {
        .vref = 5.0f,
        .offset = 0.0f,
        .a = 0.0011384f,
        .b = 0.00023245f,
        .c = 0.00000009489f,
    };
    
    //< Valores de calibração do sensor de pH
    sensor::Ph4502cState ph {
        .a = -5.831f,
        .b = 22.05f,
    };
    
    //< Valores de calibração do sensor de TDS
    sensor::TDSMeterState tds {
        .k = 0.7f,
    };

    /**
     * @brief Produz uma leitura atual de sensores a partir de uma interface de leitura analógica.
     * @todo Implementar armazenamento de tempo.
     */
    sensor::Reading measure(sensor::AnalogInterface &iface) const noexcept {
        // Medir e calcular temperatura em graus celsius
        auto curTemperature = temperature.convert(
            iface.measure_volts(ADC_CHANNEL_TEMPERATURE)
        );

        // Medir e calcular TDS em ppm
        auto curTds = tds.convert(
            iface.measure_volts(ADC_CHANNEL_TDS),
            curTemperature
        );

        // Medir e calcular pH
        auto curPh = ph.convert(
            iface.measure_volts(ADC_CHANNEL_PH)
        );

        return (sensor::Reading) {
            .time = 123456,
            .temperature = curTemperature,
            .tds = curTds,
            .ph = curPh,
        };
    }
} g_Sensors {};

//< Interface analógica para coletas do Arduino.
sensor::ArduinoInterface g_ADC(10, 5);

void setup() {
#ifdef STATUS_LED
    if constexpr (STATUS_LED != GPIO_NUM_NC) {
        pinMode(STATUS_LED, OUTPUT);
    }
#endif
    
    Serial.begin(115200);
};

void loop() {
    if (!Serial) return;
    
    auto reading = g_Sensors.measure(g_ADC);
    
    Serial.print("temp (degC):");
    Serial.println(reading.temperature);
    Serial.print("tds (ppm):");
    Serial.println(reading.tds);
    Serial.print("pH:");
    Serial.println(reading.ph);

    delay(1000);
}