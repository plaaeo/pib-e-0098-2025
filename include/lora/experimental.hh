#pragma once

#include <optional>
#include <RadioLib.h>

#include "lora/nettime.hh"
#include "lora/proto.hh"
#include "lora/util.hh"
#include "task.hh"

namespace lora {
    struct Broadcast {
        constexpr static size_t BROADCAST_SIZE = 6;

        uint8_t id;
        uint8_t layer;

        /**
         * @brief O tempo, em microsegundos, passado desde o fim da transmissão do 
         * primeiro broadcast com origem no gateway.
         */
        int32_t referenceTime_us;

        /**
         * @brief Tenta decodificar um broadcast.
         * @param buffer Um buffer contendo pelo menos `length` bytes.
         * @param length O tamanho do pacote LoRa recebido.
         * @returns O pacote decodificado, ou `std::nullopt` se não for um broadcast válido.
         */
        static std::optional<Broadcast> decode(uint8_t *buffer, size_t length);

        /**
         * @brief Tenta codificar um broadcast.
         * @param buffer Um buffer contendo pelo menos `length` bytes.
         * @param length O tamanho do buffer. Deve ser pelo menos `Broadcast::BROADCAST_SIZE`.
         * @returns `false` se o pacote não foi decodificado por falta de espaço no buffer, `true` caso contrário.
         */
        bool encode(uint8_t *buffer, size_t length);
    };

    struct ExperimentalState {
        lora::Parameters params;

        /**
         * @brief Um identificador único deste nó sensor.
         */
        uint8_t id;

        /**
         * @brief O número mínimo de hops para alcançar o gateway.
         */
        uint8_t layer;

        /**
         * @brief Usado para sincronizar o tempo entre os nós sensores.
         */
        NetworkTimer net_time;
    };

    class ExperimentalProtocol : public Protocol, private task::Task {
    protected:
        ExperimentalProtocol(PhysicalLayer *phys, ExperimentalState &state);

    public:
        /**
         * @brief Cria uma instância do protocolo experimental.
         * @param phys O radiotransmissor a ser utilizado.
         * @warning Apenas uma instância da classe `ExperimentalProtocol` pode existir
         * a qualquer momento da execução.
         * @returns A instância criada, ou `nullptr` caso já exista outra instância.
         */
        static ExperimentalProtocol *create(PhysicalLayer *phys, ExperimentalState &state);

        /**
         * @brief Agenda a transmissão de uma leitura de sensor quando possível.
         * @param reading A leitura realizada pelo nó sensor.
         * @returns `true` se foi possível agendar a transmissão.
         */
        bool schedule(const sensor::Reading& reading) override;

    private:

        /**
         * @brief Atualizando o estado atual do protocolo de acordo com um broadcast recebido.
         * @returns Um pacote de broadcast para ser re-transmitido, ou `std::nullopt`
         * caso não seja necessário.
         */
        std::optional<Broadcast> on_recv_broadcast(const Broadcast &packet);

        /**
         * @brief Inicia, executa e finaliza o estado de inicialização do protocolo.
         */
        void do_initialization_stage();

        /**
         * @brief Função principal da task do protocolo experimental.
         */
        void main() override;

        /**
         * @brief Abre uma janela de recepção contínua, recebendo quantos pacotes for possível
         * até um determinado tempo.
         * @param window_ms Tempo em milisegundos para receber pacotes.
         * @note Como nenhum radiotransmissor LoRa consegue implementar janelas de transmissão contínuas
         * por períodos arbitrários de tempo, a lógica de timeout foi implementada utilizando um timer
         * do ESP32, que acorda o microcontrolador para cancelar a recepção no momento correto.
         */
        void open_rx_continuous(uint32_t window_ms);

        /**
         * @brief Aguarda por uma notificação e lida com notificações simples.
         * @param mask Uma máscara usada para filtrar quais notificações devem ser esperadas.
         * @returns Os campos definidos no bitset da notificação.
         */
        Notification await(uint32_t mask = UINT32_MAX);

        /**
         * @brief ISR que notifica a task do protocolo experimental.
         */
        static void IRAM_ATTR isr_notify_task();
    private:
        ExperimentalState &m_State;

        //< Salva o resultado de `esp_timer_get_time()` no momento do último IRQ do radio.
        int64_t m_HRTTimeAtISR_us;

        esp_timer_handle_t m_TimeoutTimer;
    };

    
}