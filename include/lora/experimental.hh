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

        /**
         * @todo Implementar os próximos estados reais da rede.
         */
        EXECUTING,
    };

    /**
     * @brief Representa as informações de um vizinho potencialmente pai
     * do nó. Atualizado apenas durante a fase de inicialização.
     */
    struct ParentInfo {
        float last_rssi;
        float last_snr;
        uint8_t id;
        Rank rank;

        inline uint32_t score() const {
            /**
             * Shifta o inverso do 'cansaço' do nó por 15. Faz com que 1 bit do valor
             * 'cansaço' sobreponha o score do RSSI. Desta forma, um nó que esteja um
             * pouco indisposto a ser pai (tiredness = 0b01), porém possui um RSSI muito
             * melhor do que outros, pode ainda ser selecionado.
             */
            uint32_t s = static_cast<uint32_t>(rank.tiredness ^ 0b11) << 15;
            
            // Score do RSSI (-127dBm -> 0; 0dBm -> 255)
            int32_t rssiScore = 255 + static_cast<int32_t>(last_rssi * 2);
            if (rssiScore < 0) rssiScore = 0;

            // Score do SNR (linear)
            int32_t snrScore = static_cast<int32_t>(last_snr * 4);
            if (snrScore < 0) snrScore = 0;

            return s + static_cast<uint32_t>((rssiScore << 8) + snrScore);
        }
    };

    struct CandidateParents {
        /**
         * @brief Um vetor com informações de nós vizinhos candidatos a serem pais do nó atual.
         */
        port::static_vector<ParentInfo, 8> candidate_parents;

        /**
         * @brief Limpa o vetor de pais candidatos.
         */
        void clear();

        /**
         * @brief Atualiza o vetor de pais candidatos para incluir as informações do pai dado.
         */
        void add_or_update(ParentInfo &&info);

        /**
         * @brief Ordena a lista de pais candidatos usando a função de objetivo.
         * @returns O ID do pai preferido, ou `nullopt` se `candidate_parents` estiver vazio.
         */
        port::optional<uint8_t> sort_by_objective();
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
         * @brief Usado para sincronizar o tempo entre os nós sensores.
         */
        NetworkTimer net_time;

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
         * @brief `true` caso qualquer nó com `rank.hops` maior tenha sido ouvido durante
         * a inicialização.
         */
        bool has_children;

        /**
         * @brief Mantém e gerencia uma lista de possíveis pais.
         */
        CandidateParents candidate_parents;
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
         * @brief Abre uma janela de recepção contínua, recebendo quantos pacotes for possível
         * até um determinado tempo.
         * @param window_ms Tempo em milisegundos para receber pacotes.
         * @note Como nenhum radiotransmissor LoRa consegue implementar janelas de transmissão contínuas
         * por períodos arbitrários de tempo, a lógica de timeout foi implementada utilizando um timer
         * que acorda o microcontrolador para cancelar a recepção no momento correto.
         */
        void open_rx_continuous(uint32_t window_ms);

        /**
         * @brief Espera até inicializar os slots de transmissão deste nó.
         */
        void wait_for_slot();

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