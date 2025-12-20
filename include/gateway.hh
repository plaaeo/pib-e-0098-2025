
#if defined(ESP32) || defined(ARDUINO_RASPBERRY_PI_PICO_W)
#include <WiFi.h>
#elif defined(ESP8266)
#include <ESP8266WiFi.h>
#endif

#include <Firebase_ESP_Client.h>
#include <addons/TokenHelper.h>
#include <addons/RTDBHelper.h>

#include "sensors.hh"

//< Possui os '#define's com chave de API do Firebase, URL, login do Wifi, etc.
#include "secret.hh"

/**
 * @brief Código adaptado de `gateway.ino` para interagir com o Firebase/Firestore.
 * A autenticação de usuário (email/senha) foi substituída por um método menos invasivo,
 * a autenticação por conta de serviço, cujos detalhes devem ser preenchidos no arquivo 'secret.hh'
 * @todo Firestore
 * @todo Gerar documento de nome aleatório no Firestore
 * @todo Datatype do TDS no Firestore
 */
namespace gw {
    constexpr auto TAG = "gw";

    // IDs de documentos na coleção 'data_type' no Firestore
    constexpr auto DOCUMENT_DATATYPE_TEMPERATURE = "SvcR1oN5OWyqpOvc6BJy";
    constexpr auto DOCUMENT_DATATYPE_PH = "dx6iDqedSnwpvdIluMy4";
    constexpr auto DOCUMENT_DATATYPE_TDS = "???";

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
    
    //< Realiza o setup das bibliotecas necessárias para enviar os dados para o Firebase.
    void setup() {
        WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
        
        ESP_LOGI(TAG, "Connecting to Wi-Fi...");
        while (WiFi.status() != WL_CONNECTED)
            delay(300);
        
        // Log IP address
        ESP_LOGI(TAG, "Connected as %s", WiFi.localIP().toString().c_str());

        // Initialize NTP
        ESP_LOGI(TAG, "Initializing NTP...");
        configTime(GMT_OFFSET_SEC, DAYLIGHT_OFFSET_SEC, NTP_SERVER);
        ESP_LOGI(TAG, "NTP configured at %s", getCurrentTimestamp().c_str());

        ESP_LOGI(TAG, "Firebase Client v" FIREBASE_CLIENT_VERSION);

        /* Assign the RTDB URL (required) */
        config.database_url = FIREBASE_HOST;
        
        /* Configure service account credentials (from 'secret.hh') */
        config.service_account.data.project_id = FIREBASE_PROJECT_ID;
        config.service_account.data.private_key_id = FIREBASE_PRIVATE_KEY_ID;
        config.service_account.data.private_key = FIREBASE_PRIVATE_KEY;
        config.service_account.data.client_email = FIREBASE_CLIENT_EMAIL;
        config.service_account.data.client_id = FIREBASE_CLIENT_ID;

        /* Assign the callback function for the long running token generation task */
        config.token_status_callback = tokenStatusCallback; // see addons/TokenHelper.h

        // Since Firebase v4.4.x, BearSSL engine was used, the SSL buffer need to be set.
        // Large data transmission may require larger RX buffer, otherwise connection issue or data read time out can be occurred.
        fbData.setBSSLBufferSize(2048 /* Rx buffer size in bytes from 512 - 16384 */, 1024 /* Tx buffer size in bytes from 512 - 16384 */);
        Firebase.begin(&config, &auth);

        // Comment or pass false value when WiFi reconnection will control by your code or third party library e.g. WiFiManager
        Firebase.reconnectNetwork(true);
        ESP_LOGI(TAG, "Firebase initialized.");
    }

