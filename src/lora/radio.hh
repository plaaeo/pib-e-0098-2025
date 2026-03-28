#pragma once
#include <etl/utility.h>
#include <stdlib.h>

#include "port/port.hh"

namespace lora {

using packet_length = uint8_t;

/**
 * @brief Define bitflags utilizadas para interpretar flags de interrupção
 * genericamente nos módulos LoRa.
 */
enum IrqFlags
{
    /* clang-format off */
    IRQ_TX_DONE             = 1U << 0,
    IRQ_RX_DONE             = 1U << 1,
    IRQ_PREAMBLE_DETECTED   = 1U << 2,
    IRQ_SYNC_WORD_VALID     = 1U << 3,
    IRQ_HEADER_VALID        = 1U << 4,
    IRQ_HEADER_ERR          = 1U << 5,
    IRQ_CRC_ERR             = 1U << 6,
    IRQ_CAD_DONE            = 1U << 7,
    IRQ_CAD_DETECTED        = 1U << 8,
    IRQ_TIMEOUT             = 1U << 9,
    /* clang-format on */
};

/**
 * @brief Uma estrutura com todos os parâmetros configuráveis genericamente
 * do PHY LoRa.
 */
struct Parameters
{
    /// @brief A frequência central da comunicação, em hertz.
    uint32_t freq_hz;

    /// @brief A largura de banda da comunicação, em hertz.
    uint32_t bandwidth_hz;

    /// @brief O comprimento de um preâmbulo, em símbolos LoRa.
    uint16_t preamble_length;

    /// @brief A potência de transmissão a usar no canal, em dB.
    uint8_t power_db;

    /// @brief O fator de espalhamento dos símbolos LoRa.
    uint8_t spreading_factor;

    /// @brief O denominador da taxa de codificação (4 / `coding_rate`).
    uint8_t coding_rate;

    /// @brief Um byte de 'endereço'. Pacotes recebidos possuindo uma sync word
    /// diferente que essa serão ignorados.
    /// @warning Antes de definir a sync word, leia
    /// https://www.thethingsnetwork.org/forum/t/should-private-lorawan-networks-use-a-different-sync-word/34496/15
    uint8_t sync_word;
};

/**
 * @brief Contém configurações para o estado de recepção do PHY LoRa.
 */
struct RecvConfig
{
    /// @brief Determina quais flags de IRQ podem ser modificadas. Devem ser
    /// flags suportadas pelo radiotransmissor.
    IrqFlags irq_flags_mask;

    /// @brief Determina quais flags de IRQ despacham uma interrupção. Devem ser
    /// flags suportadas pelo radiotransmissor.
    IrqFlags irq_dispatch_mask;

    /// @brief Comprimento do payload de 0-255 bytes. Se for diferente de 0,
    /// esperará um pacote de cabeçalho implícito (sem cabeçalho LoRa).
    packet_length length;

    /// @brief `true` se o módulo deve continuar recebendo após receber um
    /// pacote, `false` caso contrário.
    bool continuous;
};

/**
 * @brief Contém configurações para o estado de transmissão do PHY LoRa.
 */
struct SendConfig
{
    /// @brief Um buffer com no mínimo `length` bytes compondo o payload da
    /// mensagem.
    const char *data;

    /// @brief O comprimento do payload de 0-255 bytes.
    packet_length length;

    /// @brief `true` se o pacote deve ser enviado sem cabeçalho. O módulo que
    /// receber o pacote deve ser configurado para esperar por uma mensagem de
    /// exatamente `length` bytes.
    bool implicit_header;
};

/**
 * @brief Determina o resultado das operações no radiotransmissor LoRa.
 */
enum class StatusCode
{
    /// @brief Sem erro.
    ok,

    /// @brief A operação uma das configurações dadas não é suportada.
    unsupported,

    /// @brief Falha na comunicação com o módulo (SPI, I2C, ...)
    communication_failed,

    /// @brief Timeout na comunicação com o módulo (SPI, I2C, ...)
    communication_timed_out,

    /// @brief Tentativa de configurar uma frequência não suportada.
    unsupported_frequency,

    /// @brief Tentativa de configurar um bandwidth não suportado.
    unsupported_bandwidth,

    /// @brief Tentativa de configurar um spreading factor não suportado.
    unsupported_spreading_factor,

    /// @brief Tentativa de configurar um coding rate não suportado.
    unsupported_coding_rate,

