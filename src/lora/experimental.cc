#ifdef ESP32
#   include <freertos/FreeRTOS.h>
#   include <freertos/task.h>
#else
#   include <Arduino_FreeRTOS.h>
#endif

#include "port/types.hh"
#include "lora/util.hh"
#include "lora/experimental.hh"

//< Constante de redundância do Trickle.
constexpr static auto TRICKLE_REDUNDANCY_CONSTANT   = 2;

//< Constante que, quando multiplicada com o ToA de um broadcast, dá o intervalo mínimo do Trickle.
constexpr static auto TRICKLE_MIN_INTERVAL_PACKETS  = 2;

//< Número máximo de vezes que um intervalo pode dobrar no Trickle.
constexpr static auto TRICKLE_MAX_DOUBLINGS         = 4;

constexpr static auto TDM_SUBSLOT_GUARD_SYMBOLS     = 2;
constexpr static auto TDM_SUBSLOT_MTU_BYTES         = 64;
constexpr static auto TDM_SUBSLOT_COUNT             = 1;
constexpr static auto TDM_SLOT_COUNT                = 4;

namespace lora {
    constexpr static uint32_t NOTIFICATION_IRQ = 1 << 0;
    constexpr static uint32_t NOTIFICATION_KILL = 1 << 1;
    constexpr static uint32_t NOTIFICATION_TIMER = 1 << 2;
    constexpr static uint32_t NOTIFICATION_TRICKLE = 1 << 3;

    constexpr static auto TAG = "proto.experimental";
    static ExperimentalProtocol *g_Instance = nullptr;

    ExperimentalProtocol::ExperimentalProtocol(PhysicalLayer *phys, State &state)
        : Protocol(phys)
        , port::Task("experimental-protocol", 2)
        , m_State(state)
        , m_Params({
            .freq_mhz = 915.0f,
            .power_db = 5,
            .dr = {
                .spreadingFactor = 12,
                .bandwidth = 125.f,
                .codingRate = 5,
            },
            .preamble_length = 12,
            .sync_word = 0x77
        })
        , m_MonoTimeAtISR_us(0)
        , m_TimeoutTimer(this, NOTIFICATION_TIMER)
        , m_Trickle(this, NOTIFICATION_TRICKLE, state.trickle)
    { };

    /**
     * @brief Cria uma instância do protocolo experimental.
     * @param phys O radiotransmissor a ser utilizado.
     * @warning Apenas uma instância da classe `ExperimentalProtocol` pode existir
     * a qualquer momento da execução.
     * @returns A instância criada, ou `nullptr` caso já exista outra instância.
     */
    ExperimentalProtocol *ExperimentalProtocol::create(PhysicalLayer *phys, State &state) {
        if (g_Instance) return nullptr;

        g_Instance = new ExperimentalProtocol(phys, state);
        return g_Instance;
    };

    bool ExperimentalProtocol::schedule(const sensor::Reading& reading) {
        return false;
    };

