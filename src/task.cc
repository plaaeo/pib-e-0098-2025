#include <freertos/FreeRTOS.h>

#include "task.hh"

namespace task {
    uint32_t await_notification() {
        uint32_t notification;
        
        // Interromper execução até receber uma notificação.
        xTaskNotifyWait(
            0,              // Não limpar nenhum bit da notificação na entrada.
            ULONG_MAX,      // Limpar todos os bits da notificação na saída.
            &notification,  // Armazenar valor da notificação.
            portMAX_DELAY   // Esperar próxima notificação sem timeout.
        );
        
        return notification;
    };

    Task::Task(const char *name, uint32_t priority) {
        m_Handle = xTaskCreateStatic(
            [](void* self) {
                return static_cast<Task*>(self)->main();
            },
            name,                                   // Nome da task, usado apenas para debugging.
            sizeof(m_Stack) / sizeof(StackType_t),  // Profundidade da pilha (4096 bytes).
            this,                                   // Primeiro argumento da task.
            priority,                               // Prioridade da task.
            m_Stack,
            &m_Task
        );
    }

    void Task::notify(uint32_t notification) {
        xTaskNotify(
            m_Handle,       // Acordar task.
            notification,   // Enviar notificação de IRQ.
            eSetBits        // Definir (OR) bits da notificação.
        );
    };

    bool Task::notify_from_isr(uint32_t notification) {
        BaseType_t pxHigherPriorityTaskWoken;
        xTaskNotifyFromISR(
            m_Handle,                   // Acordar task.
            notification,               // Enviar notificação de IRQ.
            eSetBits,                   // Definir (OR) bits da notificação.
            &pxHigherPriorityTaskWoken  // Verificar se a notificação acorda uma task de alta prioridade.
        );

        // Interromper a task atual se uma de prioridade maior for acordada.
        return pxHigherPriorityTaskWoken == pdTRUE;
    };
}