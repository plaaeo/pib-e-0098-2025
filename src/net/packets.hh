#pragma once

#include <etl/optional.h>

#include "net/types.hh"
#include "repr/reading.hh"

namespace net {
constexpr static auto TAG = "net";

struct Broadcast
{
    constexpr static size_t BROADCAST_MAX_SIZE = 15;

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
    static etl::optional<Broadcast> decode(uint8_t *buffer, size_t length);

    /**
     * @brief Tenta codificar um broadcast.
     * @param buffer Um buffer contendo pelo menos `length` bytes.
     * @param length O tamanho do buffer. Deve ser pelo menos
     * `BROADCAST_MAX_SIZE`.
     * @returns A quantidade de bytes do pacote escritos no buffer, ou `0` em
     * caso de erro.
     */
    size_t encode(uint8_t *buffer, size_t length) const;
};

/**
 * @brief Representa um `repr::CompressedReading` coletado por um
 * dispositivo específico.
 */
struct OwnedReading
{
    net::node_id            id;
    repr::CompressedReading reading;
};

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
    uint8_t                          *output,
    size_t                            out_capacity
);

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
    const uint8_t              *input,
    size_t                      in_length
);

}  // namespace net