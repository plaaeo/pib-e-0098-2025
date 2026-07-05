#include <Adafruit_ADS1X15.h>
#include <Arduino.h>
#include <esp_log.h>
#include <esp_pm.h>
#include <esp_task_wdt.h>
#include <esp_wifi.h>
#include <freertos/task.h>
#include <cstdint>

#include "lora/experimental.hh"
#include "lora/radios/radiolib.hh"

#include "sensor/interface.hh"
#include "sensor/ph.hh"
#include "sensor/tds.hh"
#include "sensor/temperature.hh"

constexpr auto TAG = "sens";

RTC_DATA_ATTR static struct
{
    enum : uint32_t
    {
        //< Canal do ADS conectado ao sensor de temperatura (A0).
        ADS_CHANNEL_TEMPERATURE = 0,

        //< Canal do ADS conectado ao sensor de pH (A1).
        ADS_CHANNEL_PH = 1,

        //< Canal do ADS conectado ao sensor de TDS (A2).
        ADS_CHANNEL_TDS = 2,
    };

    //< Valores de calibração do sensor de temperatura
    sensor::NTC10kState temperature{
        .vref = 5.0f,
        .offset = 0.0f,
        .a = 0.0011384f,
        .b = 0.00023245f,
        .c = 0.00000009489f,
    };

    //< Valores de calibração do sensor de pH
    sensor::Ph4502cState ph{
        .a = -5.831f,
        .b = 22.05f,
    };

    //< Valores de calibração do sensor de TDS
    sensor::TDSMeterState tds{
        .k = 0.7f,
    };

    /**
     * @brief Produz uma leitura atual de sensores a partir de uma interface de
     * leitura analógica.
     * @todo Implementar armazenamento de tempo.
     */
    sensor::Reading measure(sensor::AnalogInterface &iface) const noexcept
    {
        // Medir e calcular temperatura em graus celsius
        auto curTemperature =
            temperature.convert(iface.measure_volts(ADS_CHANNEL_TEMPERATURE));

        // Medir e calcular TDS em ppm
        auto curTds =
            tds.convert(iface.measure_volts(ADS_CHANNEL_TDS), curTemperature);

        // Medir e calcular pH
        auto curPh = ph.convert(iface.measure_volts(ADS_CHANNEL_PH));

        return (sensor::Reading){
            .time = 123456,
            .temperature = curTemperature,
            .tds = curTds,
            .ph = curPh,
        };
    }
} g_Sensors{};

//< Interface analógica para o ADS1115.
sensor::ADS1X15Interface g_ADC;

RTC_DATA_ATTR lora::PersistentState g_State = {
    net::NodeInfo{
        .id = UINT8_MAX,
        .rank = net::infinite_rank,
    },
    .rt_state =
        {
            .fsm = lora::StaggeredFSM::INITIALIZED,
            .expected_slot_wakeup_time = 0,

            //< Aproximadamente o tempo de inicialização do ESP32-S3 durante
            // deep sleep, descoberto experimentalmente.
            .slot_timer_calibration = -1000000,
        },
    .trickle = {},
    .net_time = {},
    .slot_info = {},
    .max_hops = net::UNKNOWN_MAX_HOPS,
    .has_children = false,
    .candidate_parents = {},
};

#ifdef HEARTBEAT_PIN

struct Heartbeat : public port::EventTask
{
    bool        m_IsHigh;
    port::Timer m_Timer;

    Heartbeat()
        : port::EventTask(0)
        , m_IsHigh(false)
        , m_Timer(port::make_event_isr<1>(*this)) {};

    /// @brief Inicializar GPIO do heartbeat e iniciar o timer no período máximo
    void on_start() override
    {
        gpio_set_direction(HEARTBEAT_PIN, GPIO_MODE_OUTPUT);
        gpio_set_level(HEARTBEAT_PIN, 1);
        m_IsHigh = true;
        m_Timer.start_once(port::HEARTBEAT_HIGH_DURATION);
    }

