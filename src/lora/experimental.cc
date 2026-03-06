#include <freertos/FreeRTOS.h>
#include <esp_log.h>

#include "lora/util.hh"
#include "lora/experimental.hh"

namespace lora {
    constexpr static auto TAG = "proto.experimental";
    static ExperimentalProtocol *g_Instance = nullptr;

    std::optional<Broadcast> Broadcast::decode(uint8_t *buffer, size_t length)  {
        if (length != BROADCAST_SIZE)
            return std::nullopt;
        
        Broadcast out;
        out.id = buffer[0];
        out.layer = buffer[1];
        out.referenceTime_us = buffer[2];
        out.referenceTime_us = (out.referenceTime_us << 8) | buffer[3];
        out.referenceTime_us = (out.referenceTime_us << 8) | buffer[4];
        out.referenceTime_us = (out.referenceTime_us << 8) | buffer[5];
        return out;
    }

    bool Broadcast::encode(uint8_t *buffer, size_t length)  {
        if (length < BROADCAST_SIZE)
            return false;
        
        buffer[0] = id;
        buffer[1] = layer;
        buffer[2] = (referenceTime_us >> 24) & 0xFF;
        buffer[3] = (referenceTime_us >> 16) & 0xFF;
        buffer[4] = (referenceTime_us >> 8 ) & 0xFF;
        buffer[5] = referenceTime_us & 0xFF;
        return true;
    }

