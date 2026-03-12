#include <Arduino.h>
#include <LoRa.h>
#include <stdint.h>

#include "port/time.hh"
#include "sensor/reading.hh"
#include "sensor/interface.hh"
#include "sensor/temperature.hh"
#include "sensor/tds.hh"
#include "sensor/ph.hh"
#include "repr/reading.hh"

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
bool g_HasLora = false;

void setup() {    
    Serial.begin(115200);

    LoRa.setPins(SS, LORA_RST, LORA_DIO0);
    if (LoRa.begin(915000000)) {
        g_HasLora = true;
        LoRa.setSpreadingFactor(11);
        LoRa.setSignalBandwidth(125000);
        LoRa.setCodingRate4(5);
        LoRa.setSyncWord(0x34);
        LoRa.setTxPower(12);
    } else {
        PORT_LOGE(TAG, "falha ao inicializar LoRa.h");
        pinMode(LED_BUILTIN, OUTPUT);
        digitalWrite(LED_BUILTIN, HIGH);
    }

    SPI.begin();
};

void loop() {
    if (!Serial) return;
    
    auto reading = g_Sensors.measure(g_ADC);

    PORT_LOGI(TAG, "Temperatura: %f deg C", reading.temperature);
    PORT_LOGI(TAG, "TDS: %f ppm", reading.tds);
    PORT_LOGI(TAG, "pH: %f", reading.ph);

    if (g_HasLora) {
        PORT_LOGI(TAG, "Transmitindo utilizando LoRa...");

        auto packet = repr::CompressedReading::compress(reading);

        LoRa.beginPacket();
        LoRa.write(packet.temperature);
        LoRa.write(packet.tds);
        LoRa.write(packet.ph);
        LoRa.endPacket();

        PORT_LOGI(TAG, "Transmissão finalizada");
    }

    delay(10000);
}