    /// @brief Realizar o heartbeat alternando o nível do GPIO e reiniciando o
    /// timer.
    port::event_bits on_event(port::event_bits ev) override
    {
        if (ev != 1) {
            return 0;
        }

        gpio_set_level(HEARTBEAT_PIN, !m_IsHigh);
        m_Timer.start_once(
            m_IsHigh ? port::HEARTBEAT_PERIOD : port::HEARTBEAT_HIGH_DURATION
        );

        PORT_LOGD(
            "heartbeat", "turning %s (%llu)", m_IsHigh ? "off" : "on",
            m_IsHigh ? port::HEARTBEAT_PERIOD : port::HEARTBEAT_HIGH_DURATION
        );
        m_IsHigh = !m_IsHigh;

        return 0;
    }
};

#endif

/**
 * @brief Task do nó sensor.
 */
void task_sensor()
{
#ifdef HEARTBEAT_PIN
    static Heartbeat s_Heartbeat{};
    if (!port::schedule(s_Heartbeat)) {
        PORT_LOGW(TAG, "falha ao incializar heartbeat");
    }
#endif

    static Module     s_Module(LORA_CS, LORA_DIO0, LORA_RST, LORA_BUSY, SPI);
    static LORA_RADIO s_LoraPhy = LORA_RADIO(&s_Module);
    static lora::RadioLibPhy s_Phy(s_LoraPhy);

    //< Interface para envio de leituras de sensor.
    static lora::StaggeredProtocol s_Proto(s_Phy, g_State);

    // Tentar inicializar I2C do ADS1115
    if (!Wire.begin(ADS1115_SDA, ADS1115_SCL)) {
        PORT_LOGW(
            TAG,
            "falha ao inicializar o I2C do ADS1X15, leituras não serão "
            "realizadas"
        );
    }
    // Tentar inicializar ADC externo
    else if (!g_ADC.begin(Wire)) {
        PORT_LOGW(
            TAG, "falha ao inicializar o ADS1X15, leituras não serão realizadas"
        );
    }

    SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_CS);
    s_LoraPhy.begin(915.0f);
    s_LoraPhy.forceLDRO(false);

    // Usar `UINT8_MAX` como detector de 'estado não inicializado' por enquanto
    while (g_State.id == UINT8_MAX)
        g_State.id = s_LoraPhy.randomByte();

    if (!port::schedule(s_Proto)) {
        PORT_LOGW(TAG, "falha ao incializar protocolo");
    }
}

#ifdef GATEWAY

struct Gateway : public port::EventTask
{
    constexpr static port::event_bits EVENT_BROADCAST = 1 << 0;
    constexpr static port::event_bits EVENT_IRQ = 1 << 1;

    static net::SlotTimingInfo s_TimingInfo = {
        .tdm_subslot_guard_symbols = 0x02,
        .tdm_subslot_mtu_bytes = 0x80,
        .tdm_subslot_count = 0x04,
        .tdm_slot_count = 0x10,
        .tdm_frame_count = 0x7f,
    };

    static lora::IAsyncRadio &m_Phys;
    port::Timer               m_BroadcastTimer;
    net::Clock                m_NetTime;

    Gateway(lora::IAsyncRadio &phy)
        : port::EventTask(0)
        , m_Phys(phy)
        , m_BroadcastTimer(port::make_event_isr<EVENT_BROADCAST>(*this))
        , m_NetTime() {};

    void on_start() override
    {
        // Configurar ISR
        m_Phys.set_isr(port::make_event_isr<EVENT_IRQ>(*this));

        // Definir parâmetros comuns
        LORA_ASSERT(m_Phys.set_parameters(
            net::gateway_node.calculate_personal_parameters()
        ));

        // Aguardar 30 segundos para o broadcast inicial da rede
        m_BroadcastTimer.start_once(30e+6);
    };

