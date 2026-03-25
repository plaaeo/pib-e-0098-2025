#pragma once

#include "port/random.hh"
#include "port/task.hh"
#include "port/time.hh"

namespace net {
struct TrickleTimerState
{
    uint32_t      counter;
    uint32_t      redundancy_constant;
    port::time_us interval_start_time_us;
    port::time_us transmit_delay;
    port::time_us min_interval_us;
    uint8_t       interval_duration_doublings;
    uint8_t       max_interval_doublings;

    /**
     * @brief Calcula a duração do intervalo atual em microssegundos.
     */
    constexpr port::time_us calculate_interval_duration() const
    {
        return min_interval_us * (1U << interval_duration_doublings);
    }

    /**
     * @brief Calcula o tempo do fim do intervalo atual em microssegundos.
     */
    constexpr port::time_us calculate_interval_end_time() const
    {
        return interval_start_time_us + calculate_interval_duration();
    }

    /**
     * @brief Calcula um delay `t` aleatório dentro do intervalo [T/2, T).
     */
    inline port::time_us calculate_random_transmit_delay() const
    {
        auto t = (float)(port::random()) / UINT32_MAX;
        return calculate_interval_duration() * t;
    }
};

/**
 * @brief Implementa o Trickle Timer do RFC 6206
 */
class TrickleTimer
{
public:
    TrickleTimer(port::Task        *task,
                 uint32_t           notification,
                 TrickleTimerState &state)
        : m_State(&state)
        , m_Timer(task, notification)
        , m_Running(false) {};

    /**
     * @brief Tenta inicializar o trickle timer. Não faz nada caso ele já esteja
     * executando.
     * @param redundancy_constant O número de transmissões consistentes
     * necessárias antes do tempo de transmissão para suprimir a transmissão.
     * @param min_interval O tamanho mínimo de um intervalo, em microssegundos.
     * @param max_interval_doublings A quantidade máxima de vezes que o tamanho
     * do intervalo pode dobrar.
     */
    void try_begin(uint32_t      redundancy_constant,
                   port::time_us min_interval,
                   uint8_t       max_interval_doublings);

    /**
     * @brief Interrompe a execução do trickle timer.
     */
    void stop();

    /**
     * @brief Atualiza o estado do timer ao receber uma notificação e verifica
     * se, neste instante, o nó deve ou não transmitir.
     * @warning É necessário chamar esta função sempre após uma notificação do
     * trickle.
     * @returns `true` se a transmissão pode ocorrer agora.
     */
    bool update_and_check();

    /**
     * @brief Sinaliza o timer de que uma mensagem "consistente" foi recebida.
     * A definição de "consistente" é dada pelo usuário do trickle timer.
     */
    void signal_consistency();

    /**
     * @brief Sinaliza o timer de que uma mensagem "inconsistente" foi recebida.
     * A definição de "inconsistente" é dada pelo usuário do trickle timer.
     */
    void signal_inconsistency();

private:
    TrickleTimerState *m_State;
    port::NotifyTimer  m_Timer;
    bool               m_Running;
};
}  // namespace net