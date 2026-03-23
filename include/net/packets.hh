#pragma once

#include "net/types.hh"

namespace net {
    struct Broadcast {
        constexpr static size_t BROADCAST_MAX_SIZE = 11;
        
        /**
         * @brief O tempo, em microsegundos, passado desde o fim da transmissão do 
         * primeiro broadcast com origem no gateway.
         */
        int32_t reference_time_us;

        /**
         * @brief O ID do nó transmissor da mensagem.
         */
        net::node_id id;

        /**
         * @brief O rank do nó transmissor da mensagem.
         */
        Rank rank;
        
        /**
         * @brief Mantém parâmetros de timing de slots entre nós.
         */
        net::SlotTimingInfo slot_info;

        /**
         * @brief Denota o número de hops até chegar no nó folha mais distante, caso
         * seja um valor conhecido.
         */
        uint8_t max_hops;

        /**
         * @returns O tamanho do pacote, em bytes.
         */
        size_t length() const;

        /**
         * @brief Tenta decodificar um broadcast.
         * @param buffer Um buffer contendo pelo menos `length` bytes.
         * @param length O tamanho do pacote recebido.
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
}