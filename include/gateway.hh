#if defined(ESP32) || defined(ARDUINO_RASPBERRY_PI_PICO_W)
    #include <WiFi.h>
#elif defined(ESP8266)
    #include <ESP8266WiFi.h>
#endif

#include <Firebase_ESP_Client.h>
#include <addons/TokenHelper.h>
#include <addons/RTDBHelper.h>
#include <time.h>

#include "sensors.hh"

//< Possui os '#define's com chave de API do Firebase, URL, login do Wifi, etc.
#include "secret.hh"

namespace gw {
    constexpr auto TAG = "gw";

    // Set up NTP server
    constexpr auto NTP_SERVER = "pool.ntp.org";
    constexpr long GMT_OFFSET_SEC = -10800; // Adjust if needed
    constexpr int DAYLIGHT_OFFSET_SEC = 3600;

    // Firebase and authentication
    FirebaseConfig config;
    FirebaseAuth auth;
    FirebaseData fbData;

    //< Retorna uma string identificando a data e hora atuais.
    String getCurrentTimestamp() {
        struct tm timeinfo;
        if (!getLocalTime(&timeinfo)) {
            Serial.println("Failed to obtain time");
            return "N/A";
        }

        char buffer[30];
        strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%S", &timeinfo);

        // Add milliseconds if you need high precision
        unsigned long ms = millis() % 1000;
        sprintf(buffer + strlen(buffer), ".%03lu", ms);

        return String(buffer);
    }

    String ip;
    
    //< Realiza o setup das bibliotecas necessárias para enviar os dados para o Firebase.
    void setup() {
        WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
        
        ESP_LOGI(TAG, "Connecting to Wi-Fi...");
        while (WiFi.status() != WL_CONNECTED)
            delay(300);

        // Log IP address
        ip = WiFi.localIP().toString();
        ESP_LOGI(TAG, "Connected as %s", ip.c_str());

        ESP_LOGI(TAG, "Firebase Client v" FIREBASE_CLIENT_VERSION);

        /* Assign the api key (required) */
        config.api_key = FIREBASE_API_KEY;

        /* Assign the user sign in credentials */
        auth.user.email = USER_EMAIL;
        auth.user.password = USER_PASSWORD;

        /* Assign the RTDB URL (required) */
        config.database_url = FIREBASE_HOST;

        /* Assign the callback function for the long running token generation task */
        config.token_status_callback = tokenStatusCallback; // see addons/TokenHelper.h

        // Since Firebase v4.4.x, BearSSL engine was used, the SSL buffer need to be set.
        // Large data transmission may require larger RX buffer, otherwise connection issue or data read time out can be occurred.
        fbData.setBSSLBufferSize(2048 /* Rx buffer size in bytes from 512 - 16384 */, 1024 /* Tx buffer size in bytes from 512 - 16384 */);
        Firebase.begin(&config, &auth);

        // Comment or pass false value when WiFi reconnection will control by your code or third party library e.g. WiFiManager
        Firebase.reconnectNetwork(false);
        ESP_LOGI(TAG, "Firebase initialized.");

        // Initialize NTP
        configTime(GMT_OFFSET_SEC, DAYLIGHT_OFFSET_SEC, NTP_SERVER);
    }

    //< Envia uma unica `sens::reading_t` para o Firebase.
    void send_reading(const sens::reading_t& reading) {
        float randomValue = 6.5 + static_cast<float>(random(0, 3001)) / 1000.0;
        String receivedMessage = String(randomValue, 2);  // Convert to String with 2 decimal places

        auto temperatureS = String(reading.temperature, 2);  // 2 decimal places
        auto phS = String(reading.ph, 2);
        auto tdsS = String(reading.tds, 2);

        // Print the String values
        ESP_LOGI(TAG, "Temperature (String): %s", temperatureS.c_str());
        ESP_LOGI(TAG, "pH (String): %s", phS.c_str());
        ESP_LOGI(TAG, "TDS (String): %s", tdsS.c_str());
        ESP_LOGI(TAG, "Firebase ok? %d", Firebase.ready());

        // Aguardar até o Firebase estar pronto (?)
        // TODO: Encontrar uma solução melhor...
        while (!Firebase.ready()) { };

        {
            FirebaseJson json, jsonTemp, jsonTDS;

            json.set("PH", reading.ph);  // Use receivedMessage as the dtype value
            json.set("time_created", getCurrentTimestamp()); // Replace with actual timestamp if needed

            // Send data to Firebase at path /PAI/Sensor
            if (Firebase.RTDB.pushJSON(&fbData, "/PAI/Sensor/PH", &json)) {
                ESP_LOGI(TAG, "Data sent to Firebase successfully.");
            } else {
                auto err = fbData.errorReason();
                ESP_LOGE(TAG, "Failed to send pH data to Firebase: %s", err.c_str());
            }

            jsonTemp.set("Temperatura", temperatureS);  // Use receivedMessage as the dtype value
            jsonTemp.set("time_created", getCurrentTimestamp()); // Replace with actual timestamp if needed

            // Send data to Firebase at path /PAI/Sensor
            if (Firebase.RTDB.pushJSON(&fbData, "/PAI/Sensor/Temperatura", &jsonTemp)) {
                ESP_LOGI(TAG, "Data sent to Firebase successfully.");
            } else {
                auto err = fbData.errorReason();
                ESP_LOGE(TAG, "Failed to send temperature data to Firebase: %s", err.c_str());
            }

            jsonTDS.set("TDS", tdsS);  // Use receivedMessage as the dtype value
            jsonTDS.set("time_created", getCurrentTimestamp()); // Replace with actual timestamp if needed

            // Send data to Firebase at path /PAI/Sensor
            if (Firebase.RTDB.pushJSON(&fbData, "/PAI/Sensor/TDS", &jsonTDS)) {
                ESP_LOGI(TAG, "Data sent to Firebase successfully.");
            } else {
                auto err = fbData.errorReason();
                ESP_LOGE(TAG, "Failed to send TDS data to Firebase: %s", err.c_str());
            }

            // Send the receivedMessage to Firebase at path /PAI/Sensor/LastRecord
            if (Firebase.RTDB.setString(&fbData, "/PAI/LastRecord/Temperatura", temperatureS) &&
                Firebase.RTDB.setString(&fbData, "/PAI/LastRecord/PH", phS) &&
                Firebase.RTDB.setString(&fbData, "/PAI/LastRecord/TDS", tdsS)) {
                ESP_LOGI(TAG, "LastRecord sent to Firebase successfully.");
            } else {
                auto err = fbData.errorReason();
                ESP_LOGE(TAG, "Failed to send LastRecord data to Firebase: %s", err.c_str());
            }
        }
    }
}