    ExperimentalProtocol::ExperimentalProtocol(PhysicalLayer *phys)
        : Protocol(phys)
        , task::Task("experimental-protocol", 2)
    { };

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
    bool ExperimentalProtocol::schedule(const sensor::Reading& reading) {
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
     * @brief Atualizando o estado atual do protocolo de acordo com um broadcast recebido.
     * @returns Um pacote de broadcast para ser re-transmitido, ou `std::nullopt`
     * caso não seja necessário.
     */
    std::optional<Broadcast> ExperimentalProtocol::on_recv_broadcast(const Broadcast &packet) {
        /**
         * @todo Adicionar lógica de salvamento de vizinhos.
         */

        // Caso seja de uma camada maior, não é necessário retransmitir.
        if (packet.layer > m_Layer)
            return std::nullopt;

        // Atualizar fonte de referência do tempo
        if (m_Layer == UINT8_MAX) {
            m_NetTimer.synchronize(packet.referenceTime_us, g_Instance->m_HRTTimeAtISR_us);

            ESP_LOGI(TAG, "network time is %llius (reference time was %ius)", m_NetTimer.get_time_us(), packet.referenceTime_us);
        }
    
        m_Layer = packet.layer + 1;

        // Retornar broadcast de resposta
        return (Broadcast) {
            .id = m_ID,
            .layer = m_Layer,
            .referenceTime_us = packet.referenceTime_us,
        };
    };

    void ExperimentalProtocol::do_initialization_stage() {
        uint8_t buffer[256] = { };
        Notification notif;

        m_Layer = 0xFF;
        ESP_LOGI(TAG, "starting broadcast rx session");

        esp_timer_stop(m_TimeoutTimer);

        // Calcular o tempo esperado de transmissão de um broadcast para esse radio
        int64_t expectedToA_us = m_Phys->getTimeOnAir(Broadcast::BROADCAST_SIZE);
        ESP_LOGI(TAG, "expected time on air is %llu microseconds", expectedToA_us);

        do {
            // Iniciar recepção sem timeout
            lora::recv_nonblocking(m_Phys, {
                .timeout = 0,
                .irqFlags = RADIOLIB_IRQ_RX_DEFAULT_FLAGS,
                .irqMask = 1U << RADIOLIB_IRQ_RX_DONE,
                .len = 0,
            });
            
            notif = await(NOTIFICATION_IRQ | NOTIFICATION_TIMER);            
            
            auto flags = get_irq_flags(m_Phys);
            if (!notif.irq || !flags.rx_done)
                continue;
            
            // Verificar se a recepção teve sucesso
            if (flags.crc_err || flags.header_err || flags.timeout) {
                ESP_LOGI(TAG, "received failed broadcast");
                continue;
            }

            // Verificar se o pacote tem o tamanho correto
            auto length = m_Phys->getPacketLength();
            if (length != Broadcast::BROADCAST_SIZE)
                continue;

            // Ler e decodificar broadcast
            assert(m_Phys->readData(buffer, sizeof(buffer)) == RADIOLIB_ERR_NONE);
            auto packet = Broadcast::decode(buffer, length);
            if (unlikely(!packet))
                continue;
            
            ESP_LOGI(TAG, "broadcast from ID %hhu, layer %hhu", packet->id, packet->layer);
            
            // Atualizar estado e verificar necessidade de retransmissão
            auto response = on_recv_broadcast(*packet);
            if (!response)
                continue;
             
            // Calcular um tempo aleatório antes de re-transmitir (evita colisões)
            uint32_t delay_ms = m_Phys->randomByte();
            ESP_LOGI(TAG, "waiting %u milliseconds...", delay_ms);
            vTaskDelay(delay_ms / portTICK_PERIOD_MS);
            
            /**
             * Atualizar o tempo de referência no broadcast para refletir o tempo
             * de processamento do pacote e o tempo esperado de transmissão.
             * 
             * Isso não garante sincronia com o próximo nó. Há um pequeno erro acumulado em cada
             * retransmissão equivalente a:
             * - O tempo de codificar o broadcast em um pacote;
             * - O tempo de comunicar com o radiotransmissor o pacote a ser enviado;
             * - O tempo de comunicar o radiotransmissor a iniciar a transmissão;
             * - O tempo do radiotransmissor realmente iniciar a transmissão.
             * Após a recepcão do próximo nó, ainda há um pequeno delay não deterministico de processamento
             * necessário para o radiotransmissor conseguir demodular o pacote e corrigir erros.
             */
            response->referenceTime_us = static_cast<int32_t>(
                m_NetTimer.get_time_us() + expectedToA_us
            );

            assert(response->encode(buffer, sizeof(buffer)));

            // Realizar a retransmissão
            lora::send_nonblocking(m_Phys, {
                .data = buffer,
                .len = Broadcast::BROADCAST_SIZE,
                .addr = 0,
            });
            
            // Aguardar fim da transmissão.
            notif = await(NOTIFICATION_IRQ | NOTIFICATION_TIMER);
            flags = get_irq_flags(m_Phys);
            assert(notif.irq && flags.tx_done);
            ESP_LOGI(TAG, "retransmitted broadcast");

            // Reiniciar timeout para 8 * o delay aleatório máximo
            esp_timer_stop(m_TimeoutTimer);
            esp_timer_start_once(m_TimeoutTimer, UINT8_MAX * 1000U * 8U);
        } while (!notif.timer);

        esp_timer_stop(m_TimeoutTimer);
        ESP_LOGI(TAG, "ended broadcast rx session, I'm ID %hhu at layer %hhu", m_ID, m_Layer);

        assert(m_Phys->finishReceive() == RADIOLIB_ERR_NONE);
    };

    /**
     * @brief Função principal da task do protocolo experimental.
     */
    void ExperimentalProtocol::main() {
        // Criar timer para gerenciamento manual de timeouts
        esp_timer_create_args_t timer_cfg {
            .callback = [] (void *arg) {
                auto self = static_cast<ExperimentalProtocol *>(arg);
                self->notify(NOTIFICATION_TIMER);
            },
            .arg = this,
            .dispatch_method = ESP_TIMER_TASK,
            .name = "experimental-protocol-timer",
            .skip_unhandled_events = false,
        };

#ifdef CONFIG_ESP_TIMER_SUPPORTS_ISR_DISPATCH_METHOD
        // Configurar callback especial caso timers sejam despachados por interrupts
        timer_cfg.dispatch_method = ESP_TIMER_ISR;
        timer_cfg.callback = [] (void *arg) IRAM_ATTR {
            // Interromper a task atual se uma de prioridade maior for acordada.
            auto self = static_cast<ExperimentalProtocol *>(arg);
            if (self->notify_from_isr(NOTIFICATION_TIMER))
                portYIELD_FROM_ISR();
        };
#endif

        ESP_ERROR_CHECK(esp_timer_create(&timer_cfg, &m_TimeoutTimer));

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

        // Transmitir mensagem falsa de início de broadcast
        lora::send_nonblocking(m_Phys, {
            .data = (const uint8_t *) "\x00\x00\x00\x00\x00\x00",
            .len = 6,
            .addr = 0,
        });

        await(NOTIFICATION_IRQ);

        do_initialization_stage();

        auto nextTimerTime_us = 2000000LL;

        uint32_t ledState = 0;
        for (;;) {
            nextTimerTime_us += 1000000LL;
            esp_timer_start_once(
                m_TimeoutTimer,
                nextTimerTime_us - m_NetTimer.get_time_us()
            );

            if constexpr (STATUS_LED != GPIO_NUM_NC) {
                ledState = (ledState > 0) ? 0 : 1;
                gpio_hold_dis(STATUS_LED);
                gpio_set_level(STATUS_LED, ledState);
                gpio_hold_en(STATUS_LED);
            }
            
            await(NOTIFICATION_TIMER);
            auto time_us = m_NetTimer.get_time_us();
            auto drift_us = time_us - nextTimerTime_us;
            char driftSign = '+';
            if (drift_us < 0) {
                driftSign = '-';
                drift_us = -drift_us;
            }
            ESP_LOGI(TAG, "current time is: %lli.%06llis after broadcast (drifted %c%lli.%06lli)", time_us / 1000000, time_us % 1000000LL, driftSign, drift_us / 1000000, drift_us % 1000000LL);
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
                .irqMask = RADIOLIB_IRQ_RX_DEFAULT_MASK,
                .len = 0,
            },
        };

        ESP_LOGI(TAG, "opening rx window for %ums", window_ms);
        esp_timer_stop(m_TimeoutTimer);
        esp_timer_start_once(m_TimeoutTimer, window_ms * 1000);

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
            
        } while (!flags.timeout && !notif.timer);

        // Finalizar recepção
        m_Phys->finishReceive();
        ESP_LOGI(TAG, "closing rx window");
    }

    Notification ExperimentalProtocol::await(uint32_t mask) {
#ifndef NDEBUG
        // Sincronizar logs caso seja uma build de debug
        fflush(stdout);
        fsync(fileno(stdout));
#endif

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

        // É seguro chamar `esp_timer_get_time` em um ISR
        g_Instance->m_HRTTimeAtISR_us = esp_timer_get_time();

        // Interromper a task atual se uma de prioridade maior for acordada.
        if (g_Instance->notify_from_isr(NOTIFICATION_IRQ))
            portYIELD_FROM_ISR();
    }
}