    event_bits on_event(event_bits ev) override
    {
        lora::IrqFlags      flags;
        lora::StatusCode    status;
        lora::packet_length length;
        uint8_t             buffer[UINT8_MAX];

        // Ao receber um IRQ do radio
        if (ev & EVENT_IRQ) {
            etl::tie(status, flags) = m_Phys.get_flags();
            LORA_ASSERT(status);

            // Ler pacote se algum foi recebido
            if (flags & lora::IrqFlags::IRQ_RX_DONE) {
                etl::tie(status, length) = m_Phys.get_message_length();
                LORA_ASSERT(status);
                LORA_ASSERT(m_Phys.read_message(buffer, length));

                /// @todo enviar para o servidor
            }

            // Após transmitir um broadcast
            if (flags & lora::IrqFlags::IRQ_TX_DONE) {
                // Definir o tempo atual como tempo de início da rede
                m_NetTime.synchronize(0, port::get_monotonic_time());

                // Reiniciar a rede (enviar outro broadcast) no próximo ciclo +
                // 30 segundos
                m_BroadcastTimer.start_once(
                    s_TimingInfo.calculate_network_cycle_duration() + 30e+6
                );

                PORT_LOGI(
                    TAG, "sent initial network broadcast at %lluus",
                    port::get_rtc_time()
                );

                // Iniciar recepção contínua de pacotes de nós filhos
                LORA_ASSERT(m_Phys.recv({
                    .irq_flags_mask = lora::ALL_RX_FLAGS,
                    .irq_dispatch_mask = lora::IRQ_RX_DONE,
                    .length = 0,
                    .continuous = true,
                }));
            }
        }

        // Enviar broadcast caso seja hora
        if (ev & EVENT_BROADCAST) {
            net::Broadcast broadcast{
                .reference_time_us = 0,
                .id = net::gateway_node.id,
                .rank = net::gateway_node.rank,
                .slot_info = s_TimingInfo,
                .max_hops = net::UNKNOWN_MAX_HOPS
            };

            auto length = broadcast.encode(buffer, UINT8_MAX);
            assert(length > 0);

            LORA_ASSERT(m_Phys.send({
                .data = buffer,
                .length = length,
            }));
        }

        return ev;
    };
};

void task_gateway()
{
    static Module     s_Module(LORA_CS, LORA_DIO0, LORA_RST, LORA_BUSY, SPI);
    static LORA_RADIO s_LoraPhy = LORA_RADIO(&s_Module);
    static lora::RadioLibPhy s_Phy(s_LoraPhy);

    // Inicializar interface LoRa
    SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_CS);
    s_LoraPhy.begin(915.0f);
    s_LoraPhy.forceLDRO(false);

    /// @todo init gateway (gw::setup)

    static Gateway s_Gateway(s_Phy);
    if (!port::schedule(s_Gateway)) {
        PORT_LOGW(TAG, "falha ao incializar gateway");
    }
}

#endif

//< Função `main` do protótipo
extern "C" void app_main(void)
{
    initArduino();

    if (STATUS_LED != GPIO_NUM_NC) {
        pinMode(STATUS_LED, OUTPUT);
    }

    // Definir nível de logs para DEBUG após inicialização
    esp_log_level_set("*", ESP_LOG_DEBUG);

    ESP_ERROR_CHECK(
        esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_ON)
    );

    // Configurar ESP para acordar do sono em casos de IRQ do radiotransmissor.
    ESP_ERROR_CHECK(esp_sleep_enable_ext0_wakeup(LORA_DIO0, 1));

#if CONFIG_IDF_TARGET_ESP32
    esp_pm_config_esp32_t pm_config{
        .max_freq_mhz = CONFIG_ESP32_DEFAULT_CPU_FREQ_MHZ,
        .min_freq_mhz = CONFIG_ESP32_DEFAULT_CPU_FREQ_MHZ,
#    if CONFIG_FREERTOS_USE_TICKLESS_IDLE
        .light_sleep_enable = true
#    endif
    };
#elif CONFIG_IDF_TARGET_ESP32S3
    esp_pm_config_esp32s3_t pm_config{
        .max_freq_mhz = CONFIG_ESP32S3_DEFAULT_CPU_FREQ_MHZ,
        .min_freq_mhz = CONFIG_ESP32S3_DEFAULT_CPU_FREQ_MHZ,
#    if CONFIG_FREERTOS_USE_TICKLESS_IDLE
        .light_sleep_enable = true
#    endif
    };
#endif
    ESP_ERROR_CHECK(esp_pm_configure(&pm_config));

    esp_task_wdt_init(30, true);
    task_sensor();
};