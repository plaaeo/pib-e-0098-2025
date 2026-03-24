#pragma once
#include <stdint.h>

namespace port {
    /**
     * @brief Gera um número pseudo-aleatório. Dependendo do port, pode ser
     * um número verdadeiramente aleatório.
     */
    uint32_t random();
}