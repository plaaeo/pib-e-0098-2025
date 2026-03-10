#include <Arduino.h>
#include <Adafruit_ADS1X15.h>
#include <cstdint>
#include <esp_task_wdt.h>
#include <freertos/task.h>
#include <esp_wifi.h>
#include <esp_pm.h>
#include <esp_log.h>

#include "lora/experimental.hh"

#include "sensor/interface.hh"
#include "sensor/temperature.hh"
#include "sensor/tds.hh"
#include "sensor/ph.hh"

constexpr auto TAG = "sens";

RTC_DATA_ATTR static struct {
    enum : uint32_t {
        //< Canal do ADS conectado ao sensor de temperatura (A0).
        ADS_CHANNEL_TEMPERATURE = 0,

        //< Canal do ADS conectado ao sensor de pH (A1).
        ADS_CHANNEL_PH = 1,

        //< Canal do ADS conectado ao sensor de TDS (A2).
        ADS_CHANNEL_TDS = 2,
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
            iface.measure_volts(ADS_CHANNEL_TEMPERATURE)
        );

        // Medir e calcular TDS em ppm
        auto curTds = tds.convert(
            iface.measure_volts(ADS_CHANNEL_TDS),
            curTemperature
        );

        // Medir e calcular pH
        auto curPh = ph.convert(
            iface.measure_volts(ADS_CHANNEL_PH)
        );

        return (sensor::Reading) {
            .time = 123456,
            .temperature = curTemperature,
            .tds = curTds,
            .ph = curPh,
        };
    }
} g_Sensors {};

//< Interface analógica para o ADS1115.
sensor::ADS1X15Interface g_ADC;

//< Interface para envio de leituras de sensor.
lora::Protocol *g_Proto = nullptr;

RTC_DATA_ATTR lora::ExperimentalState g_State = { };

/**
 * @brief Task do nó sensor.
 */
void task_sensor() {
    static Module s_Module(LORA_CS, LORA_DIO0, LORA_RST, LORA_BUSY, SPI);
    static LORA_RADIO s_Phys = LORA_RADIO(&s_Module);

    // Tentar inicializar ADC externo
    if (!g_ADC.begin(ADS1115_SDA, ADS1115_SCL, Wire)) {
        ESP_LOGW(TAG, "falha ao inicializar o ADS1X15, leituras não serão realizadas");
    }

    SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_CS);
    s_Phys.begin(915.0f);
    s_Phys.forceLDRO(false);

    g_State.id = s_Phys.randomByte();
    
    g_Proto = lora::ExperimentalProtocol::create(&s_Phys, g_State);
}

//< Função `main` do protótipo
extern "C" void app_main(void) {
    initArduino();

    if constexpr (STATUS_LED != GPIO_NUM_NC) {
        pinMode(STATUS_LED, OUTPUT);
    }

    // Definir nível de logs para DEBUG após inicialização
    esp_log_level_set("*", ESP_LOG_DEBUG);

    ESP_ERROR_CHECK(esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_ON));

    // Configurar ESP para acordar do sono em casos de IRQ do radiotransmissor.
    ESP_ERROR_CHECK(esp_sleep_enable_ext0_wakeup(LORA_DIO0, 1));

#if CONFIG_IDF_TARGET_ESP32
    esp_pm_config_esp32_t pm_config {
        .max_freq_mhz = CONFIG_ESP32_DEFAULT_CPU_FREQ_MHZ,
        .min_freq_mhz = CONFIG_ESP32_DEFAULT_CPU_FREQ_MHZ,
#if CONFIG_FREERTOS_USE_TICKLESS_IDLE
        .light_sleep_enable = true
#endif
    };
#elif CONFIG_IDF_TARGET_ESP32S3
    esp_pm_config_esp32s3_t pm_config {
        .max_freq_mhz = CONFIG_ESP32S3_DEFAULT_CPU_FREQ_MHZ,
        .min_freq_mhz = CONFIG_ESP32S3_DEFAULT_CPU_FREQ_MHZ,
#if CONFIG_FREERTOS_USE_TICKLESS_IDLE
        .light_sleep_enable = true
#endif
    };    
#endif
    ESP_ERROR_CHECK(esp_pm_configure(&pm_config));

    rtc_clk_32k_enable(true);
    rtc_clk_slow_freq_set(RTC_SLOW_FREQ_32K_XTAL);

    esp_task_wdt_init(30, true);
    task_sensor();
};