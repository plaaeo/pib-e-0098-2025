#include "port/port.hh"

#include <driver/gpio.h>
#include <esp_random.h>
#include <esp_sleep.h>
#include <esp_timer.h>
#include <etl/algorithm.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <rtc.h>

#if defined(CONFIG_ESP_TIMER_SUPPORTS_ISR_DISPATCH_METHOD)
#    define PORT_HAS_ISR_DISPATCH CONFIG_ESP_TIMER_SUPPORTS_ISR_DISPATCH_METHOD
#else
#    define PORT_HAS_ISR_DISPATCH 0
#endif

#define PORT_TASK_STACK_SIZE 4096

namespace port {

#if PORT_HAS_ISR_DISPATCH
static bool g_IsInISRTimer = false;
#endif

//< Controla a LED de debug
void debug_led(bool on)
{
    static bool s_Configured = false;
    if (!s_Configured) {
        gpio_config_t cfg{
            .pin_bit_mask = 1ULL << STATUS_LED,
            .mode = GPIO_MODE_OUTPUT,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };

        s_Configured = gpio_config(&cfg) == ESP_OK;
    }

    gpio_hold_dis(STATUS_LED);
    gpio_set_level(STATUS_LED, on ? 1 : 0);
    gpio_hold_en(STATUS_LED);
};

//< Retorna um número pseudo-aleatório
uint32_t random()
{
    return esp_random();
};

//< Retorna o mesmo tempo do timer de alta resolução
PORT_ISR_SAFE port::time_us get_monotonic_time()
{
    return esp_timer_get_time();
};

//< Retorna o tempo do RTC interno do ESP32
port::time_us get_rtc_time()
{
    return static_cast<port::time_us>(esp_rtc_get_time_us());
}

PORT_ISR_SAFE static void
internal_mark_for_execution(TaskHandle_t hndl, event_bits ev) noexcept
{
    // Verificar se devemos acordar a task normalmente ou no contexto de uma ISR
    if (xPortInIsrContext()) {
        BaseType_t shouldYield;
        xTaskNotifyFromISR(hndl, ev, eSetBits, &shouldYield);

        /** @todo Verificar se é válido chamar portYIELD_FROM_ISR várias vezes
         */
        if (g_IsInISRTimer && shouldYield)
            return esp_timer_isr_dispatch_need_yield();

        portYIELD_FROM_ISR(shouldYield);
    } else {
        xTaskNotify(hndl, ev, eSetBits);
    }
}

PORT_ISR_SAFE void EventTask::mark_for_execution(event_bits ev) const noexcept
{
    if (m_Impl == nullptr)
        return;

    internal_mark_for_execution(m_Impl, ev);
};

bool schedule(EventTask &task) noexcept
{
    // A task já foi adicionada
    if (task.m_Impl != nullptr)
        return true;

    return xTaskCreate(
               [](void *arg) {
                   auto task = static_cast<EventTask *>(arg);

                   // Sinalizar início do ciclo de execução
                   task->on_start();

                   port::event_bits ev;
                   while (task->m_Impl != nullptr) {
                       // Interromper execução até receber uma notificação.
                       assert(
                           xTaskNotifyWait(0, 0xFFFFFFFF, &ev, portMAX_DELAY) ==
                           pdTRUE
                       );
                       task->run_once(ev);
                   }

                   vTaskDelete(NULL);
               },
               "port-task",           // Nome da task
               PORT_TASK_STACK_SIZE,  // Tamanho da pilha
               &task,                 // Argumento da função da task
               task.m_Priority,       // Prioridade da task
               &task.m_Impl           // Ponteiro para handle da task
           ) == pdTRUE;
};

void unschedule(EventTask &task) noexcept
{
    if (task.m_Impl == nullptr)
        return;

    TaskHandle_t hndl = task.m_Impl;
    task.m_Impl = nullptr;

    // A própria task lida com o fim de sua execução. Marcamos que ela deve
    // finalizar ao definir `m_Impl` como nullptr, efetivamente "vazando" a
    // memória da task até que ela mesma se libere. Isso evita que a task seja
    // interrompida espontâneamente enquanto esteja executando.
    internal_mark_for_execution(hndl, 0);
};

void enter_deep_sleep(port::time_us duration) noexcept
{
    // HACK: Limitar sono até 15 segundos para evitar que o powerbank desligue.
    duration = etl::min(duration, HEARTBEAT_PERIOD);
    esp_deep_sleep(duration);
};

//< Cria um timer de alta resolução do ESP-IDF
Timer::Timer(port::ISR isr)
    : m_Impl(nullptr)
    , m_ISR(isr)
{
    esp_timer_create_args_t timer_cfg{
        .callback = isr.function,
        .arg = isr.argument,
        .dispatch_method = ESP_TIMER_TASK,
        .name = nullptr,
        .skip_unhandled_events = false,
    };

#if PORT_HAS_ISR_DISPATCH
    timer_cfg.dispatch_method = ESP_TIMER_ISR;
    timer_cfg.arg = this;
    timer_cfg.callback = [](void *arg) {
        auto self = static_cast<Timer *>(arg);

        // Notificar que estamos num ISR de um timer do ESP32
        g_IsInISRTimer = true;
        (self->m_ISR.function)(self->m_ISR.argument);
        g_IsInISRTimer = false;
    };
#endif

    ESP_ERROR_CHECK(esp_timer_create(
        &timer_cfg, reinterpret_cast<esp_timer_handle_t *>(&m_Impl)
    ));
};

//< Para e destroi o timer de alta resolução.
Timer::~Timer()
{
    if (m_Impl != nullptr) {
        stop();
        esp_timer_delete(static_cast<esp_timer_handle_t>(m_Impl));
    }
};

void Timer::start_once(port::time_us duration)
{
    ESP_ERROR_CHECK(
        esp_timer_start_once(static_cast<esp_timer_handle_t>(m_Impl), duration)
    );
};

void Timer::stop()
{
    esp_timer_stop(static_cast<esp_timer_handle_t>(m_Impl));
};

bool Timer::is_running()
{
    return esp_timer_is_active(static_cast<esp_timer_handle_t>(m_Impl));
};

}  // namespace port