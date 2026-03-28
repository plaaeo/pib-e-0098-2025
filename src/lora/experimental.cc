#include "lora/experimental.hh"
#include "port/port.hh"

#define TRICKLE_REDUNDANCY_CONSTANT  2
#define TRICKLE_MIN_INTERVAL_PACKETS 2
#define TRICKLE_MAX_DOUBLINGS        4

constexpr static auto TAG = "proto.experimental";

namespace lora {
static ExperimentalProtocol *g_Instance = nullptr;

constexpr port::event_bits EVENT_IRQ = (1U << 0);
constexpr port::event_bits EVENT_KILL = (1U << 1);
constexpr port::event_bits EVENT_TIMER = (1U << 2);
constexpr port::event_bits EVENT_TRICKLE = (1U << 3);

ExperimentalProtocol::ExperimentalProtocol(lora::IAsyncRadio &phys,
                                           State             &state)
    : IProtocol(phys)
    , port::EventTask(2)
    , m_State(state)
    , m_Params({ .freq_hz = 915000000,
                 .bandwidth_hz = 125000,
                 .preamble_length = 12,
                 .power_db = 5,
                 .spreading_factor = 12,
                 .coding_rate = 5,
                 .sync_word = 0x77 })
    , m_MonoTimeAtISR_us(0)
    , m_TimeoutTimer(port::make_event_isr<EVENT_TIMER>(*this))
    , m_Trickle(port::make_event_isr<EVENT_TRICKLE>(*this), state.trickle) {};

/**
 * @brief Cria uma instância do protocolo experimental.
 * @param phys O radiotransmissor a ser utilizado.
 * @warning Apenas uma instância da classe `ExperimentalProtocol` pode
 * existir a qualquer momento da execução.
 * @returns A instância criada, ou `nullptr` caso já exista outra instância.
 */
ExperimentalProtocol *ExperimentalProtocol::create(lora::IAsyncRadio &phys,
                                                   State             &state)
{
    if (g_Instance)
        return nullptr;

    g_Instance = new ExperimentalProtocol(phys, state);
    return g_Instance;
};

bool ExperimentalProtocol::schedule(const sensor::Reading &reading)
{
    return false;
};

bool ExperimentalProtocol::process_broadcast(const net::Broadcast &packet)
{
    bool reportInconsistency = false;

    // Obter métricas de qualidade do pacote
    auto [status, rssi] = m_Phys.get_rssi();
    LORA_ASSERT(status);

    auto [status, snr] = m_Phys.get_snr();
    LORA_ASSERT(status);

    PORT_LOGI(TAG, "broadcast from ID %hhu, rank %hhu.%hhu (rrsi=%f, snr=%f)",
              packet.id, packet.rank.hops, packet.rank.tiredness, rssi, snr);

    // Atualizar fonte de referência do tempo e informações de slot apenas
    // ao receber o primeiro broadcast.
    if (m_State.rank == net::infinite_rank) {
        m_State.slot_info = packet.slot_info;

        /**
         * Ao receber um pacote, um dos delays que contribuem à
         * dessincronização do tempo entre os nós é um pequeno delay de (o
         * que assumo que seja) processamento do sinal por parte do radio.
         * Como esse delay aparenta crescer com o tempo de transmissão
         * (ToA), é justo assumir que esteja relacionado ao tempo de um
         * símbolo LoRa. Como uma forma de minimizar a influência desse
         * delay na sincronização, removemos o tempo de 1 símbolo do tempo
         * de recepção (IRQ) para aproximar o tempo em que a transmissão
         * realmente finalizou.
         */
        auto symbolTime_us = m_Params.calculate_symbol_time();
        auto transmissionEndTime_us =
            g_Instance->m_MonoTimeAtISR_us - symbolTime_us;

        m_State.net_time.synchronize(packet.reference_time_us,
                                     transmissionEndTime_us);

        PORT_LOGI(TAG, "symbol time compensation was %llius", symbolTime_us);
        PORT_LOGI(TAG, "network time is %llius (reference time was %ius)",
                  m_State.net_time.get_time_us(), packet.reference_time_us);
    }

    // Caso a distância do nó mais distante tenha mudado, há uma
    // inconsistência.
    if (packet.max_hops != net::UNKNOWN_MAX_HOPS &&
        packet.max_hops > m_State.max_hops) {
        PORT_LOGI(TAG, "upgraded max_hops from %hhu -> %hhu", m_State.max_hops,
                  packet.max_hops);
        m_State.max_hops = packet.max_hops;
        reportInconsistency = true;
    }

    // Caso nosso rank tenha mudado, há uma inconsistência.
    if (m_State.rank == net::infinite_rank ||
        m_State.rank.hops > (packet.rank.hops + 1)) {
        auto old = m_State.rank.hops;

        // Todos os pais serão invalidados, pois nosso rank agora é igual ou
        // menor que o rank deles, logo nos tornamos apenas nós vizinhos.
        m_State.candidate_parents.clear();

        m_State.rank.hops = packet.rank.hops + 1;
        reportInconsistency = true;

        PORT_LOGI(TAG, "rank.hops was updated from %hhu -> %hhu", old,
                  m_State.rank.hops);
    }
    // Salvar caso tenhamos escutado um nó filho
    else if (packet.rank.hops > m_State.rank.hops) {
        PORT_LOGI(TAG, "detected child (id %hhu)", packet.id);
        m_State.has_children = true;
    }

    // Salvar caso o nó escutado seja um potencial pai
    if (m_State.rank.hops == packet.rank.hops + 1) {
        m_State.candidate_parents.add_or_update({
            .last_rssi = rssi,
            .last_snr = snr,
            .id = packet.id,
            .rank = packet.rank,
        });
    }

    return reportInconsistency;
};

void ExperimentalProtocol::sleep_until_next_slot()
{
    auto &si = m_State.slot_info;

    // Tentar configurar o modo de menor consumo energético
    if (m_Phys.sleep() != lora::StatusCode::ok)
        m_Phys.standby();

    // Calcular a duração de um slot completo
    port::time_us slotDuration =
        m_Params.calculate_time_on_air(si.tdm_subslot_mtu_bytes);

    slotDuration +=
        si.tdm_subslot_guard_symbols * m_Params.calculate_symbol_time();

    slotDuration *= si.tdm_subslot_count;

    // A duração de uma contagem monotônica de slots até a contagem
    // reiniciar
    port::time_us frameDuration = slotDuration * si.tdm_slot_count;

    // Calcular o índice do nosso slot
    uint8_t mySlot = m_State.max_hops - m_State.rank.hops;

    // Calcular o tempo até o próximo slot nosso
    port::time_us timeUntilNextSlot = mySlot * slotDuration;
    port::time_us now = m_State.net_time.get_time_us();
    timeUntilNextSlot = (timeUntilNextSlot - now) % frameDuration;
    timeUntilNextSlot = frameDuration - timeUntilNextSlot;

    m_State.rt_state.expected_slot_wakeup_time = now + timeUntilNextSlot;
    esp_deep_sleep(timeUntilNextSlot + m_State.rt_state.slot_timer_calibration);
}

void ExperimentalProtocol::handle_broadcast_recv(lora::IrqFlags flags)
{
    // Caso seja `false`, o radio foi utilizado por outro código além deste.
    if (~flags & lora::IRQ_RX_DONE) {
        PORT_LOGW(TAG, "broadcast IRQd without RX_DONE (flags = %u)", flags);
        return;
    }

    m_Phys.clear_flags(lora::ALL_RX_FLAGS);

    auto [status, length] = m_Phys.get_message_length();
    LORA_ASSERT(status);

    // Verificar se a recepção teve sucesso
    if (flags & lora::RX_ERROR_FLAGS ||
        length > net::Broadcast::BROADCAST_MAX_SIZE) {
        PORT_LOGW(TAG, "received broadcast with errors");
        return;
    }

    // Ler broadcast do radio
    uint8_t buffer[net::Broadcast::BROADCAST_MAX_SIZE];
    LORA_ASSERT(m_Phys.read_message(buffer, sizeof(buffer)));

    // Tentar decodificar o pacote do broadcast
    auto packet = net::Broadcast::decode(buffer, length);
    if (!packet) {
        PORT_LOGW(TAG, "broadcast was invalid");
        return;
    }

    // Reportar inconsistência caso o estado mude
    if (process_broadcast(*packet))
        m_Trickle.signal_inconsistency();
    else
        m_Trickle.signal_consistency();

    // Iniciar o trickle caso ele ainda não esteja iniciado.
    m_Trickle.try_begin(
        TRICKLE_REDUNDANCY_CONSTANT,
        TRICKLE_MIN_INTERVAL_PACKETS *
            m_Params.calculate_time_on_air(net::Broadcast::BROADCAST_MAX_SIZE),
        TRICKLE_MAX_DOUBLINGS);
}

port::event_bits ExperimentalProtocol::on_event(port::event_bits events)
{
    switch (m_State.rt_state.fsm) {
        case ExperimentalFSM::BROADCASTING: {
            // Verificar se o evento recebido é gerenciável nesse estado
            if (~events & EVENT_IRQ)
                return events;

            events &= ~EVENT_IRQ;

            auto [status, flags] = m_Phys.get_flags();
            LORA_ASSERT(status);

            // Caso não tenha IRQ_TX_DONE
            if (~flags & lora::IRQ_TX_DONE) {
                PORT_LOGW(TAG, "broadcast irq without `TX_DONE` (flags = %u)",
                          static_cast<uint32_t>(flags));

                return events;
            }

            m_Phys.clear_flags(lora::ALL_TX_FLAGS);
            PORT_LOGI(TAG, "TRANSMITTED - rebroadcast done");
        }
            [[fallthrough]];
        case ExperimentalFSM::INITIALIZED:
            PORT_LOGI(TAG, "RECEIVING - starting broadcast rx session");

            // Iniciar recepção sem timeout
            LORA_ASSERT(m_Phys.recv({
                .irq_flags_mask = lora::ALL_RX_FLAGS,
                .irq_dispatch_mask = lora::IRQ_RX_DONE,
                .length = 0,
                .continuous = true,
            }));

            m_State.rt_state.fsm =
                ExperimentalFSM::SCANNING_CANDIDATE_NEIGHBORS;

            [[fallthrough]];
        case ExperimentalFSM::SCANNING_CANDIDATE_NEIGHBORS: {
            auto [status, flags] = m_Phys.get_flags();
            LORA_ASSERT(status);

            // Uma mensagem pode ter sido recebida.
            if (events & EVENT_IRQ) {
                events &= ~EVENT_IRQ;
                handle_broadcast_recv(flags);
            }

            // Caso o Trickle Timer tenha finalizado, talvez seja possível
            // transmitir nosso broadcast.
            if (events & EVENT_TRICKLE) {
                events &= ~EVENT_TRICKLE;

                if (!m_Trickle.update_and_check())
                    return events;

                /**
                 * Não há necessidade de cancelar uma recepção para iniciar
                 * uma transmissão, pois nós vizinhos terão dificuldade de
                 * ouvir este broadcast, e adicionar uma transmissão apenas
                 * poluiria o canal.
                 */
                if ((flags & RECEIVING_FLAGS) == RECEIVING_FLAGS) {
                    PORT_LOGI(TAG,
                              "suppressed broadcast due to channel occupation");
                    return events;
                }

                /**
                 * Se o Trickle estiver executando um intervalo máximo, e
                 * nenhum nó filho foi detectado até agora, anunciar
                 * `max_hops` como `m_State.rank.hops`, e definir
                 * `m_State.max_hops` igualmente.
                 */
                if (!m_State.has_children &&
                    m_State.max_hops == net::UNKNOWN_MAX_HOPS &&
                    m_State.trickle.is_capped()) {
                    m_State.max_hops = m_State.rank.hops;
                    PORT_LOGI(TAG, "adopted initial guess for max_hops as %hhu",
                              m_State.max_hops);
                    m_Trickle.signal_inconsistency();
                }

                net::Broadcast broadcast = {
                    .reference_time_us =
                        static_cast<int32_t>(m_State.net_time.get_time_us()),
                    .id = m_State.id,
                    .rank = m_State.rank,
                    .slot_info = m_State.slot_info,
                    .max_hops = m_State.max_hops,
                };

                /**
                 * Atualizar o tempo de referência no broadcast para
                 * refletir o tempo de processamento do pacote e o tempo
                 * esperado de transmissão.
                 *
                 * Isso não garante sincronia com o próximo nó. Há um
                 * pequeno erro acumulado em cada retransmissão equivalente
                 * a:
                 * - O tempo de codificar o broadcast em um pacote;
                 * - O tempo de comunicar com o radiotransmissor o pacote a
                 * ser enviado;
                 * - O tempo de comunicar o radiotransmissor a iniciar a
                 * transmissão;
                 * - O tempo do radiotransmissor realmente iniciar a
                 * transmissão. Após a recepcão do próximo nó, ainda há um
                 * pequeno delay não deterministico de processamento
                 * necessário para o radiotransmissor conseguir demodular o
                 * pacote e corrigir erros.
                 */
                size_t length = broadcast.length();
                broadcast.reference_time_us +=
                    m_Params.calculate_time_on_air(length);

                // Codificar o buffer
                uint8_t buffer[length];
                assert(broadcast.encode(buffer, length));

                // Realizar a retransmissão
                LORA_ASSERT(m_Phys.send({
                    .data = buffer,
                    .length = length,
                }));

                PORT_LOGI(TAG,
                          "TRANSMITTING - unsuppressed broadcast (max_hops "
                          "= %hhu)",
                          m_State.max_hops);

                // Após o trickle atingir o intervalo máximo, assumimos que
                // a rede está estável, mas apenas se soubermos `max_hops`.
                if (m_State.max_hops != net::UNKNOWN_MAX_HOPS &&
                    m_State.trickle.is_capped()) {
                    PORT_LOGI(TAG, "network is stable");

                    // Stop transmission/reception and clear all flags
                    LORA_ASSERT(m_Phys.standby());
                    m_Phys.clear_flags(lora::ALL_RX_FLAGS | lora::ALL_TX_FLAGS);

                    m_Trickle.stop();

                    m_State.rt_state.fsm = ExperimentalFSM::EXECUTING;
                    sleep_until_next_slot();
                    return events;
                }

                m_State.rt_state.fsm = ExperimentalFSM::BROADCASTING;
                return events;
            }

            return events;
        }; break;

        case ExperimentalFSM::EXECUTING: {
            port::time_us now = m_State.net_time.get_time_us();

            if (events & EVENT_TIMER) {
                events &= ~EVENT_TIMER;

                // Calibrar o tempo de início do slot com o RTC
                auto calibDiff =
                    m_State.rt_state.expected_slot_wakeup_time - now;
                m_State.rt_state.slot_timer_calibration += calibDiff;

                PORT_LOGI(TAG, "wakeup time difference: %lli", calibDiff);

                gpio_set_level(STATUS_LED, HIGH);
                gpio_hold_en(STATUS_LED);

                vTaskDelay(1000 / portTICK_PERIOD_MS);

                gpio_hold_dis(STATUS_LED);
                gpio_set_level(STATUS_LED, LOW);
                sleep_until_next_slot();
            }

            return events;
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
void ExperimentalProtocol::on_start()
{
    // Configurar ISR
    m_Phys.set_isr({
        .function =
            [](void *arg) {
                auto self = static_cast<ExperimentalProtocol *>(arg);

                self->m_MonoTimeAtISR_us = port::get_monotonic_time();
                self->dispatch_events(EVENT_IRQ);
            },
        .argument = this,
    });

    // Configurar parâmetros iniciais conhecidos padrão
    m_Phys.set_parameters(m_Params);

    // Transmitir mensagem falsa de início de broadcast
    if (m_State.rt_state.fsm == ExperimentalFSM::INITIALIZED) {
        m_Phys.send({
            .data = (const uint8_t *)"\x00\x00\x00\x00\x00\x00\x02\x40\x01\x04",
            .length = 10,
        });
    }
}

}  // namespace lora