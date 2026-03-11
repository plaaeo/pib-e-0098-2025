#include <assert.h>
#include "port/types.hh"
#include "lora/util.hh"

namespace lora {
    /**
     * @brief Função utilitária usada para converter flags de IRQ do radiotransmissor
     * em um bitset com campos radio-agnósticos.
     * @param radio O radiotransmissor cujas flags devem ser lidas.
     * @todo Verificar se o compilador otimiza esta função.
     */
    IRQFields get_irq_flags(PhysicalLayer *radio) {
        auto flags = radio->getIrqFlags();

        return (IRQFields) {
            .tx_done            = 0 != (flags & radio->getIrqMapped(1 << RADIOLIB_IRQ_TX_DONE)),
            .rx_done            = 0 != (flags & radio->getIrqMapped(1 << RADIOLIB_IRQ_RX_DONE)),
            .preamble_detected  = 0 != (flags & radio->getIrqMapped(1 << RADIOLIB_IRQ_PREAMBLE_DETECTED)),
            .sync_word_valid    = 0 != (flags & radio->getIrqMapped(1 << RADIOLIB_IRQ_SYNC_WORD_VALID)),
            .header_valid       = 0 != (flags & radio->getIrqMapped(1 << RADIOLIB_IRQ_HEADER_VALID)),
            .header_err         = 0 != (flags & radio->getIrqMapped(1 << RADIOLIB_IRQ_HEADER_ERR)),
            .crc_err            = 0 != (flags & radio->getIrqMapped(1 << RADIOLIB_IRQ_CRC_ERR)),
            .cad_done           = 0 != (flags & radio->getIrqMapped(1 << RADIOLIB_IRQ_CAD_DONE)),
            .cad_detected       = 0 != (flags & radio->getIrqMapped(1 << RADIOLIB_IRQ_CAD_DETECTED)),
            .timeout            = 0 != (flags & radio->getIrqMapped(1 << RADIOLIB_IRQ_TIMEOUT)),
        };
    };

    /**
     * @brief Configura os parâmetros LoRa do radiotransmissor.
     */
    void set_phy_parameters(PhysicalLayer *phys, const Parameters &params) {
        int16_t status;
        
        status = phys->setDataRate({ .lora = params.dr }, RADIOLIB_MODEM_LORA);
        if (status != RADIOLIB_ERR_NONE)
            PORT_LOGE("lora", "failed to set datarate (%hi)", status);

        status = phys->setFrequency(params.freq_mhz);
        if (status != RADIOLIB_ERR_NONE) {
            PORT_LOGE("lora", "failed to set radio frequency (%hi)", status);
            abort();
        }

        status = phys->setOutputPower(params.power_db);
        if (status != RADIOLIB_ERR_NONE)
            PORT_LOGE("lora", "failed to set output power (%hi)", status);
            
        status = phys->setPreambleLength(params.preambleLength);
        if (status != RADIOLIB_ERR_NONE)
            PORT_LOGE("lora", "failed to set preamble length (%hi)", status);

        status = phys->setSyncWord((uint8_t *) &params.syncWord, 1);
        if (status != RADIOLIB_ERR_NONE)
            PORT_LOGE("lora", "failed to set syncword (%hi)", status);
    }


    /**
     * @brief Inicia uma recepção em um radiotransmissor sem bloquear a execução da task.
     * @param radio O radiotransmissor a controlar.
     * @param rx Determina como configurar o radiotransmissor para recepção.
     */
    void recv_nonblocking(PhysicalLayer *radio, ReceiveConfig_t rx) {
        RadioModeConfig_t mode { .receive = rx };

        assert(radio->stageMode(RADIOLIB_RADIO_MODE_RX, &mode) == RADIOLIB_ERR_NONE);
        assert(radio->launchMode() == RADIOLIB_ERR_NONE);
    }

    /**
     * @brief Inicia uma transmissão em um radiotransmissor sem bloquear a execução da task.
     * @param radio O radiotransmissor a controlar.
     * @param tx Determina como configurar o radiotransmissor para transmissão.
     */
    void send_nonblocking(PhysicalLayer *radio, TransmitConfig_t tx) {
        RadioModeConfig_t mode { .transmit = tx };

        assert(radio->stageMode(RADIOLIB_RADIO_MODE_TX, &mode) == RADIOLIB_ERR_NONE);
        assert(radio->launchMode() == RADIOLIB_ERR_NONE);
    }
}