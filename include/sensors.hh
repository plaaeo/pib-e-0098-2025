#pragma once
#include <cstdint>
#include <Adafruit_ADS1X15.h>

/**
 * @todo Separar constantes de calibração em variáveis do EEPROM
 * @todo 
 */
namespace sens {
    using ADS = Adafruit_ADS1115;

    enum : std::uint8_t { NULL_CHANNEL = 255 };

    /**
     * @brief Calcula a tensão no canal especificado do ADS1115.
     */
    float read_volts(ADS& ads, std::uint8_t channel) {
        auto aIn = ads.readADC_SingleEnded(channel);
        return ads.computeVolts(aIn);
    }

    struct ph4502c {
        //< Canal do ADS1115 conectado ao sensor.
        std::uint8_t channel = NULL_CHANNEL;
        
        //< Valor de calibração.
        double offset = 21.34;

        /**
         * @brief Coleta o pH de um sensor PH-4502C, ajustado por um cálculo
         * de calibração.
         */
        float ler(ADS &ads) {
            return offset - (5.70 * read_volts(ads, channel));
        }
    };

    struct dfrobottds {
        //< Canal do ADS1115 conectado ao sensor.
        std::uint8_t channel = NULL_CHANNEL;
        
        //< Valor de calibração.
        double k = 0.7;

        /**
         * @brief Coleta a condutividade no eletrodo de um sensor TDS DFRobot
         * e converte-a para TDS, em ppm, compensando pela temperatura.
         */
        float ler(ADS &ads, float temperature_c) {
            float V = read_volts(ads, channel);

            // Medir condutividade elétrica
            double ec = 133.42 * V;
            ec = (ec - 255.86) * V;
            ec = (ec + 857.39) * V;
            ec *= k;
            
            // Compensar com a temperatura
            double tc = 1.0 + 0.02 * (temperature_c - 25.0);
            return (ec / tc) * 0.5;
        }
    };

    struct ntc10k {
        //< Canal do ADS1115 conectado ao sensor.
        std::uint8_t channel = NULL_CHANNEL;
        
        //< Valores de calibração.
        double offset = 0;
        double a = 0.0011384;
        double b = 0.00023245;
        double c = 0.00000009489;

        /**
         * @brief Coleta a temperatura de um NTC 10K e converte-a para celsius
         * usando a fórmula de Steinhart-Hart.
         */
        float ler(ADS &ads) {
            // Calcular resistência do thermistor
            float V = read_volts(ads, channel);
            double R = 10000.0 * ((5.0 / V) - 1);
    
            // Calcular 1/T usando Steinhart-Hart
            double logR = std::log(R);
            double invT = a + (b * logR) + (c * logR * logR * logR);
    
            return (1.0 / invT) - 273.15 + offset;
        }
    };

    /**
     * @brief Representa a leitura atual dos sensores ativos.
     */
    struct reading_t {
        float temperature;
        float tds;
        float ph;
    };

    /**
     * @brief Gerencia todos os sensores disponíveis, produzindo `sens::reading_t`.
     */
    struct reader {
    public:
        enum {
            //< Canal do ADS conectado ao sensor de temperatura.
            ADS_CHANNEL_TEMPERATURE = 0,

            //< Canal do ADS conectado ao sensor de pH.
            ADS_CHANNEL_PH = 1,

            //< Canal do ADS conectado ao sensor de TDS.
            ADS_CHANNEL_TDS = 2,
        };
    public:
        //< A interface de leitura do ADS1115.
        Adafruit_ADS1115 ads;
    public:
        //< Leitor do sensor de pH utilizado em laboratório.
        ph4502c ph;
        
        //< Leitor do sensor de TDS utilizado em laboratório.
        dfrobottds tds;
        
        //< Leitor do sensor de temperatura utilizado em laboratório.
        ntc10k temperature;
    public:
        /**
         * @brief Inicializa o leitor com uma instância de `TwoWire`.
         */
        explicit reader(TwoWire& wire) {
            temperature.channel = ADS_CHANNEL_TEMPERATURE;
            tds.channel = ADS_CHANNEL_TDS;
            ph.channel = ADS_CHANNEL_PH;

            if (!ads.begin(ADS1X15_ADDRESS, &wire)) {
                ESP_LOGE("sens", "Falha ao inicializar o ADS1115.");
                abort();
            };
        }

        /**
         * @returns Uma leitura atual de todos os sensores.
         */
        reading_t ler() {
            reading_t reading;
            reading.temperature = temperature.ler(ads);
            reading.tds = tds.ler(ads, reading.temperature);
            reading.ph = ph.ler(ads);
            return reading;
        }
    };
}