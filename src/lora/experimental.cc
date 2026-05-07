#include "lora/experimental.hh"
#include "port/port.hh"

#define TRICKLE_REDUNDANCY_CONSTANT  2
#define TRICKLE_MIN_INTERVAL_PACKETS 2
#define TRICKLE_MAX_DOUBLINGS        4

constexpr static auto TAG = "proto.experimental";

namespace lora {
constexpr port::event_bits EVENT_IRQ = (1U << 0);
constexpr port::event_bits EVENT_KILL = (1U << 1);
constexpr port::event_bits EVENT_TIMER = (1U << 2);
constexpr port::event_bits EVENT_TRICKLE = (1U << 3);

StaggeredProtocol::StaggeredProtocol(
    lora::IAsyncRadio &phys,
    PersistentState   &state
)
    : port::EventTask(2)
    , m_Phys(phys)
    , m_State(state)
    , m_Params({
          .freq_hz = 915000000,
          .bandwidth_hz = 125000,
          .preamble_length = 12,
          .power_db = 5,
          .spreading_factor = 12,
          .coding_rate = 5,
          .sync_word = 0x77,
          .implicit_header = false,
      })
    , m_MonoTimeAtISR_us(0)
    , m_TimeoutTimer(port::make_event_isr<EVENT_TIMER>(*this))
    , m_Trickle(port::make_event_isr<EVENT_TRICKLE>(*this), state.trickle)
    , m_LastReceivedReadings(){};

void StaggeredProtocol::schedule(const sensor::Reading &reading) {

};

bool StaggeredProtocol::process_broadcast(const net::Broadcast &packet)
{
    lora::StatusCode status;
    float            rssi, snr;

    bool reportInconsistency = false;

    // Obter métricas de qualidade do pacote
    etl::tie(status, rssi) = m_Phys.get_rssi();
    LORA_ASSERT(status);

    etl::tie(status, snr) = m_Phys.get_snr();
    LORA_ASSERT(status);

    PORT_LOGI(
        TAG, "broadcast from ID %hhu, rank %hhu.%hhu (rrsi=%f, snr=%f)",
        packet.id, packet.rank.hops, packet.rank.tiredness, rssi, snr
    );

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
        auto transmissionEndTime_us = m_MonoTimeAtISR_us - symbolTime_us;

        m_State.net_time.synchronize(
            packet.reference_time_us, transmissionEndTime_us
        );

        PORT_LOGI(TAG, "symbol time compensation was %llius", symbolTime_us);
        PORT_LOGI(
            TAG, "network time is %llius (reference time was %ius)",
            m_State.net_time.get_time_us(), packet.reference_time_us
        );
    }

    // Caso a distância do nó mais distante tenha mudado, há uma
    // inconsistência.
    if (packet.max_hops != net::UNKNOWN_MAX_HOPS &&
        packet.max_hops > m_State.max_hops) {
        PORT_LOGI(
            TAG, "upgraded max_hops from %hhu -> %hhu", m_State.max_hops,
            packet.max_hops
        );
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

        PORT_LOGI(
            TAG, "rank.hops was updated from %hhu -> %hhu", old,
            m_State.rank.hops
        );
    }
    // Salvar caso tenhamos escutado um nó filho
    else if (packet.rank.hops > m_State.rank.hops) {
        PORT_LOGI(TAG, "detected child (id %hhu)", packet.id);
        m_State.has_children = true;
    }

    // Salvar caso o nó escutado seja um potencial pai
    if (m_State.rank.hops == packet.rank.hops + 1) {
        m_State.candidate_parents.add_or_update(net::ParentInfo{
            net::NodeInfo{
                .id = packet.id,
                .rank = packet.rank,
            },
            .last_rssi = rssi,
            .last_snr = snr,
        });
    }

    return reportInconsistency;
};

void StaggeredProtocol::sleep_until_next_slot() {}

void StaggeredProtocol::handle_broadcast_recv(lora::IrqFlags flags)
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
    if (process_broadcast(*packet)) {
        m_Trickle.signal_inconsistency();
    } else {
        m_Trickle.signal_consistency();
    }

    // Iniciar o trickle caso ele ainda não esteja iniciado.
    m_Trickle.try_begin(
        TRICKLE_REDUNDANCY_CONSTANT,
        TRICKLE_MIN_INTERVAL_PACKETS *
            m_Params.calculate_time_on_air(net::Broadcast::BROADCAST_MAX_SIZE),
        TRICKLE_MAX_DOUBLINGS
    );
}

