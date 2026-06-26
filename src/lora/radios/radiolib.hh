#pragma once
#include <RadioLib.h>
#include "lora/radio.hh"

namespace lora {

/**
 * @brief Converte um erro da biblioteca RadioLib para um `lora::StatusCode`.
 */
lora::StatusCode convert_radiolib_status(int16_t code);

/**
 * @brief Converte flags da biblioteca RadioLib para um `lora::IrqFlags`.
 */
lora::IrqFlags convert_radiolib_flags(RadioLibIrqFlags_t flags);

/**
 * @brief Converte um `lora::IrqFlags` para flags da biblioteca RadioLib.
 */
RadioLibIrqFlags_t convert_to_radiolib_flags(lora::IrqFlags flags);

class RadioLibPhy : public IAsyncRadio
{
public:
    RadioLibPhy(PhysicalLayer &phy)
        : m_Phy(phy)
    {
    }

    /**
     * @brief Define os parâmetros de recepção e transmissão.
     * @param params Os parâmetros de transmissão.
     * @return `StatusCode::ok` em caso de sucesso.
     * @return `StatusCode::unsupported_bandwidth`
     * @return `StatusCode::unsupported_spreading_factor`
     * @return `StatusCode::unsupported_coding_rate`
     * @return `StatusCode::unsupported_power`
     * @return `StatusCode::unsupported_preamble_length`
     * @return `StatusCode::unsupported_sync_word`
     * @return `StatusCode::communication_failed`
     * @return `StatusCode::communication_timed_out`
     * @return `StatusCode::other`
     */
    StatusCode set_parameters(const Parameters &params) override
    {
        auto status = convert_radiolib_status(
            m_Phy.setFrequency((float)(params.freq_hz) / 1000000.0f)
        );

        // Retornar cedo caso haja erro.
        if (status != lora::StatusCode::ok)
            return status;

        status = convert_radiolib_status(m_Phy.setDataRate(
            DataRate_t{
                .lora =
                    LoRaRate_t{
                        .spreadingFactor = params.spreading_factor,
                        .bandwidth = (float)(params.bandwidth_hz) / 1000.0f,
                        .codingRate = params.coding_rate,
                    }
            },
            RADIOLIB_MODEM_LORA
        ));
        if (status != lora::StatusCode::ok)
            return status;

        status = convert_radiolib_status(m_Phy.setOutputPower(params.power_db));
        if (status != lora::StatusCode::ok)
            return status;

        status = convert_radiolib_status(
            m_Phy.setPreambleLength(params.preamble_length)
        );

        /// @todo Implicit header
        return status;
    };

    /**
     * @brief Comanda o radiotransmissor a entrar em modo de configuração.
     * @return `StatusCode::ok` em caso de sucesso.
     * @return `StatusCode::unsupported`
     * @return `StatusCode::communication_failed`
     * @return `StatusCode::communication_timed_out`
     * @return `StatusCode::other`
     */
    StatusCode standby() override
    {
        return convert_radiolib_status(m_Phy.standby());
    };

    /**
     * @brief Comanda o radiotransmissor a entrar em um modo de economia de
     * energia.
     * @return `StatusCode::ok` em caso de sucesso.
     * @return `StatusCode::unsupported`
     * @return `StatusCode::communication_failed`
     * @return `StatusCode::communication_timed_out`
     * @return `StatusCode::other`
     */
    StatusCode sleep() override
    {
        return convert_radiolib_status(m_Phy.sleep());
    };

    /**
     * @brief Comanda o radiotransmissor a entrar em modo de recepção com as
     * configurações dadas.
     * @param cfg Configurações do modo de recepção de pacote.
     * @return `StatusCode::ok` em caso de sucesso.
     * @return `StatusCode::unsupported`
     * @return `StatusCode::communication_failed`
     * @return `StatusCode::communication_timed_out`
     * @return `StatusCode::other`
     */
    StatusCode recv(const RecvConfig &cfg) override
    {
        auto radiocfg = RadioModeConfig_t{
            .receive = {
                .timeout = cfg.continuous ? UINT32_MAX : 0,
                .irqFlags = convert_radiolib_flags(cfg.irq_flags_mask),
                .irqMask = convert_radiolib_flags(cfg.irq_dispatch_mask),
                .len = 0,
            }
        };

        auto status = convert_radiolib_status(
            m_Phy.stageMode(RADIOLIB_RADIO_MODE_RX, &radiocfg)
        );

        if (status != lora::StatusCode::ok)
            return status;

        return convert_radiolib_status(m_Phy.launchMode());
    };

    /**
     * @brief Comanda o radiotransmissor a entrar em modo de transmissão com as
     * configurações dadas.
     * @param cfg Configurações do modo de transmissão do pacote.
     * @return `StatusCode::ok` em caso de sucesso.
     * @return `StatusCode::unsupported`
     * @return `StatusCode::communication_failed`
     * @return `StatusCode::communication_timed_out`
     * @return `StatusCode::other`
     */
    StatusCode send(const SendConfig &cfg) override
    {
        auto radiocfg = RadioModeConfig_t{
            .transmit = {
                .data = cfg.data,
                .len = cfg.length,
                .addr = 0,
            }
        };

        auto status = convert_radiolib_status(
            m_Phy.stageMode(RADIOLIB_RADIO_MODE_TX, &radiocfg)
        );

        if (status != lora::StatusCode::ok)
            return status;

        return convert_radiolib_status(m_Phy.launchMode());
    };

