#pragma once 
#include <RadioLib.h>

#include "sensor/reading.hh"

namespace lora {
    struct Broadcast {
        constexpr static size_t BROADCAST_SIZE = 6;

        uint8_t id;
        uint8_t layer;

        /**
         * @brief O tempo, em microsegundos, passado desde o fim da transmissão do 
         * primeiro broadcast com origem no gateway.
         */
        int32_t reference_time_us;

        /**
         * @brief Tenta decodificar um broadcast.
         * @param buffer Um buffer contendo pelo menos `length` bytes.
         * @param length O tamanho do pacote LoRa recebido.
         * @param out O pacote de saída da decodificação.
         * @returns `false` se o pacote não for um broadcast válido, `true` caso contrário.
         */
        static bool decode(uint8_t *buffer, size_t length, Broadcast& out);

        /**
         * @brief Tenta codificar um broadcast.
         * @param buffer Um buffer contendo pelo menos `length` bytes.
         * @param length O tamanho do buffer. Deve ser pelo menos `Broadcast::BROADCAST_SIZE`.
         * @returns `false` se o pacote não foi decodificado por falta de espaço no buffer, `true` caso contrário.
         */
        bool encode(uint8_t *buffer, size_t length);
    };

    class Protocol {
    protected:
        PhysicalLayer* m_Phys;
        
        Protocol(PhysicalLayer *phys) : m_Phys(phys) { };

    public:
        virtual ~Protocol() = default;
        
        /**
         * @brief Agenda a transmissão de uma leitura de sensor quando possível.
         * @param reading A leitura realizada pelo nó sensor.
         * @returns `true` se foi possível agendar a transmissão.
         */
        virtual bool schedule(const sensor::Reading& reading) = 0;

        Protocol(Protocol&&) = delete;
        Protocol(const Protocol&) = delete;
        Protocol& operator=(Protocol&&) = delete;
        Protocol& operator=(const Protocol&) = delete;
    };
}