#pragma once
#include <RadioLib.h>

#include "lora/radio.hh"
#include "sensor/reading.hh"

namespace lora {
class IProtocol
{
protected:
    lora::IAsyncRadio &m_Phys;

    IProtocol(lora::IAsyncRadio &phys)
        : m_Phys(phys) {};

public:
    virtual ~IProtocol() = default;

    /**
     * @brief Agenda a transmissão de uma leitura de sensor quando possível.
     * @param reading A leitura realizada pelo nó sensor.
     * @returns `true` se foi possível agendar a transmissão.
     */
    virtual bool schedule(const sensor::Reading &reading) = 0;

    IProtocol(IProtocol &&) = delete;
    IProtocol(const IProtocol &) = delete;
    IProtocol &operator=(IProtocol &&) = delete;
    IProtocol &operator=(const IProtocol &) = delete;
};
}  // namespace lora