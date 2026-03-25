#pragma once
#include <stdint.h>

#include "net/clock.hh"
#include "net/trickle.hh"
#include "port/types.hh"

namespace net {
using node_id = uint8_t;

struct Rank
{
    /**
     * @brief O número de hops de distância entre esse nó e a raíz.
     */
    uint8_t hops : 6;

    /**
     * @brief O "cansaço" do nó, sendo 0b00 um nó com bateria cheia e 0b11 um nó
     * desgastado ou descarregado, indisposto a servir como relay.
     */
    uint8_t tiredness : 2;

    /**
     * @brief Cria um struct `Rank` a partir de um byte.
     */
    constexpr static Rank from(uint8_t value)
    {
        return {
            .hops = static_cast<uint8_t>(value >> 2U),
            .tiredness = static_cast<uint8_t>(value & 0b11),
        };
    }

    /**
     * @brief Converte o rank para um valor numérico.
     */
    explicit constexpr operator uint8_t() const
    {
        return ((static_cast<uint8_t>(hops) << 2) |
                static_cast<uint8_t>(tiredness));
    }

    constexpr bool operator>(const Rank &other) const
    {
        return static_cast<uint8_t>(*this) > static_cast<uint8_t>(other);
    }

    constexpr bool operator<(const Rank &other) const
    {
        return static_cast<uint8_t>(*this) < static_cast<uint8_t>(other);
    }

    constexpr bool operator==(const Rank &other) const
    {
        return static_cast<uint8_t>(*this) == static_cast<uint8_t>(other);
    }

    constexpr bool operator!=(const Rank &other) const
    {
        return !operator==(other);
    }

    constexpr bool operator>=(const Rank &other) const
    {
        return operator>(other) || operator==(other);
    }

    constexpr bool operator<=(const Rank &other) const
    {
        return operator<(other) || operator==(other);
    }
};

constexpr size_t  MAX_CANDIDATE_PARENTS = 16;
constexpr node_id GATEWAY_ID = 0;
constexpr uint8_t UNKNOWN_MAX_HOPS = 0;
constexpr Rank    INFINITE_RANK = Rank::from(0xFF);

/**
 * @brief Representa as informações de um vizinho potencialmente pai
 * do nó. Atualizado apenas durante a fase de inicialização.
 */
struct ParentInfo
{
    float        last_rssi;
    float        last_snr;
    net::node_id id;
    net::Rank    rank;

    inline uint32_t score() const
    {
        /**
         * Shifta o inverso do 'cansaço' do nó por 15. Faz com que 1 bit do
         * valor 'cansaço' sobreponha o score do RSSI. Desta forma, um nó que
         * esteja um pouco indisposto a ser pai (tiredness = 0b01), porém possui
         * um RSSI muito melhor do que outros, pode ainda ser selecionado.
         */
        uint32_t s = static_cast<uint32_t>(rank.tiredness ^ 0b11) << 15;

        // Score do RSSI (-127dBm -> 0; 0dBm -> 255)
        int32_t rssiScore = 255 + static_cast<int32_t>(last_rssi * 2);
        if (rssiScore < 0)
            rssiScore = 0;

        // Score do SNR (linear)
        int32_t snrScore = static_cast<int32_t>(last_snr * 4);
        if (snrScore < 0)
            snrScore = 0;

        return s + static_cast<uint32_t>((rssiScore << 8) + snrScore);
    }
};

struct CandidateParents
{
    /**
     * @brief Um vetor com informações de nós vizinhos candidatos a serem pais
     * do nó atual.
     */
    port::static_vector<ParentInfo, net::MAX_CANDIDATE_PARENTS>
        candidate_parents;

    /**
     * @brief Limpa o vetor de pais candidatos.
     */
    void clear();

    /**
     * @brief Atualiza o vetor de pais candidatos para incluir as informações do
     * pai dado.
     */
    void add_or_update(ParentInfo &&info);

    /**
     * @brief Ordena a lista de pais candidatos usando a função de objetivo.
     * @returns O ID do pai preferido, ou `nullopt` se `candidate_parents`
     * estiver vazio.
     */
    port::optional<uint8_t> sort_by_objective();
};

struct SlotTimingInfo
{
    uint8_t tdm_subslot_guard_symbols;
    uint8_t tdm_subslot_mtu_bytes;
    uint8_t tdm_subslot_count;
    uint8_t tdm_slot_count;
};

template <typename RtState>
struct State
{
    RtState rt_state;

    /**
     * @brief O estado do último trickle timer executado.
     */
    net::TrickleTimerState trickle;

    /**
     * @brief Usado para sincronizar o tempo entre os nós sensores.
     */
    net::Clock net_time;

    /**
     * @brief Mantém parâmetros de timing de slots entre nós.
     */
    net::SlotTimingInfo slot_info;

    /**
     * @brief Um identificador único deste nó sensor.
     */
    net::node_id id;

    /**
     * @brief O rank do nó RPL.
     */
    net::Rank rank;

    /**
     * @brief O número de hops necessários para transmitir um dado do nó mais
     * distante até a raíz da rede. Será 0 caso não seja conhecido.
     */
    uint8_t max_hops;

    /**
     * @brief `true` caso qualquer nó com `rank.hops` maior tenha sido ouvido
     * durante a inicialização.
     */
    bool has_children;

    /**
     * @brief Mantém e gerencia uma lista de possíveis pais.
     */
    net::CandidateParents candidate_parents;
};
}  // namespace net