    //< Envia uma unica `sens::reading_t` para o Firebase RTDB.
    void send_reading(const sens::reading_t& reading) {
        auto timestamp = getCurrentTimestamp();
        auto temperatureS = String(reading.temperature, 2);
        auto phS = String(reading.ph, 2);
        auto tdsS = String(reading.tds, 2);

        // Print the String values
        ESP_LOGI(TAG, "Temperature (String): %s", temperatureS.c_str());
        ESP_LOGI(TAG, "pH (String): %s", phS.c_str());
        ESP_LOGI(TAG, "TDS (String): %s", tdsS.c_str());
        ESP_LOGI(TAG, "Firebase ok? %d", Firebase.ready());

        // Aguardar até o Firebase estar pronto (?)
        while (!Firebase.ready()) { };

        FirebaseJson json, jsonTemp, jsonTDS;

        json.set("PH", reading.ph);
        json.set("time_created", timestamp);
        
        // Send data to Firebase at path /PAI/Sensor
        if (Firebase.RTDB.pushJSON(&fbData, "/PAI/Sensor/PH", &json)) {
            ESP_LOGI(TAG, "Data sent to Firebase successfully.");
        } else {
            auto err = fbData.errorReason();
            ESP_LOGE(TAG, "Failed to send pH data to Firebase: %s", err.c_str());
        }

        jsonTemp.set("Temperatura", temperatureS);
        jsonTemp.set("time_created", timestamp);

        // Send data to Firebase at path /PAI/Sensor
        if (Firebase.RTDB.pushJSON(&fbData, "/PAI/Sensor/Temperatura", &jsonTemp)) {
            ESP_LOGI(TAG, "Data sent to Firebase successfully.");
        } else {
            auto err = fbData.errorReason();
            ESP_LOGE(TAG, "Failed to send temperature data to Firebase: %s", err.c_str());
        }

        jsonTDS.set("TDS", tdsS);
        jsonTDS.set("time_created", timestamp);

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

    //< Envia uma unica `sens::reading_t` para o Firestore.
    void send_reading_firestore(const sens::reading_t& reading) {
        auto timestamp = getCurrentTimestamp();
        auto temperatureS = String(reading.temperature, 2);
        auto phS = String(reading.ph, 2);
        auto tdsS = String(reading.tds, 2);

        // Print the String values
        ESP_LOGI(TAG, "Temperature (String): %s", temperatureS.c_str());
        ESP_LOGI(TAG, "pH (String): %s", phS.c_str());
        ESP_LOGI(TAG, "TDS (String): %s", tdsS.c_str());
        ESP_LOGI(TAG, "Firebase ok? %d", Firebase.ready());

        // Aguardar até o Firebase estar pronto (?)
        while (!Firebase.ready()) { };

        FirebaseJson json;
        String docPath = "???";

        json.set("fields/device_id/stringValue", "0001");
        json.set("fields/last_updated/timestampValue", timestamp);
        json.set("fields/tipo_dados_id/stringValue", DOCUMENT_DATATYPE_PH);
        json.set("fields/valor/doubleValue", reading.ph);
        
        if (Firebase.Firestore.createDocument(&fbData, FIREBASE_PROJECT_ID, "", docPath.c_str(), json.raw())) {
            ESP_LOGI(TAG, "Data sent to Firestore successfully.");
        } else {
            auto err = fbData.errorReason();
            ESP_LOGE(TAG, "Failed to send pH data to Firestore: %s", err.c_str());
        }
        
        docPath = "???";
        json.set("fields/tipo_dados_id/stringValue", DOCUMENT_DATATYPE_TEMPERATURE);
        json.set("fields/valor/doubleValue", reading.temperature);

        if (Firebase.Firestore.createDocument(&fbData, FIREBASE_PROJECT_ID, "", docPath.c_str(), json.raw())) {
            ESP_LOGI(TAG, "Data sent to Firestore successfully.");
        } else {
            auto err = fbData.errorReason();
            ESP_LOGE(TAG, "Failed to send temperature data to Firestore: %s", err.c_str());
        }

        docPath = "???";
        json.set("fields/tipo_dados_id/stringValue", DOCUMENT_DATATYPE_TDS);
        json.set("fields/valor/doubleValue", reading.tds);

        if (Firebase.Firestore.createDocument(&fbData, FIREBASE_PROJECT_ID, "", docPath.c_str(), json.raw())) {
            ESP_LOGI(TAG, "Data sent to Firestore successfully.");
        } else {
            auto err = fbData.errorReason();
            ESP_LOGE(TAG, "Failed to send TDS data to Firestore: %s", err.c_str());
        }
    }

}