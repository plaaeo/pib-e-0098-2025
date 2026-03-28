#pragma once

#include <RadioLib.h>

#include "net/clock.hh"
#include "net/packets.hh"
#include "net/trickle.hh"
#include "net/types.hh"

#include "lora/proto.hh"
#include "port/port.hh"

namespace lora {
enum class ExperimentalFSM
{
    /// @brief Valor padrão após a construção inicial da máquina.
    INITIALIZED = 0,

    /// @brief O nó está aguardando por broadcasts de vizinhos.
    SCANNING_CANDIDATE_NEIGHBORS,

    /// @brief O nó está realizando um broadcast.
    BROADCASTING,

    /// @todo Implementar os próximos estados reais da rede.
    EXECUTING,
};

struct RuntimeState
{
    ExperimentalFSM fsm;

    /// @brief O tempo esperado da função `net::Clock::get_time_us()` ao acordar
    /// do slot timer.
    port::time_us expected_slot_wakeup_time;

    /// @brief Valor adicionado ao tempo de delay do slot timer para calibração.
    port::time_us slot_timer_calibration;
};

using State = net::State<RuntimeState>;

class ExperimentalProtocol : public IProtocol, private port::EventTask
{
protected:
    ExperimentalProtocol(lora::IAsyncRadio &phys, State &state);

public:
    /**
     * @brief Cria uma instância do protocolo experimental.
     * @param phys O radiotransmissor a ser utilizado.
     * @warning Apenas uma instância da classe `ExperimentalProtocol` pode
     * existir a qualquer momento da execução.
     * @returns A instância criada, ou `nullptr` caso já exista outra instância.
     */
    static ExperimentalProtocol *create(lora::IAsyncRadio &phys, State &state);

    /**
     * @brief Agenda a transmissão de uma leitura de sensor quando possível.
     * @param reading A leitura realizada pelo nó sensor.
     * @returns `true` se foi possível agendar a transmissão.
     */
    bool schedule(const sensor::Reading &reading) override;

private:
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
    State &m_State;

    lora::Parameters m_Params;

    /// @brief Salva o resultado de `port::get_monotonic_time()` no momento do
    /// último IRQ do radio.
    port::time_us m_MonoTimeAtISR_us;

    port::Timer m_TimeoutTimer;

    net::TrickleTimer m_Trickle;
};
}  // namespace lora