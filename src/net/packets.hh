#pragma once

#include <etl/optional.h>

#include "net/types.hh"
#include "repr/reading.hh"

namespace net {
constexpr static auto TAG = "net";

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

/**
 * @brief Representa um `repr::CompressedReading` coletado por um
 * dispositivo específico.
 */
struct OwnedReading {
    net::node_id id;
    repr::CompressedReading reading;
}

/**
 * @brief Codifica um vetor de leituras comprimidas em um buffer para
 * ser transmitido em uma mensagem.
 * @param readings O vetor contendo as leituras para codificar.
 * @param[out] output O buffer de saída da codificação.
 * @param out_capacity A capacidade do buffer `output` em bytes.
 * @returns A quantidade de bytes codificados.
 */
size_t encode_readings(
    const etl::ivector<OwnedReading> &readings,
    uint8_t *output,
    size_t out_capacity,
) {
    constexpr size_t EXPECTED_CR_SIZE = 7;
    constexpr size_t EXPECTED_SMALL_CR_SIZE = 4;
    
    // Sinaliza para atualizar o código quando esses valores mudarem
    static_assert(CompressedReading::BIT_SIZE == EXPECTED_CR_SIZE);
    static_assert(META_TEMPERATURE.bits == 8);
    static_assert(META_TDS.bits == 8);
    static_assert(META_PH.bits == 8);

    if (out_capacity <= EXPECTED_CR_SIZE)
        return 0;

    // Codificar primeiro a base
    size_t i = 0;
    auto &base = readings.front();

    // Codificar forma padrão da OwnedReading
    output[i++] = static_cast<uint8_t>(base.reading.time & 0xFF);
    output[i++] = static_cast<uint8_t>((base.reading.time >> 8) & 0xFF);
    output[i++] = static_cast<uint8_t>((base.reading.time >> 16) & 0xFF);
    output[i++] = static_cast<uint8_t>((base.reading.time >> 24) & 0xFF);
    output[i++] = base.id;
    output[i++] = static_cast<uint8_t>(base.reading.temperature);
    output[i++] = static_cast<uint8_t>(base.reading.tds);
    output[i++] = static_cast<uint8_t>(base.reading.ph);

    for (auto it = readings.begin() + 1; it != readings.end(); i += 1) {
        // Verificatr se terá overflow no buffer
        if ((out_capacity - i) < EXPECTED_SMALL_CR_SIZE)
            break;

        auto timediff = it->reading.time - base.reading.time;

        // Verificar se terá overflow do tempo
        if (timediff > UINT8_MAX)
            PORT_LOGW(TAG, "small time representation overflow for %hhu's reading", base.id);

        // Codificar forma padrão da OwnedReading
        output[i++] = static_cast<uint8_t>(timediff);
        output[i++] = base.id;
        output[i++] = static_cast<uint8_t>(base.reading.temperature);
        output[i++] = static_cast<uint8_t>(base.reading.tds);
        output[i++] = static_cast<uint8_t>(base.reading.ph);
    }
    
    return i;
}


/**
 * @brief Decodifica um vetor de leituras comprimidas de um buffer
 * recebido em uma mensagem.
 * @param[out] readings O vetor de saída da decodificação.
 * @param input O buffer de entrada com a mensagem recebida.
 * @param in_length A quantidade de bytes no buffer `input` em bytes.
 * @returns A quantidade de bytes decodificados.
 */
size_t decode_readings(
    etl::ivector<OwnedReading> &readings,
    const uint8_t *input,
    size_t in_length,
) {
    constexpr size_t EXPECTED_CR_SIZE = 7;
    constexpr size_t EXPECTED_SMALL_CR_SIZE = 4;
    
    // Sinaliza para atualizar o código quando esses valores mudarem
    static_assert(CompressedReading::BIT_SIZE == EXPECTED_CR_SIZE);
    static_assert(META_TEMPERATURE.bits == 8);
    static_assert(META_TDS.bits == 8);
    static_assert(META_PH.bits == 8);

    if (in_length <= EXPECTED_CR_SIZE || readings.full())
        return 0;

    size_t i = 0;

    // Decodificar forma padrão da OwnedReading
    OwnedReading base = { };
    base.reading.time |= static_cast<uint32_t>(input[i++]);
    base.reading.time |= static_cast<uint32_t>(input[i++]) << 8;
    base.reading.time |= static_cast<uint32_t>(input[i++]) << 16;
    base.reading.time |= static_cast<uint32_t>(input[i++]) << 24;
    base.id = input[i++];
    base.reading.temperature = input[i++];
    base.reading.tds = input[i++];
    base.reading.ph = input[i++];

    // Caso haja overflow no vetor
    readings.push_back(base);

    in_length -= EXPECTED_CR_SIZE;

    size_t smallCRs = in_length / EXPECTED_SMALL_CR_SIZE;
    for (size_t it = 0; it < smallCRs && !readings.full(); i++) {
        OwnedReading item = { };

        // Decodificar forma minimizada da OwnedReading
        item.reading.time = base.reading.time; 
        item.reading.time += input[i++];
        item.id = input[i++];
        item.reading.temperature = input[i++];
        item.reading.tds = input[i++];
        item.reading.ph = input[i++];

        // Caso haja overflow no vetor
        readings.push_back(item);
    }
    
    return it + 1;
}

}  // namespace net