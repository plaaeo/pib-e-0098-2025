#pragma once
#include <stdint.h>

#ifdef ESP32
#    include "port/esp32/def.hh"
#endif

namespace port {

/**
 * @brief Usado para diferenciar pinos GPIO de constantes quaisquer.
 */
enum class gpio : uint32_t;

/**
 * @brief Modelo de função de serviço de interrupções.
 */
using isr_function = void (*)(void *argument);

/**
 * @brief Usado para representar uma ISR com argumento.
 * @warning A função DEVE ser declarada com o macro `PORT_ISR_SAFE`.
 */
struct ISR
{
    isr_function function;
    void        *argument;
};

/**
 * @brief Representa uma medida de tempo, em microssegundos.
 */
using time_us = int64_t;

constexpr static port::time_us HEARTBEAT_HIGH_DURATION = 2500;
constexpr static port::time_us HEARTBEAT_PERIOD = 9e+6;

/**
 * @returns Uma medida de tempo monotônica (sempre crescente), em
 * microssegundos.
 */
PORT_ISR_SAFE port::time_us get_monotonic_time();

/**
 * @returns Uma medida de tempo de um RTC, em microssegundos.
 */
port::time_us get_rtc_time();

/**
 * @brief Gera um número pseudo-aleatório (ou realmente aleatório! depende da
 * plataforma).
 * @note Definido pela plataforma.
 */
uint32_t random();

/**
 * @brief Usado para definir prioridades de tasks (maior = mais prioridade).
 */
using task_priority = uint32_t;

/**
 * @brief Um bitset usado para representar eventos pendentes numa task.
 */
using event_bits = uint32_t;

/**
 * @brief Adiciona a task da lista de tasks gerenciadas pela plataforma.
 * @returns `true` se foi possível adicionar a task, `false` caso contrário.
 *
 * @note Definido pela plataforma.
 * @note Em caso de sucesso, o método `on_start()` da task deve ser
 * executado uma vez pela plataforma. A qualquer momento após a execução da
 * função `mark_for_execution(x)`, `run_once(x)` deve ser executado.
 * @note O campo `m_Impl` pode ser usado para armazenar dados de
 * implementação da plataforma.
 */
bool schedule(class EventTask &task) noexcept;

/**
 * @brief Remove a task da lista de tasks gerenciadas pela plataforma.
 *
 * @note Definido pela plataforma.
 * @note A task NÃO deve mais executada pela plataforma até a próxima
 * chamada de `port::schedule(...);` na mesma task.
 */
void unschedule(class EventTask &task) noexcept;

/**
 * @brief Coloca o dispositivo em sono profundo, interrompendo a execução de
 * código. Após `duration` microssegundos, o dispositivo reiniciará.
 *
 * @note Definido pela plataforma.
 * @todo Num futuro distante, implementar `schedule_deep_sleep`, permitindo deep
 * sleeps concorrentes entre tasks.
 */
void enter_deep_sleep(port::time_us duration) noexcept;

/**
 * @brief Controla uma LED de debug, ou alguma outra forma de visualização de
 * estado do dispositivo.
 * @param on `true` para ligar a LED, `false` para desligar.
 * @note Definido pela plataforma.
 */
void debug_led(bool on);

/**
 * @brief Usado como uma abstração para a implementação de tasks possívelmente
 * concorrentes, dependendo da plataforma.
 */
class EventTask
{
public:
    virtual ~EventTask() { port::unschedule(*this); };

    /**
     * @brief Marca a task para execução definindo dados bits de evento como
     * não-resolvidos.
     * @param ev Os bits de evento que devem ser marcados como não-resolvidos.
     */
    PORT_ISR_SAFE inline void dispatch_events(event_bits ev) noexcept
    {
        // O port lidará com o resumo da execução da task.
        mark_for_execution(ev);
    };

protected:
    inline EventTask(task_priority priority)
        : m_Impl(nullptr)
        , m_Priority(priority)
        , m_Pending(0) {};

    /**
     * @brief Executado ao criar a task.
     */
    virtual void on_start() {};

    /**
     * @brief Executado sempre que o campo de eventos for modificado
     * externamente.
     * @param ev Os bits de eventos que ainda não foram resolvidos.
     * @returns Os bits de eventos que não foram resolvidos pela função.
     */
    virtual event_bits on_event(event_bits ev) = 0;

private:
    /**
     * @brief Avança o estado da task executando `on_event(...)` e salvando os
     * eventos que não forem resolvidos.
     */
    inline void run_once(event_bits ev)
    {
        m_Pending |= ev;
        m_Pending &= on_event(m_Pending);
    }

    /**
     * @brief Garante que, em algum momento no futuro, a função `run_once(...)`
     * será executada com o parâmetro dado.
     * @param ev O parâmetro que deve ser repassado para `run_once(...)`.
     * @note Definido pela plataforma.
     */
    PORT_ISR_SAFE void mark_for_execution(event_bits ev) const noexcept;

    friend bool schedule(EventTask &task) noexcept;
    friend void unschedule(EventTask &task) noexcept;

private:
    /**
     * @brief Definido pela plataforma.
     */
    void *m_Impl;

    /**
     * @brief A prioridade da task.
     */
    task_priority m_Priority;

    /**
     * @brief Eventos que já foram passados para `on_event(...)` e foram
     * enfileirados.
     */
    event_bits m_Pending;

    EventTask(EventTask &&) = delete;
    EventTask(const EventTask &) = delete;
    EventTask &operator=(EventTask &&) = delete;
    EventTask &operator=(const EventTask &) = delete;
};

template <event_bits Events>
/**
 * @brief Cria um ISR que despacha um ou mais eventos para uma task.
 * @param task Um ponteiro para a task que receberá os eventos.
 * @tparam Events Os eventos que serão despachados.
 */
inline port::ISR make_event_isr(port::EventTask &task) noexcept
{
    struct Nested
    {
        PORT_ISR_SAFE static void isr(void *task)
        {
            static_cast<port::EventTask *>(task)->dispatch_events(Events);
        };
    };

    return port::ISR{
        .function = Nested::isr,
        .argument = &task,
    };
}

/**
 * @brief Abstração para timers de hardware ou software, dependendo da
 * plataforma. Usado para executar callbacks com precisão.
 *
 * @warning Callbacks podem ou não ser executados em contextos de interrupt,
 * logo, devem ser definidos com `PORT_ISR_SAFE`.
 */
struct Timer
{
    /**
     * @brief Cria o timer sem inicializá-lo.
     * @note Definido pela plataforma.
     */
    Timer(port::ISR isr);

    /**
     * @brief Desativa e destroi o timer.
     * @note Definido pela plataforma.
     */
    ~Timer();

    /**
     * @brief Inicia o timer com uma duração especificada.
     * @note Definido pela plataforma.
     */
    void start_once(port::time_us duration);

    /**
     * @brief Interrompe o timer, ou não faz nada caso o timer
     * já esteja interrompido.
     * @note Definido pela plataforma.
     */
    void stop();

    /**
     * @returns `true` se o timer estiver executando.
     * @note Definido pela plataforma.
     */
    bool is_running();

private:
    /**
     * @brief Definido pela plataforma.
     */
    void *m_Impl;

    /**
     * @brief A ISR que será executada no fim do timer.
     */
    port::ISR m_ISR;
};

}  // namespace port