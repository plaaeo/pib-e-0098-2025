#include <freertos/FreeRTOS.h>

#include "lora/util.hh"
#include "lora/experimental.hh"

namespace lora {
    constexpr static auto TAG = "proto.experimental";
    static ExperimentalProtocol *g_Instance = nullptr;

    /**
     * @brief Cria uma instância do protocolo experimental.
     * @param phys O radiotransmissor a ser utilizado.
     * @warning Apenas uma instância da classe `ExperimentalProtocol` pode existir
     * a qualquer momento da execução.
     * @returns A instância criada, ou `nullptr` caso já exista outra instância.
     */
    ExperimentalProtocol *ExperimentalProtocol::create(PhysicalLayer *phys) {
        if (g_Instance) return nullptr;

        g_Instance = new ExperimentalProtocol(phys);
        return g_Instance;
    };

    /**
     * @brief Agenda a transmissão de uma leitura de sensor quando possível.
     * @param reading A leitura realizada pelo nó sensor.
     * @returns `true` se foi possível agendar a transmissão.
     */
    bool ExperimentalProtocol::schedule(const sens::Reading& reading) {
        return false;
    };

    
    /**
     * @brief Configura os parâmetros do radiotransmissor.
     */
    void ExperimentalProtocol::set_phy_parameters(Parameters params) {
        int16_t status;
        
        status = m_Phys->setDataRate({ .lora = params.dr }, RADIOLIB_MODEM_LORA);
        if (status != RADIOLIB_ERR_NONE)
            ESP_LOGE(TAG, "failed to set datarate (%hi)", status);

        status = m_Phys->setFrequency(params.freq_mhz);
        if (status != RADIOLIB_ERR_NONE)
            ESP_LOGE(TAG, "failed to set radio frequency (%hi)", status);

        status = m_Phys->setOutputPower(params.power_db);
        if (status != RADIOLIB_ERR_NONE)
            ESP_LOGE(TAG, "failed to set output power (%hi)", status);
            
        status = m_Phys->setPreambleLength(params.preambleLength);
        if (status != RADIOLIB_ERR_NONE)
            ESP_LOGE(TAG, "failed to set preamble length (%hi)", status);

        status = m_Phys->setSyncWord(&params.syncWord, 1);
        if (status != RADIOLIB_ERR_NONE)
            ESP_LOGE(TAG, "failed to set syncword (%hi)", status);
    }

    /**
     * @brief Função principal da task do protocolo experimental.
     */
    void ExperimentalProtocol::main() {
        lora::IRQFields flags;
        RadioModeConfig_t cfg;
        uint8_t buffer[256] = { };
        
        // Criar timer para janelas de recepção
        esp_timer_create_args_t timer_cfg {
            .callback = [] (void *arg) IRAM_ATTR {
                // Interromper a task atual se uma de prioridade maior for acordada.
                if (static_cast<Task *>(arg)->notify_from_isr(NOTIFICATION_TIMER))
                    portYIELD_FROM_ISR();
            },
            .arg = this,
#ifdef CONFIG_ESP_TIMER_SUPPORTS_ISR_DISPATCH_METHOD
            .dispatch_method = ESP_TIMER_ISR,
#else
            .dispatch_method = ESP_TIMER_TASK,
#endif
            .name = "experimental-protocol-timer",
            .skip_unhandled_events = false
        };

        ESP_ERROR_CHECK(esp_timer_create(&timer_cfg, &m_EspTimer));

        // Configurar ISR
        m_Phys->setPacketSentAction(ExperimentalProtocol::isr_notify_task);
        m_Phys->setPacketReceivedAction(ExperimentalProtocol::isr_notify_task);
        m_Phys->setChannelScanAction(ExperimentalProtocol::isr_notify_task);

        // Configurar parâmetros iniciais conhecidos padrão
        set_phy_parameters({
            .freq_mhz = 915.0f,
            .power_db = 10,
            .dr = {
                .spreadingFactor = 7,
                .bandwidth = 125.0f,
                .codingRate = 5,
            },
            .preambleLength = 8,
            .syncWord = 0xAE
        });
        
        cfg = (RadioModeConfig_t) {
            .transmit = {
                .data = (const uint8_t *) ("hello"),
                .len = 5,
            },
        };

        m_Phys->stageMode(RADIOLIB_RADIO_MODE_TX, &cfg);
        m_Phys->launchMode();

        ESP_LOGI(TAG, "launched TX mode");

        do {
            await(NOTIFICATION_IRQ);
            flags = get_irq_flags(m_Phys);
        } while (!flags.tx_done);

        // receber pacotes por 30 segundos
        open_rx_continuous(30000);

        /* --- rx --- */

        cfg = (RadioModeConfig_t) {
            .receive = {
                .timeout = 0,
                .irqFlags = RADIOLIB_IRQ_RX_DEFAULT_FLAGS,
                .irqMask = RADIOLIB_IRQ_RX_DEFAULT_MASK,
                .len = 0,
            },
        };

        // Iniciar recepção contínua (sem timeout)
        m_Phys->stageMode(RADIOLIB_RADIO_MODE_RX, &cfg);
        m_Phys->launchMode();
        ESP_LOGI(TAG, "launched RX mode");

        for (;;) {
            await(NOTIFICATION_IRQ);
            flags = get_irq_flags(m_Phys);

            if (flags.rx_done) {
                if (!flags.crc_err && !flags.header_err && !flags.timeout) {
                    auto length = m_Phys->getPacketLength();
                    assert(length < 256);
                    m_Phys->readData(buffer, length);
                    buffer[length] = '\0';
                    auto rssi = m_Phys->getRSSI();
                    auto snr = m_Phys->getSNR();
                    ESP_LOGI(TAG, "\treceived %u (%s) (rssi=%f; snr=%f)", length, (char *)buffer, rssi, snr);
                }
            }

            m_Phys->clearIrq(RADIOLIB_IRQ_RX_DEFAULT_FLAGS);
        }
    }

