#include "lora.hh"

namespace lora {
    TaskHandle_t g_Task = nullptr;
    PhysicalLayer *g_Radio = nullptr;
    



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
     * @brief Função utilitária usada para converter flags de IRQ do radiotransmissor
     * em um bitset com campos radio-agnósticos.
     * @todo Verificar se o compilador otimiza esta função.
     */
    IRQFields get_irq_flags() {
        auto flags = g_Radio->getIrqFlags();

        return (IRQFields) {
            .tx_done            = 1 == (flags & g_Radio->getIrqMapped(RADIOLIB_IRQ_TX_DONE)),
            .rx_done            = 1 == (flags & g_Radio->getIrqMapped(RADIOLIB_IRQ_RX_DONE)),
            .preamble_detected  = 1 == (flags & g_Radio->getIrqMapped(RADIOLIB_IRQ_PREAMBLE_DETECTED)),
            .sync_word_valid    = 1 == (flags & g_Radio->getIrqMapped(RADIOLIB_IRQ_SYNC_WORD_VALID)),
            .header_valid       = 1 == (flags & g_Radio->getIrqMapped(RADIOLIB_IRQ_HEADER_VALID)),
            .header_err         = 1 == (flags & g_Radio->getIrqMapped(RADIOLIB_IRQ_HEADER_ERR)),
            .crc_err            = 1 == (flags & g_Radio->getIrqMapped(RADIOLIB_IRQ_CRC_ERR)),
            .cad_done           = 1 == (flags & g_Radio->getIrqMapped(RADIOLIB_IRQ_CAD_DONE)),
            .cad_detected       = 1 == (flags & g_Radio->getIrqMapped(RADIOLIB_IRQ_CAD_DETECTED)),
            .timeout            = 1 == (flags & g_Radio->getIrqMapped(RADIOLIB_IRQ_TIMEOUT)),
        };
    }
}