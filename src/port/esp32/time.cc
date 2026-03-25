#include <esp_timer.h>
#include <rtc.h>
#include <stdint.h>

#include "port/time.hh"

namespace port {
ISR_SAFE_ATTR port::time_us get_monotonic_time()
{
    return esp_timer_get_time();
};

port::time_us get_rtc_time()
{
    return static_cast<port::time_us>(esp_rtc_get_time_us());
}

NotifyTimer::NotifyTimer(port::Task *task, uint32_t notification)
    : m_Inner(nullptr), m_Task(task), m_Notification(notification)
{
    esp_timer_create_args_t timer_cfg{
        .callback =
            [](void *arg) {
                auto self = static_cast<port::NotifyTimer *>(arg);
                self->m_Task->notify(self->m_Notification);
            },
        .arg = this,
        .dispatch_method = ESP_TIMER_TASK,
        .name = nullptr,
        .skip_unhandled_events = false,
    };

#ifdef CONFIG_ESP_TIMER_SUPPORTS_ISR_DISPATCH_METHOD
    // Configurar callback especial caso timers sejam despachados por interrupts
    timer_cfg.dispatch_method = ESP_TIMER_ISR;
    timer_cfg.callback = [](void *arg) ISR_SAFE_ATTR {
        // Interromper a task atual se uma de prioridade maior for acordada.
        auto self = static_cast<port::NotifyTimer *>(arg);
        if (self->m_Task->notify_from_isr(self->m_Notification))
            portYIELD_FROM_ISR();
    };
#endif

    ESP_ERROR_CHECK(esp_timer_create(
        &timer_cfg, reinterpret_cast<esp_timer_handle_t *>(&m_Inner)));
};

NotifyTimer::~NotifyTimer()
{
    if (m_Inner != nullptr) {
        stop();
        esp_timer_delete(static_cast<esp_timer_handle_t>(m_Inner));
    }
};

void NotifyTimer::stop()
{
    esp_timer_stop(static_cast<esp_timer_handle_t>(m_Inner));
};

void NotifyTimer::start_once(port::time_us duration_us)
{
    ESP_ERROR_CHECK(esp_timer_start_once(
        static_cast<esp_timer_handle_t>(m_Inner), duration_us));
};

bool NotifyTimer::is_running()
{
    return esp_timer_is_active(static_cast<esp_timer_handle_t>(m_Inner));
};
}  // namespace port