    void ExperimentalProtocol::open_rx_continuous(uint32_t window_ms) {
        Notification notif;
        lora::IRQFields flags;
        
        // Configuração do modo RX
        RadioModeConfig_t cfg = (RadioModeConfig_t) {
            .receive = {
                .timeout = UINT32_MAX,
                .irqFlags = RADIOLIB_IRQ_RX_DEFAULT_FLAGS,
                .irqMask = (1UL << RADIOLIB_IRQ_RX_DONE) | (1UL << RADIOLIB_IRQ_TIMEOUT),
                .len = 0,
            },
        };

        ESP_LOGI(TAG, "opening rx window for %ums", window_ms);
        ESP_ERROR_CHECK(esp_timer_start_once(m_EspTimer, window_ms * 1000));

        do {
            // Iniciar recepção contínua
            m_Phys->stageMode(RADIOLIB_RADIO_MODE_RX, &cfg);
            m_Phys->launchMode();
            
            // Aguardar por IRQs ou a notificação do timer
            notif = await();

            if (notif.irq) {
                flags = get_irq_flags(m_Phys);
                
                ESP_LOGD(TAG, "ExperimentalProtocol::await_phys_irq() {");
                ESP_LOGD(TAG, "\t.tx_done = %i", flags.tx_done);
                ESP_LOGD(TAG, "\t.rx_done = %i", flags.rx_done);
                ESP_LOGD(TAG, "\t.preamble_detected = %i", flags.preamble_detected);
                ESP_LOGD(TAG, "\t.sync_word_valid = %i", flags.sync_word_valid);
                ESP_LOGD(TAG, "\t.header_valid = %i", flags.header_valid);
                ESP_LOGD(TAG, "\t.header_err = %i", flags.header_err);
                ESP_LOGD(TAG, "\t.crc_err = %i", flags.crc_err);
                ESP_LOGD(TAG, "\t.cad_done = %i", flags.cad_done);
                ESP_LOGD(TAG, "\t.cad_detected = %i", flags.cad_detected);
                ESP_LOGD(TAG, "\t.timeout = %i", flags.timeout);
                ESP_LOGD(TAG, "}");

                /** @todo Lidar com recepções */
    
                m_Phys->clearIrq(RADIOLIB_IRQ_RX_DEFAULT_FLAGS);
            }
            
        } while (!flags.timeout || notif.timer);

        // Finalizar recepção
        m_Phys->finishReceive();
        ESP_LOGI(TAG, "closing rx window");
    }

    Notification ExperimentalProtocol::await(uint32_t mask) {
        uint32_t notification = 0;
        
        do {
            notification = task::await_notification();

            if (notification & NOTIFICATION_KILL) {
                /** @todo Receber notificações KILL */
                ESP_LOGE(TAG, "ordered to kill ExperimentalProtocol task");
                notification ^= NOTIFICATION_KILL;
            }
        } while ((notification & mask) == 0);

        return (Notification) {
            .irq    = 0 != (notification & NOTIFICATION_IRQ),
            .timer  = 0 != (notification & NOTIFICATION_TIMER),
        };
    };


    /**
     * @brief ISR que notifica a task do protocolo experimental.
     */
    void IRAM_ATTR ExperimentalProtocol::isr_notify_task() {
        if (g_Instance == NULL)
            return;

        // Interromper a task atual se uma de prioridade maior for acordada.
        if (g_Instance->notify_from_isr(NOTIFICATION_IRQ))
            portYIELD_FROM_ISR();
    }
}