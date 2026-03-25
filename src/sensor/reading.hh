#pragma once

/**
 * @todo Adicionar estruturas e funções de (des)compactação de leituras
 * conjuntas.
 */
namespace sensor {
/**
 * @brief Representa uma leitura dos sensores.
 */
struct Reading
{
    uint32_t time;
    float    temperature;
    float    tds;
    float    ph;
};
}  // namespace sensor