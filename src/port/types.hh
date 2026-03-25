#pragma once
#include <assert.h>
#include <stdlib.h>
#include <string.h>

namespace port {
//< Usado para construir um valor opcional vazio.
constexpr struct nullopt_t
{
} nullopt;

/**
 * @brief Uma implementação simples do `std::optional`.
 * @warning T deve ser uma estrutura simples (veja
 * https://en.cppreference.com/w/cpp/language/classes.html#Standard-layout_class)
 * @todo Testar todas as funcionalidades
 */
template <typename T>
struct optional
{
    inline optional(nullopt_t) : m_Present(false) {};
    inline explicit optional() : m_Present(false) {};
    inline explicit optional(const T &value)
        : m_Value(value), m_Present(true) {};
    inline explicit optional(T &&value) : m_Value(value), m_Present(true) {};

    inline bool has_value() const { return m_Present; }
    inline      operator bool() const { return m_Present; }

    inline const T &const_value() const
    {
        assert(m_Present);
        return m_Value;
    }

    inline T &value()
    {
        assert(m_Present);
        return m_Value;
    }

    inline T       &operator*() { return value(); }
    inline const T &operator*() const { return const_value(); }
    inline T       *operator->() { return &(value()); }
    inline const T *operator->() const { return &(const_value()); }

   private:
    T    m_Value;
    bool m_Present;
};

/**
 * @brief Implementação simples de um vetor de capacidade fixa.
 * @warning T deve ser uma estrutura simples (veja
 * https://en.cppreference.com/w/cpp/language/classes.html#Standard-layout_class)
 * @todo Testar todas as funcionalidades
 */
template <typename T, size_t Capacity>
struct static_vector
{
   public:
    static_assert(Capacity > 0);

    inline static_vector() : m_Size(0) {};

    /**
     * @brief Atribui um elemento à uma posição do vetor, alocando elementos
     * antecessores caso necessário.
     * @returns `true` se o elemento foi atribuído, `false` se o índice é
     * inválido.
     */
    inline bool assign(const T &value, size_t index)
    {
        if (index >= Capacity)
            return false;

        if (index >= m_Size)
            m_Size = index;

        m_Contents[index] = value;
        return true;
    }

    /**
     * @brief Insere um elemento numa posição do vetor, movendo os elementos
     * posteriores.
     * @returns `true` se o elemento foi inserido, `false` se o vetor está cheio
     * ou o índice é inválido.
     */
    inline bool insert(const T &value, size_t index)
    {
        if (index >= Capacity || m_Size == Capacity)
            return false;

        // Mover elementos posteriores para a direita
        memmove(&m_Contents[index + 1], &m_Contents[index],
                (m_Size - index) * sizeof(T));

        return assign(value, index);
    }

    /**
     * @brief Remove um elemento de uma posição do vetor, movendo os elementos
     * posteriores.
     * @returns O elemento, ou `nullopt` se o vetor está vazio ou o índice é
     * inválido.
     */
    inline port::optional<T> remove(size_t index)
    {
        if (index >= m_Size)
            return port::nullopt;

        T copy = m_Contents[index];

        // Mover elementos posteriores para a esquerda
        memmove(&m_Contents[index], &m_Contents[index + 1],
                (m_Size - index) * sizeof(T));

        m_Size--;
        return port::optional<T>(copy);
    }

    /**
     * @brief Move um elemento de um índice para outro, movendo elementos
     * intermediários. O mesmo que `sv.remove(from); sv.insert(..., to)`, porém
     * otimizado.
     * @returns `false` se a origem for inválida.
     */
    inline bool move(size_t from, size_t to)
    {
        if (from >= m_Size || to >= Capacity)
            return false;

        // Não move nada
        if (from == to)
            return true;

        // Incrementar tamanho
        if (to >= m_Size)
            m_Size++;

        // `memmove` vai sobrepor o elemento a ser movido.
        T copy = m_Contents[from];

        if (from > to) {
            // Mover elementos intermediários para a direita
            memmove(&m_Contents[to + 1], &m_Contents[to],
                    (from - to) * sizeof(T));
        } else {
            // Mover elementos intermediários para a esquerda
            memmove(&m_Contents[from], &m_Contents[from + 1],
                    (to - from) * sizeof(T));
        }

        return assign(copy, to);
    }

    /**
     * @brief Limpa o vetor estático.
     */
    inline void clear() { m_Size = 0; }

    //< Alias para `sv.insert(value, sv.size())`
    inline bool push_back(const T &value) { return insert(value, m_Size); };

    //< Alias para `sv.remove(sv.size())`
    inline port::optional<T> pop_back() { return remove(m_Size); }

    //< Alias para `sv.insert(value, 0)`
    inline bool push_front(const T &value) { return insert(value, 0); };

    //< Alias para `sv.remove(0)`
    inline port::optional<T> pop_front() { return remove(0); }

    inline const T &operator[](size_t index) const { return m_Contents[index]; }

    inline T       *begin() { return m_Contents; }
    inline T       *end() { return m_Contents + m_Size; }
    inline const T *cbegin() const { return m_Contents; }
    inline const T *cend() const { return m_Contents + m_Size; }

    inline T               *data() { return m_Contents; }
    inline size_t           size() const { return m_Size; }
    static constexpr size_t capacity() { return Capacity; }

   private:
    T      m_Contents[Capacity];
    size_t m_Size;
};
}  // namespace port

#ifdef ESP32
#    include <esp_log.h>
#    include <hal/gpio_types.h>

namespace port {
using gpio_t = gpio_num_t;
}

#    define ISR_SAFE_ATTR IRAM_ATTR
#    define PORT_LOGI     ESP_LOGI
#    define PORT_LOGE     ESP_LOGE
#    define PORT_LOGW     ESP_LOGW
#    define PORT_LOGD     ESP_LOGD
#    define PORT_LOGV     ESP_LOGV

#else
#    include <LibPrintf.h>
#    include "port/time.hh"

namespace port {
using gpio_t = int;
}

#    define ISR_SAFE_ATTR
#    define RTC_DATA_ATTR
#    define PORT_LOGI(tag, format, ...)       \
        printf(                               \
            "\033[0;32m"                      \
            "I (%li) [%s] " format "\033[0m", \
            port::get_monotonic_time(), tag, ##__VA_ARGS__)
#    define PORT_LOGE(tag, format, ...)       \
        printf(                               \
            "\033[0;31m"                      \
            "E (%li) [%s] " format "\033[0m", \
            port::get_monotonic_time(), tag, ##__VA_ARGS__)
#    define PORT_LOGW(tag, format, ...)       \
        printf(                               \
            "\033[0;33m"                      \
            "W (%li) [%s] " format "\033[0m", \
            port::get_monotonic_time(), tag, ##__VA_ARGS__)
#    define PORT_LOGD(tag, format, ...)                                      \
        printf("D (%li) [%s] " format "\033[0m", port::get_monotonic_time(), \
               tag, ##__VA_ARGS__)
#    define PORT_LOGV(tag, format, ...)                                      \
        printf("V (%li) [%s] " format "\033[0m", port::get_monotonic_time(), \
               tag, ##__VA_ARGS__)

#endif