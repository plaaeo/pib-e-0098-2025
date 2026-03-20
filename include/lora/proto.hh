#pragma once 
#include <RadioLib.h>

#define INFINITE_RANK (::lora::Rank { .hops = 0b111111, .tiredness = 0b11 })

#include "sensor/reading.hh"
#include "port/types.hh"

namespace lora {
    struct Rank {
        /**
         * @brief O número de hops de distância entre esse nó e a raíz.
         */
        uint8_t hops : 6;

        /**
         * @brief O "cansaço" do nó, sendo 0b00 um nó com bateria cheia e 0b11 um nó desgastado
         * ou descarregado, indisposto a servir como relay.
         */
        uint8_t tiredness : 2;

        /**
         * @brief Cria um struct `Rank` a partir de um byte.
         */
        inline static Rank from(uint8_t value) {
            return {
                .hops       = value & 0b11111100,
                .tiredness  = value & 0b00000011,
            };
        }

        /**
         * @brief Converte o rank para um valor numérico.
         */
        explicit inline operator uint8_t() const {
            uint8_t x;
            memcpy(&x, this, sizeof(uint8_t));
            return x;
        }

        inline bool operator>(const Rank &other) const {
            return static_cast<uint8_t>(*this) > static_cast<uint8_t>(other);
        }

        inline bool operator<(const Rank &other) const {
            return static_cast<uint8_t>(*this) < static_cast<uint8_t>(other);
        }

        inline bool operator==(const Rank &other) const {
            return static_cast<uint8_t>(*this) == static_cast<uint8_t>(other);
        }

        inline bool operator!=(const Rank &other) const { return !operator==(other); }

        inline bool operator>=(const Rank &other) const { return operator>(other) || operator==(other); }

        inline bool operator<=(const Rank &other) const { return operator<(other) || operator==(other); }
    };

    struct Node {
        uint8_t id;
        Rank rank;
    };

    struct Broadcast {
        constexpr static size_t BROADCAST_MAX_SIZE = 7;
        
        /**
         * @brief O tempo, em microsegundos, passado desde o fim da transmissão do 
         * primeiro broadcast com origem no gateway.
         */
        int32_t reference_time_us;

        /**
         * @brief O ID do nó transmissor da mensagem.
         */
        uint8_t id;

        /**
         * @brief O rank do nó transmissor da mensagem.
         */
        Rank rank;

        /**
         * @brief Denota o número de hops até chegar no nó folha mais distante, caso
         * seja um valor conhecido.
         */
        port::optional<uint8_t> max_hops;

        /**
         * @returns O tamanho do pacote, em bytes.
         */
        size_t length() const;

        /**
         * @brief Tenta decodificar um broadcast.
         * @param buffer Um buffer contendo pelo menos `length` bytes.
         * @param length O tamanho do pacote LoRa recebido.
         * @param out O pacote de saída da decodificação.
         * @returns O pacote decodificado, ou nada se o pacote não for um broadcast válido.
         */
        static port::optional<Broadcast> decode(uint8_t *buffer, size_t length);

        /**
         * @brief Tenta codificar um broadcast.
         * @param buffer Um buffer contendo pelo menos `length` bytes.
         * @param length O tamanho do buffer. Deve ser pelo menos `BROADCAST_MAX_SIZE`.
         * @returns A quantidade de bytes do pacote escritos no buffer, ou `0` em caso de erro.
         */
        size_t encode(uint8_t *buffer, size_t length);
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