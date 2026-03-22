#include "net/trickle.hh"
#include "lora/util.hh"

namespace net {
    constexpr static auto TAG = "trickle";

    TrickleTimer::TrickleTimer(port::Task *task, uint32_t notification, TrickleTimerState &state)
        : m_State(&state)
        , m_Timer(task, notification)
        , m_Running(false)
    { };

    void TrickleTimer::try_begin(
        uint32_t redundancy_constant,
        port::time_us min_interval,
        uint8_t max_interval_doublings
    ) {
        // Garantir que há estado
        if (m_State == nullptr) return;

        // Garantir que o timer não está rodando
        if (m_Running) return;
        
        *m_State = {
            .counter = 0,
            .redundancy_constant = redundancy_constant,
            .interval_start_time_us = 0,
            .transmit_delay = 0,
            .min_interval_us = min_interval,
            .interval_duration_doublings = 0,
            .max_interval_doublings = max_interval_doublings,
        };

        // Calcular delay de transmissão e iniciar timer
        m_State->transmit_delay = m_State->calculate_random_transmit_delay();
        m_Timer.start_once(m_State->transmit_delay);

        m_State->interval_start_time_us = port::get_rtc_time();

        PORT_LOGD(TAG, "initialized timer (k = %u, I = %lluus, t = %lluus)",
                  redundancy_constant, min_interval, m_State->transmit_delay);
        
        m_Running = true;
    };

    void TrickleTimer::stop() {
        m_Timer.stop();
        m_Running = false;
    };

    bool TrickleTimer::update_and_check() {
        if (m_State == nullptr || !m_Running) return false;
    
        // Verificar se o timeout ocorreu no tempo `t`
        auto timeNow = port::get_rtc_time();
        auto endTime = m_State->calculate_interval_end_time();
        
        PORT_LOGD(TAG, "timed out (count = %u, start = %lluus, now = %lluus, end = %lluus)",
                  m_State->counter, m_State->interval_start_time_us, timeNow, endTime);

        if (timeNow < endTime) {
            // Agendar timer para o fim do intervalo
            m_Timer.start_once(endTime - timeNow);
            return m_State->counter < m_State->redundancy_constant;
        }

        // O intervalo já acabou, deve-se iniciar o próximo intervalo
        m_State->counter = 0;
        
        if (m_State->interval_duration_doublings < m_State->max_interval_doublings)
            m_State->interval_duration_doublings += 1;
        
        // Calcular novo delay de transmissão e iniciar timer
        m_State->transmit_delay = m_State->calculate_random_transmit_delay();
        m_Timer.start_once(m_State->transmit_delay);

        m_State->interval_start_time_us = port::get_rtc_time();

        PORT_LOGD(TAG, "extended timer (I(%hhu) = %lluus, t = %lluus)",
                  m_State->max_interval_doublings, m_State->calculate_interval_duration(), m_State->transmit_delay);

        return false;
    };

    void TrickleTimer::signal_consistency() {
        if (m_State != nullptr)
            m_State->counter++;
    };

    void TrickleTimer::signal_inconsistency() {
        /**
         * 6.  If Trickle hears a transmission that is "inconsistent" and I is
         *     greater than Imin, it resets the Trickle timer.  To reset the
         *     timer, Trickle sets I to Imin and starts a new interval as in
         *     step 2.  If I is equal to Imin when Trickle hears an
         *     "inconsistent" transmission, Trickle does nothing.  Trickle can
         *     also reset its timer in response to external "events".
         */
        if (m_State != nullptr && m_State->interval_duration_doublings > 0) {
            stop();

            // Iniciar novo intervalo com as mesmas constantes
            try_begin(
                m_State->redundancy_constant,
                m_State->min_interval_us,
                m_State->max_interval_doublings
            );
        }
    };
}