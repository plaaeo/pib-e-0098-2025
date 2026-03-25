#pragma once

#include <etl/optional.h>
#include "net/types.hh"

namespace net {
struct Broadcast
{
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
    inline size_t length() const
    {
        return max_hops ? BROADCAST_MAX_SIZE : (BROADCAST_MAX_SIZE - 1);
    };

    /**
     * @brief Tenta decodificar um broadcast.
     * @param buffer Um buffer contendo pelo menos `length` bytes.
     * @param length O tamanho do pacote recebido.
     * @param out O pacote de saída da decodificação.
     * @returns O pacote decodificado, ou nada se o pacote não for um broadcast
     * válido.
     */
    static etl::optional<Broadcast> decode(uint8_t *buffer, size_t length)
    {
        if (length < BROADCAST_MAX_SIZE - 1)
            return etl::nullopt;

        Broadcast out;
        out.id = buffer[0];
        out.rank = Rank::from(buffer[1]);
        out.reference_time_us = buffer[2];
        out.reference_time_us = (out.reference_time_us << 8) | buffer[3];
        out.reference_time_us = (out.reference_time_us << 8) | buffer[4];
        out.reference_time_us = (out.reference_time_us << 8) | buffer[5];
        out.slot_info.tdm_subslot_guard_symbols = buffer[6];
        out.slot_info.tdm_subslot_mtu_bytes = buffer[7];
        out.slot_info.tdm_subslot_count = buffer[8];
        out.slot_info.tdm_slot_count = buffer[9];
        out.max_hops = net::UNKNOWN_MAX_HOPS;

        // Decodificar `max_hops` caso presente
        if (length == BROADCAST_MAX_SIZE)
            out.max_hops = buffer[10];

        return etl::optional<Broadcast>{ out };
    };

    /**
     * @brief Tenta codificar um broadcast.
     * @param buffer Um buffer contendo pelo menos `length` bytes.
     * @param length O tamanho do buffer. Deve ser pelo menos
     * `BROADCAST_MAX_SIZE`.
     * @returns A quantidade de bytes do pacote escritos no buffer, ou `0` em
     * caso de erro.
     */
    size_t encode(uint8_t *buffer, size_t length) const
    {
        if (length < this->length())
            return 0;

        buffer[0] = id;
        buffer[1] = static_cast<uint8_t>(rank);
        buffer[2] = (reference_time_us >> 24) & 0xFF;
        buffer[3] = (reference_time_us >> 16) & 0xFF;
        buffer[4] = (reference_time_us >> 8) & 0xFF;
        buffer[5] = reference_time_us & 0xFF;
        buffer[6] = slot_info.tdm_subslot_guard_symbols;
        buffer[7] = slot_info.tdm_subslot_mtu_bytes;
        buffer[8] = slot_info.tdm_subslot_count;
        buffer[9] = slot_info.tdm_slot_count;

        // Codificar `max_hops` caso presente
        if (max_hops != net::UNKNOWN_MAX_HOPS) {
            buffer[10] = max_hops;
            return BROADCAST_MAX_SIZE;
        }

        return BROADCAST_MAX_SIZE - 1;
    };
};
}  // namespace net