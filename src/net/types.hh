#pragma once
#include <etl/optional.h>
#include <etl/vector.h>
#include <stdint.h>

#include "lora/radio.hh"
#include "net/clock.hh"
#include "net/trickle.hh"

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
        return (
            (static_cast<uint8_t>(hops) << 2) | static_cast<uint8_t>(tiredness)
        );
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

constexpr uint32_t PARAM_BANDWIDTH = 125000;
constexpr uint32_t PARAM_CHANNEL_GAP = PARAM_BANDWIDTH / 8;
constexpr uint32_t PARAM_FREQUENCY_MIN = 915000000;
constexpr uint32_t PARAM_FREQUENCY_MAX = 928000000;
constexpr size_t   MAX_CANDIDATE_PARENTS = 16;
constexpr node_id  GATEWAY_ID = 0;
constexpr uint8_t  UNKNOWN_MAX_HOPS = 0;
constexpr Rank     infinite_rank = Rank::from(0xFF);

constexpr lora::Parameters BASE_PARAMETERS = {
    .freq_hz = MIN_FREQUENCY,
    .bandwidth_hz = PARAM_BANDWIDTH,
    .preamble_length = 12,
    .power_db = 5,
    .spreading_factor = 12,
    .coding_rate = 5,
    .sync_word = 0x77,
    .implicit_header = false,
};

struct NodeInfo
{
    /**
     * @brief Um identificador único deste nó sensor.
     */
    net::node_id id;

    /**
     * @brief O rank do nó RPL.
     */
    net::Rank rank;

    /**
     * @brief Calcula os parâmetros LoRa usados para se comunicar com um nó
     * deste rank.
     */
    inline lora::Parameters calculate_personal_parameters() const noexcept
    {
        lora::Parameters parameters = BASE_PARAMETERS;

        // Adicionar offset específico de bandwidth baseado no ID
        uint32_t freq_offset_hz = id * (PARAM_BANDWIDTH + PARAM_CHANNEL_GAP);
        freq_offset_hz %= PARAM_FREQUENCY_MAX - PARAM_FREQUENCY_MIN;

        parameters.freq_hz += freq_offset_hz;

        assert(parameters.freq_hz >= PARAM_FREQUENCY_MIN);
        assert(parameters.freq_hz <= PARAM_FREQUENCY_MAX);

        return parameters;
    }
};

/**
 * @brief Representa as informações de um vizinho potencialmente pai
 * do nó. Atualizado apenas durante a fase de inicialização.
 */
struct ParentInfo : public NodeInfo
{
    float last_rssi;
    float last_snr;

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
    etl::vector<ParentInfo, net::MAX_CANDIDATE_PARENTS> candidate_parents;

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
    etl::optional<uint8_t> sort_by_objective();
};

struct SlotTimingInfo
{
    uint8_t tdm_subslot_guard_symbols;
    uint8_t tdm_subslot_mtu_bytes;
    uint8_t tdm_subslot_count;
    uint8_t tdm_slot_count;
};

template <typename RtState>
struct State : public NodeInfo
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

    /**
     * @brief Calcula a duração de um subslot completo da rede. Um subslot equivale à um
     * tempo de transmissão máximo, de um filho para seu pai, alocado para um único filho.
     */
    constexpr port::time_us calculate_subslot_duration() const noexcept
    {
        // Calcular a duração de um subslot
        port::time_us duration = BASE_PARAMETERS.calculate_time_on_air(
            slot_info.tdm_subslot_mtu_bytes
        );

        // Somar gap entre subslots
        duration += slot_info.tdm_subslot_guard_symbols *
                    BASE_PARAMETERS.calculate_symbol_time();

        return duration;
    }

    /**
     * @brief Calcula a duração de um slot completo da rede. Um slot equivale à
     * um período único de transmissão de dados dos nós filhos aos seus
     * respectivos pais.
     */
    constexpr port::time_us calculate_slot_duration() const noexcept
    {
        return calculate_subslot_duration() * slot_info.tdm_subslot_count;
    }

    /**
     * @brief Calcula o tempo de espera para o início de transmissão deste nó.
     */
    constexpr port::time_us calculate_tx_wait_time() const noexcept
    {
        return calculate_subslot_duration() * (id % slot_info.tdm_subslot_count);
    }

    /**
     * @brief Calcula o tempo da rede, em microssegundos, em que este nó deve
     * entrar na sua próxima janela de recepção. Retorna `INT64_MAX` caso não
     * seja possível determinar este tempo no estado atual da rede.
     */
    constexpr port::time_us calculate_next_rx_time() const noexcept
    {
        if (max_hops == UNKNOWN_MAX_HOPS)
            return INT64_MAX;

        auto slotDuration = calculate_slot_duration();

        // A duração de uma contagem monotônica de slots até a mesma contagem
        // reiniciar
        auto frameDuration = slotDuration * slot_info.tdm_slot_count;

        // Calcular o índice do nosso slot
        uint8_t mySlot = max_hops - rank.hops;

        // Calcular o tempo até o próximo slot de RX nosso
        port::time_us timeUntilNextSlot = mySlot * slotDuration;
        port::time_us now = m_State.net_time.get_time_us();
        timeUntilNextSlot = (now - timeUntilNextSlot) % frameDuration;
        timeUntilNextSlot = frameDuration - timeUntilNextSlot;

        return now + timeUntilNextSlot;
    }
};
}  // namespace net