#include "packets.hh"

namespace net {
etl::optional<Broadcast> Broadcast::decode(uint8_t *buffer, size_t length)
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
    out.slot_info.tdm_frame_count = buffer[10];
    out.max_hops = net::UNKNOWN_MAX_HOPS;

    // Decodificar `max_hops` caso presente
    if (length == BROADCAST_MAX_SIZE)
        out.max_hops = buffer[11];

    return etl::optional<Broadcast>{out};
};

size_t Broadcast::encode(uint8_t *buffer, size_t length) const
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
    buffer[10] = slot_info.tdm_frame_count;

    // Codificar `max_hops` caso presente
    if (max_hops != net::UNKNOWN_MAX_HOPS) {
        buffer[11] = max_hops;
        return BROADCAST_MAX_SIZE;
    }

    return BROADCAST_MAX_SIZE - 1;
};

size_t encode_readings(
    const etl::ivector<OwnedReading> &readings,
    uint8_t                          *output,
    size_t                            out_capacity
)
{
    constexpr size_t EXPECTED_CR_SIZE = 7;
    constexpr size_t EXPECTED_SMALL_CR_SIZE = 4;

    // Sinaliza para atualizar o código quando esses valores mudarem
    static_assert(repr::CompressedReading::BIT_SIZE == EXPECTED_CR_SIZE * 8);
    static_assert(repr::META_TEMPERATURE.bits == 8);
    static_assert(repr::META_TDS.bits == 8);
    static_assert(repr::META_PH.bits == 8);

    if (out_capacity <= EXPECTED_CR_SIZE)
        return 0;

    // Codificar primeiro a base
    size_t i = 0;
    auto  &base = readings.front();

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
            PORT_LOGW(
                TAG, "small time representation overflow for %hhu's reading",
                base.id
            );

        // Codificar forma padrão da OwnedReading
        output[i++] = static_cast<uint8_t>(timediff);
        output[i++] = base.id;
        output[i++] = static_cast<uint8_t>(base.reading.temperature);
        output[i++] = static_cast<uint8_t>(base.reading.tds);
        output[i++] = static_cast<uint8_t>(base.reading.ph);
    }

    return i;
}

size_t decode_readings(
    etl::ivector<OwnedReading> &readings,
    const uint8_t              *input,
    size_t                      in_length
)
{
    constexpr size_t EXPECTED_CR_SIZE = 7;
    constexpr size_t EXPECTED_SMALL_CR_SIZE = 4;

    // Sinaliza para atualizar o código quando esses valores mudarem
    static_assert(repr::CompressedReading::BIT_SIZE == EXPECTED_CR_SIZE * 8);
    static_assert(repr::META_TEMPERATURE.bits == 8);
    static_assert(repr::META_TDS.bits == 8);
    static_assert(repr::META_PH.bits == 8);

    if (in_length <= EXPECTED_CR_SIZE || readings.full())
        return 0;

    size_t i = 0;

    // Decodificar forma padrão da OwnedReading
    OwnedReading base = {};
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

    size_t it, smallCRs = in_length / EXPECTED_SMALL_CR_SIZE;
    for (it = 0; it < smallCRs && !readings.full(); i++) {
        OwnedReading item = {};

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