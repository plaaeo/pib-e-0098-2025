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
    constexpr static auto TAG = "proto.experimental";
    static ExperimentalProtocol *g_Instance = nullptr;

    void CandidateParents::clear() {
        candidate_parents.clear();
    };

    void CandidateParents::add_or_update(ParentInfo &&info) {
        // Buscar potencial pai no vetor `candidate_parents`
        for (size_t i = 0; i < candidate_parents.size(); i++) {
            // Atualizar caso tenhamos encontrado o pai
            if (candidate_parents[i].id == info.id) {
                candidate_parents.assign(info, i);
                return;
            }
        }
                
        // Tentar inserir no vetor, caso não seja possível, substituir o pior pai
        if (!candidate_parents.push_back(info)) {
            size_t worstIndex = SIZE_MAX;
            uint32_t worstScore = info.score();

            // Buscar pai com pior pontuação
            for (size_t i = 0; i < candidate_parents.size(); i++) {
                uint32_t score = candidate_parents[i].score();
                if (score < worstScore) {
                    worstIndex = i;
                    worstScore = score;
                }
            }

            // Descartar pai novo
            if (worstIndex == SIZE_MAX) {
                PORT_LOGI(TAG, "discarding low-quality parent %hhu (new)", info.id);
                return;
            }

            // Descartar pai antigo
            PORT_LOGI(TAG, "discarding low-quality parent %hhu for %hhu", candidate_parents[worstIndex].id, info.id);
            candidate_parents.assign(info, worstIndex);
        }
    };
    
    static int compare_parent_info(const void *a, const void *b) {
        return static_cast<const ParentInfo*>(a)->score() - static_cast<const ParentInfo*>(b)->score();
    };

    port::optional<uint8_t> CandidateParents::sort_by_objective() {
        if (candidate_parents.size() == 0)
            return port::nullopt;

        // Ordenar usando quicksort
        qsort(
            candidate_parents.data(),
            candidate_parents.size(),
            sizeof(ParentInfo),
            compare_parent_info
        );

        return port::optional<uint8_t>(candidate_parents[0].id);
    };

    ExperimentalProtocol::ExperimentalProtocol(PhysicalLayer *phys, ExperimentalState &state)
        : Protocol(phys)
        , port::Task("experimental-protocol", 2)
        , m_State(state)
        , m_TimeoutTimer(this, NOTIFICATION_TIMER)
        , m_Trickle(this, state.trickle)
    { };

    /**
     * @brief Cria uma instância do protocolo experimental.
     * @param phys O radiotransmissor a ser utilizado.
     * @warning Apenas uma instância da classe `ExperimentalProtocol` pode existir
     * a qualquer momento da execução.
     * @returns A instância criada, ou `nullptr` caso já exista outra instância.
     */
    ExperimentalProtocol *ExperimentalProtocol::create(PhysicalLayer *phys, ExperimentalState &state) {
        if (g_Instance) return nullptr;

        g_Instance = new ExperimentalProtocol(phys, state);
        return g_Instance;
    };

    bool ExperimentalProtocol::schedule(const sensor::Reading& reading) {
        return false;
    };

    bool ExperimentalProtocol::process_broadcast(const Broadcast &packet) {
        bool reportInconsistency = false;

        // Atualizar fonte de referência do tempo ao receber o primeiro broadcast.
        if (m_State.rank == INFINITE_RANK) {
            /**
             * Ao receber um pacote, um dos delays que contribuem à dessincronização do tempo entre os nós é
             * um pequeno delay de (o que assumo que seja) processamento do sinal por parte do radio. Como esse
             * delay aparenta crescer com o tempo de transmissão (ToA), é justo assumir que esteja relacionado ao
             * tempo de um símbolo LoRa. Como uma forma de minimizar a influência desse delay na sincronização, removemos
             * o tempo de 1 símbolo do tempo de recepção (IRQ) para aproximar o tempo em que a transmissão realmente finalizou.
             */
            auto symbolTime_us = m_State.params.calculate_symbol_time();
            auto transmissionEndTime_us = g_Instance->m_MonoTimeAtISR_us - symbolTime_us;

            m_State.net_time.synchronize(packet.reference_time_us, transmissionEndTime_us);

            PORT_LOGI(TAG, "symbol time compensation was %llius", symbolTime_us);
            PORT_LOGI(TAG, "network time is %llius (reference time was %ius)", m_State.net_time.get_time_us(), packet.reference_time_us);
        }
    
        // Caso a distância do nó mais distante tenha mudado, há uma inconsistência.
        if (packet.max_hops && *packet.max_hops > m_State.max_hops) {
            PORT_LOGI(TAG, "upgraded max_hops from %hhu -> %hhu", m_State.max_hops, *packet.max_hops);
            m_State.max_hops = *packet.max_hops;
            reportInconsistency = true;
        }

        // Caso nosso rank tenha mudado, há uma inconsistência.
        if (m_State.rank == INFINITE_RANK || m_State.rank.hops > (packet.rank.hops + 1)) {
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
        slotDuration += TDM_SUBSLOT_COUNT * TDM_SUBSLOT_GUARD_SYMBOLS * m_State.params.calculate_symbol_time();
        
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
            || length > Broadcast::BROADCAST_MAX_SIZE
        ) {
            PORT_LOGW(TAG, "received broadcast with errors");
            return;
        }

        // Obter métricas de qualidade do pacote
        float rssi = m_Phys->getRSSI();
        float snr = m_Phys->getSNR();

        // Ler broadcast do radio
        uint8_t buffer[Broadcast::BROADCAST_MAX_SIZE];
        assert(m_Phys->readData(buffer, sizeof(buffer)) == RADIOLIB_ERR_NONE);
        
        // Tentar decodificar o pacote do broadcast
        auto packet = Broadcast::decode(buffer, length);
        if (!packet) {
            PORT_LOGW(TAG, "broadcast was invalid");
            return;
        }

        PORT_LOGI(TAG, "broadcast from ID %hhu, rank %hhu (rrsi=%f, snr=%f)",
                    packet->id, static_cast<uint8_t>(packet->rank), rssi, snr);

        // Reportar inconsistência caso o estado mude
        if (process_broadcast(*packet)) m_Trickle.received_inconsistent();
        else m_Trickle.received_consistent();

        // Iniciar o trickle caso ele ainda não esteja iniciado.
        m_Trickle.try_begin(
            TRICKLE_REDUNDANCY_CONSTANT,
            TRICKLE_MIN_INTERVAL_PACKETS * m_Phys->getTimeOnAir(Broadcast::BROADCAST_MAX_SIZE),
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
                    if (!m_Trickle.timed_out())
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
                        && m_State.max_hops == 0
                        && m_State.trickle.interval_duration_doublings == m_State.trickle.max_interval_doublings
                    ) {
                        m_State.max_hops = m_State.rank.hops;
                        PORT_LOGI(TAG, "adopted initial guess for max_hops as %hhu", m_State.max_hops);
                        m_Trickle.received_inconsistent();
                    }
                    
                    Broadcast broadcast = {
                        .reference_time_us = static_cast<int32_t>(m_State.net_time.get_time_us()),
                        .id = m_State.id,
                        .rank = m_State.rank,
                        .max_hops = (
                            m_State.max_hops == 0
                            ? port::nullopt
                            : port::optional<uint8_t>(m_State.max_hops)
                        )
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
                        m_State.max_hops != 0
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

        m_State.params = {
            .freq_mhz = 915.0f,
            .power_db = 5,
            .dr = {
                .spreadingFactor = 12,
                .bandwidth = 125.f,
                .codingRate = 5,
            },
            .preamble_length = 12,
            .sync_word = 0x77
        };

        // Configurar parâmetros iniciais conhecidos padrão
        lora::set_phy_parameters(m_Phys, m_State.params);

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

    void ExperimentalProtocol::open_rx_continuous(uint32_t window_ms) {
        Notification notif;
        lora::IRQFields flags;
        
        // Configuração do modo RX
        RadioModeConfig_t cfg = (RadioModeConfig_t) {
            .receive = {
                .timeout = UINT32_MAX,
                .irqFlags = RADIOLIB_IRQ_RX_DEFAULT_FLAGS,
                .irqMask = RADIOLIB_IRQ_RX_DEFAULT_MASK,
                .len = 0,
            },
        };

        PORT_LOGI(TAG, "opening rx window for %ums", window_ms);

        m_TimeoutTimer.start_once(window_ms * 1000);

        do {
            // Iniciar recepção contínua
            m_Phys->stageMode(RADIOLIB_RADIO_MODE_RX, &cfg);
            m_Phys->launchMode();
            
            // Aguardar por IRQs ou a notificação do timer
            notif = await();

            if (notif.irq) {
                flags = lora::get_irq_flags(m_Phys);

                PORT_LOGD(TAG, "ExperimentalProtocol::await_phys_irq() {");
                PORT_LOGD(TAG, "\t.tx_done = %u", flags.tx_done);
                PORT_LOGD(TAG, "\t.rx_done = %u", flags.rx_done);
                PORT_LOGD(TAG, "\t.preamble_detected = %u", flags.preamble_detected);
                PORT_LOGD(TAG, "\t.sync_word_valid = %u", flags.sync_word_valid);
                PORT_LOGD(TAG, "\t.header_valid = %u", flags.header_valid);
                PORT_LOGD(TAG, "\t.header_err = %u", flags.header_err);
                PORT_LOGD(TAG, "\t.crc_err = %u", flags.crc_err);
                PORT_LOGD(TAG, "\t.cad_done = %u", flags.cad_done);
                PORT_LOGD(TAG, "\t.cad_detected = %u", flags.cad_detected);
                PORT_LOGD(TAG, "\t.timeout = %u", flags.timeout);
                PORT_LOGD(TAG, "}");

                /** @todo Lidar com recepções */
    
                m_Phys->clearIrq(RADIOLIB_IRQ_RX_DEFAULT_FLAGS);
            }
            
        } while (!flags.timeout && !notif.timer);

        // Finalizar recepção
        m_Phys->finishReceive();
        PORT_LOGI(TAG, "closing rx window");
    }

    Notification ExperimentalProtocol::await(uint32_t mask) {
#if !defined(NDEBUG) && defined(ESP32) && ESP32
        // Sincronizar logs caso seja uma build de debug
        fflush(stdout);
        fsync(fileno(stdout));
#endif

        uint32_t notification = 0;
        
        do {
            notification = port::await_notification();

            if (notification & NOTIFICATION_KILL) {
                /** @todo Receber notificações KILL */
                PORT_LOGE(TAG, "ordered to kill ExperimentalProtocol task");
                notification ^= NOTIFICATION_KILL;
            }
        } while ((notification & mask) == 0);

        return (Notification) {
            .irq    = 0 != (notification & NOTIFICATION_IRQ),
            .timer  = 0 != (notification & NOTIFICATION_TIMER),
        };
    };

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