void StaggeredProtocol::on_state_enter()
{
    switch (m_State.rt_state.fsm) {
    case StaggeredFSM::INITIALIZED: {
        // Envia um broadcast falso
        m_Phys.send({
            .data = (const uint8_t *)"\x00\x00\x00\x00\x00\x00\x02\x40\x01\x04",
            .length = 10,
        });

        return;
    }

    case StaggeredFSM::RECEIVING_BROADCASTS: {
        PORT_LOGI(TAG, "starting broadcast rx session");

        // Iniciar recepção sem timeout
        LORA_ASSERT(m_Phys.recv({
            .irq_flags_mask = lora::ALL_RX_FLAGS,
            .irq_dispatch_mask = lora::IRQ_RX_DONE,
            .length = 0,
            .continuous = true,
        }));

        return;
    };

    case StaggeredFSM::SENDING_BROADCAST: {
        net::Broadcast broadcast = {
            .reference_time_us = 0,
            .id = m_State.id,
            .rank = m_State.rank,
            .slot_info = m_State.slot_info,
            .max_hops = m_State.max_hops,
        };

        // Atualizar o tempo de referência no broadcast para
        // refletir o tempo de processamento do pacote e o tempo
        // esperado de transmissão.
        //
        // Isso não garante sincronia com o próximo nó. Há um
        // pequeno erro acumulado em cada retransmissão equivalente a:
        // - O tempo de codificar o broadcast em um pacote;
        // - O tempo de comunicar com o radiotransmissor o pacote a ser enviado;
        // - O tempo de comunicar o radiotransmissor a iniciar a transmissão;
        // - O tempo do radiotransmissor realmente iniciar a transmissão. Após a
        // recepcão do próximo nó, ainda há um pequeno delay não deterministico
        // de processamento necessário para o radiotransmissor conseguir
        // demodular o pacote e corrigir erros.
        size_t length = broadcast.length();

        broadcast.reference_time_us += m_Params.calculate_time_on_air(length);
        broadcast.reference_time_us += m_State.net_time.get_time_us();

        // Codificar o buffer
        uint8_t buffer[length];
        assert(broadcast.encode(buffer, length));

        // Realizar a retransmissão
        LORA_ASSERT(m_Phys.send({
            .data = buffer,
            .length = static_cast<lora::packet_length>(length),
        }));

        PORT_LOGI(
            TAG, "transmitting broadcast (max_hops = %hhu)", m_State.max_hops
        );

        return;
    };

    case StaggeredFSM::WAITING_TO_RX: {
        // Tentar configurar o modo de menor consumo energético
        if (m_Phys.sleep() != lora::StatusCode::ok)
            LORA_ASSERT(m_Phys.standby());

        // Calcular tempo que o MCU deve acordar para receber
        port::time_us wakeupTime = m_State.calculate_next_rx_time();
        port::time_us now = m_State.net_time.get_time_us();

        m_State.rt_state.expected_slot_wakeup_time = wakeupTime;

        // Entrar em modo de sono. O código para de executar após esta chamada
        // de função.
        port::enter_deep_sleep(
            (wakeupTime - now) + m_State.rt_state.slot_timer_calibration
        );

        return;
    }

    case StaggeredFSM::RECEIVING_FROM_CHILDREN: {
        m_LastReceivedReadings.clear();

        // Definir timeout para finalizar recepções
        m_TimeoutTimer.start_once(m_State.calculate_slot_duration());

        // Configurar parâmetros de comunicação direta
        LORA_ASSERT(m_Phys.set_parameters(m_State.calculate_personal_parameters(
        )));

        // Iniciar recepção sem timeout
        LORA_ASSERT(m_Phys.recv({
            .irq_flags_mask = lora::ALL_RX_FLAGS,
            .irq_dispatch_mask = lora::IRQ_RX_DONE,
            .length = 0,
            .continuous = true,
        }));

        PORT_LOGI(TAG, "receiving readings from children");
        return;
    }

    case StaggeredFSM::WAITING_TO_TX: {
        /// @todo lógica de CSMA em subslots (desnecessário para o experimento)

        // Definir timeout para inciar a transmissão
        m_TimeoutTimer.start_once(m_State.calculate_tx_wait_time());

        PORT_LOGI(TAG, "waiting for my transmission slot");
        return;
    }

    case StaggeredFSM::TRANSMITTING_TO_PARENT: {
        /// @todo Inserir a leitura deste nó em m_LastReceivedReadings
        uint8_t buffer[UINT8_MAX];

        // Codifica todas as leituras atuais
        auto length =
            net::encode_readings(m_LastReceivedReadings, buffer, UINT8_MAX);

        assert(length > 0);

        // Transmite a mensagem codificada
        LORA_ASSERT(m_Phys.send(
            {.data = buffer, .length = static_cast<lora::packet_length>(length)}
        ));

        return;
    }
    }
};

