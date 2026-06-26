#include "lora/radios/radiolib.hh"

namespace lora {
/**
 * @brief Converte um erro da biblioteca RadioLib para um `lora::StatusCode`.
 */
lora::StatusCode convert_radiolib_status(int16_t code)
{
    switch (code) {
    case RADIOLIB_ERR_NONE:
        return lora::StatusCode::ok;
    case RADIOLIB_ERR_SPI_CMD_FAILED:
    case RADIOLIB_ERR_SPI_CMD_INVALID:
        return lora::StatusCode::communication_failed;
    case RADIOLIB_ERR_SPI_CMD_TIMEOUT:
        return lora::StatusCode::communication_timed_out;
    case RADIOLIB_ERR_INVALID_FREQUENCY:
        return lora::StatusCode::unsupported_frequency;
    case RADIOLIB_ERR_INVALID_BANDWIDTH:
    case RADIOLIB_ERR_INVALID_RX_BANDWIDTH:
        return lora::StatusCode::unsupported_bandwidth;
    case RADIOLIB_ERR_INVALID_SPREADING_FACTOR:
        return lora::StatusCode::unsupported_spreading_factor;
    case RADIOLIB_ERR_INVALID_CODING_RATE:
        return lora::StatusCode::unsupported_coding_rate;
    case RADIOLIB_ERR_INVALID_PREAMBLE_LENGTH:
        return lora::StatusCode::unsupported_preamble_length;
    case RADIOLIB_ERR_INVALID_SYNC_WORD:
        return lora::StatusCode::unsupported_sync_word;
    case RADIOLIB_ERR_UNSUPPORTED:
    case RADIOLIB_ERR_UNSUPPORTED_ENCODING:
        return lora::StatusCode::unsupported;
    default:
        PORT_LOGD(
            "radiolib", "received unrecognized RadioLib error (%hi)", code
        );
        return lora::StatusCode::other;
    }
}

/**
 * @brief Converte flags da biblioteca RadioLib para um `lora::IrqFlags`.
 */
lora::IrqFlags convert_radiolib_flags(RadioLibIrqFlags_t flags)
{
    lora::IrqFlags transformed = static_cast<lora::IrqFlags>(0);

    if (flags & (1U << RADIOLIB_IRQ_TX_DONE))
        transformed = transformed | lora::IrqFlags::IRQ_TX_DONE;
    if (flags & (1U << RADIOLIB_IRQ_RX_DONE))
        transformed = transformed | lora::IrqFlags::IRQ_RX_DONE;
    if (flags & (1U << RADIOLIB_IRQ_PREAMBLE_DETECTED))
        transformed = transformed | lora::IrqFlags::IRQ_PREAMBLE_DETECTED;
    if (flags & (1U << RADIOLIB_IRQ_SYNC_WORD_VALID))
        transformed = transformed | lora::IrqFlags::IRQ_SYNC_WORD_VALID;
    if (flags & (1U << RADIOLIB_IRQ_HEADER_VALID))
        transformed = transformed | lora::IrqFlags::IRQ_HEADER_VALID;
    if (flags & (1U << RADIOLIB_IRQ_HEADER_ERR))
        transformed = transformed | lora::IrqFlags::IRQ_HEADER_ERR;
    if (flags & (1U << RADIOLIB_IRQ_CRC_ERR))
        transformed = transformed | lora::IrqFlags::IRQ_CRC_ERR;
    if (flags & (1U << RADIOLIB_IRQ_CAD_DONE))
        transformed = transformed | lora::IrqFlags::IRQ_CAD_DONE;
    if (flags & (1U << RADIOLIB_IRQ_CAD_DETECTED))
        transformed = transformed | lora::IrqFlags::IRQ_CAD_DETECTED;
    if (flags & (1U << RADIOLIB_IRQ_TIMEOUT))
        transformed = transformed | lora::IrqFlags::IRQ_TIMEOUT;

    return transformed;
}

/**
 * @brief Converte um `lora::IrqFlags` para flags da biblioteca RadioLib.
 */
RadioLibIrqFlags_t convert_to_radiolib_flags(lora::IrqFlags flags)
{
    RadioLibIrqFlags_t transformed = static_cast<RadioLibIrqFlags_t>(0);

    if (flags & lora::IrqFlags::IRQ_TX_DONE)
        transformed = transformed | (1U << RADIOLIB_IRQ_TX_DONE);
    if (flags & lora::IrqFlags::IRQ_RX_DONE)
        transformed = transformed | (1U << RADIOLIB_IRQ_RX_DONE);
    if (flags & lora::IrqFlags::IRQ_PREAMBLE_DETECTED)
        transformed = transformed | (1U << RADIOLIB_IRQ_PREAMBLE_DETECTED);
    if (flags & lora::IrqFlags::IRQ_SYNC_WORD_VALID)
        transformed = transformed | (1U << RADIOLIB_IRQ_SYNC_WORD_VALID);
    if (flags & lora::IrqFlags::IRQ_HEADER_VALID)
        transformed = transformed | (1U << RADIOLIB_IRQ_HEADER_VALID);
    if (flags & lora::IrqFlags::IRQ_HEADER_ERR)
        transformed = transformed | (1U << RADIOLIB_IRQ_HEADER_ERR);
    if (flags & lora::IrqFlags::IRQ_CRC_ERR)
        transformed = transformed | (1U << RADIOLIB_IRQ_CRC_ERR);
    if (flags & lora::IrqFlags::IRQ_CAD_DONE)
        transformed = transformed | (1U << RADIOLIB_IRQ_CAD_DONE);
    if (flags & lora::IrqFlags::IRQ_CAD_DETECTED)
        transformed = transformed | (1U << RADIOLIB_IRQ_CAD_DETECTED);
    if (flags & lora::IrqFlags::IRQ_TIMEOUT)
        transformed = transformed | (1U << RADIOLIB_IRQ_TIMEOUT);

    return transformed;
}
}  // namespace lora