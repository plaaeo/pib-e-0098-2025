#pragma once

#include <RadioLib.h>

#include "net/clock.hh"
#include "net/packets.hh"
#include "net/trickle.hh"
#include "net/types.hh"

#include "lora/radio.hh"

#include "sensor/reading.hh"

#include "port/port.hh"

namespace lora {
enum class StaggeredFSM
{
    /// @brief Valor padrão após a construção inicial da máquina.
    INITIALIZED = 0,

    /// @brief O nó está aguardando por broadcasts de vizinhos.
    RECEIVING_BROADCASTS,

    /// @brief O nó está realizando um broadcast.
    SENDING_BROADCAST,

    /// @brief O nó está dormindo (deep sleep) aguardando o slot de recepção.
    WAITING_TO_RX,

    /// @brief O nó está ouvindo os pacotes de seus filhos.
    RECEIVING_FROM_CHILDREN,

    /// @brief O nó está aguardando o slot de transmissão ativamente.
    WAITING_TO_TX,

    /// @brief O nó está enviando uma mensagem.
    TRANSMITTING_TO_PARENT,
};

struct RuntimeState
{
    StaggeredFSM fsm;

    /// @brief O tempo esperado da função `net::Clock::get_time_us()` ao acordar
    /// do slot timer.
    port::time_us expected_slot_wakeup_time;

    /// @brief Valor adicionado ao tempo de delay do slot timer para calibração.
    port::time_us slot_timer_calibration;
};

/**
 * @brief Uma estrutura contendo o estado da rede e do protocolo. Deve ser
 * armazenado de forma persistente entre ciclos de sono do dispositivo.
 */
using PersistentState = net::State<RuntimeState>;

class StaggeredProtocol : private port::EventTask
{
public:
    StaggeredProtocol(lora::IAsyncRadio &phys, PersistentState &state);

    /**
     * @brief Agenda a transmissão de uma leitura de sensor quando possível.
     * @param reading A leitura realizada pelo nó sensor.
     */
    void schedule(const sensor::Reading &reading);

private:
    /**
     * @brief Chamado após qualquer transição de estado.
     */
    void on_state_enter();

    /**
     * @brief Chamado para processar um evento recente ou enfileirado,
     * dependendo do estado da rede e do protocolo.
     * @param[inout] events Um bitset com os eventos recebidos. Deve ser
     * modificado para limpar eventos resolvidos.
     * @returns O próximo estado do protocolo.
     */
    StaggeredFSM on_state_event(port::event_bits &events);

    /**
     * @brief Função principal da task do protocolo experimental.
     */
    void on_start() override;

    /**
     * @brief Avança o estado atual da FSM do protocolo de acordo com um evento
     * externo.
     */
    port::event_bits on_event(port::event_bits events) override;

    /**
     * @brief Atualizando o estado atual do protocolo de acordo com um broadcast
     * recebido.
     * @param packet O pacote de broadcast recebido.
     * @returns `true` caso o broadcast tenha gerado uma inconsistência, ou
     * seja, tenha modificado o estado da rede na perspectiva deste nó.
     */
    bool process_broadcast(const net::Broadcast &packet);

    /**
     * @brief Chamado ao receber `EVENT_IRQ` durante a inicialização.
     */
    void handle_broadcast_recv(lora::IrqFlags flags);

    /**
     * @brief Dorme até inicializar os slots de recepção deste nó.
     */
    void sleep_until_next_slot();

private:
    lora::IAsyncRadio &m_Phys;

    PersistentState &m_State;

    lora::Parameters m_Params;

    /// @brief Salva o resultado de `port::get_monotonic_time()` no momento do
    /// último IRQ do radio.
    port::time_us m_MonoTimeAtISR_us;

    port::Timer m_TimeoutTimer;

    net::TrickleTimer m_Trickle;
};
}  // namespace lora