    /**
     * @brief Obtém a intensidade do sinal recebido da última mensagem, em dBm.
     * @returns Um par com o código de status da operação e o RSSI. Caso haja um
     * erro, o RSSI deve ser ignorado.
     * @return `StatusCode::ok` em caso de sucesso.
     * @return `StatusCode::unsupported`
     * @return `StatusCode::communication_failed`
     * @return `StatusCode::communication_timed_out`
     * @return `StatusCode::other`
     */
    etl::tuple<StatusCode, float> get_rssi() override
    {
        /// @todo Verificar se é possível detectar uma falha nessa função.
        return {lora::StatusCode::ok, m_Phy.getRSSI()};
    };

    /**
     * @brief Obtém a relação sinal-ruído da última mensagem, em dB.
     * @returns Um par com o código de status da operação e o SNR. Caso haja um
     * erro, o SNR deve ser ignorado.
     * @return `StatusCode::ok` em caso de sucesso.
     * @return `StatusCode::unsupported`
     * @return `StatusCode::communication_failed`
     * @return `StatusCode::communication_timed_out`
     * @return `StatusCode::other`
     */
    etl::tuple<StatusCode, float> get_snr() override
    {
        /// @todo Verificar se é possível detectar uma falha nessa função.
        return {lora::StatusCode::ok, m_Phy.getSNR()};
    };

    /**
     * @brief Define um ISR para lidar com eventos despachados pelo
     * radiotransmissor.
     * @param isr O ISR a ser usado.
     */
    void set_isr(port::ISR isr) override
    {
        static port::ISR s_ISR = isr;

        /// @todo Implementar uma tabela de ISRs em `port` (desnecessário para o
        /// projeto atualmente)
        struct Unique
        {
            PORT_ISR_SAFE static void isr()
            {
                (s_ISR.function)(s_ISR.argument);
            };
        };

        m_Phy.setPacketReceivedAction(Unique::isr);
        m_Phy.setPacketSentAction(Unique::isr);
    };

    /**
     * @brief Obtém as flags de interrupção suportadas pelo radiotransmissor.
     */
    IrqFlags get_supported_flags() override
    {
        return convert_radiolib_flags(m_Phy.getIrqMapped(UINT32_MAX));
    };

    /**
     * @brief Obtém as flags de interrupção atualmente ativas no
     * radiotransmissor.
     * @returns Um par com o código de status da operação e as flags. Caso haja
     * um erro, as flags retornadas devem ser ignoradas.
     * @return `StatusCode::ok` em caso de sucesso.
     * @return `StatusCode::communication_failed`
     * @return `StatusCode::communication_timed_out`
     * @return `StatusCode::other`
     */
    etl::tuple<StatusCode, IrqFlags> get_flags() override
    {
        /// @todo Verificar se é possível detectar uma falha nessa função.
        return {
            lora::StatusCode::ok,
            convert_radiolib_flags(m_Phy.getIrqMapped(m_Phy.getIrqFlags()))
        };
    };

    /**
     * @brief Limpa as flags de interrupção dadas no radiotransmissor. Flags que
     * não são suportadas pelo radiotransmissor serão ignoradas.
     * @param mask Uma máscara de bits mascarando as flags que devem ser limpas.
     * @return `StatusCode::ok` em caso de sucesso.
     * @return `StatusCode::communication_failed`
     * @return `StatusCode::communication_timed_out`
     * @return `StatusCode::other`
     */
    StatusCode clear_flags(IrqFlags mask) override
    {
        return convert_radiolib_status(
            m_Phy.clearIrq(convert_to_radiolib_flags(mask))
        );
    };

    /**
     * @brief Obtém o comprimento da ultima mensagem recebida pelo
     * radiotransmissor.
     * @returns Um par com o código de status da operação e o comprimento do
     * pacote. Caso haja um erro, o comprimento retornado deve ser ignorado.
     * @return `StatusCode::ok` em caso de sucesso.
     * @return `StatusCode::communication_failed`
     * @return `StatusCode::communication_timed_out`
     * @return `StatusCode::other`
     */
    etl::tuple<StatusCode, packet_length> get_message_length() override
    {
        /// @todo Verificar se é possível detectar uma falha nessa função.
        return {lora::StatusCode::ok, m_Phy.getPacketLength()};
    };

    /**
     * @brief Obtém o payload da ultima mensagem recebida pelo radiotransmissor.
     * @param data Um buffer de pelo menos `length` bytes. Lerá menos bytes caso
     * o pacote tenha um comprimento menor que `length`.
     * @param length O máximo de bytes que devem ser lidos.
     * @return `StatusCode::ok` em caso de sucesso.
     * @return `StatusCode::communication_failed`
     * @return `StatusCode::communication_timed_out`
     * @return `StatusCode::other`
     */
    StatusCode read_message(uint8_t *data, packet_length length) override
    {
        /// @todo Verificar se é possível detectar uma falha nessa função.
        return convert_radiolib_status(m_Phy.readData(data, length));
    };

private:
    PhysicalLayer &m_Phy;
};

}  // namespace lora