    bool ExperimentalProtocol::process_broadcast(const net::Broadcast &packet) {
        bool reportInconsistency = false;

        // Atualizar fonte de referência do tempo ao receber o primeiro broadcast.
        if (m_State.rank == net::INFINITE_RANK) {
            /**
             * Ao receber um pacote, um dos delays que contribuem à dessincronização do tempo entre os nós é
             * um pequeno delay de (o que assumo que seja) processamento do sinal por parte do radio. Como esse
             * delay aparenta crescer com o tempo de transmissão (ToA), é justo assumir que esteja relacionado ao
             * tempo de um símbolo LoRa. Como uma forma de minimizar a influência desse delay na sincronização, removemos
             * o tempo de 1 símbolo do tempo de recepção (IRQ) para aproximar o tempo em que a transmissão realmente finalizou.
             */
            auto symbolTime_us = m_Params.calculate_symbol_time();
            auto transmissionEndTime_us = g_Instance->m_MonoTimeAtISR_us - symbolTime_us;

            m_State.net_time.synchronize(packet.reference_time_us, transmissionEndTime_us);

            PORT_LOGI(TAG, "symbol time compensation was %llius", symbolTime_us);
            PORT_LOGI(TAG, "network time is %llius (reference time was %ius)", m_State.net_time.get_time_us(), packet.reference_time_us);
        }
    
        // Caso a distância do nó mais distante tenha mudado, há uma inconsistência.
        if (packet.max_hops != net::UNKNOWN_MAX_HOPS && packet.max_hops > m_State.max_hops) {
            PORT_LOGI(TAG, "upgraded max_hops from %hhu -> %hhu", m_State.max_hops, packet.max_hops);
            m_State.max_hops = packet.max_hops;
            reportInconsistency = true;
        }

        // Caso nosso rank tenha mudado, há uma inconsistência.
        if (m_State.rank == net::INFINITE_RANK || m_State.rank.hops > (packet.rank.hops + 1)) {
            auto old = m_State.rank;

            // Todos os pais serão invalidados, pois nosso rank agora é igual ou menor que o rank deles,
            // logo nos tornamos apenas nós vizinhos.
            m_State.candidate_parents.clear();

            m_State.rank.hops = packet.rank.hops + 1;
            reportInconsistency = true;

            PORT_LOGI(TAG, "rank was updated from %hhu -> %hhu", static_cast<uint8_t>(old), static_cast<uint8_t>(m_State.rank));
        }
        // Salvar caso tenhamos escutado um nó filho
        else if (packet.rank.hops > m_State.rank.hops) {
            PORT_LOGI(TAG, "detected child (id %hhu)", packet.id);
            m_State.has_children = true;
        }

        // Salvar caso o nó escutado seja um potencial pai
        if (m_State.rank.hops == packet.rank.hops + 1) {
            m_State.candidate_parents.add_or_update({
                .last_rssi = m_Phys->getRSSI(),
                .last_snr = m_Phys->getSNR(),
                .id = packet.id,
                .rank = packet.rank,
            });
        }

        return reportInconsistency;
    };

    void ExperimentalProtocol::wait_for_slot() {
        // A duração de um slot completo
        port::time_us slotDuration = m_Phys->getTimeOnAir(TDM_SUBSLOT_MTU_BYTES);
        slotDuration += TDM_SUBSLOT_COUNT * TDM_SUBSLOT_GUARD_SYMBOLS * m_Params.calculate_symbol_time();
        
        // A duração de uma contagem monotônica de slots até a contagem reiniciar
        port::time_us frameDuration = slotDuration * TDM_SLOT_COUNT;
        
        // Calcular o índice do nosso slot
        uint8_t mySlot = m_State.max_hops - m_State.rank.hops;
        
        // Calcular o tempo até o próximo slot nosso
        port::time_us timeUntilNextSlot = m_State.net_time.get_time_us();
        timeUntilNextSlot -= mySlot * slotDuration;
        timeUntilNextSlot %= frameDuration;
        timeUntilNextSlot = frameDuration - timeUntilNextSlot;
        m_TimeoutTimer.start_once(timeUntilNextSlot);
    }

    void ExperimentalProtocol::handle_broadcast_irq_notification() {
        auto flags = lora::get_irq_flags(m_Phys);

        // Caso seja `false`, o radio foi utilizado por outro código além deste.
        assert(flags.rx_done);

        // Limpar flags de IRQ
        assert(m_Phys->clearIrq(RADIOLIB_IRQ_RX_DEFAULT_FLAGS) == RADIOLIB_ERR_NONE);

        auto length = m_Phys->getPacketLength();

        // Verificar se a recepção teve sucesso
        if (
            flags.crc_err
            || flags.header_err
            || flags.timeout
            || length > net::Broadcast::BROADCAST_MAX_SIZE
        ) {
            PORT_LOGW(TAG, "received broadcast with errors");
            return;
        }

        // Obter métricas de qualidade do pacote
        float rssi = m_Phys->getRSSI();
        float snr = m_Phys->getSNR();

        // Ler broadcast do radio
        uint8_t buffer[net::Broadcast::BROADCAST_MAX_SIZE];
        assert(m_Phys->readData(buffer, sizeof(buffer)) == RADIOLIB_ERR_NONE);
        
        // Tentar decodificar o pacote do broadcast
        auto packet = net::Broadcast::decode(buffer, length);
        if (!packet) {
            PORT_LOGW(TAG, "broadcast was invalid");
            return;
        }

        PORT_LOGI(TAG, "broadcast from ID %hhu, rank %hhu (rrsi=%f, snr=%f)",
                    packet->id, static_cast<uint8_t>(packet->rank), rssi, snr);

        // Reportar inconsistência caso o estado mude
        if (process_broadcast(*packet)) m_Trickle.signal_inconsistency();
        else m_Trickle.signal_consistency();

        // Iniciar o trickle caso ele ainda não esteja iniciado.
        m_Trickle.try_begin(
            TRICKLE_REDUNDANCY_CONSTANT,
            TRICKLE_MIN_INTERVAL_PACKETS * m_Phys->getTimeOnAir(net::Broadcast::BROADCAST_MAX_SIZE),
            TRICKLE_MAX_DOUBLINGS
        );
    }

