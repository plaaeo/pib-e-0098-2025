#include <Arduino.h>
#include <Adafruit_ADS1X15.h>
#include <cstdint>
#include <esp_task_wdt.h>
#include <freertos/task.h>

#include "gateway.hh"
#include "sensors.hh"

enum {
    //< Quantidade de leituras a cada transmissão LoRa.
    READINGS_PER_MESSAGE = 10,
};

/**
 * Garantir que não há padding.
 * O correto seria garantir que o tamanho das structs é igual entre
 * o receptor/transmissor, mas precisaria de comunicação entre ambos.
 */
static_assert(sizeof(sens::reading_t) == sizeof(float) * 3);

/**
 * Garantir que a ordem de bytes é LE.
 * O correto seria garantir que o ambos receptor/transmissor respeitam
 * a mesma ordem de bytes, mas precisaria de comunicação entre ambos.
 */
static_assert(BYTE_ORDER == LITTLE_ENDIAN);

/**
 * @brief Task do nó sensor.
 */
void task_sensor() {
    constexpr auto TAG = "sens";

    enum {
        //< Quantidade de segundos para dormir entre leituras.
        SLEEP_SECONDS = 5,
    };

    // Configurar ESP para acordar do sono leve em `SLEEP_SECONDS` segundos.
    ESP_ERROR_CHECK(esp_sleep_enable_timer_wakeup(SLEEP_SECONDS * 1000000UL));

    if (!Wire.begin(ADS1115_SDA, ADS1115_SCL)) {
        ESP_LOGE(TAG, "Falha ao inicializar o Wire");
        abort();
    }
    
    // Inicializar conexão I2C com o ADS1115.
    sens::reader g_sens = sens::reader(Wire);
    
    // Fazer o setup do gateway
    gw::setup();

    // Iniciar coleta
    while (true) {
        // Atualizar última leitura e inserí-la no buffer.
        auto latest = g_sens.ler();
        
        ESP_LOGI(TAG, "%f C; %f ppm; %f pH;", latest.temperature, latest.tds, latest.ph);

        // Enviar última leitura diretamente para o Firebase
        gw::send_reading(latest);
        
        // Iniciar sono leve
        ESP_ERROR_CHECK(esp_light_sleep_start());
    }

    ESP_LOGE(TAG, "Execução antingiu código inalcançável.");
    abort();
}

extern "C" void app_main(void) {
    initArduino();
    esp_task_wdt_init(30, true);
    task_sensor();
};