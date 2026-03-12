#include <Arduino.h>
#include <stdint.h>
#include <RadioLib.h>
#include <esp_task_wdt.h>
#include <freertos/task.h>
#include <esp_wifi.h>
#include <esp_pm.h>
#include <esp_log.h>

#include "sensor/interface.hh"
#include "sensor/temperature.hh"
#include "sensor/tds.hh"
#include "sensor/ph.hh"
#include "repr/reading.hh"

#include "gateway.hh"

constexpr auto TAG = "main";

ISR_SAFE_ATTR TaskHandle_t g_GatewayTask;

void ISR_SAFE_ATTR isr_notify_gateway() {
    BaseType_t pxHigherPriorityTaskWoken;

    xTaskNotifyFromISR(
        g_GatewayTask,
        0,
        eNoAction,
        &pxHigherPriorityTaskWoken
    );

    portYIELD_FROM_ISR(pxHigherPriorityTaskWoken);
}

Module g_Module(LORA_CS, LORA_DIO0, LORA_RST, LORA_BUSY, SPI);
LORA_RADIO g_Phys = LORA_RADIO(&g_Module);

/**
 * @brief Task do gateway.
 */
void task_gateway() {
    g_GatewayTask = xTaskGetCurrentTaskHandle();

    // Inicializar LoRa
    SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_CS);
    assert(g_Phys.begin(915.0f, 125.0f, 11, 5, 0x23, 10, 8) == RADIOLIB_ERR_NONE);

    g_Phys.setPacketReceivedAction(isr_notify_gateway);

    // Inicializar gateway
    gw::setup();

    // Iniciar recepção contínua
    assert(g_Phys.startReceive(UINT32_MAX) == RADIOLIB_ERR_NONE);

    for (;;) {
        // Aguardar IRQ
        assert(xTaskNotifyWait(
            0,
            ULONG_MAX,
            nullptr,
            pdFALSE
        ) == pdPASS);

        if (g_Phys.getPacketLength() != 3) {
            PORT_LOGI(TAG, "received unrecognized packet (len=%u)", g_Phys.getPacketLength());
            continue;
        }

        // Ler o pacote comprimido
        uint8_t buffer[3];
        assert(g_Phys.readData(buffer, 3) == RADIOLIB_ERR_NONE);

        repr::CompressedReading packet = {
            .temperature = buffer[0],
            .tds = buffer[1],
            .ph = buffer[2],
        };

        // Descomprimir e enviar o pacote
        auto reading = packet.decompress();
        PORT_LOGI(TAG, "received sensor packet (temp=%f degC; tds=%f ppm; ph=%f)", reading.temperature, reading.tds, reading.ph);

        gw::send_reading_firestore(reading);
    }
}

//< Função `main` do protótipo
extern "C" void app_main(void) {
    initArduino();

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

    esp_task_wdt_init(60, true);
    task_gateway();
};