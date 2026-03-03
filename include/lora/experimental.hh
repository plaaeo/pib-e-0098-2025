#pragma once

#include <RadioLib.h>

#include "lora/proto.hh"
#include "lora/util.hh"
#include "task.hh"

namespace lora {
    class ExperimentalProtocol : public Protocol, private task::Task {
    protected:
        ExperimentalProtocol(PhysicalLayer *phys) : Protocol(phys), task::Task("experimental-protocol", 2)
        { };

    public:
        /**
         * @brief Cria uma instância do protocolo experimental.
         * @param phys O radiotransmissor a ser utilizado.
         * @warning Apenas uma instância da classe `ExperimentalProtocol` pode existir
         * a qualquer momento da execução.
         * @returns A instância criada, ou `nullptr` caso já exista outra instância.
         */
        static ExperimentalProtocol *create(PhysicalLayer *phys);

        /**
         * @brief Agenda a transmissão de uma leitura de sensor quando possível.
         * @param reading A leitura realizada pelo nó sensor.
         * @returns `true` se foi possível agendar a transmissão.
         */
        bool schedule(const sensor::Reading& reading) override;

    private:
        /**
         * @brief Configura os parâmetros do radiotransmissor.
         */
        void set_phy_parameters(Parameters params);

        /**
         * @brief Função principal da task do protocolo experimental.
         */
        void main() override;

        /**
         * @brief Abre uma janela de recepção contínua, recebendo quantos pacotes for possível
         * até um determinado tempo.
         * @param window_ms Tempo em milisegundos para receber pacotes.
         */
        void open_rx_continuous(uint32_t window_ms);

        /**
         * @brief Aguarda por uma notificação e lida com notificações simples.
         * @param mask Uma máscara usada para filtrar quais notificações devem ser esperadas.
         * @returns Os campos definidos no bitset da notificação.
         */
        Notification await(uint32_t mask = UINT32_MAX);

        /**
         * @brief ISR que notifica a task do protocolo experimental.
         */
        static void IRAM_ATTR isr_notify_task();
    
    private:
        esp_timer_handle_t m_EspTimer;

    };
}