#include <Arduino.h>
#include <RadioLib.h>

#include "sensors.hh"

namespace lora {
    enum Protocol {
        EXPERIMENTAL
    };

    /**
     * @brief Inicializa o estado global do serviço LoRa.
     * @param radio O radiotransmissor que será utilizado. Apenas um radiotransmissor pode
     * ser utilizado por vez. O radiotransmissor deve ser inicializado antes da chamada desta
     * função utilizando a respectiva função `begin(...)` do módulo.
     * @param mode O protocolo que será utilizado na rede.
     */
    void initialize(PhysicalLayer *radio, Protocol mode);


    void schedule(const sens::Reading& reading);

    SPIClass g_spi;
    SX1276 g_radio = new Module(LORA_CS, LORA_DIO0, LORA_RST, GPIO_NUM_NC, g_spi);

    /**
     * @brief Inicializa o radio LoRa.
     */
    void init_radio() {
        constexpr auto POWER_DBM        = 16;
        constexpr auto SPREADING_FACTOR = 12;
        constexpr auto BANDWIDTH_KHZ    = 125.f;
        constexpr auto CODING_RATE      = 5;
        constexpr auto SYNC_WORD        = 0x34;
        constexpr auto PREAMBLE_LENGTH  = 8;

        // Inicializa o SPI
        g_spi.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_CS);

        // Inicializa a RadioLib
        int err = g_radio.begin(915.f, BANDWIDTH_KHZ, SPREADING_FACTOR, CODING_RATE, SYNC_WORD, POWER_DBM, PREAMBLE_LENGTH);
        if (err != RADIOLIB_ERR_NONE) {
            ESP_LOGE("main", "Erro an inicialização da RadioLib (%d)", err);
            abort();
        }
    }
}