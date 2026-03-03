#include <Arduino.h>
#include <Adafruit_ADS1X15.h>
#include <cstdint>
#include <esp_task_wdt.h>
#include <freertos/task.h>
#include <esp_wifi.h>
#include <esp_pm.h>

#include "sensors.hh"
#include "lora/experimental.hh"

constexpr auto TAG = "sens";

Module g_Module(LORA_CS, LORA_DIO0, LORA_RST, LORA_BUSY, SPI);
auto g_Phys = LORA_RADIO(&g_Module);
lora::Protocol *g_Proto = nullptr;

/**
 * @brief Task do nó sensor.
 */
void task_sensor() {
    if (false) {
        if (!Wire.begin(ADS1115_SDA, ADS1115_SCL)) {
            ESP_LOGE(TAG, "Falha ao inicializar o Wire");
            abort();
        }
        
        // Inicializar conexão I2C com o ADS1115.
        sens::reader g_sens = sens::reader(Wire);
    }

    SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_CS);
    g_Phys.begin(915.0f);
    ESP_LOGI(TAG, "creating protocol");
    g_Proto = lora::ExperimentalProtocol::create(&g_Phys);
}

//< Função `main` do protótipo
extern "C" void app_main(void) {
    initArduino();

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

    esp_task_wdt_init(30, true);
    task_sensor();
};