void StaggeredProtocol::handle_child_recv()
{
    lora::StatusCode    status;
    lora::IrqFlags      flags;
    lora::packet_length length;

    // Verificar flags causadoras do IRQ
    etl::tie(status, flags) = m_Phys.get_flags();
    LORA_ASSERT(status);

    // Caso seja `false`, o radio foi utilizado por outro código além deste.
    if (~flags & lora::IRQ_RX_DONE) {
        PORT_LOGW(
            TAG,
            "received IRQ without RX_DONE, expected child message (flags = %u)",
            flags
        );
        return;
    }

    m_Phys.clear_flags(lora::ALL_RX_FLAGS);

    // Verificar se a recepção teve sucesso
    if (flags & lora::RX_ERROR_FLAGS) {
        PORT_LOGW(TAG, "received child message with errors");
        return;
    }

    etl::tie(status, length) = m_Phys.get_message_length();
    LORA_ASSERT(status);

    // Ler buffer da mensagem recebida
    uint8_t buffer[length];
    LORA_ASSERT(m_Phys.read_message(buffer, length));

    // Decodificar leituras recebidas
    net::decode_readings(m_LastReceivedReadings, buffer, length);
}

StaggeredFSM StaggeredProtocol::on_state_event(port::event_bits &events)
{
    switch (m_State.rt_state.fsm) {
    case StaggeredFSM::RECEIVING_BROADCASTS: {
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

            // Verificar se podemos iniciar uma transmissão
            if (!m_Trickle.update_and_check())
                return StaggeredFSM::RECEIVING_BROADCASTS;

            // Não há necessidade de cancelar uma recepção para iniciar
            // uma transmissão, pois adicionar uma transmissão concorrente
            // apenas poluiria o canal, e nós vizinhos teriam dificuldade de
            // ouvir este broadcast.
            if ((flags & RECEIVING_FLAGS) == RECEIVING_FLAGS) {
                PORT_LOGI(
                    TAG, "suppressed broadcast due to channel occupation"
                );
                return StaggeredFSM::RECEIVING_BROADCASTS;
            }

            // Caso não estejamos no tempo máximo do trickle, sempre devemos
            // transmitir
            if (!m_State.trickle.is_capped())
                return StaggeredFSM::SENDING_BROADCAST;

            // Decidir se a rede está estável ou não
            if (m_State.max_hops == net::UNKNOWN_MAX_HOPS) {
                // Não conhecemos `max_hops`, mas sabemos que não possuímos nós
                // filhos, logo, vamos chutar que nós estamos no `max_hops`
                if (!m_State.has_children) {
                    m_State.max_hops = m_State.rank.hops;

                    PORT_LOGI(
                        TAG, "adopted initial guess for max_hops as %hhu",
                        m_State.max_hops
                    );

                    m_Trickle.signal_inconsistency();
                }
            } else {
                // Conhecemos o `max_hops` e o trickle atingiu o tempo máximo
                // novamente, logo, a rede está estável.
                PORT_LOGI(TAG, "network is stable");

                // Limpar timers e estado do phys LoRa.
                LORA_ASSERT(m_Phys.standby());
                m_Phys.clear_flags(lora::ALL_RX_FLAGS | lora::ALL_TX_FLAGS);
                m_Trickle.stop();

                return StaggeredFSM::WAITING_TO_RX;
            }

            return StaggeredFSM::SENDING_BROADCAST;
        }

        return StaggeredFSM::RECEIVING_BROADCASTS;
    };

    case StaggeredFSM::INITIALIZED:
    case StaggeredFSM::SENDING_BROADCAST: {
        // Verificar se o evento recebido é gerenciável nesse estado
        if (~events & EVENT_IRQ)
            return m_State.rt_state.fsm;

        // Marca o evento de IRQ como tratado
        events &= ~EVENT_IRQ;

        auto [status, flags] = m_Phys.get_flags();
        LORA_ASSERT(status);

        // Verificar se a interrupção foi por algum motivo desconhecido
        if (~flags & lora::IRQ_TX_DONE) {
            PORT_LOGW(
                TAG, "broadcast IRQd without `TX_DONE` (flags = %u)",
                static_cast<uint32_t>(flags)
            );

            return m_State.rt_state.fsm;
        }

        m_Phys.clear_flags(lora::ALL_TX_FLAGS);
        PORT_LOGI(TAG, "broadcast done");

        return StaggeredFSM::RECEIVING_BROADCASTS;
    };

    case StaggeredFSM::RECEIVING_FROM_CHILDREN: {
        // Caso tenhamos recebido uma mensagem
        if (events & EVENT_IRQ) {
            events &= ~EVENT_IRQ;
            handle_child_recv();
        }

        // Caso o tempo de recepção tenha acabado
        if (events & EVENT_TIMER) {
            events &= ~EVENT_TIMER;

            // Finalizar qualquer recepção que exceda a janela de RX
            LORA_ASSERT(m_Phys.standby());

            return StaggeredFSM::WAITING_TO_TX;
        }

        return m_State.rt_state.fsm;
    };

    case StaggeredFSM::WAITING_TO_TX: {
        // Caso o tempo de espera tenha acabado
        if (events & EVENT_TIMER) {
            events &= ~EVENT_TIMER;
            return StaggeredFSM::TRANSMITTING_TO_PARENT;
        }

        /// @note Deve ser inalcançável.
        return m_State.rt_state.fsm;
    };

    case StaggeredFSM::TRANSMITTING_TO_PARENT: {
        // Caso tenhamos finalizado a transmissão
        if (events & EVENT_IRQ) {
            events &= ~EVENT_IRQ;
            return StaggeredFSM::WAITING_TO_RX;
        }

        /// @note Deve ser inalcançável.
        return m_State.rt_state.fsm;
    };

    case StaggeredFSM::WAITING_TO_RX: {
        /// @note Deve ser inalcançável.
        abort();
        return m_State.rt_state.fsm;
    };
    }

    PORT_LOGW(TAG, "reached end of 'on_state_event' with no return");
    return m_State.rt_state.fsm;
};

