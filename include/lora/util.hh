#pragma once 
#include <cstdint>
#include <RadioLib.h>

namespace lora {
    /**
     * @brief Bitset utilitário usado para codificar `RadioLibIrqFlag_t` de forma mais
     * conveniente ao programador.
     */
    struct IRQFields {
        uint32_t tx_done : 1;
        uint32_t rx_done : 1;
        uint32_t preamble_detected : 1;
        uint32_t sync_word_valid : 1;
        uint32_t header_valid : 1;
        uint32_t header_err : 1;
        uint32_t crc_err : 1;
        uint32_t cad_done : 1;
        uint32_t cad_detected : 1;
        uint32_t timeout : 1;
    };

    /**
     * @brief Uma estrutura com todos os parâmetros configuráveis genericamente 
     * da PHY LoRa.
     */
    struct Parameters {
        float freq_mhz;
        uint8_t power_db;
        LoRaRate_t dr;
        uint16_t preambleLength;
        uint8_t syncWord;
    };

    /**
     * @brief Função utilitária usada para converter flags de IRQ do radiotransmissor
     * em um bitset com campos radio-agnósticos.
     * @param radio O radiotransmissor cujas flags devem ser lidas.
     * @todo Verificar se o compilador otimiza esta função.
     */
    IRQFields get_irq_flags(PhysicalLayer *radio);

    /**
     * @brief Configura os parâmetros LoRa do radiotransmissor.
     */
    void set_phy_parameters(PhysicalLayer *phys, const Parameters &params);

    /**
     * @brief Inicia uma recepção em um radiotransmissor sem bloquear a execução da task.
     * @param radio O radiotransmissor a controlar.
     * @param rx Determina como configurar o radiotransmissor para recepção.
     */
    void recv_nonblocking(PhysicalLayer *radio, ReceiveConfig_t rx);

    /**
     * @brief Inicia uma transmissão em um radiotransmissor sem bloquear a execução da task.
     * @param radio O radiotransmissor a controlar.
     * @param tx Determina como configurar o radiotransmissor para transmissão.
     */
    void send_nonblocking(PhysicalLayer *radio, TransmitConfig_t tx);

    constexpr static uint32_t NOTIFICATION_IRQ = 1 << 0;
    constexpr static uint32_t NOTIFICATION_KILL = 1 << 1;
    constexpr static uint32_t NOTIFICATION_TIMER = 1 << 2;

    /**
     * @brief Uma estrutura utilitária para detectar motivos de notificação.
     */
    struct Notification {
        uint32_t irq : 1;
        uint32_t timer : 1;
    };
}