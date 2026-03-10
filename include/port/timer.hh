#pragma once
#include <stdint.h>
#include "task.hh"

namespace port {
    /**
     * @brief Uma abstração para um timer que notifica uma task ao finalizar.
     */
    struct NotifyTimer {
        /**
         * @brief Cria o timer para a task dada sem inicializá-lo.
         */
        NotifyTimer(task::Task *task, uint32_t notification);
        
        /**
         * @brief Desativa e destroi o timer.
         */
        ~NotifyTimer();

        /**
         * @brief Interrompe o timer.
         */
        void stop();

        /**
         * @brief 
         */
        void start_once();
    };
}