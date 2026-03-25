#include "port/random.hh"
#include "esp_random.h"

namespace port {
uint32_t random()
{
    return esp_random();
};
}  // namespace port