    ExperimentalFSM ExperimentalProtocol::handle_notification(uint32_t &notification) {
        switch (m_State.state) {
            case ExperimentalFSM::BROADCASTING:
                // A mensagem deve ter terminado de transmitir.
                if (notification & NOTIFICATION_IRQ) {
                    notification ^= NOTIFICATION_IRQ;
                    auto flags = lora::get_irq_flags(m_Phys);
                    /** @todo as vezes, tx_done = 0! investigar o porque */
                    assert(flags.tx_done);
                    assert(m_Phys->clearIrq(1U << RADIOLIB_IRQ_TX_DONE) == RADIOLIB_ERR_NONE);
                    PORT_LOGI(TAG, "TRANSMITTED - rebroadcast done");
                } else {
                    // A mensagem ainda não terminou de transmitir
                    return ExperimentalFSM::BROADCASTING;
                }

                [[fallthrough]];
            case ExperimentalFSM::INITIALIZED:
                PORT_LOGI(TAG, "RECEIVING - starting broadcast rx session");

                // Iniciar recepção sem timeout
                lora::recv_nonblocking(m_Phys, {
                    .timeout = UINT32_MAX,
                    .irqFlags = RADIOLIB_IRQ_RX_DEFAULT_FLAGS,
                    .irqMask = 1U << RADIOLIB_IRQ_RX_DONE,
                    .len = 0,
                });

                // Finalizar apenas caso não haja notificação
                if (notification == 0)
                    return ExperimentalFSM::SCANNING_CANDIDATE_NEIGHBORS;
                
                [[fallthrough]];
            case ExperimentalFSM::SCANNING_CANDIDATE_NEIGHBORS: {
                // Uma mensagem pode ter sido recebida.
                if (notification & NOTIFICATION_IRQ) {
                    notification ^= NOTIFICATION_IRQ;
                    handle_broadcast_irq_notification();
                }

                // Caso o Trickle Timer tenha finalizado, talvez seja possível transmitir nosso broadcast.
                if (notification & NOTIFICATION_TRICKLE) {
                    notification ^= NOTIFICATION_TRICKLE;
                    if (!m_Trickle.update_and_check())
                        return ExperimentalFSM::SCANNING_CANDIDATE_NEIGHBORS;

                    /**
                     * Não há necessidade de cancelar uma recepção para iniciar uma transmissão, pois
                     * nós vizinhos terão dificuldade de ouvir este broadcast, e adicionar uma transmissão
                     * apenas poluiria o canal.
                     */
                    if (lora::is_receiving(m_Phys)) {
                        PORT_LOGI(TAG, "suppressed broadcast due to channel occupation");
                        return ExperimentalFSM::SCANNING_CANDIDATE_NEIGHBORS;
                    }

                    /**
                     * Se o Trickle estiver executando um intervalo máximo, e nenhum nó filho foi detectado 
                     * até agora, anunciar `max_hops` como `m_State.rank.hops`, e definir `m_State.max_hops` igualmente.
                     */
                    if (
                        !m_State.has_children
                        && m_State.max_hops == net::UNKNOWN_MAX_HOPS
                        && m_State.trickle.interval_duration_doublings == m_State.trickle.max_interval_doublings
                    ) {
                        m_State.max_hops = m_State.rank.hops;
                        PORT_LOGI(TAG, "adopted initial guess for max_hops as %hhu", m_State.max_hops);
                        m_Trickle.signal_inconsistency();
                    }
                    
                    net::Broadcast broadcast = {
                        .reference_time_us = static_cast<int32_t>(m_State.net_time.get_time_us()),
                        .id = m_State.id,
                        .rank = m_State.rank,
                        .max_hops = m_State.max_hops,
                    };

                    /**
                     * Atualizar o tempo de referência no broadcast para refletir o tempo
                     * de processamento do pacote e o tempo esperado de transmissão.
                     * 
                     * Isso não garante sincronia com o próximo nó. Há um pequeno erro acumulado em cada
                     * retransmissão equivalente a:
                     * - O tempo de codificar o broadcast em um pacote;
                     * - O tempo de comunicar com o radiotransmissor o pacote a ser enviado;
                     * - O tempo de comunicar o radiotransmissor a iniciar a transmissão;
                     * - O tempo do radiotransmissor realmente iniciar a transmissão.
                     * Após a recepcão do próximo nó, ainda há um pequeno delay não deterministico de processamento
                     * necessário para o radiotransmissor conseguir demodular o pacote e corrigir erros.
                     */
                    size_t length = broadcast.length();
                    broadcast.reference_time_us += m_Phys->getTimeOnAir(length);
                    
                    // Codificar o buffer
                    uint8_t buffer[length];
                    assert(broadcast.encode(buffer, length));

                    // Realizar a retransmissão
                    lora::send_nonblocking(m_Phys, { .data = buffer, .len = length, .addr = 0 });
                    PORT_LOGI(TAG, "TRANSMITTING - unsuppressed broadcast (max_hops = %hhu)", m_State.max_hops);

                    // Após o trickle atingir o intervalo máximo, assumimos que a rede está estável, mas apenas se soubermos `max_hops`.
                    if (
                        m_State.max_hops != net::UNKNOWN_MAX_HOPS
                        && m_State.trickle.interval_duration_doublings == m_State.trickle.max_interval_doublings
                    ) {
                        PORT_LOGI(TAG, "network is stable");
                        assert(m_Phys->finishReceive() == RADIOLIB_ERR_NONE);
                        m_Trickle.stop();

                        wait_for_slot();
                        return ExperimentalFSM::EXECUTING;
                    }

                    return ExperimentalFSM::BROADCASTING;
                }

                return ExperimentalFSM::SCANNING_CANDIDATE_NEIGHBORS;
            }; break;

            case ExperimentalFSM::EXECUTING: {
                if (notification & NOTIFICATION_TIMER) {
                    notification ^= NOTIFICATION_TIMER;
                    gpio_set_level(STATUS_LED, HIGH);
                    gpio_hold_en(STATUS_LED);

                    vTaskDelay(1000 / portTICK_PERIOD_MS);
                    
                    gpio_hold_dis(STATUS_LED);
                    gpio_set_level(STATUS_LED, LOW);
                    wait_for_slot();
                }
                
                return ExperimentalFSM::EXECUTING;
            }; break;
            
            default: {
                ESP_LOGE(TAG, "FSM reached undefined state");
                abort();
            }; break;
        }
    };


