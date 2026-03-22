#pragma once

#include <RadioLib.h>

#include "net/trickle.hh"
#include "net/clock.hh"
#include "net/types.hh"
#include "net/packets.hh"

#include "lora/proto.hh"
#include "lora/util.hh"

#include "port/task.hh"
#include "port/time.hh"

namespace lora {
    enum class ExperimentalFSM {
        /**
         * @brief Valor padrão após a construção inicial da máquina.
         */
        INITIALIZED,

        /**
         * @brief O nó está aguardando por broadcasts de vizinhos.
         */
        SCANNING_CANDIDATE_NEIGHBORS,

        /**
         * @brief O nó está realizando um broadcast.
         */
        BROADCASTING,

        /**
         * @todo Implementar os próximos estados reais da rede.
         */
        EXECUTING,
    };

    using State = net::State<ExperimentalFSM>;

    class ExperimentalProtocol : public Protocol, private port::Task {
    protected:
        ExperimentalProtocol(PhysicalLayer *phys, State &state);

    public:
        /**
         * @brief Cria uma instância do protocolo experimental.
         * @param phys O radiotransmissor a ser utilizado.
         * @warning Apenas uma instância da classe `ExperimentalProtocol` pode existir
         * a qualquer momento da execução.
         * @returns A instância criada, ou `nullptr` caso já exista outra instância.
         */
        static ExperimentalProtocol *create(PhysicalLayer *phys, State &state);

        /**
         * @brief Agenda a transmissão de uma leitura de sensor quando possível.
         * @param reading A leitura realizada pelo nó sensor.
         * @returns `true` se foi possível agendar a transmissão.
         */
        bool schedule(const sensor::Reading& reading) override;

    private:

        /**
         * @brief Atualizando o estado atual do protocolo de acordo com um broadcast recebido.
         * @param packet O pacote de broadcast recebido.
         * @returns `true` caso o broadcast tenha gerado uma inconsistência, ou seja, tenha
         * modificado o estado da rede na perspectiva deste nó.
         */
        bool process_broadcast(const net::Broadcast &packet);

        /**
         * @brief Chamado ao receber `NOTIFICATION_IRQ` durante a inicialização.
         */
        void handle_broadcast_irq_notification();

        /**
         * @brief Avança o estado atual da FSM do protocolo de acordo com um evento externo.
         * @returns O próximo estado da FSM.
         */
        ExperimentalFSM handle_notification(uint32_t &notification);

        /**
         * @brief Função principal da task do protocolo experimental.
         */
        void main() override;

        /**
         * @brief Espera até inicializar os slots de transmissão deste nó.
         */
        void wait_for_slot();

        /**
         * @brief ISR que notifica a task do protocolo experimental.
         */
        static void ISR_SAFE_ATTR isr_notify_task();
    private:
        State              &m_State;

        lora::Parameters    m_Params;

        //< Salva o resultado de `port::get_monotonic_time()` no momento do último IRQ do radio.
        port::time_us       m_MonoTimeAtISR_us;

        port::NotifyTimer   m_TimeoutTimer;

        net::TrickleTimer   m_Trickle;
    };    
}