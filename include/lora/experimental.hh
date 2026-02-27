#pragma once 
#include "lora/proto.hh"
#include "task.hh"

namespace lora {
    class ExperimentalProtocol : public Protocol, private task::Task {
    protected:
        ExperimentalProtocol(PhysicalLayer *radio);

    public:
        /**
         * @brief Cria uma instância do protocolo experimental.
         * @param radio O radiotransmissor a ser utilizado.
         * @warning Apenas uma instância da classe `ExperimentalProtocol` pode existir
         * a qualquer momento da execução.
         * @returns A instância criada, ou `nullptr` caso já exista outra instância.
         */
        ExperimentalProtocol *create(PhysicalLayer *radio);

        /**
         * @brief Agenda a transmissão de uma leitura de sensor quando possível.
         * @param reading A leitura realizada pelo nó sensor.
         * @returns `true` se foi possível agendar a transmissão.
         */
        bool schedule(const sens::Reading& reading) override;

    private:
        /**
         * @brief Configura o radio com parâmetros iniciais conhecidos.
         */
        void configure_radio_init();

        /**
         * @brief Função principal da task do protocolo experimental.
         */
        void main() override;

        /**
         * @brief ISR que notifica a task do protocolo experimental.
         */
        static void IRAM_ATTR isr_notify_task();
    };
}