void StaggeredProtocol::on_start()
{
    struct TimeAndNotify
    {
        PORT_ISR_SAFE static void isr(void *arg)
        {
            auto self = static_cast<StaggeredProtocol *>(arg);

            self->m_MonoTimeAtISR_us = port::get_monotonic_time();
            self->dispatch_events(EVENT_IRQ);
        };
    };

    // Configurar ISR
    m_Phys.set_isr({
        .function = TimeAndNotify::isr,
        .argument = this,
    });

    // Configurar parâmetros iniciais conhecidos padrão
    m_Phys.set_parameters(m_Params);

    // O estado `WAITING_TO_RX` entra em deep sleep, e a transição ocorre ao
    // acordar do deep sleep.
    if (m_State.rt_state.fsm == StaggeredFSM::WAITING_TO_RX) {
        m_State.rt_state.fsm = StaggeredFSM::RECEIVING_FROM_CHILDREN;
    }

    on_state_enter();
}

port::event_bits StaggeredProtocol::on_event(port::event_bits events)
{
    bool didChangeState = true;

    do {
        auto nextState = on_state_event(events);

        didChangeState = nextState != m_State.rt_state.fsm;

        // Se houve mudança de estado, executar `on_state_enter()` do novo
        // estado.
        if (didChangeState) {
            m_State.rt_state.fsm = nextState;
            on_state_enter();
        }

        // Re-executar `on_state_event()` mais cedo caso haja evento enfileirado
    } while (didChangeState && events != 0);

    return events;
};
}  // namespace lora