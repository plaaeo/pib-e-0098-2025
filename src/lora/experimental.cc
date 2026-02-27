#include <freertos/FreeRTOS.h>

#include "lora/util.hh"
#include "lora/experimental.hh"

namespace lora {
    constexpr static auto TAG = "proto.experimental";
    static ExperimentalProtocol *g_Instance = nullptr;

    ExperimentalProtocol::ExperimentalProtocol(PhysicalLayer *radio) : Protocol(radio), task::Task(TAG, 2)
    {
        
    };

    /**
     * @brief Cria uma instância do protocolo experimental.
     * @param radio O radiotransmissor a ser utilizado.
     * @warning Apenas uma instância da classe `ExperimentalProtocol` pode existir
     * a qualquer momento da execução.
     * @returns A instância criada, ou `nullptr` caso já exista outra instância.
     */
    ExperimentalProtocol *ExperimentalProtocol::create(PhysicalLayer *radio) {
        if (g_Instance) return nullptr;

        g_Instance = new ExperimentalProtocol(radio);
        return g_Instance;
    };

    /**
     * @brief Agenda a transmissão de uma leitura de sensor quando possível.
     * @param reading A leitura realizada pelo nó sensor.
     * @returns `true` se foi possível agendar a transmissão.
     */
    bool ExperimentalProtocol::schedule(const sens::Reading& reading) {

    };

    /**
     * @brief Configura o radio com parâmetros iniciais conhecidos.
     */
    void ExperimentalProtocol::configure_radio_init() {
        uint8_t syncWord = 0xAE;
        m_Radio->setDataRate({
            .lora = {
                .spreadingFactor = 7,
                .bandwidth = 125.0f,
                .codingRate = 5,
            }
        }, RADIOLIB_MODEM_LORA);
        m_Radio->setFrequency(915.0f);
        m_Radio->setPreambleLength(8);
        m_Radio->setSyncWord(&syncWord, 1);
    }

    /**
     * @brief Função principal da task do protocolo experimental.
     */
    void ExperimentalProtocol::main() {        
        configure_radio_init();

        // Iniciar recepção contínua (sem timeout)
        m_Radio->startReceive();

        for( ;; ) {
            auto flags = get_irq_flags(m_Radio);

            auto notification = task::await_notification();
        }
    }

    /**
     * @brief ISR que notifica a task do protocolo experimental.
     */
    void IRAM_ATTR ExperimentalProtocol::isr_notify_task() {
        if (g_Instance == NULL)
            return;

        // Interromper a task atual se uma de prioridade maior for acordada.
        if (g_Instance->notify_from_isr(NOTIFICATION_IRQ))
            portYIELD_FROM_ISR();
    }
}