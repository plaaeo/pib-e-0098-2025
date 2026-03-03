#pragma once 
#include <RadioLib.h>

#include "sensors.hh"

namespace lora {
    class Protocol {
    protected:
        PhysicalLayer* m_Phys;
        
        Protocol(PhysicalLayer *phys) : m_Phys(phys) { };

    public:
        /**
         * @brief Agenda a transmissão de uma leitura de sensor quando possível.
         * @param reading A leitura realizada pelo nó sensor.
         * @returns `true` se foi possível agendar a transmissão.
         */
        virtual bool schedule(const sens::Reading& reading) = 0;

        Protocol(Protocol&&) = delete;
        Protocol(const Protocol&) = delete;
        Protocol& operator=(Protocol&&) = delete;
        Protocol& operator=(const Protocol&) = delete;
    };
}