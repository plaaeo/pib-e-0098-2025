#pragma once

/**
 * @todo Adicionar estruturas e funções de (des)compactação de leituras conjuntas.
 */
namespace sensor {
    /**
     * @brief Representa uma leitura recente dos sensores.
     * @todo Adicionar tempo de leitura.
     * @todo Adicionar campos opcionais.
     */
    struct Reading {
        float temperature;
        float tds;
        float ph;
    };
}