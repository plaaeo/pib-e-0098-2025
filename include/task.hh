#pragma once
#include <cstdint>
#include <freertos/task.h>

namespace task {
    /**
     * @brief Interrompe a execução da task atual até receber uma notificação.
     * @returns A notificação recebida.
     */
    uint32_t await_notification();

    /**
     * @brief Abstração simples para lidar com tasks do FreeRTOS.
     */
    class Task {
    protected:
        /**
         * @brief Cria uma task.
         * @param name O nome da task. Usado apenas para debugging.
         * @param priority A prioridade da task.
         */
        Task(const char *name, uint32_t priority);

        /**
         * @brief Função principal da task.
         */
        virtual void main() = 0;

    public:
        /**
         * @brief Envia uma notificação para esta task.
         * @param notification A notificação a ser enviada.
         * @warning Esta função não deve ser executada por qualquer ISR.
         */
        void notify(uint32_t notification);

        /**
         * @brief Envia uma notificação para esta task, de um ISR.
         * @param notification A notificação a ser enviada.
         * @returns `true` caso uma task de prioridade mais alta tiver sido acordada.
         * Neste caso, é necessario executar `portYIELD_FROM_ISR` no fim da ISR.
         */
        bool notify_from_isr(uint32_t notification);
        
    protected:
        TaskHandle_t m_Handle;
    private:
        StaticTask_t m_Task;
        StackType_t m_Stack[4096];

        Task(Task&&) = delete;
        Task(const Task&) = delete;
        Task& operator=(Task&&) = delete;
        Task& operator=(const Task&) = delete;
    };
}