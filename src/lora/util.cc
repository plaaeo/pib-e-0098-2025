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
            .tx_done            = 1 == (flags & radio->getIrqMapped(RADIOLIB_IRQ_TX_DONE)),
            .rx_done            = 1 == (flags & radio->getIrqMapped(RADIOLIB_IRQ_RX_DONE)),
            .preamble_detected  = 1 == (flags & radio->getIrqMapped(RADIOLIB_IRQ_PREAMBLE_DETECTED)),
            .sync_word_valid    = 1 == (flags & radio->getIrqMapped(RADIOLIB_IRQ_SYNC_WORD_VALID)),
            .header_valid       = 1 == (flags & radio->getIrqMapped(RADIOLIB_IRQ_HEADER_VALID)),
            .header_err         = 1 == (flags & radio->getIrqMapped(RADIOLIB_IRQ_HEADER_ERR)),
            .crc_err            = 1 == (flags & radio->getIrqMapped(RADIOLIB_IRQ_CRC_ERR)),
            .cad_done           = 1 == (flags & radio->getIrqMapped(RADIOLIB_IRQ_CAD_DONE)),
            .cad_detected       = 1 == (flags & radio->getIrqMapped(RADIOLIB_IRQ_CAD_DETECTED)),
            .timeout            = 1 == (flags & radio->getIrqMapped(RADIOLIB_IRQ_TIMEOUT)),
        };
    };
}