    /// @brief Tentativa de configurar uma potência não suportada.
    unsupported_power,

    /// @brief Tentativa de configurar um preâmbulo não suportado.
    unsupported_preamble_length,

    /// @brief Tentativa de configurar uma sync word não suportada.
    unsupported_sync_word,
};

/**
 * @brief Uma interface genérica para um radiotransmissor LoRa assíncrono.
 * Resultados de operações longas (rx/tx) são reportados por interrupções.
 */
struct IAsyncRadio
{
public:
    virtual ~IAsyncRadio() = default;

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
     */
    virtual StatusCode set_parameters(const Parameters &params) = 0;

    /**
     * @brief Comanda o radiotransmissor a entrar em modo de configuração.
     * @return `StatusCode::ok` em caso de sucesso.
     * @return `StatusCode::unsupported`
     * @return `StatusCode::communication_failed`
     * @return `StatusCode::communication_timed_out`
     */
    virtual StatusCode standby() = 0;

    /**
     * @brief Comanda o radiotransmissor a entrar em um modo de economia de
     * energia.
     * @return `StatusCode::ok` em caso de sucesso.
     * @return `StatusCode::unsupported`
     * @return `StatusCode::communication_failed`
     * @return `StatusCode::communication_timed_out`
     */
    virtual StatusCode sleep() = 0;

    /**
     * @brief Comanda o radiotransmissor a entrar em modo de recepção com as
     * configurações dadas.
     * @param cfg Configurações do modo de recepção de pacote.
     * @return `StatusCode::ok` em caso de sucesso.
     * @return `StatusCode::unsupported`
     * @return `StatusCode::communication_failed`
     * @return `StatusCode::communication_timed_out`
     */
    virtual StatusCode recv(const RecvConfig &cfg) = 0;

    /**
     * @brief Comanda o radiotransmissor a entrar em modo de transmissão com as
     * configurações dadas.
     * @param cfg Configurações do modo de transmissão do pacote.
     * @return `StatusCode::ok` em caso de sucesso.
     * @return `StatusCode::unsupported`
     * @return `StatusCode::communication_failed`
     * @return `StatusCode::communication_timed_out`
     */
    virtual StatusCode send(const SendConfig &cfg) = 0;

    /**
     * @brief Define um ISR para lidar com eventos despachados pelo
     * radiotransmissor.
     * @param isr O ISR a ser usado.
     */
    virtual void set_isr(port::ISR isr) = 0;

    /**
     * @brief Obtém as flags de interrupção suportadas pelo radiotransmissor.
     */
    virtual IrqFlags get_supported_flags() = 0;

    /**
     * @brief Obtém as flags de interrupção atualmente ativas no
     * radiotransmissor.
     * @returns Um par com o código de status da operação e as flags. Caso haja
     * um erro, as flags retornadas devem ser ignoradas.
     * @return `StatusCode::ok` em caso de sucesso.
     * @return `StatusCode::communication_failed`
     * @return `StatusCode::communication_timed_out`
     */
    virtual etl::pair<StatusCode, IrqFlags> get_flags() = 0;

    /**
     * @brief Limpa as flags de interrupção dadas no radiotransmissor. Flags que
     * não são suportadas pelo radiotransmissor serão ignoradas.
     * @param mask Uma máscara de bits mascarando as flags que devem ser limpas.
     * @return `StatusCode::ok` em caso de sucesso.
     * @return `StatusCode::communication_failed`
     * @return `StatusCode::communication_timed_out`
     */
    virtual StatusCode clear_flags(IrqFlags mask) = 0;

    /**
     * @brief Obtém o comprimento da ultima mensagem recebida pelo
     * radiotransmissor.
     * @returns Um par com o código de status da operação e o comprimento do
     * pacote. Caso haja um erro, o comprimento retornado deve ser ignorado.
     * @return `StatusCode::ok` em caso de sucesso.
     * @return `StatusCode::communication_failed`
     * @return `StatusCode::communication_timed_out`
     */
    virtual etl::pair<StatusCode, packet_length> get_message_length() = 0;

    /**
     * @brief Obtém o payload da ultima mensagem recebida pelo radiotransmissor.
     * @return `StatusCode::ok` em caso de sucesso.
     * @return `StatusCode::communication_failed`
     * @return `StatusCode::communication_timed_out`
     */
    virtual StatusCode read_message(const char *data, packet_length length) = 0;
};

}  // namespace lora