#pragma once
#include <stdint.h>
#include "port/task.hh"

namespace port {
    using time_us = int64_t;

    /**
     * @returns Uma medida de tempo monotônica (sempre crescente), em microssegundos.
     */
    ISR_SAFE_ATTR port::time_us get_monotonic_time();

    /**
     * @returns Uma medida de tempo de um RTC, em microssegundos.
     */
    port::time_us get_rtc_time();

    /**
     * @brief Uma abstração para um timer que notifica uma task ao finalizar.
     */
    struct NotifyTimer {
        /**
         * @brief Cria o timer para a task dada sem inicializá-lo.
         */
        NotifyTimer(port::Task *task, uint32_t notification);
        
        /**
         * @brief Desativa e destroi o timer.
         */
        ~NotifyTimer();

        /**
         * @brief Interrompe o timer, ou não faz nada caso o timer
         * já esteja interrompido.
         */
        void stop();

        /**
         * @brief Inicia o timer com uma duração especificada.
         */
        void start_once(port::time_us duration);
    
    private:
        void       *m_Inner;
        port::Task *m_Task;
        uint32_t    m_Notification;
    };
}