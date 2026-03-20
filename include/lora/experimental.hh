#pragma once

#include <RadioLib.h>

#include "lora/trickle.hh"
#include "lora/nettime.hh"
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
    };

    struct ExperimentalState {
        ExperimentalFSM state;

        /**
         * @brief O estado do último trickle timer executado.
         */
        lora::TrickleTimerState trickle;

        /**
         * @brief Os parâmetros LoRa atuais.
         */
        lora::Parameters params;

        /**
         * @brief Um identificador único deste nó sensor.
         */
        uint8_t id;

        /**
         * @brief O rank do nó RPL.
         */
        Rank rank;

        /**
         * @brief O número de hops necessários para transmitir um dado do nó mais distante
         * até a raíz da rede. Será 0 caso não seja conhecido.
         */
        uint8_t max_hops;

        /**
         * @brief Usado para sincronizar o tempo entre os nós sensores.
         */
        NetworkTimer net_time;
    };

    class ExperimentalProtocol : public Protocol, private port::Task {
    protected:
        ExperimentalProtocol(PhysicalLayer *phys, ExperimentalState &state);

    public:
        /**
         * @brief Cria uma instância do protocolo experimental.
         * @param phys O radiotransmissor a ser utilizado.
         * @warning Apenas uma instância da classe `ExperimentalProtocol` pode existir
         * a qualquer momento da execução.
         * @returns A instância criada, ou `nullptr` caso já exista outra instância.
         */
        static ExperimentalProtocol *create(PhysicalLayer *phys, ExperimentalState &state);

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
        bool process_broadcast(const Broadcast &packet);

        /**
         * @brief Inicia, executa e finaliza o estado de inicialização do protocolo.
         */
        void do_initialization_stage();

        /**
         * @brief Avança o estado atual da FSM do protocolo de acordo com um evento externo.
         */
        void handle_notification(uint32_t notification);

        /**
         * @brief Função principal da task do protocolo experimental.
         */
        void main() override;

        /**
         * @brief Abre uma janela de recepção contínua, recebendo quantos pacotes for possível
         * até um determinado tempo.
         * @param window_ms Tempo em milisegundos para receber pacotes.
         * @note Como nenhum radiotransmissor LoRa consegue implementar janelas de transmissão contínuas
         * por períodos arbitrários de tempo, a lógica de timeout foi implementada utilizando um timer
         * que acorda o microcontrolador para cancelar a recepção no momento correto.
         */
        void open_rx_continuous(uint32_t window_ms);

        /**
         * @brief Espera até poder transmitir novamente.
         */
        void wait_until_tx();

        /**
         * @brief Aguarda por uma notificação e lida com notificações simples.
         * @param mask Uma máscara usada para filtrar quais notificações devem ser esperadas.
         * @returns Os campos definidos no bitset da notificação.
         */
        Notification await(uint32_t mask = UINT32_MAX);

        /**
         * @brief ISR que notifica a task do protocolo experimental.
         */
        static void ISR_SAFE_ATTR isr_notify_task();
    private:
        ExperimentalState  &m_State;

        //< Salva o resultado de `port::get_monotonic_time()` no momento do último IRQ do radio.
        port::time_us       m_MonoTimeAtISR_us;

        port::NotifyTimer   m_TimeoutTimer;

        lora::TrickleTimer  m_Trickle;
    };    
}