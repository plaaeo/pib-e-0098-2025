#include "lora/proto.hh"

namespace lora {
    bool Broadcast::decode(uint8_t *buffer, size_t length, Broadcast &out)  {
        if (length != BROADCAST_SIZE)
            return false;
        
        out.id = buffer[0];
        out.layer = buffer[1];
        out.reference_time_us = buffer[2];
        out.reference_time_us = (out.reference_time_us << 8) | buffer[3];
        out.reference_time_us = (out.reference_time_us << 8) | buffer[4];
        out.reference_time_us = (out.reference_time_us << 8) | buffer[5];
        return true;
    }

    bool Broadcast::encode(uint8_t *buffer, size_t length)  {
        if (length < BROADCAST_SIZE)
            return false;
        
        buffer[0] = id;
        buffer[1] = layer;
        buffer[2] = (reference_time_us >> 24) & 0xFF;
        buffer[3] = (reference_time_us >> 16) & 0xFF;
        buffer[4] = (reference_time_us >> 8 ) & 0xFF;
        buffer[5] = reference_time_us & 0xFF;
        return true;
    }
}