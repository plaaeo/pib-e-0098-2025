#include "net/packets.hh"

namespace net {
    size_t Broadcast::length() const {
        if (max_hops) return BROADCAST_MAX_SIZE;
        return BROADCAST_MAX_SIZE - 1;
    };

    port::optional<Broadcast> Broadcast::decode(uint8_t *buffer, size_t length)  {
        if (length < BROADCAST_MAX_SIZE - 1)
            return port::nullopt;
        
        Broadcast out;
        out.id = buffer[0];
        out.rank = Rank::from(buffer[1]);
        out.reference_time_us = buffer[2];
        out.reference_time_us = (out.reference_time_us << 8) | buffer[3];
        out.reference_time_us = (out.reference_time_us << 8) | buffer[4];
        out.reference_time_us = (out.reference_time_us << 8) | buffer[5];
        out.max_hops = net::UNKNOWN_MAX_HOPS;
        
        // Decodificar `max_hops` caso presente
        if (length == BROADCAST_MAX_SIZE)
            out.max_hops = buffer[6];
        
        return port::optional<Broadcast>(out);
    }

    size_t Broadcast::encode(uint8_t *buffer, size_t length)  {
        if (length < this->length())
            return 0;
        
        buffer[0] = id;
        buffer[1] = static_cast<uint8_t>(rank);
        buffer[2] = (reference_time_us >> 24) & 0xFF;
        buffer[3] = (reference_time_us >> 16) & 0xFF;
        buffer[4] = (reference_time_us >> 8 ) & 0xFF;
        buffer[5] = reference_time_us & 0xFF;
        
        // Codificar `max_hops` caso presente
        if (max_hops == net::UNKNOWN_MAX_HOPS) {
            buffer[6] = max_hops;
            return BROADCAST_MAX_SIZE;
        }

        return BROADCAST_MAX_SIZE - 1;
    }
}