    /**
     * @brief Função principal da task do protocolo experimental.
     */
    void ExperimentalProtocol::main() {
        // Configurar ISR
        m_Phys->setPacketSentAction(ExperimentalProtocol::isr_notify_task);
        m_Phys->setPacketReceivedAction(ExperimentalProtocol::isr_notify_task);
        m_Phys->setChannelScanAction(ExperimentalProtocol::isr_notify_task);

        // Configurar parâmetros iniciais conhecidos padrão
        lora::set_phy_parameters(m_Phys, m_Params);

        // Transmitir mensagem falsa de início de broadcast
        lora::send_nonblocking(m_Phys, {
            .data = (const uint8_t *) "\x00\x00\x00\x00\x00\x00",
            .len = 6,
            .addr = 0,
        });

        port::await_notification();

        uint32_t notification = 0;

        for ( ;; ) {
            m_State.state = handle_notification(notification);            
            notification |= port::await_notification();
        }
    }

    /**
     * @brief ISR que notifica a task do protocolo experimental.
     */
    void ISR_SAFE_ATTR ExperimentalProtocol::isr_notify_task() {
        if (g_Instance == NULL)
            return;

        g_Instance->m_MonoTimeAtISR_us = port::get_monotonic_time();

        // Interromper a task atual se uma de prioridade maior for acordada.
        if (g_Instance->notify_from_isr(NOTIFICATION_IRQ))
            portYIELD_FROM_ISR();
    }
}